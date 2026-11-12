"""A small mutation tester for the IDE's toolkit-independent core (ide/thunder_ide/core).

Mutation testing judges the *tests*: each mutant is the real code with one deliberate bug
(a flipped comparison, an off-by-one constant, an `and` for an `or`...). Good tests kill the
mutant (at least one test fails); a surviving mutant is a bug the tests would let through.

    python tools/mutation.py                 # the default set of pure modules
    python tools/mutation.py --module edits  # one module
    python tools/mutation.py --list          # show the mutants without running anything

Only the standard library is used. Each mutant runs the unit tests that cover its module
(fast, GUI-free ones), with --failfast, in parallel subprocesses; the original file is
restored whatever happens. Exits non-zero if the mutation score is below --fail-under.
"""

from __future__ import annotations

import argparse
import ast
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "ide" / "thunder_ide" / "core"

# Which fast test modules exercise which core module. Pure modules only: mutating the Qt
# layer or the toolchain would need a GUI or the C++ build and take minutes per mutant.
TESTS_FOR = {
    "bitwise": ["tests.python.test_bitwise"],
    "edits": ["tests.python.test_edits"],
    "editing": ["tests.python.test_layout_settings_editing"],
    "layout": ["tests.python.test_layout_settings_editing"],
    "settings": ["tests.python.test_layout_settings_editing"],
    "highlight": ["tests.python.test_highlight"],
    "direction": ["tests.python.test_direction"],
    "canvas": ["tests.python.test_canvas"],
    "debugger": ["tests.python.test_debugger"],
}

_COMPARES = {ast.Lt: ast.LtE, ast.LtE: ast.Lt, ast.Gt: ast.GtE, ast.GtE: ast.Gt, ast.Eq: ast.NotEq, ast.NotEq: ast.Eq}
_ARITHMETIC = {ast.Add: ast.Sub, ast.Sub: ast.Add}
_BOOLEAN = {ast.And: ast.Or, ast.Or: ast.And}


@dataclass
class Mutant:
    module: str
    line: int
    description: str
    source: str  # the whole mutated file


def _mutate_node(tree: ast.Module, target: ast.AST, change) -> str:
    change(target)
    try:
        return ast.unparse(tree)
    finally:
        change(target)  # the same change undoes itself (all swaps below are involutions)


def mutants_of(path: Path) -> list[Mutant]:
    """Every single-change mutant of one file, each with a one-line description."""
    source = path.read_text(encoding="utf-8")
    tree = ast.parse(source)
    found: list[Mutant] = []
    module = path.stem

    # Constants inside decorators (e.g. @dataclass(frozen=True)) do not change behaviour the
    # tests could see: mutating them only produces equivalent mutants that muddy the score.
    decorator_nodes: set[int] = set()
    for owner in ast.walk(tree):
        for decorator in getattr(owner, "decorator_list", []):
            decorator_nodes.update(id(inner) for inner in ast.walk(decorator))

    def add(node: ast.AST, description: str, change) -> None:
        if id(node) in decorator_nodes:
            return
        found.append(Mutant(module, getattr(node, "lineno", 0), description, _mutate_node(tree, node, change)))

    for node in ast.walk(tree):
        if isinstance(node, ast.Compare) and len(node.ops) == 1 and type(node.ops[0]) in _COMPARES:
            swapped = _COMPARES[type(node.ops[0])]
            def flip(n, cls=swapped, original=type(node.ops[0])):
                n.ops[0] = cls() if isinstance(n.ops[0], original) else original()
            add(node, f"comparison {ast.unparse(node)!r}: flipped", flip)
        elif isinstance(node, ast.BinOp) and type(node.op) in _ARITHMETIC:
            swapped = _ARITHMETIC[type(node.op)]
            def arith(n, cls=swapped, original=type(node.op)):
                n.op = cls() if isinstance(n.op, original) else original()
            add(node, f"arithmetic {ast.unparse(node)!r}: +/- swapped", arith)
        elif isinstance(node, ast.BoolOp) and type(node.op) in _BOOLEAN:
            swapped = _BOOLEAN[type(node.op)]
            def logic(n, cls=swapped, original=type(node.op)):
                n.op = cls() if isinstance(n.op, original) else original()
            add(node, f"boolean {ast.unparse(node)[:40]!r}: and/or swapped", logic)
        elif isinstance(node, ast.Constant) and node.value is True:
            def flip_bool(n):
                n.value = not n.value
            add(node, "constant True -> False", flip_bool)
        elif isinstance(node, ast.Constant) and isinstance(node.value, int) and not isinstance(node.value, bool) \
                and 0 <= node.value <= 4:
            def bump(n):
                n.value = n.value + 1 if n.value != -1 else -2  # involution via second call below
            # bump is not an involution, so apply/undo by value instead:
            original = node.value
            def change(n, original=original):
                n.value = original + 1 if n.value == original else original
            add(node, f"constant {original} -> {original + 1}", change)
    return found


def run_mutant(mutant: Mutant, path: Path, env: dict) -> bool:
    """True if the tests killed the mutant. The mutated text goes to the real path, because the
    tests import the package by name; the caller restores the original afterwards."""
    path.write_text(mutant.source, encoding="utf-8")
    try:
        completed = subprocess.run(
            [sys.executable, "-m", "unittest", *TESTS_FOR[mutant.module], "--failfast", "-q"],
            capture_output=True, timeout=120, cwd=ROOT, env=env)
        return completed.returncode != 0
    except subprocess.TimeoutExpired:
        return True  # an endless loop is a very dead program


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--module", action="append", help="core module name(s), e.g. edits (default: all mapped)")
    parser.add_argument("--list", action="store_true", help="only list the mutants")
    parser.add_argument("--fail-under", type=float, default=0.0, help="fail if the kill rate is below this %%")
    arguments = parser.parse_args(argv)
    modules = arguments.module or sorted(TESTS_FOR)

    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join([str(ROOT / "ide"), str(ROOT)])
    env.setdefault("PYTHONDONTWRITEBYTECODE", "1")

    killed = survived = 0
    survivors: list[Mutant] = []
    for module in modules:
        path = CORE / f"{module}.py"
        original = path.read_text(encoding="utf-8")
        mutants = mutants_of(path)
        print(f"-- {module}: {len(mutants)} mutant(s)", flush=True)
        if arguments.list:
            for mutant in mutants:
                print(f"   {module}.py:{mutant.line}: {mutant.description}")
            continue
        try:
            # One worker per mutant batch would race on the single file: mutants of one module
            # run serially, but several modules could be interleaved. Keep it simple and serial
            # per module; the test runs themselves are the cost and they are short.
            for index, mutant in enumerate(mutants):
                if run_mutant(mutant, path, env):
                    killed += 1
                else:
                    survived += 1
                    survivors.append(mutant)
                    print(f"   SURVIVED {module}.py:{mutant.line}: {mutant.description}", flush=True)
                if (index + 1) % 25 == 0:
                    print(f"   ... {index + 1}/{len(mutants)}", flush=True)
        finally:
            path.write_text(original, encoding="utf-8")

    total = killed + survived
    if total:
        score = 100.0 * killed / total
        print(f"\nmutation score: {killed}/{total} killed ({score:.1f}%)")
        if survivors:
            print("surviving mutants (each is a bug the tests would miss):")
            for mutant in survivors:
                print(f"  {mutant.module}.py:{mutant.line}: {mutant.description}")
        return 0 if score >= arguments.fail_under else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
