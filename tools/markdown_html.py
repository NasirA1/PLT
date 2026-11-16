"""A small Markdown-to-HTML renderer for the SDK reference (standard library only).

It covers the GitHub dialect the documents in docs/ use: ATX headings (with GitHub-style ids,
so links to ``language.md#types`` keep working), paragraphs, fenced code blocks, tables with
alignment, ordered and unordered lists (nested by indentation), block quotes, horizontal rules,
and inline code, emphasis, links, images and a handful of harmless HTML tags (``<sup>``,
``<sub>``, ``<br>``, ``<kbd>``). Everything else is escaped, so a document can never inject
markup into the generated site.

Links are passed through ``link`` so the caller can turn ``library.md`` into the page it became.
"""

from __future__ import annotations

import html
import re
from dataclasses import dataclass, field
from typing import Callable

ALLOWED_TAGS = ("sup", "sub", "br", "kbd")

_FENCE = re.compile(r"^(\s*)(```+|~~~+)\s*([\w+-]*)\s*$")
_HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
_RULE = re.compile(r"^\s*(?:-{3,}|\*{3,}|_{3,})\s*$")
_BULLET = re.compile(r"^(\s*)([-*+])\s+(.*)$")
_NUMBER = re.compile(r"^(\s*)(\d+)[.)]\s+(.*)$")
_TABLE_SEPARATOR = re.compile(r"^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)*\|?\s*$")
_QUOTE = re.compile(r"^\s*>\s?(.*)$")


@dataclass
class Heading:
    level: int
    text: str   # plain text, for a table of contents
    id: str


@dataclass
class Rendered:
    html: str
    headings: list[Heading] = field(default_factory=list)
    title: str = ""


def slug(text: str, taken: dict[str, int] | None = None) -> str:
    """A GitHub-style heading id: lower case, punctuation gone, spaces as hyphens, unique."""
    plain = re.sub(r"`([^`]*)`", r"\1", text)
    plain = re.sub(r"[*_~]", "", plain)
    plain = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", plain)
    plain = plain.strip().lower()
    plain = re.sub(r"[^\w\- ]", "", plain)
    plain = re.sub(r"\s+", "-", plain)
    if taken is not None:
        count = taken.get(plain, 0)
        taken[plain] = count + 1
        if count:
            plain = f"{plain}-{count}"
    return plain or "section"


class Renderer:
    """Renders one document. ``link`` maps a link target to the one the HTML should carry."""

    def __init__(self, link: Callable[[str], str] | None = None):
        self.link = link or (lambda target: target)
        self.ids: dict[str, int] = {}
        self.headings: list[Heading] = []

    # ---------------------------------------------------------------- blocks

    def render(self, text: str) -> Rendered:
        lines = text.replace("\r\n", "\n").split("\n")
        parts = self._blocks(lines)
        title = next((heading.text for heading in self.headings if heading.level == 1), "")
        return Rendered("\n".join(parts), self.headings, title)

    def _blocks(self, lines: list[str]) -> list[str]:
        out: list[str] = []
        index = 0
        paragraph: list[str] = []

        def flush() -> None:
            if paragraph:
                out.append(f"<p>{self.inline(' '.join(part.strip() for part in paragraph))}</p>")
                paragraph.clear()

        while index < len(lines):
            line = lines[index]
            if not line.strip():
                flush()
                index += 1
                continue
            fence = _FENCE.match(line)
            if fence:
                flush()
                index = self._code(lines, index, fence, out)
                continue
            heading = _HEADING.match(line)
            if heading:
                flush()
                level = len(heading.group(1))
                raw = heading.group(2)
                anchor = slug(raw, self.ids)
                plain = html.unescape(re.sub(r"<[^>]+>", "", self.inline(raw)))
                self.headings.append(Heading(level, plain, anchor))
                out.append(f'<h{level} id="{anchor}">{self.inline(raw)}<a class="anchor" href="#{anchor}" aria-label="link">#</a></h{level}>')
                index += 1
                continue
            if _RULE.match(line) and not paragraph:
                flush()
                out.append("<hr>")
                index += 1
                continue
            if self._is_table(lines, index):
                flush()
                index = self._table(lines, index, out)
                continue
            if _BULLET.match(line) or _NUMBER.match(line):
                flush()
                index = self._list(lines, index, out)
                continue
            if _QUOTE.match(line):
                flush()
                index = self._quote(lines, index, out)
                continue
            paragraph.append(line)
            index += 1
        flush()
        return out

    def _code(self, lines: list[str], index: int, fence: re.Match, out: list[str]) -> int:
        marker = fence.group(2)
        language = fence.group(3)
        body: list[str] = []
        index += 1
        while index < len(lines):
            closing = _FENCE.match(lines[index])
            if closing and closing.group(2)[0] == marker[0] and len(closing.group(2)) >= len(marker) and not closing.group(3):
                index += 1
                break
            body.append(lines[index])
            index += 1
        attribute = f' class="language-{html.escape(language)}"' if language else ""
        out.append(f"<pre><code{attribute}>{html.escape(chr(10).join(body))}</code></pre>")
        return index

    @staticmethod
    def _is_table(lines: list[str], index: int) -> bool:
        return ("|" in lines[index] and index + 1 < len(lines) and _TABLE_SEPARATOR.match(lines[index + 1]) is not None
                and "|" in lines[index + 1])

    @staticmethod
    def _cells(line: str) -> list[str]:
        stripped = line.strip()
        if stripped.startswith("|"):
            stripped = stripped[1:]
        if stripped.endswith("|") and not stripped.endswith("\\|"):
            stripped = stripped[:-1]
        cells: list[str] = []
        current: list[str] = []
        escaped = False
        for character in stripped:
            if escaped:
                current.append(character)
                escaped = False
            elif character == "\\":
                escaped = True
                current.append(character)
            elif character == "|":
                cells.append("".join(current))
                current = []
            else:
                current.append(character)
        cells.append("".join(current))
        return [cell.strip().replace("\\|", "|") for cell in cells]

    def _table(self, lines: list[str], index: int, out: list[str]) -> int:
        header = self._cells(lines[index])
        alignments = []
        for cell in self._cells(lines[index + 1]):
            left, right = cell.startswith(":"), cell.endswith(":")
            alignments.append("center" if left and right else "right" if right else "left" if left else "")
        index += 2
        rows: list[list[str]] = []
        while index < len(lines) and lines[index].strip() and "|" in lines[index]:
            rows.append(self._cells(lines[index]))
            index += 1

        def cell(tag: str, text: str, column: int) -> str:
            align = alignments[column] if column < len(alignments) else ""
            style = f' style="text-align:{align}"' if align else ""
            return f"<{tag}{style}>{self.inline(text)}</{tag}>"

        parts = ["<table>", "<thead><tr>"]
        parts += [cell("th", text, column) for column, text in enumerate(header)]
        parts += ["</tr></thead>", "<tbody>"]
        for row in rows:
            row = (row + [""] * len(header))[:len(header)]
            parts.append("<tr>" + "".join(cell("td", text, column) for column, text in enumerate(row)) + "</tr>")
        parts += ["</tbody>", "</table>"]
        out.append("\n".join(parts))
        return index

    def _list(self, lines: list[str], index: int, out: list[str]) -> int:
        """A list, nested by indentation: items at a deeper indent belong to the item above."""
        first = _BULLET.match(lines[index]) or _NUMBER.match(lines[index])
        indent = len(first.group(1))
        ordered = _NUMBER.match(lines[index]) is not None and _BULLET.match(lines[index]) is None
        items: list[list[str]] = []
        while index < len(lines):
            line = lines[index]
            if not line.strip():
                # A blank line ends the list unless the next non-blank line is still part of it.
                look = index + 1
                while look < len(lines) and not lines[look].strip():
                    look += 1
                if look < len(lines) and (len(lines[look]) - len(lines[look].lstrip())) > indent:
                    items[-1].append("")
                    index += 1
                    continue
                if look < len(lines) and self._item_at(lines[look], indent, ordered):
                    index = look
                    continue
                break
            match = self._item_at(line, indent, ordered)
            if match:
                items.append([match.group(3)])
                index += 1
                continue
            if (len(line) - len(line.lstrip())) > indent and items:
                items[-1].append(line[indent + 2:] if len(line) > indent + 2 else line.strip())
                index += 1
                continue
            break
        tag = "ol" if ordered else "ul"
        start = ""
        if ordered and first.group(2) != "1":
            start = f' start="{int(first.group(2))}"'
        parts = [f"<{tag}{start}>"]
        for item in items:
            head = item[0]
            rest = [part for part in item[1:]]
            plain = all(part.strip() and not _BULLET.match(part) and not _NUMBER.match(part) for part in rest)
            if rest and plain:
                # A wrapped item: the continuation lines are the same paragraph.
                head = " ".join([head] + [part.strip() for part in rest])
                rest = []
            if any(part.strip() for part in rest):
                nested = self._blocks(rest)
                parts.append(f"<li>{self.inline(head)}\n{chr(10).join(nested)}</li>")
            else:
                parts.append(f"<li>{self.inline(head)}</li>")
        parts.append(f"</{tag}>")
        out.append("\n".join(parts))
        return index

    @staticmethod
    def _item_at(line: str, indent: int, ordered: bool) -> re.Match | None:
        match = _NUMBER.match(line) if ordered else _BULLET.match(line)
        if match and len(match.group(1)) == indent:
            return match
        return None

    def _quote(self, lines: list[str], index: int, out: list[str]) -> int:
        body: list[str] = []
        while index < len(lines):
            match = _QUOTE.match(lines[index])
            if not match:
                break
            body.append(match.group(1))
            index += 1
        out.append("<blockquote>\n" + "\n".join(self._blocks(body)) + "\n</blockquote>")
        return index

    # ---------------------------------------------------------------- inline

    def inline(self, text: str) -> str:
        """Inline markup: code spans first (their contents are literal), then the rest."""
        parts: list[str] = []
        index = 0
        pattern = re.compile(r"(`+)(.+?)\1")
        while True:
            match = pattern.search(text, index)
            if not match:
                parts.append(self._spans(text[index:]))
                break
            parts.append(self._spans(text[index:match.start()]))
            parts.append(f"<code>{html.escape(match.group(2).strip())}</code>")
            index = match.end()
        return "".join(parts)

    def _spans(self, text: str) -> str:
        if not text:
            return ""
        out: list[str] = []
        index = 0
        token = re.compile(
            r"!\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)"   # 1,2 image
            r"|\[([^\]]+)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)"    # 3,4 link
            r"|<(https?://[^>\s]+)>"                           # 5 autolink
            r"|</?(sup|sub|br|kbd)\s*/?>"                      # 6 allowed HTML
            r"|\*\*(.+?)\*\*"                                  # 7 bold
            r"|(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])"      # 8 italic
            r"|(?<!\w)_(?!\s)([^_]+?)(?<!\s)_(?!\w)"           # 9 italic with underscores
            r"|(https?://[^\s<>()\[\]\"']+)"                   # 10 bare URL
        )
        while index < len(text):
            match = token.search(text, index)
            if not match:
                out.append(html.escape(text[index:]))
                break
            out.append(html.escape(text[index:match.start()]))
            groups = match.groups()
            if groups[0] is not None or (match.group(0).startswith("![") and groups[1]):
                out.append(f'<img src="{html.escape(self.link(groups[1]))}" alt="{html.escape(groups[0] or "")}">')
            elif groups[2] is not None:
                target = html.escape(self.link(groups[3]))
                external = ' target="_blank" rel="noopener"' if "://" in groups[3] else ""
                out.append(f'<a href="{target}"{external}>{self._spans(groups[2])}</a>')
            elif groups[4] is not None:
                out.append(f'<a href="{html.escape(groups[4])}" target="_blank" rel="noopener">{html.escape(groups[4])}</a>')
            elif groups[5] is not None:
                out.append(match.group(0).lower())
            elif groups[6] is not None:
                out.append(f"<strong>{self._spans(groups[6])}</strong>")
            elif groups[7] is not None:
                out.append(f"<em>{self._spans(groups[7])}</em>")
            elif groups[8] is not None:
                out.append(f"<em>{self._spans(groups[8])}</em>")
            elif groups[9] is not None:
                out.append(f'<a href="{html.escape(groups[9])}" target="_blank" rel="noopener">{html.escape(groups[9])}</a>')
            index = match.end()
        return "".join(out)


def render(text: str, link: Callable[[str], str] | None = None) -> Rendered:
    """Markdown to HTML with the document's headings (for a table of contents) and its title."""
    return Renderer(link).render(text)
