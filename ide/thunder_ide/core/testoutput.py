"""Colours for a test file's transcript in the Output panel.

The testing module prints ``✓ name`` for a test that passed, ``✗ name: message`` for each
expectation that failed, and a summary - ``3 passed, 1 failed`` (Pashto ``۳ تېرې شوې، ۱
ناکامې``), per suite too (``numbers: 2 passed, 0 failed``). This module splits output into
styled pieces: the marks green and red, and in a summary the passed count green and a failed
count red (a failed count of zero is not an alarm and keeps the ordinary colour).

Output arrives in pieces of any length, so the caller keeps one bit of state between them -
whether the next piece starts a line - and a mark is only a mark at the start of a line
(after the indentation of a suite's tests). Pure text work: no toolkit, tested on its own.
"""

from __future__ import annotations

import re

PASS = "test_pass"
FAIL = "test_fail"

_DIGITS = r"[0-9۰-۹٠-٩]+"
# "3 passed, 1 failed" / "۳ تېرې شوې، ۱ ناکامې", after an optional "suite: ".
_SUMMARY = re.compile(
    rf"^(?P<head>.*?: )?(?P<passed>{_DIGITS} (?:passed|تېرې شوې))(?P<between>[,،] )"
    rf"(?P<failed>(?P<count>{_DIGITS}) (?:failed|ناکامې))$")
_MARK = re.compile(r"^(?P<indent>[ \t]*)(?P<mark>[✓✗])")


def _is_zero(count: str) -> bool:
    return all(digit in "0۰٠" for digit in count)


def _line_segments(line: str, at_line_start: bool) -> list[tuple[str, str | None]]:
    if not at_line_start:
        return [(line, None)]
    summary = _SUMMARY.match(line)
    if summary:
        pieces: list[tuple[str, str | None]] = []
        if summary.group("head"):
            pieces.append((summary.group("head"), None))
        pieces.append((summary.group("passed"), PASS))
        pieces.append((summary.group("between"), None))
        pieces.append((summary.group("failed"), None if _is_zero(summary.group("count")) else FAIL))
        return pieces
    mark = _MARK.match(line)
    if mark:
        style = PASS if mark.group("mark") == "✓" else FAIL
        pieces = []
        if mark.group("indent"):
            pieces.append((mark.group("indent"), None))
        pieces.append((mark.group("mark"), style))
        if mark.end() < len(line):
            pieces.append((line[mark.end():], None))
        return pieces
    return [(line, None)]


def segments(text: str, at_line_start: bool) -> tuple[list[tuple[str, str | None]], bool]:
    """``text`` as (piece, style) pairs - style PASS, FAIL or None - and whether the output that
    follows starts a line. Joining the pieces gives ``text`` back unchanged."""
    result: list[tuple[str, str | None]] = []
    lines = text.split("\n")
    for index, line in enumerate(lines):
        if index:
            result.append(("\n", None))
            at_line_start = True
        if line:
            result.extend(_line_segments(line, at_line_start))
            at_line_start = False
    # Merge neighbours of one style, so the panel inserts as few pieces as it can.
    merged: list[tuple[str, str | None]] = []
    for piece, style in result:
        if merged and merged[-1][1] == style:
            merged[-1] = (merged[-1][0] + piece, style)
        else:
            merged.append((piece, style))
    return merged, at_line_start
