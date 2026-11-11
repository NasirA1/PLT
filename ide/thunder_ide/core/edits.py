"""The text operations behind the editor's commands, as pure functions.

Commenting, tidying, folding and searching are all "text in, text out", so they live here
rather than in the Qt editor: they can be read, reasoned about and tested without a GUI, and
another toolkit gets them for nothing (see core.ui).

Positions are code-point indexes into the whole text, as ``str`` uses them.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Optional

COMMENT = "//"
INDENT_WIDTH = 4  # a tab counts as four columns, as the lexer does


# ---------------------------------------------------------------------- lines

def line_bounds(text: str, index: int) -> tuple[int, int]:
    """The start and end (without its line break) of the line containing ``index``."""
    index = max(0, min(index, len(text)))
    start = text.rfind("\n", 0, index) + 1
    end = text.find("\n", index)
    return start, len(text) if end < 0 else end


def selected_lines(text: str, start: int, end: int) -> tuple[int, int]:
    """The region covering every line touched by the selection ``start``..``end``."""
    if end < start:
        start, end = end, start
    first = line_bounds(text, start)[0]
    # A selection that ends exactly at the start of a line does not include that line.
    if end > start and end == line_bounds(text, end)[0]:
        end -= 1
    return first, line_bounds(text, end)[1]


def indent_width(line: str) -> int:
    """The width of a line's leading whitespace, counting a tab to the next multiple of four."""
    width = 0
    for char in line:
        if char == "\t":
            width = (width // INDENT_WIDTH + 1) * INDENT_WIDTH
        elif char == " ":
            width += 1
        else:
            break
    return width


# ---------------------------------------------------------------------- comment / uncomment

def toggle_comment(text: str, start: int, end: int) -> tuple[str, int, int]:
    """Comments the selected lines, or uncomments them if they are all commented.

    Returns the new text and the selection that still covers the same lines. Blank lines are
    left alone, and the ``//`` goes at the shallowest indentation of the lines involved, so a
    commented block keeps its shape.
    """
    first, last = selected_lines(text, start, end)
    lines = text[first:last].split("\n")
    content = [line for line in lines if line.strip()]
    if not content:
        return text, start, end

    commented = all(line.lstrip().startswith(COMMENT) for line in content)
    if commented:
        changed = [_uncomment_line(line) for line in lines]
    else:
        column = min(indent_width(line) for line in content)
        changed = [_comment_line(line, column) if line.strip() else line for line in lines]
    body = "\n".join(changed)
    return text[:first] + body + text[last:], first, first + len(body)


def _comment_line(line: str, column: int) -> str:
    # Insert at 'column', which every line of the block has, so the block keeps its shape.
    cut = 0
    width = 0
    while cut < len(line) and width < column:
        width += INDENT_WIDTH - width % INDENT_WIDTH if line[cut] == "\t" else 1
        cut += 1
    return line[:cut] + COMMENT + " " + line[cut:]


def _uncomment_line(line: str) -> str:
    stripped = line.lstrip()
    if not stripped.startswith(COMMENT):
        return line
    indent = line[:len(line) - len(stripped)]
    rest = stripped[len(COMMENT):]
    return indent + (rest[1:] if rest.startswith(" ") else rest)


# ---------------------------------------------------------------------- tidy

def tidy(text: str) -> str:
    """Tidies the layout without changing what the program means.

    Tabs become spaces (to the next multiple of four, exactly as the lexer measures them),
    trailing whitespace goes, runs of blank lines are cut to one, and the file ends with a
    single line break. The *amount* of indentation is never changed: in Thunder the
    indentation is the block structure, so re-indenting would be rewriting the program.
    """
    out: list[str] = []
    blanks = 0
    for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        expanded = _expand_tabs(line).rstrip()
        if not expanded:
            blanks += 1
            if blanks > 1:
                continue
        else:
            blanks = 0
        out.append(expanded)
    while out and not out[-1]:
        out.pop()
    return "\n".join(out) + "\n" if out else ""


def _expand_tabs(line: str) -> str:
    out: list[str] = []
    width = 0
    for char in line:
        if char == "\t":
            spaces = INDENT_WIDTH - width % INDENT_WIDTH
            out.append(" " * spaces)
            width += spaces
        else:
            out.append(char)
            width += 1
    return "".join(out)


# ---------------------------------------------------------------------- folding

@dataclass(frozen=True)
class Fold:
    """An indented block: the line that opens it and the last line that belongs to it (0-based)."""

    header: int
    last: int

    def contains(self, line: int) -> bool:
        return self.header <= line <= self.last


def fold_regions(text: str) -> list[Fold]:
    """Every indented block of the program, outermost first, in line order.

    Thunder marks blocks by indentation, so a block is a line followed by lines indented more
    than it; blank lines belong to it only if indented lines follow. A block of one line is not
    worth folding and is left out.
    """
    lines = text.split("\n")
    widths = [indent_width(line) if line.strip() else None for line in lines]
    folds: list[Fold] = []
    for index, width in enumerate(widths):
        if width is None:
            continue
        last = index
        for following in range(index + 1, len(lines)):
            if widths[following] is None:
                continue  # a blank line only ends the block if nothing indented follows
            if widths[following] <= width:
                break
            last = following
        if last > index:
            folds.append(Fold(index, last))
    return folds


def fold_at(text: str, line: int) -> Optional[Fold]:
    """The innermost block whose *header* is at or above ``line`` and that contains it."""
    best: Optional[Fold] = None
    for fold in fold_regions(text):
        if fold.contains(line) and (best is None or fold.header >= best.header):
            best = fold
    return best


# ---------------------------------------------------------------------- find and replace

def _pattern(needle: str, case_sensitive: bool, whole_word: bool) -> re.Pattern:
    expression = re.escape(needle)
    if whole_word:
        # \b works for Pashto letters too: Python's \w is Unicode-aware.
        prefix = r"\b" if needle[:1].isalnum() or needle[:1] == "_" else ""
        suffix = r"\b" if needle[-1:].isalnum() or needle[-1:] == "_" else ""
        expression = prefix + expression + suffix
    return re.compile(expression, 0 if case_sensitive else re.IGNORECASE)


def find(text: str, needle: str, start: int, *, case_sensitive: bool = False, whole_word: bool = False,
         backwards: bool = False, wrap: bool = True) -> Optional[tuple[int, int]]:
    """The next match at or after ``start`` (or before it, searching backwards).

    Returns its bounds, or None if the text does not contain ``needle`` at all. With ``wrap``
    the search continues from the other end, as every editor's "find next" does.
    """
    if not needle:
        return None
    pattern = _pattern(needle, case_sensitive, whole_word)
    matches = [(match.start(), match.end()) for match in pattern.finditer(text)]
    if not matches:
        return None
    if backwards:
        earlier = [span for span in matches if span[1] <= start]
        if earlier:
            return earlier[-1]
        return matches[-1] if wrap else None
    later = [span for span in matches if span[0] >= start]
    if later:
        return later[0]
    return matches[0] if wrap else None


def replace_all(text: str, needle: str, replacement: str, *, case_sensitive: bool = False,
                whole_word: bool = False) -> tuple[str, int]:
    """Every match replaced; returns the new text and how many were replaced."""
    if not needle:
        return text, 0
    pattern = _pattern(needle, case_sensitive, whole_word)
    new_text, count = pattern.subn(lambda _match: replacement, text)
    return new_text, count


def matches(text: str, needle: str, *, case_sensitive: bool = False, whole_word: bool = False) -> int:
    """How many times ``needle`` occurs (what Replace All would change)."""
    if not needle:
        return 0
    return len(_pattern(needle, case_sensitive, whole_word).findall(text))


# ---------------------------------------------------------------------- line commands
#
# Each takes the text and the selection (code-point indexes, in either order) and returns
# the new text and the selection that still covers the same lines, so a command can be
# repeated: Alt+Down three times moves the lines three down.

def _lines_region(text: str, start: int, end: int) -> tuple[int, int, int]:
    """The selected lines as (first, last, after): the region [first, last) without its final
    line break, and the index just after that break (len(text) if there is none)."""
    first, last = selected_lines(text, start, end)
    after = last + 1 if last < len(text) else last
    return first, last, after


def duplicate_lines(text: str, start: int, end: int) -> tuple[str, int, int]:
    """Copies the selected lines below themselves; the selection moves to the copy."""
    first, last, _after = _lines_region(text, start, end)
    block = text[first:last]
    new_text = text[:last] + "\n" + block + text[last:]
    return new_text, last + 1, last + 1 + len(block)


def delete_lines(text: str, start: int, end: int) -> tuple[str, int, int]:
    """Removes the selected lines (with their line break); the caret goes to where they were."""
    first, last, after = _lines_region(text, start, end)
    if after == len(text) and first > 0:
        first -= 1  # the last line: take the break before it instead, so no empty line is left
    new_text = text[:first] + text[after:]
    caret = min(first, len(new_text))
    return new_text, caret, caret


def move_lines(text: str, start: int, end: int, delta: int) -> tuple[str, int, int]:
    """Swaps the selected lines with the line above (delta -1) or below (delta +1)."""
    first, last, after = _lines_region(text, start, end)
    block = text[first:last]
    if delta < 0:
        if first == 0:
            return text, start, end
        previous_start = line_bounds(text, first - 1)[0]
        previous = text[previous_start:first - 1]
        new_text = text[:previous_start] + block + "\n" + previous + text[last:]
        return new_text, previous_start, previous_start + len(block)
    if after >= len(text) or last == len(text):
        return text, start, end
    next_start, next_end = line_bounds(text, after)
    following = text[next_start:next_end]
    new_text = text[:first] + following + "\n" + block + text[next_end:]
    new_first = first + len(following) + 1
    return new_text, new_first, new_first + len(block)


def indent_lines(text: str, start: int, end: int) -> tuple[str, int, int]:
    """Indents the selected lines by one level (four spaces); blank lines are left alone."""
    first, last, _after = _lines_region(text, start, end)
    lines = text[first:last].split("\n")
    changed = [(" " * INDENT_WIDTH + line) if line.strip() else line for line in lines]
    body = "\n".join(changed)
    return text[:first] + body + text[last:], first, first + len(body)


def outdent_lines(text: str, start: int, end: int) -> tuple[str, int, int]:
    """Removes one level of indentation (up to four spaces, or one tab) from the selected lines."""
    first, last, _after = _lines_region(text, start, end)
    lines = text[first:last].split("\n")
    changed = []
    for line in lines:
        if line.startswith("\t"):
            changed.append(line[1:])
        else:
            cut = 0
            while cut < INDENT_WIDTH and cut < len(line) and line[cut] == " ":
                cut += 1
            changed.append(line[cut:])
    body = "\n".join(changed)
    return text[:first] + body + text[last:], first, first + len(body)
