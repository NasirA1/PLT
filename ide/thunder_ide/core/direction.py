"""How a line of source code is ordered on screen in a right-to-left (or left-to-right) program.

The Unicode Bidirectional Algorithm is made for prose. Applied to code it merges every run of
Latin identifiers, operators and string literals into one left-to-right phrase, so the Pashto
statement ``متغیر name = "Nasir"`` comes out as ``"name = "Nasir متغیر``: the assignment reads
backwards and a quote mark is stranded at the far end.

Thunder programs are read **token by token in the direction of the program's language**.
Each token (name, keyword, number, string literal, comment) keeps the direction of its own
script, and everything between tokens (operators, brackets, commas, spaces) follows the
language. For Pashto this gives ``"Nasir" = name متغیر`` with the keyword at the right and the
value at the left, exactly as a fully Pashto program ``متغیر نوم = "ناصر"`` is read.

This module produces that ordering from a logical line of text using Unicode *isolates*
(U+2068 FIRST STRONG ISOLATE ... U+2069 POP DIRECTIONAL ISOLATE): every token is wrapped in a
pair, which makes it an opaque unit for the bidirectional algorithm, and the text engine of the
GUI toolkit then lays the line out, shapes the letters and mirrors the brackets. The marks are
only inserted into the *displayed* string; the program text itself is never changed. The
functions below map positions between the two strings and decide where the caret goes.
"""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass
from typing import Optional

FSI = "⁨"  # first strong isolate: the token takes the direction of its first letter
PDI = "⁩"  # pop directional isolate
LRM = "\u200e"  # left-to-right mark: keeps a negative number's sign on the left of its digits
MARKS = (FSI, PDI, LRM)

# The units a line is read in: comments, string literals (either quote style, possibly
# unterminated), decimal numbers and words (names, keywords and numbers in any script).
#
# A decimal number comes before the general word, so that 9.81 and ۹٫۸۱ stay *one* unit. The
# decimal point is not a word character, so without this the two halves would be two tokens
# with the point between them, and a right-to-left line would order them as ۸۱٫۹.
_TOKEN = re.compile(
    r"//[^\n]*"
    r"|/\*.*?(?:\*/|$)"
    r"|\"(?:\\.|[^\"\\\n])*\"?"
    r"|«(?:\\.|[^»\\\n])*»?"
    r"|[“”„](?:\\.|[^“”„\\\n])*[“”„]?"
    r"|\d+[.\u066b]\d+"
    r"|\w+",
    re.DOTALL,
)


def is_right_to_left_text(text: str) -> bool:
    """True if the first letter with a direction reads right to left (as FSI decides)."""
    for char in text:
        bidi_class = unicodedata.bidirectional(char)
        if bidi_class in ("R", "AL"):
            return True
        if bidi_class == "L":
            return False
    return False


def has_right_to_left_text(text: str) -> bool:
    return any(unicodedata.bidirectional(char) in ("R", "AL", "AN") for char in text)


@dataclass(frozen=True)
class Token:
    """A token of the line (logical offsets) and whether it reads against the line direction."""

    start: int
    end: int
    opposite: bool


@dataclass(frozen=True)
class LineLayout:
    text: str                   # the logical line, as stored in the program
    right_to_left: bool         # the direction of the program's language
    display: str                # ``text`` with isolate marks around the tokens (what is drawn)
    offsets: tuple[int, ...]    # display index of every logical index 0..len(text)
    tokens: tuple[Token, ...]   # the isolated tokens, in order (empty when no marks were needed)

    def to_logical(self, display_index: int) -> int:
        """The logical index for a display index (marks count as nothing)."""
        display_index = max(0, min(display_index, len(self.display)))
        return display_index - sum(1 for char in self.display[:display_index] if char in MARKS)

    def token_containing(self, index: int) -> Optional[Token]:
        for token in self.tokens:
            if token.start <= index < token.end:
                return token
        return None

    def caret(self, index: int) -> tuple[int, bool]:
        """Where to draw the caret for the logical position ``index``.

        Returns a display index and whether the caret sits at the *trailing* edge of that
        display character (False: its leading edge); the toolkit converts that into an x
        position. The caret follows what was typed last: after a character it is drawn at that
        character's trailing edge, and inside a token at the leading edge of the token's next
        character, which is the same point. Qt merges an isolate mark with the character after
        it for cursor purposes, so a token's *first* character is never asked about directly:
        for a one-character token the position of one of its marks is used instead (the mark
        before it lies on the side where the token starts in reading order).
        """
        index = min(index, len(self.text))
        if index <= 0:
            return 0, False  # the start edge of the line
        previous = index - 1
        token = self.token_containing(previous)
        if token is None:
            return self.offsets[previous], True
        if index < token.end:
            return self.offsets[index], False
        if token.end - token.start == 1:
            first = self.offsets[token.start]
            return (first - 1 if token.opposite else first + 1), False
        return self.offsets[previous], True


_DIGITS = "0123456789٠١٢٣٤٥٦٧٨٩۰۱۲۳۴۵۶۷۸۹"
_DECIMAL_POINTS = ".\u066b"  # '.' and the Arabic decimal separator '٫'
_NUMBER_PART = _DIGITS + _DECIMAL_POINTS
_BEFORE_SIGN = "=+-*/%<>(,[{:،"  # after these a '-' is a sign, not a subtraction


def _with_signs(text: str, tokens: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Extends number tokens to include a unary minus sign.

    Negative numbers are written with the sign to the left of the digits in every script
    (an exception to the right-to-left reading), so "-۳" must stay one unit; a lone "-"
    between tokens would be ordered with the paragraph and end up right of the number.
    """
    adjusted = []
    for start, end in tokens:
        if start > 0 and text[start - 1] == "-" and text[start] in _DIGITS:
            before = text[:start - 1].rstrip()
            if not before or before[-1] in _BEFORE_SIGN:
                start -= 1
        adjusted.append((start, end))
    return adjusted


def analyse_line(text: str, right_to_left: bool) -> LineLayout:
    """Lays out one logical line of code for display in a program of the given direction."""
    tokens = _with_signs(text, [(match.start(), match.end()) for match in _TOKEN.finditer(text)])
    if not right_to_left and not has_right_to_left_text(text):
        # Pure left-to-right text needs no help: the display is the text itself.
        return LineLayout(text, right_to_left, text, tuple(range(len(text) + 1)), ())

    display: list[str] = []
    offsets: list[int] = []
    isolated: list[Token] = []

    def copy_token(start: int, end: int) -> None:
        """Copies one token into the display, keeping negative numbers sign-first.

        Inside a comment or string the bidirectional algorithm runs free (they are prose), so a
        "-۳" in a right-to-left comment would come out as "۳-"; a small isolate keeps the sign on
        the left there too. The number tokens of the code itself were already merged with their
        sign by _with_signs, so they need nothing here.
        """
        marked = False
        for index in range(start, end):
            char = text[index]
            if marked and char not in _NUMBER_PART:
                display.append(LRM)
                marked = False
            if (char == "-" and index > start and index + 1 < end and text[index + 1] in _DIGITS
                    and text[index - 1] not in _DIGITS and not marked):
                display.append(LRM)
                marked = True
            offsets.append(len(display))
            display.append(char)
        if marked:
            display.append(LRM)

    position = 0
    for start, end in tokens:
        for index in range(position, start):
            offsets.append(len(display))
            display.append(text[index])
        # A token with no letter of its own (a number, possibly with its sign) is surrounded by
        # left-to-right marks inside its isolate: after a Pashto word the bidirectional algorithm
        # turns the digits into Arabic numbers and puts the "-" of a negative number to their
        # right; a strong left-to-right context restores the sign-first order that numbers have
        # in every script. The marks sit inside the isolate, so nothing leaks out of the token.
        weak = not any(unicodedata.bidirectional(char) in ("L", "R", "AL") for char in text[start:end])
        display.append(FSI)
        if weak:
            display.append(LRM)
        copy_token(start, end)
        if weak:
            display.append(LRM)
        display.append(PDI)
        isolated.append(Token(start, end, is_right_to_left_text(text[start:end]) != right_to_left))
        position = end
    for index in range(position, len(text)):
        offsets.append(len(display))
        display.append(text[index])
    offsets.append(len(display))
    return LineLayout(text, right_to_left, "".join(display), tuple(offsets), tuple(isolated))


# ---------------------------------------------------------------------- UTF-16 positions
#
# GUI toolkits written in C++ count text positions in UTF-16 code units; Python counts code
# points. They differ only for characters outside the Basic Multilingual Plane (e.g. emoji).

def _has_astral(text: str) -> bool:
    return any(ord(char) > 0xFFFF for char in text)


def index_to_utf16(text: str, index: int) -> int:
    """The UTF-16 offset of the code-point index ``index`` in ``text``."""
    if not _has_astral(text):
        return index
    return index + sum(1 for char in text[:index] if ord(char) > 0xFFFF)


def utf16_to_index(text: str, offset: int) -> int:
    """The code-point index for a UTF-16 offset in ``text`` (inside a pair, the character's index)."""
    if not _has_astral(text):
        return min(max(offset, 0), len(text))
    units = 0
    for index, char in enumerate(text):
        width = 2 if ord(char) > 0xFFFF else 1
        if units + width > offset:
            return index
        units += width
    return len(text)
