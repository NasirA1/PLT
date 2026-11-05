"""Right-to-left text support: the Unicode Bidirectional Algorithm and Arabic shaping.

Tk 8.6 on Linux draws text strictly left to right, one character after another, without
joining Arabic-script letters. To show Pashto correctly there, the IDE computes the *visual*
form of each line itself:

* ``bidi_levels`` / ``visual_order`` implement the parts of the Unicode Bidirectional
  Algorithm (UAX #9) that matter for single lines without explicit embedding controls:
  rules W1-W7, N1-N2, I1-I2 and L1-L2, plus mirroring of brackets (L4).
* ``shape`` replaces Arabic letters with their contextual presentation forms (initial,
  medial, final, isolated) and forms the mandatory lam-alef ligatures. The forms are read
  from Python's Unicode database, so no extra data file is needed. Letters that Unicode has
  no presentation forms for (several Pashto letters such as ټ ښ ځ) are shown in their
  isolated form and do not join.

Everything here is pure Python and toolkit-independent; it is unit-tested on its own.
"""

from __future__ import annotations

import unicodedata
from dataclasses import dataclass
from functools import lru_cache

# ---------------------------------------------------------------------- bidi classes

_STRONG_RTL = {"R", "AL"}
_NEUTRAL = {"B", "S", "WS", "ON", "BN"}

MIRRORS = {
    "(": ")", ")": "(", "[": "]", "]": "[", "{": "}", "}": "{", "<": ">", ">": "<",
    "«": "»", "»": "«", "‹": "›", "›": "‹", "≤": "≥", "≥": "≤",
}


def bidi_class(char: str) -> str:
    cls = unicodedata.bidirectional(char)
    if cls in ("LRE", "LRO", "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI"):
        return "BN"  # explicit formatting characters are ignored (rule X9)
    return cls or "L"


def has_rtl(text: str) -> bool:
    """True if the text contains right-to-left letters."""
    return any(unicodedata.bidirectional(c) in _STRONG_RTL for c in text)


def paragraph_is_rtl(text: str, default_rtl: bool = False) -> bool:
    """Rules P2-P3: the first strong character decides the direction of a line."""
    for char in text:
        cls = bidi_class(char)
        if cls == "L":
            return False
        if cls in _STRONG_RTL:
            return True
    return default_rtl


def _resolve_marks_and_letters(types: list[str], sos: str) -> list[str]:
    """W1: marks take the previous type. W2: European numbers after Arabic letters become
    Arabic numbers. W3: Arabic letters are then treated as right-to-left letters."""
    previous = sos
    for i, t in enumerate(types):
        if t == "NSM":
            types[i] = previous
        elif t != "BN":
            previous = types[i]
    last_strong = sos
    for i, t in enumerate(types):
        if t in ("L", "R", "AL"):
            last_strong = t
        elif t == "EN" and last_strong == "AL":
            types[i] = "AN"
    return ["R" if t == "AL" else t for t in types]


def _resolve_numbers(types: list[str], sos: str) -> list[str]:
    """W4-W7: separators and terminators around numbers."""
    n = len(types)
    for i in range(1, n - 1):  # W4: a single separator between two numbers joins them
        if types[i] == "ES" and types[i - 1] == "EN" and types[i + 1] == "EN":
            types[i] = "EN"
        elif types[i] == "CS" and types[i - 1] == types[i + 1] and types[i - 1] in ("EN", "AN"):
            types[i] = types[i - 1]
    i = 0
    while i < n:  # W5: terminators (e.g. %) next to European numbers belong to them
        if types[i] != "ET":
            i += 1
            continue
        j = i
        while j < n and types[j] == "ET":
            j += 1
        if (i > 0 and types[i - 1] == "EN") or (j < n and types[j] == "EN"):
            types[i:j] = ["EN"] * (j - i)
        i = j
    types = ["ON" if t in ("ES", "ET", "CS") else t for t in types]  # W6
    last_strong = sos
    for i, t in enumerate(types):  # W7: numbers after left-to-right text read left to right
        if t in ("L", "R"):
            last_strong = t
        elif t == "EN" and last_strong == "L":
            types[i] = "L"
    return types


def _resolve_neutrals(types: list[str], sos: str, rtl: bool) -> list[str]:
    """N1-N2: neutrals between two strong types of the same direction take that direction;
    other neutrals take the paragraph direction. Numbers count as right-to-left here."""
    def strong(t: str) -> str:
        return "R" if t in ("R", "EN", "AN") else t

    n = len(types)
    i = 0
    while i < n:
        if types[i] not in _NEUTRAL:
            i += 1
            continue
        j = i
        while j < n and types[j] in _NEUTRAL:
            j += 1
        before = strong(types[i - 1]) if i > 0 else sos
        after = strong(types[j]) if j < n else sos
        types[i:j] = [before if before == after else ("R" if rtl else "L")] * (j - i)
        i = j
    return types


def bidi_levels(text: str, rtl: bool) -> list[int]:
    """Resolved embedding level of each character (even = left-to-right, odd = right-to-left)."""
    sos = "R" if rtl else "L"
    types = _resolve_marks_and_letters([bidi_class(c) for c in text], sos)
    types = _resolve_numbers(types, sos)
    types = _resolve_neutrals(types, sos, rtl)

    # I1-I2: implicit levels.
    if rtl:
        levels = [2 if t in ("L", "EN", "AN") else 1 for t in types]
    else:
        levels = [1 if t == "R" else 2 if t in ("AN", "EN") else 0 for t in types]

    # L1: trailing whitespace goes back to the paragraph level.
    k = len(text) - 1
    while k >= 0 and unicodedata.bidirectional(text[k]) in ("WS", "S", "BN", "B"):
        levels[k] = 1 if rtl else 0
        k -= 1
    return levels


def visual_order(levels: list[int]) -> list[int]:
    """Rule L2: logical indices in left-to-right display order."""
    order = list(range(len(levels)))
    if not levels:
        return order
    highest = max(levels)
    lowest_odd = min((level for level in levels if level % 2), default=highest + 1)
    for level in range(highest, lowest_odd - 1, -1):
        i = 0
        while i < len(order):
            if levels[order[i]] >= level:
                j = i
                while j < len(order) and levels[order[j]] >= level:
                    j += 1
                order[i:j] = reversed(order[i:j])
                i = j
            else:
                i += 1
    return order


# ---------------------------------------------------------------------- Arabic shaping

@lru_cache(maxsize=1)
def _presentation_forms() -> dict[str, dict[str, str]]:
    """base letter -> {"isolated"|"final"|"initial"|"medial": presentation form}."""
    forms: dict[str, dict[str, str]] = {}
    for code in list(range(0xFB50, 0xFE00)) + list(range(0xFE70, 0xFF00)):
        decomposition = unicodedata.decomposition(chr(code)).split()
        if len(decomposition) == 2 and decomposition[0] in ("<isolated>", "<final>", "<initial>", "<medial>"):
            forms.setdefault(chr(int(decomposition[1], 16)), {})[decomposition[0][1:-1]] = chr(code)
    return forms


_LAM_ALEF = {  # alef variant -> (isolated ligature, final ligature)
    "آ": ("ﻵ", "ﻶ"),
    "أ": ("ﻷ", "ﻸ"),
    "إ": ("ﻹ", "ﻺ"),
    "ا": ("ﻻ", "ﻼ"),
}


def joining_type(char: str) -> str:
    """D (dual-joining), R (right-joining), C (join-causing), T (transparent) or U (non-joining)."""
    if char in ("ـ", "‍"):  # tatweel, zero-width joiner
        return "C"
    forms = _presentation_forms().get(char)
    if forms is None:
        return "T" if unicodedata.category(char) in ("Mn", "Me") else "U"
    if "initial" in forms or "medial" in forms:
        return "D"
    return "R" if "final" in forms else "U"


def shape(text: str) -> list[str]:
    """The glyph string to show for each character of ``text`` (same length as ``text``).

    A character absorbed into a lam-alef ligature maps to the empty string.
    """
    forms = _presentation_forms()
    types = [joining_type(c) for c in text]
    n = len(text)
    out = list(text)

    def neighbour(index: int, step: int) -> int:
        index += step
        while 0 <= index < n and types[index] == "T":
            index += step
        return index

    for i, char in enumerate(text):
        t = types[i]
        if t not in ("D", "R"):
            continue
        prev = neighbour(i, -1)
        nxt = neighbour(i, 1)
        joins_prev = prev >= 0 and types[prev] in ("D", "C")
        joins_next = t == "D" and nxt < n and types[nxt] in ("D", "R", "C")

        if char == "ل" and nxt < n and text[nxt] in _LAM_ALEF:  # lam followed by alef
            isolated, final = _LAM_ALEF[text[nxt]]
            out[i] = final if joins_prev else isolated
            out[nxt] = ""
            types[nxt] = "U"  # the alef has been consumed
            continue
        if out[i] == "":
            continue

        available = forms[char]
        if joins_prev and joins_next:
            wanted = ("medial", "final", "initial", "isolated")
        elif joins_prev:
            wanted = ("final", "isolated")
        elif joins_next:
            wanted = ("initial", "isolated")
        else:
            wanted = ("isolated",)
        for form in wanted:
            if form in available:
                out[i] = available[form]
                break
    return out


# ---------------------------------------------------------------------- lines for display

@dataclass(frozen=True)
class Chunk:
    """A piece of a line in display order: text drawn with one style."""

    text: str
    style: str | None
    rtl: bool  # part of a right-to-left run


def render_line(text: str, styles: list[str | None] | None = None, default_rtl: bool = False,
                shape_letters: bool = True, reverse_rtl_text: bool = True) -> tuple[list[Chunk], bool]:
    """Splits a logical line into styled chunks in left-to-right display order.

    ``styles`` gives a style name per character. With ``reverse_rtl_text`` (for toolkits that
    draw characters strictly left to right, like Tk on X11) right-to-left runs are reversed
    and mirrored character by character, and ``shape_letters`` joins Arabic letters. Without
    it (for toolkits whose text engine reorders runs itself) only the order of the chunks is
    changed. Returns the chunks and whether the line is right-to-left.
    """
    styles = styles if styles is not None else [None] * len(text)
    rtl = paragraph_is_rtl(text, default_rtl)
    if not text:
        return [], rtl
    if not rtl and not has_rtl(text):
        return _coalesce([(text[i], styles[i], False) for i in range(len(text))]), False

    levels = bidi_levels(text, rtl)
    glyphs = shape(text) if shape_letters and reverse_rtl_text else list(text)
    cells: list[tuple[str, str | None, bool]] = []
    if reverse_rtl_text:
        for index in visual_order(levels):
            glyph = glyphs[index]
            odd = levels[index] % 2 == 1
            if odd:
                glyph = MIRRORS.get(glyph, glyph)
            cells.append((glyph, styles[index], odd))
        return _coalesce(cells), rtl

    # Native mode: order whole chunks; keep the characters of each chunk in logical order.
    runs: list[list[int]] = []
    for index in range(len(text)):
        if runs and levels[runs[-1][-1]] == levels[index] and styles[runs[-1][-1]] == styles[index]:
            runs[-1].append(index)
        else:
            runs.append([index])
    run_levels = [levels[run[0]] for run in runs]
    chunks = []
    for run_index in visual_order(run_levels):
        run = runs[run_index]
        odd = run_levels[run_index] % 2 == 1
        chunk_text = "".join(text[i] for i in run)
        if odd and not has_rtl(chunk_text):
            chunk_text = "".join(MIRRORS.get(c, c) for c in reversed(chunk_text))
        chunks.append(Chunk(chunk_text, styles[run[0]], odd))
    return chunks, rtl


def _coalesce(cells: list[tuple[str, str | None, bool]]) -> list[Chunk]:
    chunks: list[Chunk] = []
    for glyph, style, rtl in cells:
        if chunks and chunks[-1].style == style and chunks[-1].rtl == rtl:
            chunks[-1] = Chunk(chunks[-1].text + glyph, style, rtl)
        else:
            chunks.append(Chunk(glyph, style, rtl))
    return chunks


def visual_text(text: str, default_rtl: bool = False, reverse_rtl_text: bool = True) -> str:
    """The whole line as one display string (for labels, menus and table cells)."""
    chunks, _ = render_line(text, None, default_rtl, reverse_rtl_text=reverse_rtl_text)
    return "".join(chunk.text for chunk in chunks)
