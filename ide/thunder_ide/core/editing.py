"""Editing behaviour shared by every editor implementation: indentation-aware typing.

Thunder marks blocks by indentation, so the editor helps the way Python editors do: Enter keeps
the current indentation, indents one level after a line that opens a block (``if``, ``else``,
``while``, ``for``, ``func``, ``struct``, or their Pashto spellings) and un-indents after a line
that leaves one (``return``, ``break``, ``continue``, ``pass``). Tab inserts four spaces and
Backspace in the leading whitespace removes one level.

These are pure functions so that any GUI toolkit gives the same behaviour. The words that open
and leave blocks come from the compiler's language description (see ``BlockWords``).
"""

from __future__ import annotations

from dataclasses import dataclass, field

INDENT = "    "

# Letters that Arabic (not Pashto) keyboard layouts produce, folded to the Pashto letters the
# language registers: ك (U+0643) -> ک (U+06A9), گ (U+06AF) -> ګ (U+06AB), and the yeh variants
# ي (U+064A) and ى (U+0649) -> ی (U+06CC). The compiler folds the same way (fold_letters in
# src/core/language_pack.cpp), so كرښه چاپ or متغير typed on such a keyboard is recognised
# everywhere.
_ARABIC_FOLD = str.maketrans({"\u0643": "\u06a9", "\u06af": "\u06ab", "\u064a": "\u06cc", "\u0649": "\u06cc"})


def fold_letters(text: str) -> str:
    return text.translate(_ARABIC_FOLD)

# Token kinds (as reported by thunderc --language-info) whose statements open or leave a block.
BLOCK_OPENING_KINDS = ("KwIf", "KwElse", "KwWhile", "KwFor", "KwFunc", "KwStruct")
BLOCK_LEAVING_KINDS = ("KwReturn", "KwBreak", "KwContinue", "KwPass")


@dataclass(frozen=True)
class BlockWords:
    """The keywords that start a block and the ones after which a block usually ends."""

    opening: frozenset[str] = frozenset({"if", "else", "while", "for", "func", "struct"})
    leaving: frozenset[str] = frozenset({"return", "break", "continue", "pass"})

    @classmethod
    def from_language_info(cls, info: dict) -> "BlockWords":
        opening: set[str] = set()
        leaving: set[str] = set()
        for keyword in info.get("keywords", []):
            spellings = keyword.get("spellings", [keyword.get("text", "")])
            if keyword.get("kind") in BLOCK_OPENING_KINDS:
                opening.update(spellings)
            elif keyword.get("kind") in BLOCK_LEAVING_KINDS:
                leaving.update(spellings)
        return cls(frozenset(opening), frozenset(leaving)) if opening else cls()


@dataclass(frozen=True)
class Edit:
    """Replace the text before the cursor on the current line with ``before`` and insert
    ``insert`` at the cursor; then move the cursor ``cursor_back`` characters back."""

    before: str
    insert: str
    cursor_back: int = 0


def indent_of(line: str) -> str:
    return line[: len(line) - len(line.lstrip(" \t"))]


def first_word(line: str) -> str:
    """The first word of a line of code (letters, digits, underscore in any script)."""
    stripped = line.lstrip(" \t")
    end = 0
    while end < len(stripped) and (stripped[end].isalnum() or stripped[end] == "_"):
        end += 1
    return stripped[:end]


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
            if char in closing:
                closing = None
        elif char == '"':
            closing = '"'
        elif char == "«":
            closing = "»"
        elif char in "\u201c\u201d\u201e":  # any curly double quote closes a curly-quoted string
            closing = "\u201c\u201d\u201e"
        elif prefix.startswith("//", index):
            return True
        index += 1
    return closing is not None


def _code_part(line: str) -> str:
    """The line without a trailing // comment (strings are respected)."""
    closing = None
    for index, char in enumerate(line):
        if closing is not None:
            if char in closing:
                closing = None
        elif char == '"':
            closing = '"'
        elif char == "«":
            closing = "»"
        elif char in "\u201c\u201d\u201e":  # any curly double quote closes a curly-quoted string
            closing = "\u201c\u201d\u201e"
        elif line.startswith("//", index):
            return line[:index]
    return line


def on_return(before: str, after: str, words: BlockWords = BlockWords()) -> Edit:
    """Enter: keep the indentation; one level more after a block header, one less after
    return/break/continue/pass. ``after`` is the rest of the line (kept on the new line)."""
    indent = indent_of(before)
    code = _code_part(before).strip()  # a trailing comment does not matter
    word = fold_letters(first_word(code))
    if code and word in words.opening and not inside_string_or_comment(code):
        return Edit(before, "\n" + indent + INDENT)
    if code and word in words.leaving and not inside_string_or_comment(code):
        return Edit(before, "\n" + indent[: max(len(indent) - len(INDENT), 0)])
    del after  # the text after the cursor stays as it is
    return Edit(before, "\n" + indent)


def on_tab(before: str) -> Edit:
    """Tab inserts one level of indentation (spaces, never a tab character)."""
    return Edit(before, INDENT)


def on_backspace(before: str) -> Edit | None:
    """Backspace inside the leading whitespace removes a whole level; None means "default behaviour"."""
    if not before or before.strip():
        return None
    remove = len(before) % len(INDENT) or len(INDENT)
    return Edit(before[:-remove], "")
