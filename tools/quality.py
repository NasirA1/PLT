"""Code-quality metrics for the summary printed by run_tests: complexity, size and static analysis.

Only the standard library is used, so the metrics work on any machine with Python.
"""

from __future__ import annotations

import ast
import re
import shutil
import subprocess
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class FunctionComplexity:
    name: str
    file: str
    complexity: int


@dataclass
class ComplexityReport:
    functions: list[FunctionComplexity] = field(default_factory=list)

    @property
    def average(self) -> float:
        return sum(f.complexity for f in self.functions) / len(self.functions) if self.functions else 0.0

    @property
    def worst(self) -> Optional[FunctionComplexity]:
        return max(self.functions, key=lambda f: f.complexity, default=None)

    def above(self, threshold: int) -> list[FunctionComplexity]:
        return sorted((f for f in self.functions if f.complexity > threshold), key=lambda f: -f.complexity)


# ---------------------------------------------------------------------- Python (exact, via the AST)

def _python_decisions(node: ast.AST) -> int:
    """McCabe complexity contributions of the nodes inside one function (not nested functions)."""
    count = 0
    for child in ast.iter_child_nodes(node):
        if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)):
            continue
        if isinstance(child, (ast.If, ast.For, ast.AsyncFor, ast.While, ast.IfExp, ast.ExceptHandler, ast.With,
                              ast.AsyncWith, ast.Assert)):
            count += 1
        elif isinstance(child, ast.BoolOp):
            count += len(child.values) - 1
        elif isinstance(child, ast.comprehension):
            count += 1 + len(child.ifs)
        elif isinstance(child, ast.match_case):
            count += 1
        count += _python_decisions(child)
    return count


def python_complexity(files: list[Path], root: Path) -> ComplexityReport:
    report = ComplexityReport()
    for path in files:
        tree = ast.parse(path.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                report.functions.append(FunctionComplexity(node.name, str(path.relative_to(root)),
                                                           1 + _python_decisions(node)))
    return report


# ---------------------------------------------------------------------- C++ (approximate, lexical)

_CPP_NOISE = re.compile(r'//[^\n]*|/\*.*?\*/|R"\((?:.|\n)*?\)"|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.DOTALL)
_CPP_DECISION = re.compile(r"\b(?:if|for|while|case|catch)\b|&&|\|\||\?")
_CPP_CONTROL = re.compile(r"\b(?:if|for|while|switch|catch|else|do|try)\s*(?:\(.*\))?\s*$", re.DOTALL)
_CPP_NAME = re.compile(r"([A-Za-z_~][\w:~<>]*)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*(?:const|override|noexcept|final|->[^{]*)*\s*$",
                       re.DOTALL)


def cpp_complexity(files: list[Path], root: Path) -> ComplexityReport:
    """Counts decision points (if, for, while, case, catch, &&, ||, ?:) in each function body.

    Function bodies are found lexically: a top-level '{' after something that looks like a
    parameter list. Lambdas count towards the function that contains them.
    """
    report = ComplexityReport()
    for path in files:
        text = _CPP_NOISE.sub(lambda m: " " * len(m.group()) if "\n" not in m.group() else "\n" * m.group().count("\n"),
                              path.read_text(encoding="utf-8"))
        depth = 0
        function_depth: Optional[int] = None
        function_name = ""
        function_start = 0
        statement_start = 0
        for index, char in enumerate(text):
            if char == "{":
                header = text[statement_start:index]
                if function_depth is None and not _CPP_CONTROL.search(header):
                    match = _CPP_NAME.search(header)
                    if match and "=" not in header.split("(")[0]:
                        function_depth = depth
                        function_name = match.group(1).split("::")[-1] if "operator" not in match.group(1) else match.group(1)
                        function_start = index
                depth += 1
                statement_start = index + 1
            elif char == "}":
                depth -= 1
                statement_start = index + 1
                if function_depth is not None and depth == function_depth:
                    body = text[function_start:index]
                    report.functions.append(FunctionComplexity(function_name, str(path.relative_to(root)),
                                                               1 + len(_CPP_DECISION.findall(body))))
                    function_depth = None
            elif char == ";":
                statement_start = index + 1
    return report


# ---------------------------------------------------------------------- size

@dataclass
class LineCounts:
    files: int = 0
    code: int = 0
    comments: int = 0
    blank: int = 0


def count_lines(files: list[Path], comment_prefixes: tuple[str, ...]) -> LineCounts:
    counts = LineCounts()
    for path in files:
        counts.files += 1
        for line in path.read_text(encoding="utf-8").splitlines():
            stripped = line.strip()
            if not stripped:
                counts.blank += 1
            elif stripped.startswith(comment_prefixes):
                counts.comments += 1
            else:
                counts.code += 1
    return counts


# ---------------------------------------------------------------------- clang-tidy

@dataclass
class TidyReport:
    available: bool
    findings: Counter = field(default_factory=Counter)
    message: str = ""

    @property
    def total(self) -> int:
        return sum(self.findings.values())


def run_clang_tidy(files: list[Path], build_dir: Path, jobs: int = 4) -> TidyReport:
    executable = shutil.which("clang-tidy")
    if executable is None:
        return TidyReport(False, message="clang-tidy not installed")
    if not (build_dir / "compile_commands.json").is_file():
        return TidyReport(False, message="no compile_commands.json (use the Ninja or Makefile generator)")

    def check(path: Path) -> str:
        completed = subprocess.run([executable, "-p", str(build_dir), "--quiet", str(path)], capture_output=True,
                                   text=True, encoding="utf-8", errors="replace")
        return completed.stdout

    report = TidyReport(True)
    seen: set[str] = set()
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        for output in pool.map(check, files):
            for line in output.splitlines():
                match = re.search(r"^(.*?:\d+:\d+): warning: .* \[([\w.,-]+)\]$", line)
                if match and match.group(0) not in seen:  # headers are reported once per includer
                    seen.add(match.group(0))
                    report.findings[match.group(2)] += 1
    return report
