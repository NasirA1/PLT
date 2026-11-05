"""Syntax highlighting for the code editor.

The highlighter runs while the user types, so it uses a few regular expressions instead of
the real lexer. The reserved *words*, however, come from the compiler
(``thunderc --language-info``), so the editor always agrees with the language, including
translated keyword sets such as Pashto.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

# \d and \w are Unicode-aware, so Pashto digits (۰-۹) and letters are recognised too.
_TOKEN = re.compile(
    r"(?P<comment>//[^\n]*|/\*.*?(?:\*/|\Z))"
    r"|(?P<string>\"(?:\\.|[^\"\\\n])*\"?|«(?:\\.|[^»\\\n])*»?)"
    r"|(?P<number>\b\d+\b)"
    r"|(?P<word>[^\W\d]\w*)",
    re.DOTALL,
)

# Fallback used when the compiler cannot be asked (keeps the editor usable).
DEFAULT_WORDS = {
    "keyword": {"var", "const", "func", "struct", "if", "else", "while", "for", "break", "continue", "return"},
    "type": {"int", "bool", "string"},
    "literal": {"true", "false"},
    "builtin": {"print", "println", "len", "str", "to_int", "array", "inspect", "dump", "copy"},
}


@dataclass
class WordClasses:
    """Which highlight class each reserved word belongs to."""

    words: dict[str, str] = field(default_factory=dict)

    @classmethod
    def default(cls) -> "WordClasses":
        return cls({word: tag for tag, words in DEFAULT_WORDS.items() for word in words})

    @classmethod
    def from_language_info(cls, info: dict) -> "WordClasses":
        words: dict[str, str] = {}
        for keyword in info.get("keywords", []):
            category = keyword.get("category", "keyword")
            tag = category if category in ("keyword", "type", "literal") else "keyword"
            for spelling in keyword.get("spellings", [keyword["text"]]):
                words[spelling] = tag
        for builtin in info.get("builtins", []):
            words[builtin["name"]] = "builtin"
        return cls(words)


@dataclass(frozen=True)
class Highlight:
    tag: str    # "comment", "string", "number", "keyword", "type", "literal" or "builtin"
    start: int  # character offset
    end: int


def highlight(text: str, classes: WordClasses) -> list[Highlight]:
    """Returns the highlighted regions of ``text`` in order."""
    result: list[Highlight] = []
    for match in _TOKEN.finditer(text):
        kind = match.lastgroup
        if kind == "word":
            tag = classes.words.get(match.group())
            if tag is None:
                continue
        else:
            tag = kind
        result.append(Highlight(tag, match.start(), match.end()))
    return result


def styles_per_character(text: str, regions: list[Highlight]) -> list[str | None]:
    """The highlight tag of every character (None for plain text)."""
    styles: list[str | None] = [None] * len(text)
    for region in regions:
        for index in range(region.start, min(region.end, len(text))):
            styles[index] = region.tag
    return styles
