"""Syntax highlighting for the code editor.

The highlighter runs while the user types, so it uses a few regular expressions instead of
the real lexer. The reserved *words*, however, come from the compiler
(``thunderc --language-info``), so the editor always agrees with the language, including
translated keyword sets such as Pashto, whose built-ins may consist of several words
(``کرښه چاپ``).
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from thunder_ide.core.editing import fold_letters

# \d and \w are Unicode-aware, so Pashto digits (۰-۹) and letters are recognised too.
_TOKEN = re.compile(
    r"(?P<comment>#[^\n]*|'''.*?(?:'''|\Z))"
    r"|(?P<string>\"(?:\\.|[^\"\\\n])*\"?|«(?:\\.|[^»\\\n])*»?|[“”„](?:\\.|[^“”„\\\n])*[“”„]?)"
    r"|(?P<number>\b0[xX][0-9a-fA-F]+|\b0[bB][01]+|\b\d+(?:[.٫]\d+)?)"
    r"|(?P<word>[^\W\d]\w*)",
    re.DOTALL,
)
_NEXT_WORD = re.compile(r"[ \t]+([^\W\d]\w*)")

# Fallback used when the compiler cannot be asked (keeps the editor usable).
DEFAULT_WORDS = {
    "keyword": {"var", "const", "func", "struct", "if", "else", "while", "for", "break", "continue", "return", "pass",
                "import", "in", "and", "or", "not"},
    "type": {"int", "float", "bool", "string"},
    "literal": {"true", "false"},
    "builtin": {"print", "println", "len", "str", "to_int", "array", "inspect", "dump", "copy", "has", "remove", "keys",
                "values", "input", "end_of_input", "substring", "trim", "to_upper", "to_lower", "split", "join",
                "replace", "starts_with", "ends_with", "contains", "index_of", "repeat", "reverse", "char_code",
                "from_char_code", "is_int", "first", "second", "swap", "zip", "enumerate", "items", "push", "pop",
                "insert", "remove_at", "slice", "concat", "sort", "range", "sum", "map", "filter", "reduce", "get",
                "to_float", "is_number", "abs", "min", "max", "round", "floor", "ceil", "sqrt", "pow",
                "exp", "log", "sin", "cos", "tan", "pi", "random"},
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

    @property
    def compound_firsts(self) -> set[str]:
        """First words of the reserved words that are written as several words."""
        return {word.split(" ", 1)[0] for word in self.words if " " in word}


@dataclass(frozen=True)
class Highlight:
    tag: str    # "comment", "string", "number", "keyword", "type", "literal" or "builtin"
    start: int  # character offset
    end: int


def highlight(text: str, classes: WordClasses) -> list[Highlight]:
    """Returns the highlighted regions of ``text`` in order."""
    result: list[Highlight] = []
    compound_firsts = classes.compound_firsts
    position = 0
    while True:
        match = _TOKEN.search(text, position)
        if match is None:
            return result
        position = match.end()
        kind = match.lastgroup
        if kind != "word":
            result.append(Highlight(str(kind), match.start(), match.end()))
            continue
        # Words are looked up with Arabic keyboard letters folded (كرښه means کرښه).
        word = fold_letters(match.group())
        if word in compound_firsts:
            # A multi-word name such as "کرښه چاپ": join the following words while they
            # still form a reserved word (spelt with single spaces).
            while True:
                following = _NEXT_WORD.match(text, position)
                if following is None or f"{word} {fold_letters(following.group(1))}" not in classes.words:
                    break
                word = f"{word} {fold_letters(following.group(1))}"
                position = following.end()
        tag = classes.words.get(word)
        if tag is not None:
            result.append(Highlight(tag, match.start(), position))

