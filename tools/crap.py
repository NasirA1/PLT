"""The CRAP score of every Python function: complexity vs. test coverage.

CRAP (Change Risk Anti-Patterns, Savoia & Lowell) combines cyclomatic complexity with
coverage: CRAP(m) = comp(m)^2 * (1 - cov(m))^3 + comp(m). A simple, well-tested function
scores near 1; above 30 is commonly treated as "fix this": either test it or simplify it.

    python tools/pycoverage.py --source ide/thunder_ide --tests tests/python \
        --json build/dev/python-coverage.json       # (run_tests.py writes this file too)
    python tools/crap.py --coverage build/dev/python-coverage.json

Only the standard library is used; complexity comes from tools/quality.py.
"""

from __future__ import annotations

import argparse
import ast
import json
from pathlib import Path

from quality import _python_decisions  # the same McCabe count the quality summary uses

ROOT = Path(__file__).resolve().parents[1]


def function_spans(path: Path) -> list[tuple[str, int, int, int]]:
    """(name, first line, last line, complexity) of every function in a file."""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    spans = []
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            spans.append((node.name, node.lineno, node.end_lineno or node.lineno, 1 + _python_decisions(node)))
    return spans


def crap_scores(coverage: dict) -> list[tuple[float, str, str, int, float]]:
    """(score, file, function, complexity, coverage%) for every function, worst first."""
    rows = []
    for name, data in coverage["files"].items():
        path = ROOT / name
        if not path.is_file():
            continue
        missing = set(data["missing"])
        for function, first, last, complexity in function_spans(path):
            lines = set(range(first, last + 1))
            statements = lines - {first}  # the def line itself is always "covered" by import
            missed = len(statements & missing)
            # Approximate statement count inside the span from the file's statement lines:
            covered_fraction = 1.0 - (missed / max(len(statements), 1))
            score = complexity ** 2 * (1.0 - covered_fraction) ** 3 + complexity
            rows.append((score, name, function, complexity, 100.0 * covered_fraction))
    rows.sort(reverse=True)
    return rows


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--coverage", type=Path, required=True, help="pycoverage.py --json output")
    parser.add_argument("--threshold", type=float, default=30.0, help="print functions at or above this score")
    parser.add_argument("--top", type=int, default=15, help="always print at least the top N")
    arguments = parser.parse_args(argv)

    coverage = json.loads(arguments.coverage.read_text(encoding="utf-8"))
    rows = crap_scores(coverage)
    over = [row for row in rows if row[0] >= arguments.threshold]
    print(f"{'CRAP':>7}  {'cx':>3}  {'cover':>6}  function")
    for score, name, function, complexity, percent in rows[:max(arguments.top, len(over))]:
        marker = "  <-- fix: test it or simplify it" if score >= arguments.threshold else ""
        print(f"{score:>7.1f}  {complexity:>3}  {percent:>5.1f}%  {name}: {function}{marker}")
    print(f"\n{len(over)} function(s) at or above CRAP {arguments.threshold:.0f} "
          f"(of {len(rows)}; worst possible for an untested function of complexity c is c^2 + c)")
    return 1 if over else 0


if __name__ == "__main__":
    raise SystemExit(main())
