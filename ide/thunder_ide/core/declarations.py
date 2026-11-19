"""The declarations of a Thunder program, read from its text alone (no compilation).

Both Settings > API help on hover (``core.hoverdocs``) and Settings > IntelliSense
(``core.completion``) need to know what a program - or a file it imports - declares: its
functions and methods, structs, interfaces, enums and their members, modules, variables and
constants, a struct's fields and constructor. The compiler would know exactly, but only for a
program that compiles; while the user types it usually does not. This scanner works line by
line on the indentation, the way the language itself is laid out, so it answers for half-typed
programs too, never goes stale and is tested without a toolchain.

Words are matched with the Arabic-keyboard letters folded (``editing.fold_letters``), exactly as
the compiler folds them, so ``متغير`` declares a variable just like ``متغیر``; the declaration's
own text is kept as written.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Optional

from thunder_ide.core.editing import fold_letters


@dataclass(frozen=True)
class Declaration:
    row: int          # 0-based line of the declaration
    kind: str         # func, struct, interface, enum, var, const, field, ctor, member, module
    name: str         # the declared name as written
    owner: str        # the struct, interface or enum whose body holds it ("" otherwise)
    module: str       # the module block holding it ("" at the top level)
    indent: int       # the line's indentation, in characters
    code: str         # the declaration line without its trailing comment
    trailing: str     # the trailing comment of the line ("" when there is none)
    type: str = ""    # var/const/field: the declared type; func: the return type ("" if none)
    params: tuple[str, ...] = ()  # func/ctor: the parameters as written ("n: int", ...)
    local: bool = False  # declared inside a function's body (a local variable)
    end_row: int = -1    # func/ctor/module: the last line of its body (what is in scope where)
    value: str = ""      # var/const: the initialiser as written ("Stack()"; "" when there is none)

    @property
    def private(self) -> bool:
        """A member whose name starts with '_' belongs to its own module or struct."""
        return self.name.startswith("_")


def keyword_spellings(info: dict, kind: str, fallback: str) -> list[str]:
    """Every spelling of a keyword, the preferred one first.

    ``text`` is the spelling as written (``اعشاري``); ``spellings`` are the folded lookup forms
    (``اعشاری``) - the first is what to display, all of them are matched in source text.
    """
    for entry in info.get("keywords") or []:
        if entry.get("kind") == kind:
            spellings = [entry["text"]] if entry.get("text") else []
            spellings += [spelling for spelling in (entry.get("spellings") or []) if spelling not in spellings]
            return spellings or [fallback]
    return [fallback]


def strip_comment(line: str) -> tuple[str, str]:
    """The code of a line and its trailing comment (respecting strings)."""
    closing = None
    for index, ch in enumerate(line):
        if closing is not None:
            if ch in closing:
                closing = None
        elif ch == '"':
            closing = '"'
        elif ch == "«":
            closing = "»"
        elif ch in "“”„":
            closing = "“”„"
        elif ch == "#":
            return line[:index].rstrip(), line[index + 1:].strip()
    return line.rstrip(), ""


def split_top_level(text: str) -> list[str]:
    """``a: int, f: func(int, int): int`` split at its top-level commas (both , and ،)."""
    parts: list[str] = []
    depth = 0
    current = ""
    for ch in text:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth -= 1
        if ch in ",،" and depth == 0:
            parts.append(current.strip())
            current = ""
            continue
        current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def _bracketed(code: str, open_at: int) -> tuple[str, int]:
    """The text inside the bracket opening at ``open_at`` and the index just past its close."""
    depth = 0
    for index in range(open_at, len(code)):
        if code[index] == "(":
            depth += 1
        elif code[index] == ")":
            depth -= 1
            if depth == 0:
                return code[open_at + 1:index], index + 1
    return code[open_at + 1:], len(code)


class _Words:
    """The regular expressions of one language's declaration keywords (matched folded)."""

    def __init__(self, info: dict):
        def pattern(*kinds: tuple[str, str]) -> str:
            words: list[str] = []
            for kind, fallback in kinds:
                for spelling in keyword_spellings(info, kind, fallback):
                    folded = fold_letters(spelling)
                    if folded not in words:
                        words.append(folded)
            return "|".join(re.escape(word) for word in sorted(words, key=len, reverse=True))

        name = r"([^\s<:(),،=#]+)"
        self.func = re.compile(rf"^(\s*)(?:(?:{pattern(('KwNative', 'native'))})\s+)?(?:{pattern(('KwFunc', 'func'))})\s+{name}\s*(?:<[^>(]*>)?\s*\(")
        # A mixin ("mixin Bordered") reads like a struct: fields and methods that a struct takes in.
        self.type = re.compile(rf"^(\s*)({pattern(('KwStruct', 'struct'), ('KwMixin', 'mixin'))}|{pattern(('KwInterface', 'interface'))}|{pattern(('KwEnum', 'enum'))})\s+{name}")
        self.struct_words = {fold_letters(word) for word in keyword_spellings(info, "KwStruct", "struct")
                             + keyword_spellings(info, "KwMixin", "mixin")}
        self.interface_words = {fold_letters(word) for word in keyword_spellings(info, "KwInterface", "interface")}
        self.var = re.compile(rf"^(\s*)({pattern(('KwVar', 'var'))}|{pattern(('KwConst', 'const'))})\s+{name}")
        self.const_words = {fold_letters(word) for word in keyword_spellings(info, "KwConst", "const")}
        self.module = re.compile(rf"^(\s*)(?:{pattern(('KwModule', 'module'))})\s+{name}")
        self.field = re.compile(rf"^(\s+){name}\s*(\(\s*\))?\s*:\s*(\S.*)$")
        self.ctor = re.compile(rf"^(\s+){name}\s*\(")


def declarations(text: str, info: dict) -> list[Declaration]:
    """Every declaration of ``text``, in the order written (see the module documentation)."""
    words = _Words(info)
    result: list[Declaration] = []
    # The blocks the current line is inside: (indent, kind, name, index into result or -1).
    stack: list[tuple[int, str, str, int]] = []
    original_lines = text.split("\n")
    for row, folded_line in enumerate(fold_letters(text).split("\n")):
        line = original_lines[row]
        folded_code, _ = strip_comment(folded_line)
        if not folded_code.strip():
            continue
        code, trailing = strip_comment(line)
        indent = len(folded_code) - len(folded_code.lstrip(" \t"))
        while stack and indent <= stack[-1][0]:
            _close(stack.pop(), row, result, original_lines)
        module = next((name for _i, kind, name, _r in reversed(stack) if kind == "module"), "")
        container = stack[-1] if stack else None
        in_function = any(kind in ("func", "ctor") for _i, kind, _n, _r in stack)
        owner = container[2] if container is not None and container[1] in ("struct", "interface", "enum") else ""

        def add(kind: str, span: tuple[int, int], **extra) -> int:
            # The folding keeps every length, so the span of the folded match is the name as written.
            result.append(Declaration(row, kind, code[span[0]:span[1]], owner, module, indent, code, trailing, **extra))
            return len(result) - 1

        if container is not None and container[1] == "enum":
            # One member per line, or several separated by commas: the first word of each part.
            for member in re.finditer(r"(?:^|[,،])\s*([^\s,،=]+)", folded_code):
                if not member.group(1)[0].isdigit():
                    add("member", member.span(1))
            continue
        matched = words.module.match(folded_code)
        if matched and not in_function:
            index = add("module", matched.span(2))
            stack.append((indent, "module", matched.group(2), index))
            continue
        matched = words.func.match(folded_code)
        if matched:
            params, past = _bracketed(code, code.index("(", len(matched.group(0)) - 1))
            after = code[past:]
            returns = after.strip()[1:].strip() if after.strip().startswith(":") else ""
            index = add("func", matched.span(2), params=tuple(split_top_level(params)), type=returns,
                        local=in_function)
            stack.append((indent, "func", matched.group(2), index))
            continue
        matched = words.type.match(folded_code)
        if matched and not in_function:
            keyword = matched.group(2)
            kind = "struct" if keyword in words.struct_words else ("interface" if keyword in words.interface_words else "enum")
            add(kind, matched.span(3))
            stack.append((indent, kind, matched.group(3), -1))
            continue
        matched = words.var.match(folded_code)
        if matched:
            rest = folded_code[len(matched.group(0)):].strip()
            declared = ""
            written = code[len(code) - len(rest):]  # the same characters, as written
            if written.startswith(":"):
                declared = written[1:].split("=")[0].strip()
            value = written.split("=", 1)[1].strip() if "=" in written else ""
            add("const" if matched.group(2) in words.const_words else "var", matched.span(3), type=declared,
                local=in_function, value=value)
            continue
        if owner and container is not None and container[1] in ("struct", "interface"):
            matched = words.ctor.match(folded_code)
            if matched and fold_letters(matched.group(2)) == fold_letters(owner):
                params, _after = _bracketed(code, code.index("("))
                index = add("ctor", matched.span(2), params=tuple(split_top_level(params)))
                stack.append((indent, "ctor", matched.group(2), index))
                continue
            matched = words.field.match(folded_code)
            if matched:
                start = len(code) - len(matched.group(4))
                add("field", matched.span(2), type=code[start:].strip())
                continue
        if not in_function:
            # Any other line that opens a block (an if at the top level, a while...): its body
            # is no declaration's, but its indentation still has to be tracked.
            stack.append((indent, "block", "", -1))
    while stack:
        _close(stack.pop(), len(original_lines), result, original_lines)
    return result


def _close(entry: tuple[int, str, str, int], row: int, result: list[Declaration], lines: list[str]) -> None:
    """Records where a function's or a module's body ended (the last non-blank line before ``row``)."""
    _indent, kind, _name, index = entry
    if index < 0 or kind not in ("func", "ctor", "module"):
        return
    last = row - 1
    while last > result[index].row and not lines[last].strip():
        last -= 1
    declaration = result[index]
    result[index] = Declaration(declaration.row, declaration.kind, declaration.name, declaration.owner,
                                declaration.module, declaration.indent, declaration.code, declaration.trailing,
                                declaration.type, declaration.params, declaration.local, last, declaration.value)


def enclosing_function(found: list[Declaration], row: int) -> Optional[Declaration]:
    """The innermost function, method or constructor whose body holds ``row``."""
    best = None
    for declaration in found:
        if declaration.kind in ("func", "ctor") and declaration.row < row <= declaration.end_row:
            if best is None or declaration.row > best.row:
                best = declaration
    return best


def parameter_name(parameter: str) -> str:
    """``n: int`` -> ``n``; ``...values`` -> ``values``."""
    return parameter.split(":")[0].strip().lstrip(".")


def twin(found: list[Declaration], declaration: Declaration, other: list[Declaration]) -> Optional[Declaration]:
    """The declaration of a translated twin file that corresponds to ``declaration``.

    The SDK is written twice - lib/thunder/en and lib/thunder/ps - declaration for declaration
    (a test keeps them in step), so the n-th function of one is the n-th function of the other,
    the n-th field the n-th field, and so on. Local variables are left out: a function's body
    is free to differ.
    """
    def ordinal_list(items: list[Declaration], kind: str) -> list[Declaration]:
        return [item for item in items if item.kind == kind and not item.local]

    mine = ordinal_list(found, declaration.kind)
    if declaration not in mine:
        return None
    theirs = ordinal_list(other, declaration.kind)
    if len(theirs) != len(mine):
        return None
    candidate = theirs[mine.index(declaration)]
    if bool(candidate.owner) != bool(declaration.owner) or bool(candidate.module) != bool(declaration.module):
        return None
    return candidate
