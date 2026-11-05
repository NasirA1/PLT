#!/usr/bin/env python3
"""Line coverage for Python code using only the standard library.

Runs a unittest suite under a line tracer and reports which lines of the
measured package were executed. (coverage.py is the usual tool, but it is a
third-party package; this keeps the test suite runnable on a fresh Python
installation on both Linux and Windows.)

    python tools/pycoverage.py --source ide/thunder_ide --tests tests/python [--json out.json]

Lines (or whole statements) marked with "# pragma: no cover" are excluded.
"""

from __future__ import annotations

import argparse
import ast
import json
import sys
import threading
import unittest
from collections import defaultdict
from pathlib import Path
from types import CodeType, FrameType
from typing import Optional


def executable_lines(path: Path) -> set[int]:
    """Line numbers that contain code, according to the compiled bytecode."""
    source = path.read_text(encoding="utf-8")
    lines: set[int] = set()

    def walk(code: CodeType) -> None:
        for _start, _end, line in code.co_lines():
            if line is not None and line > 0:
                lines.add(line)
        for constant in code.co_consts:
            if isinstance(constant, CodeType):
                walk(constant)

    walk(compile(source, str(path), "exec"))
    return lines - excluded_lines(source)


def excluded_lines(source: str) -> set[int]:
    """Lines of statements marked "# pragma: no cover" (including the statement's body)."""
    marked = {number for number, text in enumerate(source.splitlines(), start=1) if "pragma: no cover" in text}
    excluded: set[int] = set()
    if not marked:
        return excluded
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.stmt) and node.lineno in marked:
            excluded.update(range(node.lineno, (node.end_lineno or node.lineno) + 1))
    return excluded | marked


class LineTracer:
    """Records executed lines in files below the given directories (all threads)."""

    def __init__(self, roots: list[Path]):
        self.roots = [str(root.resolve()) for root in roots]
        self.hits: dict[str, set[int]] = defaultdict(set)
        self._wanted: dict[str, bool] = {}

    def _is_wanted(self, filename: str) -> bool:
        wanted = self._wanted.get(filename)
        if wanted is None:
            try:
                resolved = str(Path(filename).resolve())
            except (OSError, ValueError):
                resolved = filename
            wanted = any(resolved.startswith(root) for root in self.roots)
            self._wanted[filename] = wanted
        return wanted

    def _global(self, frame: FrameType, event: str, _arg: object) -> Optional[object]:
        if event == "call" and self._is_wanted(frame.f_code.co_filename):
            return self._local
        return None

    def _local(self, frame: FrameType, event: str, _arg: object) -> object:
        if event == "line":
            self.hits[frame.f_code.co_filename].add(frame.f_lineno)
        return self._local

    def start(self) -> None:
        threading.settrace(self._global)
        sys.settrace(self._global)

    def stop(self) -> None:
        sys.settrace(None)
        threading.settrace(None)  # type: ignore[arg-type]


def measure(source: Path, tests: Path, top_level: Path, verbosity: int) -> tuple[unittest.TestResult, dict]:
    tracer = LineTracer([source])
    tracer.start()
    try:
        suite = unittest.defaultTestLoader.discover(str(tests), top_level_dir=str(top_level))
        result = unittest.TextTestRunner(verbosity=verbosity, stream=sys.stdout).run(suite)
    finally:
        tracer.stop()

    hits_by_file: dict[str, set[int]] = defaultdict(set)
    for filename, lines in tracer.hits.items():
        hits_by_file[str(Path(filename).resolve())] |= lines

    files = {}
    for path in sorted(source.rglob("*.py")):
        statements = executable_lines(path)
        executed = statements & hits_by_file.get(str(path.resolve()), set())
        files[str(path.relative_to(top_level)).replace("\\", "/")] = {
            "statements": len(statements),
            "executed": len(executed),
            "missing": sorted(statements - executed),
        }
    total = sum(f["statements"] for f in files.values())
    executed = sum(f["executed"] for f in files.values())
    summary = {
        "files": files,
        "statements": total,
        "executed": executed,
        "percent": 100.0 * executed / total if total else 100.0,
        "tests_run": result.testsRun,
        "failures": len(result.failures),
        "errors": len(result.errors),
        "skipped": len(result.skipped),
    }
    return result, summary


def compress_ranges(numbers: list[int]) -> str:
    """[1, 2, 3, 7] -> "1-3, 7"."""
    parts: list[str] = []
    start = previous = None
    for number in numbers:
        if start is None:
            start = previous = number
        elif number == previous + 1:
            previous = number
        else:
            parts.append(f"{start}-{previous}" if start != previous else str(start))
            start = previous = number
    if start is not None:
        parts.append(f"{start}-{previous}" if start != previous else str(start))
    return ", ".join(parts)


def print_report(summary: dict) -> None:
    print()
    print(f"{'Python file':<36} {'Stmts':>6} {'Miss':>6} {'Cover':>7}  Missing lines")
    print("-" * 100)
    for name, data in summary["files"].items():
        statements, executed = data["statements"], data["executed"]
        percent = 100.0 * executed / statements if statements else 100.0
        print(f"{name:<36} {statements:>6} {statements - executed:>6} {percent:>6.1f}%  "
              f"{compress_ranges(data['missing'])[:60]}")
    print("-" * 100)
    print(f"{'TOTAL':<36} {summary['statements']:>6} {summary['statements'] - summary['executed']:>6} "
          f"{summary['percent']:>6.1f}%")


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", required=True, type=Path, help="package directory to measure")
    parser.add_argument("--tests", required=True, type=Path, help="directory containing test_*.py files")
    parser.add_argument("--top-level", type=Path, default=Path.cwd(), help="project root (default: current directory)")
    parser.add_argument("--json", type=Path, help="write the summary to this file")
    parser.add_argument("--fail-under", type=float, default=0.0, help="fail if total coverage is below this percentage")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args(argv)

    result, summary = measure(args.source.resolve(), args.tests.resolve(), args.top_level.resolve(),
                              2 if args.verbose else 1)
    print_report(summary)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    if not result.wasSuccessful():
        return 1
    if summary["percent"] < args.fail_under:
        print(f"Coverage {summary['percent']:.1f}% is below the required {args.fail_under:.1f}%")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
