"""Editing behaviour shared by every editor implementation: indentation-aware typing.

Thunder marks blocks by indentation, so the editor helps the way Python editors do: Enter keeps
the current indentation, indents one level after a line that opens a block (``if``, ``else``,
``while``, ``for``, ``func``, ``struct``, ``interface``, ``enum``, ``module``, ``with``, or their Pashto
spellings - Pashto writes ``with`` after the handle, ``لاستی سره = ...``, or at the end of the
line, ``ساکټ سره``) and un-indents after a line
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
BLOCK_OPENING_KINDS = ("KwIf", "KwElse", "KwWhile", "KwFor", "KwFunc", "KwStruct", "KwEnum", "KwModule", "KwWith", "KwInterface", "KwMixin")
BLOCK_LEAVING_KINDS = ("KwReturn", "KwBreak", "KwContinue", "KwPass")


@dataclass(frozen=True)
class BlockWords:
    """The keywords that start a block and the ones after which a block usually ends."""

    opening: frozenset[str] = frozenset({"if", "else", "while", "for", "func", "struct", "enum", "module", "with", "interface"})
    leaving: frozenset[str] = frozenset({"return", "break", "continue", "pass"})
    # Words that open a block when they END the line: Pashto's "with" ("ساکټ سره").
    opening_last: frozenset[str] = frozenset()
    # The word that writes a function value ("func", "دنده"): a line ending with one of their
    # headers opens a block too, wherever it stands ("var t = Task(func()").
    function: frozenset[str] = frozenset({"func"})

    @classmethod
    def from_language_info(cls, info: dict) -> "BlockWords":
        opening: set[str] = set()
        leaving: set[str] = set()
        opening_last: set[str] = set()
        function: set[str] = set()
        for keyword in info.get("keywords", []):
            spellings = keyword.get("spellings", [keyword.get("text", "")])
            if keyword.get("kind") == "KwFunc":
                function.update(fold_letters(spelling) for spelling in spellings)
            if keyword.get("kind") == "KwWith" and info.get("with_after_resource"):
                opening_last.update(spellings)
            elif keyword.get("kind") in BLOCK_OPENING_KINDS:
                opening.update(spellings)
            elif keyword.get("kind") in BLOCK_LEAVING_KINDS:
                leaving.update(spellings)
        if not opening:
            return cls()
        return cls(frozenset(opening), frozenset(leaving), frozenset(opening_last),
                   frozenset(function) or cls().function)


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


def second_word(line: str) -> str:
    """The second word of a line of code ("" if there is none)."""
    stripped = line.lstrip(" \t")
    first = first_word(stripped)
    rest = stripped[len(first):].lstrip(" \t")
    return first_word(rest) if first and rest and rest[0] != "." else ""


def last_word(line: str) -> str:
    """The last word of a line of code (letters, digits, underscore in any script)."""
    stripped = line.rstrip(" \t")
    start = len(stripped)
    while start > 0 and (stripped[start - 1].isalnum() or stripped[start - 1] == "_"):
        start -= 1
    return stripped[start:]


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
        elif prefix.startswith("#", index):
            return True
        index += 1
    return closing is not None


def _code_part(line: str) -> str:
    """The line without a trailing # comment (strings are respected)."""
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
        elif line.startswith("#", index):
            return line[:index]
    return line


def _code_tokens(code: str) -> list[tuple[str, bool]]:
    """The tokens of a line of code as (text, is_word); a string literal counts as one token."""
    tokens: list[tuple[str, bool]] = []
    index = 0
    while index < len(code):
        char = code[index]
        if char in " \t":
            index += 1
            continue
        if char in '"«\u201c\u201d\u201e':
            closing = '"' if char == '"' else ("»" if char == "«" else "\u201c\u201d\u201e")
            end = index + 1
            while end < len(code) and code[end] not in closing:
                end += 2 if code[end] == "\\" else 1
            tokens.append((code[index:min(end + 1, len(code))], False))
            index = end + 1
            continue
        if char.isalnum() or char == "_":
            end = index
            while end < len(code) and (code[end].isalnum() or code[end] == "_"):
                end += 1
            tokens.append((code[index:end], True))
            index = end
            continue
        tokens.append((char, False))
        index += 1
    return tokens


def opens_lambda_block(code: str, function_words: frozenset[str]) -> bool:
    """True if the line ends with the header of a function value, which opens a block.

    This is the rule the compiler's lexer uses (``Lexer::tokenize``): the header's ``)`` has
    closed, the bracket that holds the function value (if any) is still open, and the last
    token of the line can end a header - the ``)`` itself or the last token of the type the
    function answers with. ``var t = Task(func()``, ``var step = func(n: int): int`` and ``var step = func(n):``
    open a block; ``println(map(a, func(n): n * n))`` does not.
    """
    tokens = _code_tokens(code)
    if not tokens:
        return False
    depth = 0
    open_headers: list[int] = []     # the depth of each 'func' whose '(' is still open
    header_depth: int | None = None  # the depth of the last header that has closed
    previous_word = ""
    for text, is_word in tokens:
        if text == "(":
            if previous_word and fold_letters(previous_word) in function_words:
                open_headers.append(depth)
            depth += 1
        elif text in "[{":
            depth += 1
        elif text == ")":
            depth = max(depth - 1, 0)
            if open_headers and open_headers[-1] == depth:
                open_headers.pop()
                header_depth = depth
            elif header_depth is not None and depth < header_depth:
                header_depth = None  # the bracket that held the function value closed
        elif text in "]}":
            depth = max(depth - 1, 0)
        previous_word = text if is_word else ""
    if header_depth is None or depth < header_depth:
        return False
    text, is_word = tokens[-1]
    # ':' ends the header of a function value whose types are worked out ("func(n):").
    return is_word or text in (")", "]", ">", ":")


def on_return(before: str, after: str, words: BlockWords = BlockWords()) -> Edit:
    """Enter: keep the indentation; one level more after a block header, one less after
    return/break/continue/pass. ``after`` is the rest of the line (kept on the new line)."""
    indent = indent_of(before)
    code = _code_part(before).strip()  # a trailing comment does not matter
    word = fold_letters(first_word(code))
    # Pashto's "with" follows the thing it goes with: the handle ("لاستی سره = ...", the second
    # word) or, without a handle, the last word of the line ("ساکټ سره").
    opens = word in words.opening or (bool(words.opening_last) and (
        fold_letters(last_word(code)) in words.opening_last or fold_letters(second_word(code)) in words.opening_last))
    # A function value written in place opens a block from the end of its header, wherever the
    # line begins: "var later = Task(func()" or "var step = func(n: int): int".
    opens = opens or opens_lambda_block(code, words.function)
    if code and opens and not inside_string_or_comment(code):
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
