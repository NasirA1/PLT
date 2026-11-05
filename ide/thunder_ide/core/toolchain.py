"""Running the Thunder command-line tools.

The IDE never parses Thunder itself: it writes the editor contents to a file,
runs ``thunderc --json`` and reads the report, and runs ``thundervm`` to
execute the compiled bytecode. This keeps the IDE honest - students see
exactly what the real toolchain does - and the commands are shown in the
Build Log so they can repeat them in a terminal.
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Optional

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


class ProgramRun:
    """A running thundervm process whose output is streamed to callbacks.

    Callbacks are invoked on background threads; GUI code must hand them over
    to its own thread (the IDE uses a queue polled by Tk).
    """

    def __init__(self, command: list[str], result_path: Path, trace_path: Optional[Path],
                 on_output: OutputCallback, on_exit: ExitCallback):
        self.command = command
        self._result_path = result_path
        self._trace_path = trace_path
        self._on_output = on_output
        self._on_exit = on_exit
        self._stopped = False
        self._process: Optional[subprocess.Popen] = None
        self._done = threading.Event()

    def start(self) -> None:
        self._process = subprocess.Popen(
            self.command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
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
        for line in iter(stream.readline, ""):
            self._on_output(name, line)
        stream.close()

    def _wait(self, readers: list[threading.Thread]) -> None:
        assert self._process is not None
        exit_code = self._process.wait()
        for reader in readers:
            reader.join()
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

    def compile(self, source_text: str, file_name: str) -> CompileOutcome:
        """Compiles editor text. ``file_name`` (e.g. "loops.th") names the file in diagnostics."""
        name = Path(file_name).name or "untitled.th"
        source_path = self.work_dir / name
        bytecode_path = source_path.with_suffix(".thc")
        source_path.write_text(source_text, encoding="utf-8", newline="\n")
        if bytecode_path.exists():
            bytecode_path.unlink()

        command = [str(self.compiler), "--json", "--lang", self.language, "-o", str(bytecode_path), str(source_path)]
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

    def start_run(self, bytecode_path: Path, on_output: OutputCallback, on_exit: ExitCallback,
                  max_steps: int = 0, trace_limit: Optional[int] = None) -> ProgramRun:
        """Starts thundervm on a compiled program. With ``trace_limit`` a JSON trace is recorded."""
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
        command.append(str(bytecode_path))

        run = ProgramRun(command, result_path, trace_path, on_output, on_exit)
        run.start()
        return run
