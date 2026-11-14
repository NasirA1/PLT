#!/usr/bin/env python3
"""Builds the toolchain with instrumentation, runs every test suite and prints a summary.

Used by run_tests.sh (Linux) and run_tests.ps1 (Windows):

  1. builds the C++ code in build/coverage (Linux/GCC: gcov coverage + AddressSanitizer +
     UndefinedBehaviorSanitizer, warnings are errors; Windows/MSVC: Debug build, /W4)
  2. runs the C++ unit and golden tests (thunder_tests)
  3. runs the Python tests (IDE, command-line tools, toolchain) under a line-coverage tracer;
     the command-line tests run the instrumented executables, so they add to C++ coverage
  4. collects C++ coverage (gcov on GCC; OpenCppCoverage on Windows, if installed)
  5. prints coverage and code-quality metrics and fails if a test fails or coverage is
     below the threshold
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pyenv  # noqa: E402
import quality  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "coverage"
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""
THRESHOLD = 90.0


# ---------------------------------------------------------------------- output helpers

class Style:
    # Colour only when writing to a terminal that understands ANSI codes.
    enabled = sys.stdout.isatty() and (not IS_WINDOWS or os.environ.get("WT_SESSION") is not None)

    @classmethod
    def wrap(cls, code: str, text: str) -> str:
        return f"\033[{code}m{text}\033[0m" if cls.enabled else text

    @classmethod
    def ok(cls, text: str) -> str:
        return cls.wrap("32", text)

    @classmethod
    def bad(cls, text: str) -> str:
        return cls.wrap("31", text)

    @classmethod
    def bold(cls, text: str) -> str:
        return cls.wrap("1", text)


def banner(text: str) -> None:
    print("\n" + Style.bold(f"==> {text}"), flush=True)


def run(command: list[str], env: Optional[dict] = None, capture: bool = False,
        cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
    """Runs a command, echoing it; with capture=True the output is also returned."""
    print("$ " + " ".join(command), flush=True)
    if not capture:
        return subprocess.run(command, env=env, cwd=cwd)
    process = subprocess.Popen(command, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace")
    lines = []
    assert process.stdout is not None
    for line in process.stdout:
        print(line, end="", flush=True)
        lines.append(line)
    process.wait()
    return subprocess.CompletedProcess(command, process.returncode, "".join(lines), "")


# ---------------------------------------------------------------------- results

@dataclass
class Coverage:
    covered: int = 0
    total: int = 0

    @property
    def percent(self) -> Optional[float]:
        return 100.0 * self.covered / self.total if self.total else None


@dataclass
class CppCoverage:
    measured: bool = False
    note: str = ""
    lines: dict[str, Coverage] = field(default_factory=dict)      # per component
    functions: dict[str, Coverage] = field(default_factory=dict)
    branches: dict[str, Coverage] = field(default_factory=dict)


@dataclass
class Results:
    compiler: str = ""
    build_ok: bool = False
    warnings: int = 0
    warnings_note: str = ""
    cpp_passed: int = 0
    cpp_failed: int = 0
    cpp_ran: bool = False
    python: Optional[dict] = None
    python_ok: bool = False
    cpp_coverage: CppCoverage = field(default_factory=CppCoverage)
    sanitizers: bool = False
    tidy: Optional[quality.TidyReport] = None
    durations: dict[str, float] = field(default_factory=dict)


# ---------------------------------------------------------------------- steps

def compiler_info() -> tuple[str, str]:
    """(compiler id, version) as detected by CMake, e.g. ("GNU", "15.2.0") or ("MSVC", "19.50...")."""
    for path in sorted(BUILD.glob("CMakeFiles/*/CMakeCXXCompiler.cmake")):
        text = path.read_text(encoding="utf-8", errors="replace")
        compiler_id = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]*)"\)', text)
        version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]*)"\)', text)
        if compiler_id:
            return compiler_id.group(1), version.group(1) if version else ""
    return "", ""


def configure_and_build(results: Results, clean: bool) -> None:
    banner("Configuring and building the C++ toolchain (build/coverage)")
    if shutil.which("cmake") is None:
        print(Style.bad("CMake was not found on PATH."))
        return
    command = ["cmake", "-S", str(ROOT), "-B", str(BUILD), "-DTHUNDER_BUILD_TESTS=ON", "-DCMAKE_BUILD_TYPE=Debug"]
    if not IS_WINDOWS:
        if shutil.which("ninja") and not (BUILD / "CMakeCache.txt").is_file():
            command += ["-G", "Ninja"]
        command += ["-DTHUNDER_WARNINGS_AS_ERRORS=ON", "-DTHUNDER_SANITIZERS=ON"]
    if run(command).returncode != 0:
        return

    compiler_id, version = compiler_info()
    results.compiler = f"{compiler_id} {version}".strip() or "unknown compiler"
    if compiler_id == "GNU":
        run(["cmake", "-DTHUNDER_COVERAGE=ON", str(BUILD)])
    results.sanitizers = not IS_WINDOWS

    build = ["cmake", "--build", str(BUILD), "--config", "Debug", "--parallel"]
    if clean:
        build.append("--clean-first")
    completed = run(build, capture=True)
    results.build_ok = completed.returncode == 0
    results.warnings = len(re.findall(r"\bwarning\b[ :]", completed.stdout, flags=re.IGNORECASE))
    if IS_WINDOWS:
        results.warnings_note = "MSVC /W4" + ("" if clean else "; files compiled in this run (use --clean for all)")
    else:
        results.warnings_note = "-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion, warnings are errors"


def run_cpp_tests(results: Results, opencppcoverage: Optional[str]) -> None:
    banner("C++ unit and golden tests (thunder_tests)")
    executable = BUILD / "bin" / f"thunder_tests{EXE}"
    for stale in BUILD.rglob("*.gcda"):
        stale.unlink()  # start coverage from zero
    command = [str(executable)]
    if opencppcoverage:
        command = [opencppcoverage, "--quiet", "--sources", str(ROOT / "src"), "--sources", str(ROOT / "include"),
                   "--export_type",
                   f"binary:{BUILD / 'unit-tests.cov'}", "--", str(executable)]
    completed = run(command, capture=True)
    match = re.search(r"(\d+) test case\(s\): (\d+) passed, (\d+) failed", completed.stdout)
    results.cpp_ran = match is not None
    if match:
        results.cpp_passed, results.cpp_failed = int(match.group(2)), int(match.group(3))
    if completed.returncode != 0 and results.cpp_failed == 0:
        results.cpp_failed = 1  # crashed or failed to start


def run_python_tests(results: Results, opencppcoverage: Optional[str]) -> None:
    banner("Python tests: IDE, command-line tools and toolchain (with line coverage)")
    summary_path = BUILD / "python-coverage.json"
    if summary_path.exists():
        summary_path.unlink()
    env = dict(os.environ)
    env["THUNDER_BIN"] = str(BUILD / "bin")
    env["PYTHONPATH"] = str(ROOT / "ide")
    env["PYTHONIOENCODING"] = "utf-8"
    env.setdefault("ASAN_OPTIONS", "detect_leaks=1")
    command = [sys.executable, str(ROOT / "tools" / "pycoverage.py"), "--source", str(ROOT / "ide" / "thunder_ide"),
               "--tests", str(ROOT / "tests" / "python"), "--top-level", str(ROOT), "--json", str(summary_path)]
    if opencppcoverage:
        # Also measure the C++ code run by the command-line tests (child processes).
        command = [opencppcoverage, "--quiet", "--cover_children", "--sources", str(ROOT / "src"),
                   "--sources", str(ROOT / "include"),
                   "--input_coverage", str(BUILD / "unit-tests.cov"), "--export_type",
                   f"cobertura:{BUILD / 'cpp-coverage.xml'}", "--", *command]
    completed = run(command, env=env, cwd=ROOT)
    if summary_path.is_file():
        results.python = json.loads(summary_path.read_text(encoding="utf-8"))
    results.python_ok = completed.returncode == 0 and results.python is not None


def component_of(path: str) -> Optional[str]:
    """"core" for the compiler/VM library, "cli" for the command-line tools, None for anything else."""
    normalized = path.replace("\\", "/")
    root = str(ROOT).replace("\\", "/").lower()
    lowered = normalized.lower()
    if not (lowered.startswith(root + "/src/") or lowered.startswith(root + "/include/")):
        return None
    return "cli" if ("/src/cli/" in lowered or "/include/thunder/cli/" in lowered) else "core"


def collect_gcov(coverage: CppCoverage) -> None:
    gcda_files = [str(path) for path in BUILD.rglob("*.gcda") if "tests" not in path.parts]
    gcov = shutil.which("gcov")
    if not gcda_files or gcov is None:
        coverage.note = "no gcov data"
        return
    completed = subprocess.run([gcov, "--json-format", "--stdout", "--branch-probabilities", *gcda_files],
                               capture_output=True, text=True,
                               encoding="utf-8", errors="replace", cwd=BUILD)
    line_counts: dict[tuple[str, int], int] = defaultdict(int)
    function_counts: dict[tuple[str, str], int] = defaultdict(int)
    branch_counts: dict[tuple[str, int, int], int] = defaultdict(int)
    for document in completed.stdout.splitlines():
        if not document.strip():
            continue
        for entry in json.loads(document).get("files", []):
            name = entry["file"]
            if component_of(name) is None:
                continue
            for line in entry.get("lines", []):
                key = (name, line["line_number"])
                line_counts[key] += line["count"]
                for index, branch in enumerate(line.get("branches", [])):
                    if not branch.get("throw"):
                        branch_counts[(name, line["line_number"], index)] += branch["count"]
            for function in entry.get("functions", []):
                function_counts[(name, function["name"])] += function["execution_count"]

    for target, counts in ((coverage.lines, line_counts), (coverage.functions, function_counts),
                           (coverage.branches, branch_counts)):
        for key, count in counts.items():
            component = component_of(key[0])
            assert component is not None
            for bucket in (component, "total"):
                stats = target.setdefault(bucket, Coverage())
                stats.total += 1
                stats.covered += 1 if count > 0 else 0
    coverage.measured = bool(coverage.lines)
    coverage.note = "gcov; unit tests + command-line tests"


def collect_cobertura(coverage: CppCoverage, xml_path: Path) -> None:
    if not xml_path.is_file():
        coverage.note = "OpenCppCoverage produced no report"
        return
    tree = ElementTree.parse(xml_path)
    sources = [source.text or "" for source in tree.iter("source")]
    seen: dict[tuple[str, int], bool] = {}
    for cls in tree.iter("class"):
        filename = cls.get("filename", "")
        path = filename if os.path.isabs(filename) or not sources else os.path.join(sources[0], filename)
        for line in cls.iter("line"):
            key = (path, int(line.get("number", "0")))
            seen[key] = seen.get(key, False) or int(line.get("hits", "0")) > 0
    for (path, _number), hit in seen.items():
        component = component_of(path)
        if component is None:
            continue
        for bucket in (component, "total"):
            stats = coverage.lines.setdefault(bucket, Coverage())
            stats.total += 1
            stats.covered += 1 if hit else 0
    coverage.measured = bool(coverage.lines)
    coverage.note = "OpenCppCoverage; unit tests + command-line tests"


def find_opencppcoverage() -> Optional[str]:
    if not IS_WINDOWS:
        return None
    found = shutil.which("OpenCppCoverage")
    if found:
        return found
    default = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "OpenCppCoverage" / "OpenCppCoverage.exe"
    return str(default) if default.is_file() else None


# ---------------------------------------------------------------------- summary

def percent_text(value: Optional[float]) -> str:
    return f"{value:5.1f}%" if value is not None else "   n/a"


def verdict(value: Optional[float]) -> str:
    if value is None:
        return "not measured"
    return Style.ok(f"PASS (>= {THRESHOLD:.0f}%)") if value >= THRESHOLD else Style.bad(f"FAIL (< {THRESHOLD:.0f}%)")


def print_summary(results: Results, metrics: dict) -> bool:
    width = 96
    print("\n" + "=" * width)
    print(Style.bold(" THUNDER TEST SUMMARY").ljust(width))
    print("=" * width)
    print(f" Platform: {platform.system()} {platform.machine()}, Python {platform.python_version()}, "
          f"compiler: {results.compiler or 'unknown'}")

    ok = results.build_ok
    print(Style.bold("\n Tests"))
    if results.cpp_ran:
        line = f"{results.cpp_passed} passed, {results.cpp_failed} failed"
        print(f"   {'C++ unit + golden tests':<34} {Style.ok(line) if results.cpp_failed == 0 else Style.bad(line)}")
    else:
        print(f"   {'C++ unit + golden tests':<34} {Style.bad('did not run')}")
    ok = ok and results.cpp_ran and results.cpp_failed == 0
    if results.python is not None:
        py = results.python
        failed = py["failures"] + py["errors"]
        line = f"{py['tests_run'] - failed - py['skipped']} passed, {failed} failed, {py['skipped']} skipped"
        print(f"   {'Python tests (IDE, CLI, toolchain)':<34} {Style.ok(line) if results.python_ok else Style.bad(line)}")
        if py["skipped"]:
            print("     (skipped tests need PySide6 (Qt) or the built toolchain)")
    else:
        print(f"   {'Python tests (IDE, CLI, toolchain)':<34} {Style.bad('did not run')}")
    ok = ok and results.python_ok

    print(Style.bold("\n Coverage") + f"{'lines':>31} {'functions':>11} {'branches':>10}")
    cov = results.cpp_coverage
    if cov.measured:
        for key, label in (("core", "C++ core (src/core, include)"), ("cli", "C++ command line (src/cli)"),
                           ("total", "C++ total")):
            lines = cov.lines.get(key, Coverage()).percent
            functions = cov.functions.get(key, Coverage()).percent
            branches = cov.branches.get(key, Coverage()).percent
            suffix = f"   {verdict(lines)}" if key == "total" else ""
            print(f"   {label:<34}{percent_text(lines):>7} {percent_text(functions):>11} {percent_text(branches):>10}{suffix}")
        print(f"     ({cov.note})")
        ok = ok and (cov.lines.get("total", Coverage()).percent or 0) >= THRESHOLD
    else:
        print(f"   {'C++':<34}{'n/a':>7}   {cov.note}")
    if results.python is not None:
        py_percent = results.python["percent"]
        print(f"   {'Python IDE (ide/thunder_ide)':<34}{percent_text(py_percent):>7} {'':>11} {'':>10}   {verdict(py_percent)}")
        for part, label in (("core", "core (toolkit-independent)"), ("qt", "qt (Qt/PySide6 backend)")):
            files = {name: data for name, data in results.python["files"].items() if f"/thunder_ide/{part}/" in name}
            total = sum(data["statements"] for data in files.values())
            covered = sum(data["executed"] for data in files.values())
            if total:
                print(f"     {label:<32}{percent_text(100.0 * covered / total):>7}")
        ok = ok and py_percent >= THRESHOLD

    print(Style.bold("\n Code quality"))
    warnings_text = f"{results.warnings}"
    print(f"   {'Compiler warnings':<34} {Style.ok(warnings_text) if results.warnings == 0 else Style.bad(warnings_text)}"
          f"  ({results.warnings_note})")
    if results.sanitizers:
        print(f"   {'Sanitizers':<34} AddressSanitizer + UndefinedBehaviorSanitizer enabled for every test run")
    for label, report, threshold in (("Complexity, C++ (approximate)", metrics["cpp_complexity"], 25),
                                     ("Complexity, Python (McCabe)", metrics["py_complexity"], 10)):
        worst = report.worst
        above = report.above(threshold)
        worst_text = f"{worst.complexity} ({worst.name}, {worst.file})" if worst else "-"
        print(f"   {label:<34} {len(report.functions)} functions, average {report.average:.1f}, max {worst_text}")
        print(f"   {'':<34} {len(above)} function(s) above {threshold}"
              + (": " + ", ".join(f"{f.name} {f.complexity}" for f in above[:5]) if above else ""))
    if results.tidy is None:
        print(f"   {'clang-tidy':<34} not run (add --tidy; takes several minutes)")
    elif not results.tidy.available:
        print(f"   {'clang-tidy':<34} unavailable: {results.tidy.message}")
    else:
        top = ", ".join(f"{check} {count}" for check, count in results.tidy.findings.most_common(6))
        print(f"   {'clang-tidy':<34} {results.tidy.total} finding(s)" + (f": {top}" if top else ""))
    print(f"   {'Lines of code (code/comment)':<34} " + ", ".join(
        f"{label} {counts.code}/{counts.comments}" for label, counts in metrics["lines"].items()))

    durations = ", ".join(f"{name} {seconds:.0f}s" for name, seconds in results.durations.items())
    print(f"\n Time: {durations}")
    print("=" * width)
    print(Style.bold(" RESULT: ") + (Style.ok("PASS") if ok else Style.bad("FAIL")))
    print("=" * width)
    return ok


def gather_metrics() -> dict:
    cpp_sources = sorted((ROOT / "src").rglob("*.cpp")) + sorted((ROOT / "include").rglob("*.hpp"))
    py_sources = sorted((ROOT / "ide" / "thunder_ide").rglob("*.py"))
    return {
        "cpp_complexity": quality.cpp_complexity([p for p in cpp_sources if p.suffix == ".cpp"], ROOT),
        "py_complexity": quality.python_complexity(py_sources, ROOT),
        "lines": {
            "C++": quality.count_lines(cpp_sources, ("//",)),
            "Python IDE": quality.count_lines(py_sources, ("#",)),
            "VS Code extension": quality.count_lines(sorted((ROOT / "ide" / "vscode" / "src").rglob("*.js")), ("//",)),
            "tests": quality.count_lines(sorted((ROOT / "tests").rglob("*.cpp")) + sorted((ROOT / "tests").rglob("*.hpp"))
                                         + sorted((ROOT / "tests").rglob("*.py"))
                                         + sorted((ROOT / "ide" / "vscode" / "test").rglob("*.js")), ("//", "#")),
            "samples": quality.count_lines(sorted((ROOT / "samples").rglob("*.th")), ("#",)),
        },
    }


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Build Thunder and run all tests with coverage.")
    parser.add_argument("--clean", action="store_true", help="rebuild everything (complete warning count)")
    parser.add_argument("--tidy", action="store_true", help="also run clang-tidy static analysis (slow)")
    args = parser.parse_args(argv)

    try:
        pyenv.ensure_python()  # the Qt tests need PySide6: re-executes under the repository's .venv if needed
    except (RuntimeError, subprocess.CalledProcessError, OSError) as exc:
        print(Style.bad(f"Python environment: {exc} (the Qt tests will be skipped)"))

    results = Results()
    started = time.monotonic()
    configure_and_build(results, args.clean)
    results.durations["build"] = time.monotonic() - started
    if results.build_ok:
        opencppcoverage = find_opencppcoverage()
        step = time.monotonic()
        run_cpp_tests(results, opencppcoverage)
        results.durations["C++ tests"] = time.monotonic() - step
        step = time.monotonic()
        run_python_tests(results, opencppcoverage)
        results.durations["Python tests"] = time.monotonic() - step

        banner("Collecting coverage and code metrics")
        if IS_WINDOWS:
            if opencppcoverage:
                collect_cobertura(results.cpp_coverage, BUILD / "cpp-coverage.xml")
            else:
                results.cpp_coverage.note = ("not measured: MSVC has no gcov. Install OpenCppCoverage "
                                             "(https://github.com/OpenCppCoverage/OpenCppCoverage) to measure it.")
        elif compiler_info()[0] == "GNU":
            collect_gcov(results.cpp_coverage)
        else:
            results.cpp_coverage.note = "not measured: gcov coverage needs GCC"
        if args.tidy:
            step = time.monotonic()
            files = sorted((ROOT / "src").rglob("*.cpp"))
            results.tidy = quality.run_clang_tidy(files, BUILD, jobs=os.cpu_count() or 4)
            results.durations["clang-tidy"] = time.monotonic() - step
    results.durations["total"] = time.monotonic() - started
    return 0 if print_summary(results, gather_metrics()) else 1


if __name__ == "__main__":
    sys.exit(main())
