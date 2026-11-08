"""Running the Thunder command-line tools.

The IDE never parses Thunder itself: it writes the editor contents to a file,
runs ``thunderc --json`` and reads the report, and runs ``thundervm`` to
execute the compiled bytecode. This keeps the IDE honest - students see
exactly what the real toolchain does - and the commands are shown in the
Build Log so they can repeat them in a terminal.
"""

from __future__ import annotations

import codecs
import io
import json
import os
import subprocess
import tempfile
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Optional

from thunder_ide.core.canvas import RecordSplitter

EXE_SUFFIX = ".exe" if os.name == "nt" else ""

# Hide console windows for child processes on Windows.
_CREATION_FLAGS = getattr(subprocess, "CREATE_NO_WINDOW", 0)


class ToolchainError(Exception):
    """The command-line tools are missing or behaved unexpectedly."""


def repository_root() -> Path:
    """The directory containing ide/, src/ and samples/."""
    return Path(__file__).resolve().parents[3]


def find_bin_dir(explicit: Optional[str] = None) -> Path:
    """Finds the directory containing thunderc and thundervm.

    Search order: ``explicit``, the THUNDER_BIN environment variable, then the
    build directories used by the run scripts.
    """
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if os.environ.get("THUNDER_BIN"):
        candidates.append(Path(os.environ["THUNDER_BIN"]))
    root = repository_root()
    candidates += [root / "build" / name / "bin" for name in ("release", "dev", "coverage")]

    for candidate in candidates:
        if (candidate / f"thunderc{EXE_SUFFIX}").is_file() and (candidate / f"thundervm{EXE_SUFFIX}").is_file():
            return candidate.resolve()
    searched = ", ".join(str(c) for c in candidates)
    raise ToolchainError(
        f"thunderc and thundervm were not found (searched: {searched}). "
        "Build them with ./run.sh (or run.ps1 on Windows), or set THUNDER_BIN."
    )


def format_command(command: list[str]) -> str:
    """A command line as it could be typed in a shell (for the Build Log)."""
    def quote(arg: str) -> str:
        return f'"{arg}"' if (" " in arg or not arg) else arg

    return " ".join(quote(arg) for arg in command)


@dataclass
class CompileOutcome:
    """The result of running ``thunderc --json``."""

    report: dict
    command: list[str]
    exit_code: int
    source_path: Path
    bytecode_path: Optional[Path]
    stderr: str = ""

    @property
    def success(self) -> bool:
        return bool(self.report.get("success"))

    @property
    def diagnostics(self) -> list[dict]:
        return list(self.report.get("diagnostics", []))

    @property
    def error_count(self) -> int:
        return int(self.report.get("error_count", 0))

    @property
    def warning_count(self) -> int:
        return int(self.report.get("warning_count", 0))


@dataclass
class RunResult:
    """How a program run ended."""

    exit_code: int
    stopped: bool = False
    result: Optional[dict] = None   # contents of --result-json, if written
    trace: Optional[dict] = None    # contents of --trace-json, if requested
    error: Optional[str] = None     # problem reading the outputs

    @property
    def runtime_error(self) -> Optional[dict]:
        return self.result.get("error") if self.result else None

    @property
    def stats(self) -> Optional[dict]:
        return self.result.get("stats") if self.result else None


OutputCallback = Callable[[str, str], None]  # (stream name "stdout"/"stderr", text)
ExitCallback = Callable[[RunResult], None]
GraphicsCallback = Callable[[dict], None]    # a frame record (see core.canvas)


class ProgramRun:
    """A running thundervm process whose output is streamed to callbacks.

    Callbacks are invoked on background threads; GUI code must hand them over
    to its own thread (the IDE posts them to the GUI's event loop). Output is passed on as
    soon as it arrives, not line by line, so a prompt such as ``input("Name? ")`` appears
    before the program waits. ``write_input`` sends text to the program's standard input.
    """

    def __init__(self, command: list[str], result_path: Path, trace_path: Optional[Path],
                 on_output: OutputCallback, on_exit: ExitCallback, cwd: Optional[Path] = None,
                 on_graphics: Optional[GraphicsCallback] = None):
        self.command = command
        self.cwd = cwd
        self._on_graphics = on_graphics
        self._result_path = result_path
        self._trace_path = trace_path
        self._on_output = on_output
        self._on_exit = on_exit
        self._stopped = False
        self._process: Optional[subprocess.Popen] = None
        self._done = threading.Event()
        self._input_lock = threading.Lock()

    def start(self) -> None:
        # Binary pipes: the output is decoded incrementally (see _pump) so partial lines arrive at once.
        self._process = subprocess.Popen(
            self.command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
            cwd=self.cwd,
            creationflags=_CREATION_FLAGS,
        )
        readers = [
            threading.Thread(target=self._pump, args=(self._process.stdout, "stdout"), daemon=True),
            threading.Thread(target=self._pump, args=(self._process.stderr, "stderr"), daemon=True),
        ]
        for reader in readers:
            reader.start()
        threading.Thread(target=self._wait, args=(readers,), daemon=True).start()

    def _pump(self, stream, name: str) -> None:
        # UTF-8 characters and "\r\n" line endings may be split between two reads.
        decoder = io.IncrementalNewlineDecoder(codecs.getincrementaldecoder("utf-8")(errors="replace"), translate=True)
        # With --canvas, frame records travel in the standard output among the program's text.
        splitter = RecordSplitter() if name == "stdout" and self._on_graphics is not None else None
        while True:
            chunk = os.read(stream.fileno(), 65536)
            text = decoder.decode(chunk, final=not chunk)
            if splitter is not None and text:
                text, records = splitter.feed(text)
                for record in records:
                    self._on_graphics(record)  # type: ignore[misc]
            if text:
                self._on_output(name, text)
            if not chunk:
                break
        stream.close()

    def write_input(self, text: str) -> bool:
        """Sends text to the program's standard input; False if the program no longer reads it."""
        with self._input_lock:
            stdin = self._process.stdin if self._process is not None else None
            if stdin is None or stdin.closed:
                return False
            try:
                stdin.write(text.encode("utf-8"))
                stdin.flush()
                return True
            except (OSError, ValueError):  # the program has ended
                return False

    def close_input(self) -> None:
        """Ends the program's input: end_of_input() becomes true and input() returns ""."""
        with self._input_lock:
            if self._process is not None and self._process.stdin is not None and not self._process.stdin.closed:
                try:
                    self._process.stdin.close()
                except OSError:  # pragma: no cover - the pipe is already broken
                    pass

    def _wait(self, readers: list[threading.Thread]) -> None:
        assert self._process is not None
        exit_code = self._process.wait()
        for reader in readers:
            reader.join()
        self.close_input()
        result = RunResult(exit_code=exit_code, stopped=self._stopped)
        if not self._stopped:
            try:
                if self._result_path.is_file():
                    result.result = json.loads(self._result_path.read_text(encoding="utf-8"))
                if self._trace_path is not None and self._trace_path.is_file():
                    result.trace = json.loads(self._trace_path.read_text(encoding="utf-8"))
            except (OSError, ValueError) as exc:
                result.error = str(exc)
        try:
            self._on_exit(result)
        finally:
            self._done.set()

    def stop(self) -> None:
        """Kills the program (used by the Stop button)."""
        self._stopped = True
        if self._process is not None and self._process.poll() is None:
            self._process.kill()

    @property
    def running(self) -> bool:
        return not self._done.is_set()

    def wait(self, timeout: Optional[float] = None) -> bool:
        """Blocks until the run has finished and on_exit was called."""
        return self._done.wait(timeout)


@dataclass
class TranslationOutcome:
    """The result of running ``thunderc --translate``."""

    ok: bool
    text: str
    messages: str  # errors, or notes about renamed names


@dataclass
class PackageOutcome:
    """The result of running ``thunderc --package``."""

    ok: bool
    output: str      # the executable that was asked for
    message: str     # what thunderc said (what it wrote, or why it could not)
    command: list[str]


@dataclass
class Toolchain:
    """The pair of command-line tools plus a scratch directory for build outputs.

    ``language`` is the default language of programs (a directive in the source overrides it).
    """

    bin_dir: Path
    work_dir: Path = field(default_factory=lambda: Path(tempfile.mkdtemp(prefix="thunder-ide-")))
    language: str = "en"
    _language_info: dict = field(default_factory=dict)

    @classmethod
    def locate(cls, explicit: Optional[str] = None) -> "Toolchain":
        return cls(find_bin_dir(explicit))

    @property
    def compiler(self) -> Path:
        return self.bin_dir / f"thunderc{EXE_SUFFIX}"

    @property
    def vm(self) -> Path:
        return self.bin_dir / f"thundervm{EXE_SUFFIX}"

    def _run_sync(self, command: list[str], timeout: float = 60) -> subprocess.CompletedProcess:
        try:
            return subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                  timeout=timeout, creationflags=_CREATION_FLAGS)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise ToolchainError(f"failed to run {format_command(command)}: {exc}") from exc

    def language_info(self, language: Optional[str] = None) -> dict:
        """Keywords, built-ins, punctuation and opcodes of a language, straight from the compiler (cached)."""
        language = language or self.language
        if language not in self._language_info:
            command = [str(self.compiler), "--language-info", "--lang", language]
            completed = self._run_sync(command)
            try:
                self._language_info[language] = json.loads(completed.stdout)
            except ValueError as exc:
                raise ToolchainError(f"unexpected output from {format_command(command)}: {completed.stderr}") from exc
        return self._language_info[language]

    def translate(self, source_text: str, target_language: str) -> TranslationOutcome:
        """Translates a program into another language (keywords, built-ins, digits, punctuation)."""
        source_path = self.work_dir / "translate-input.th"
        source_path.write_text(source_text, encoding="utf-8", newline="\n")
        command = [str(self.compiler), "--lang", self.language, "--translate", target_language, str(source_path)]
        completed = self._run_sync(command)
        return TranslationOutcome(completed.returncode == 0, completed.stdout, completed.stderr.strip())

    def compile(self, source_text: str, file_name: str, module_dir: Optional[Path] = None) -> CompileOutcome:
        """Compiles editor text. ``file_name`` (e.g. "loops.th") names the file in diagnostics.

        The text is compiled from a scratch directory, so files it imports (``import "lib/x"``)
        are looked up in ``module_dir``: the directory the program was opened from.
        """
        name = Path(file_name).name or "untitled.th"
        source_path = self.work_dir / name
        bytecode_path = source_path.with_suffix(".thc")
        source_path.write_text(source_text, encoding="utf-8", newline="\n")
        if bytecode_path.exists():
            bytecode_path.unlink()

        command = [str(self.compiler), "--json", "--lang", self.language]
        if module_dir is not None:
            command += ["--module-path", str(module_dir)]
        command += ["-o", str(bytecode_path), str(source_path)]
        completed = self._run_sync(command)
        try:
            report = json.loads(completed.stdout)
        except ValueError as exc:
            raise ToolchainError(
                f"thunderc did not produce a report (exit code {completed.returncode}): {completed.stderr.strip()}"
            ) from exc
        return CompileOutcome(
            report=report,
            command=command,
            exit_code=completed.returncode,
            source_path=source_path,
            bytecode_path=bytecode_path if bytecode_path.is_file() else None,
            stderr=completed.stderr,
        )

    def package(self, source_text: str, file_name: str, output_path: str,
                module_dir: Optional[Path] = None) -> "PackageOutcome":
        """Writes a native executable that runs the program on its own (thunderc --package)."""
        name = Path(file_name).name or "untitled.th"
        source_path = self.work_dir / name
        source_path.write_text(source_text, encoding="utf-8", newline="\n")
        command = [str(self.compiler), "--lang", self.language]
        if module_dir is not None:
            command += ["--module-path", str(module_dir)]
        command += ["--package", output_path, str(source_path)]
        completed = self._run_sync(command, timeout=300)  # copying the VM takes a moment
        message = (completed.stdout + completed.stderr).strip()
        return PackageOutcome(completed.returncode == 0, output_path, message, command)

    def start_run(self, bytecode_path: Path, on_output: OutputCallback, on_exit: ExitCallback,
                  max_steps: int = 0, trace_limit: Optional[int] = None,
                  arguments: Optional[list[str]] = None, cwd: Optional[Path] = None,
                  on_graphics: Optional[GraphicsCallback] = None) -> ProgramRun:
        """Starts thundervm on a compiled program. With ``trace_limit`` a JSON trace is recorded.

        ``arguments`` are passed to the program (``args()`` in module os); ``cwd`` is its working
        directory (relative file names in ``read_file`` and friends start there). With
        ``on_graphics`` the program runs with ``--canvas``: each frame it shows is passed to the
        callback, and key and mouse events can be sent with ``ProgramRun.write_input``.
        """
        result_path = self.work_dir / "result.json"
        trace_path = self.work_dir / "program.trace.json" if trace_limit is not None else None
        for path in (result_path, trace_path):
            if path is not None and path.exists():
                path.unlink()

        command = [str(self.vm), "--result-json", str(result_path)]
        if max_steps > 0:
            command += ["--max-steps", str(max_steps)]
        if trace_path is not None:
            command += ["--trace-json", str(trace_path), "--trace-limit", str(trace_limit)]
        if on_graphics is not None:
            command.append("--canvas")
        command.append(str(bytecode_path))
        command += arguments or []

        run = ProgramRun(command, result_path, trace_path, on_output, on_exit, cwd, on_graphics)
        run.start()
        return run
