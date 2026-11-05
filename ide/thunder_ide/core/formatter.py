"""Re-indents Thunder code in Allman style.

Allman style puts every brace on a line of its own, aligned with the statement
that owns it, and indents the contents by four spaces::

    if (x > 0)
    {
        println("positive");
    }
    else
    {
        println("not positive");
    }

The formatter works line by line: it splits lines at braces that are not
inside strings or comments and re-indents everything by brace depth. Blank
lines and comments are kept.
"""

from __future__ import annotations

import re

INDENT = "    "

# Splits a line into strings, line comments and other code.
_PIECES = re.compile(r'"(?:\\.|[^"\\])*"?|«(?:\\.|[^»\\])*»?|//.*|[^"«/]+|/')


def _split_braces(code: str) -> list[str]:
    """Splits one line of code into parts, putting each brace in a part of its own."""
    parts: list[str] = []
    current = ""
    for piece in _PIECES.findall(code):
        if piece.startswith(('"', "«", "//")):
            current += piece
            continue
        for char in piece:
            if char in "{}":
                if current.strip():
                    parts.append(current.strip())
                parts.append(char)
                current = ""
            else:
                current += char
    if current.strip():
        parts.append(current.strip())
    return parts


_CONTINUES = ("+", "-", "*", "/", "%", "×", "÷", "٪", "&&", "||", "==", "!=", "<", ">", "<=", ">=", "=", ",", "،", "(", "[")


def _code_part(line: str) -> str:
    """The line without strings and a trailing comment (for bracket counting)."""
    return "".join('""' if piece.startswith(('"', "«")) else piece
                   for piece in _PIECES.findall(line) if not piece.startswith("//"))


def format_allman(text: str) -> str:
    """Returns ``text`` re-formatted in Allman style (idempotent).

    A line that continues the previous statement (because that line ended with an operator or
    a comma, or left a bracket open) is indented one extra level.
    """
    output: list[str] = []
    depth = 0
    open_brackets = 0
    continues = False
    in_block_comment = False

    for raw_line in text.replace("\r\n", "\n").split("\n"):
        stripped = raw_line.strip()

        if in_block_comment:
            output.append(raw_line.rstrip())
            if "*/" in stripped:
                in_block_comment = False
            continue
        if stripped.startswith("/*"):
            output.append(INDENT * depth + stripped)
            in_block_comment = "*/" not in stripped
            continue
        if not stripped:
            output.append("")
            continue
        if stripped.startswith("//"):
            output.append(INDENT * depth + stripped)
            continue

        extra = INDENT if (continues or open_brackets > 0) and not stripped.startswith(("{", "}")) else ""
        # A comment that followed a brace on the same line ends up on its own line.
        for part in _split_braces(stripped):
            if part == "}":
                depth = max(depth - 1, 0)
            output.append(INDENT * depth + (extra if part not in "{}" else "") + part)
            if part == "{":
                depth += 1

        code = _code_part(stripped).rstrip()
        open_brackets = max(open_brackets + code.count("(") + code.count("[") - code.count(")") - code.count("]"), 0)
        continues = code.endswith(_CONTINUES) and not code.endswith(("++", "--"))

    # Collapse runs of blank lines created by splitting and drop trailing blanks.
    result: list[str] = []
    for line in output:
        if line == "" and result and result[-1] == "":
            continue
        result.append(line)
    while result and result[-1] == "":
        result.pop()
    return "\n".join(result) + "\n"
