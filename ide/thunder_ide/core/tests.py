"""The tests of a test file, for the Thunder IDE: finding them, and reading their results.

A test file marks its tests with ``@test`` (Pashto ``@ازموینه``): a function without parameters
is a test; a struct or module marked ``@test`` is a suite whose public methods (or functions)
without parameters are its tests; ``@test`` on one method of a struct or function of a module
makes that one a test of that suite. This module finds them in the text the way the compiler
does - the same rules as the VS Code extension's ``src/testing.js``, whose tests and these
check the same cases - so the editor can put a button beside each one before anything is
compiled.

Running is the testing module's own business: the program's arguments choose the tests
("suite/name", a suite's name, or a test's name), and its transcript says what happened -
a suite's name on a line of its own, then ``✓ name`` for a pass and ``✗ name: message`` for
each failed expectation. Pure text work: no toolkit, tested on its own.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Optional


def _fold(word: str) -> str:
    return word.replace("ي", "ی").replace("ى", "ی").replace("ك", "ک").replace("گ", "ګ")


TEST_WORDS = {"test", "ازموینه", "آزموینه"}
MOCK_WORDS = {"mock", "پېښه ګر", "پېښه_ګر"}
CLOSE_WORDS = {"close", "وتړه"}

_ANNOTATION = re.compile(r"^(\s*)@([^\s(#]+(?:[ \t]+[ګگ]ر)?)\s*(?:\(\s*(?:\"([^\"]*)\"|«([^»]*)»|[“„]([^”“]*)[”“])\s*\))?\s*(?:#.*)?$")
_FUNCTION = re.compile(r"^(\s*)(?:func|دنده)\s+([^\s(<]+)\s*(<[^>]*>)?\s*\(([^)]*)\)\s*(:)?")
_SUITE = re.compile(r"^(\s*)(struct|جوړښت|module|ټولګی|ټولګى|ټولگی|ټولگى)\s+([^\s:<]+)")
_OTHER_BLOCK = re.compile(r"^(\s*)(?:mixin|ګډون|گډون|interface|برسېر|برسیر|enum|ګڼنه|گڼنه)\s")

_PASS = re.compile(r"^✓ (.*)$")
_FAIL = re.compile(r"^✗ (.*?): (.*)$")
_SUITE_LINE = re.compile(r"^(\S[^:✓✗]*)$")


@dataclass(frozen=True)
class Test:
    name: str
    suite: str   # "" for a test outside any suite
    line: int    # 0-based: the annotation's line, or the declaration's for a test of a marked suite
    end: int     # the last line of its block

    @property
    def key(self) -> str:
        """The program argument that runs it: "suite/name", or the name."""
        return f"{self.suite}/{self.name}" if self.suite else self.name


@dataclass(frozen=True)
class Suite:
    name: str
    line: int
    end: int

    @property
    def key(self) -> str:
        return self.name


@dataclass
class Result:
    state: str                    # "running", "passed", "failed", "errored" or "skipped"
    messages: list[str] = field(default_factory=list)


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip())


def display_name(declared: str) -> str:
    """``largest_of_a_list`` -> ``largest of a list``: a test's name when its annotation gives none."""
    return declared.replace("_", " ").strip() or declared


def block_end(lines: list[str], start: int) -> int:
    """The last line of the declaration at (or after the annotations on) ``start``."""
    declaration = start
    while declaration < len(lines) and (lines[declaration].lstrip().startswith(("@", "#")) or not lines[declaration].strip()):
        declaration += 1
    if declaration >= len(lines):
        return start
    indent = _indent(lines[declaration])
    end = declaration
    for index in range(declaration + 1, len(lines)):
        line = lines[index]
        if not line.strip():
            continue
        if _indent(line) <= indent:
            break
        end = index
    return end


def discover(text: str) -> tuple[list[Test], list[Suite]]:
    """The tests a file declares, in the order they run, and the suites that hold any."""
    lines = text.split("\n")
    tests: list[Test] = []
    suites: list[Suite] = []
    pending: list[tuple[str, Optional[str], int]] = []  # (word, description, line) since the last declaration
    suite: Optional[dict] = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        indent = _indent(line)
        if suite is not None and indent <= suite["indent"]:
            suite = None  # the suite's block has ended
        annotation = _ANNOTATION.match(line)
        if annotation:
            description = next((group for group in annotation.groups()[2:] if group is not None), None)
            pending.append((_fold(re.sub(r"[ \t]+", " ", annotation.group(2))), description, index))
            continue
        marks, pending = pending, []
        test = next((mark for mark in marks if mark[0] in TEST_WORDS), None)
        mock = any(mark[0] in MOCK_WORDS for mark in marks)
        declared = _SUITE.match(line)
        if declared:
            name = test[1] if test is not None and test[1] is not None else declared.group(3)
            suite = None if mock else {"name": name, "marked": test is not None, "indent": indent,
                                       "line": test[2] if test is not None else index}
            continue
        if _OTHER_BLOCK.match(line):
            suite = {"name": "", "marked": False, "indent": indent, "inert": True, "line": index}
            continue
        function = _FUNCTION.match(line)
        if not function:
            continue
        _, name, generic, parameters, returns = function.groups()
        if suite is not None and suite.get("inert") and indent > suite["indent"]:
            continue
        in_suite = suite is not None and indent > suite["indent"]
        shaped = not generic and not parameters.strip() and not returns
        implied = (test is None and in_suite and suite["marked"] and shaped and not name.startswith("_")
                   and _fold(name) not in CLOSE_WORDS)
        if (test is None or not shaped) and not implied:
            continue
        start = test[2] if test is not None else index
        tests.append(Test(test[1] if test is not None and test[1] is not None else display_name(name),
                          suite["name"] if in_suite else "", start, block_end(lines, start)))
        if in_suite and not any(known.name == suite["name"] for known in suites):
            suites.append(Suite(suite["name"], suite["line"], block_end(lines, suite["line"])))
    return tests, suites


def parse_transcript(stdout: str, suites: set[str]) -> tuple[set[str], dict[str, list[str]]]:
    """What the testing module printed, by test key: the passes, and each failure's messages.

    A line that is neither a pass nor a failure and is not indented is a suite's heading - but
    only a suite the file declares, so a line the program itself prints is never taken for one."""
    passed: set[str] = set()
    failed: dict[str, list[str]] = {}
    suite = ""
    for line in stdout.splitlines():
        indented = line[:1].isspace()
        text = line.strip()
        key = lambda name: f"{suite}/{name}" if indented and suite else name  # noqa: E731
        found = _PASS.match(text)
        if found:
            passed.add(key(found.group(1)))
            continue
        found = _FAIL.match(text)
        if found:
            failed.setdefault(key(found.group(1)), []).append(found.group(2))
            continue
        if not indented and _SUITE_LINE.match(line) and text in suites:
            suite = text
    return passed, failed


@dataclass(frozen=True)
class TestMark:
    """A button beside a test or a suite in the editor's gutter."""

    line: int        # 0-based
    key: str
    name: str
    kind: str        # "test" or "suite"
    state: str       # "" (not run), "running", "passed" or "failed"
    message: str = ""  # a failed test's first message, shown at the end of its line


@dataclass(frozen=True)
class TestPeek:
    """The panel under a failed test: its name and everything it reported."""

    line: int
    title: str
    messages: tuple[str, ...]


def state_of(key: str, results: dict[str, Result]) -> str:
    result = results.get(key)
    if result is None or result.state == "skipped":
        return ""
    return "failed" if result.state == "errored" else result.state


def suite_state(suite: Suite, tests: list[Test], results: dict[str, Result]) -> str:
    states = [state_of(test.key, results) for test in tests if test.suite == suite.name]
    if "running" in states:
        return "running"
    if "failed" in states:
        return "failed"
    return "passed" if states and all(state == "passed" for state in states) else ""


def marks(tests: list[Test], suites: list[Suite], results: dict[str, Result]) -> list[TestMark]:
    """The gutter's buttons: one per suite and one per test, with their states."""
    found = [TestMark(suite.line, suite.key, suite.name, "suite", suite_state(suite, tests, results)) for suite in suites]
    for test in tests:
        state = state_of(test.key, results)
        result = results.get(test.key)
        message = result.messages[0] if state == "failed" and result is not None and result.messages else ""
        found.append(TestMark(test.line, test.key, test.name, "test", state, message))
    return sorted(found, key=lambda mark: mark.line)


def at_line(line: int, tests: list[Test], suites: list[Suite], containing: bool = False):
    """The test or suite declared on a line; with ``containing``, the innermost one whose block
    holds it (Run Test at Cursor)."""
    for item in [*tests, *suites]:
        if item.line == line:
            return item
    if not containing:
        return None
    around = [item for item in [*tests, *suites] if item.line <= line <= item.end]
    return min(around, key=lambda item: item.end - item.line) if around else None


def keys_under(item, tests: list[Test]) -> list[str]:
    """The keys of the tests a run of ``item`` (a test or a suite) covers."""
    if isinstance(item, Suite):
        return [test.key for test in tests if test.suite == item.name]
    return [item.key]
