"""Editing behaviour shared by every editor implementation: Allman-style auto-indentation.

These are pure functions so that any GUI toolkit gives the same behaviour.
"""

from __future__ import annotations

from dataclasses import dataclass

from thunder_ide.core.formatter import INDENT


@dataclass(frozen=True)
class Edit:
    """Replace the text before the cursor on the current line with ``before`` and insert
    ``insert`` at the cursor; then move the cursor ``cursor_back`` characters back."""

    before: str
    insert: str
    cursor_back: int = 0


def indent_of(line: str) -> str:
    return line[: len(line) - len(line.lstrip(" \t"))]


def inside_string_or_comment(prefix: str) -> bool:
    """True if the end of ``prefix`` (text of a line before the cursor) is inside a string or comment."""
    closing = None
    index = 0
    while index < len(prefix):
        char = prefix[index]
        if closing is not None:
            if char == "\\":
                index += 2
                continue
            if char == closing:
                closing = None
        elif char == '"':
            closing = '"'
        elif char == "«":
            closing = "»"
        elif prefix.startswith("//", index):
            return True
        index += 1
    return closing is not None


def on_return(before: str, after: str) -> Edit:
    """Enter: keep the indentation; after '{' indent one more level (and close a '{}' pair)."""
    indent = indent_of(before)
    if before.rstrip().endswith("{") and not inside_string_or_comment(before):
        if after.strip().startswith("}"):
            inner = "\n" + indent + INDENT
            return Edit(before, inner + "\n" + indent, cursor_back=len(indent) + 1)
        return Edit(before, "\n" + indent + INDENT)
    return Edit(before, "\n" + indent)


def on_open_brace(before: str) -> Edit:
    """'{' typed after code moves to a line of its own, aligned with the statement (Allman style)."""
    if before.strip() and not inside_string_or_comment(before):
        return Edit(before.rstrip() + "\n" + indent_of(before), "{")
    return Edit(before, "{")


def on_close_brace(before: str) -> Edit:
    """'}' typed on an otherwise empty line is moved back one level."""
    if before and not before.strip():
        return Edit(before[: max(len(before) - len(INDENT), 0)], "}")
    return Edit(before, "}")
