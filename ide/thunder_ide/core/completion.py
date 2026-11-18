"""IntelliSense for the Qt IDE (Settings > IntelliSense): what can be typed at the caret, and the
signature of the call the caret is in.

It offers the same things as the VS Code extension's completion provider, in the program's own
language:

    after ``import``     the built-in modules that need importing, and the program's module files
    after a ``.``        a built-in module's functions and enums (``math.``), an enum's members
                         (``Direction.``), a module block's public members (``geometry.``), or the
                         fields and methods of the struct a variable holds (``s.`` on a Stack)
    after ``:``          the types: the type words, and the structs, interfaces and enums in reach
    anywhere else        the names in scope (locals, parameters, the struct's own members inside a
                         method), the program's and its imports' declarations, the built-ins and
                         the keywords

Like the hover help (``core.hoverdocs``) this is plain text work - the keywords and built-ins come
from ``thunderc --language-info``, the declarations from ``core.declarations`` - so it answers
while the program does not compile yet, which is when completion is wanted, and it is tested
without a GUI. A variable's struct is known from its declared type (``var s: Stack<int>``), its
initialiser (``var b = Button("OK")``) or a parameter's type; that covers the programs a learner
writes, without the semantic analyser.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from html import escape
from typing import Callable, Optional, Sequence

from thunder_ide.core import hoverdocs
from thunder_ide.core.declarations import (Declaration, declarations, enclosing_function, keyword_spellings,
                                           parameter_name, split_top_level, strip_comment)
from thunder_ide.core.editing import fold_letters

# What an item is; the editor colours and orders by it. The groups decide the order of the list:
# what the program itself declares first, then the language's built-ins, then its keywords.
KIND_GROUP = {
    "parameter": 0, "variable": 0, "constant": 0, "field": 0, "method": 0, "member": 0,
    "function": 1, "struct": 1, "interface": 1, "enum": 1, "module": 3, "file": 1,
    "builtin": 2, "property": 2, "type": 4, "keyword": 4, "literal": 4,
}


@dataclass(frozen=True)
class CompletionItem:
    label: str               # what the list shows and what is matched against the typed word
    kind: str                # see KIND_GROUP
    detail: str = ""         # the signature or type ("println(value, ...)", "Stack<int>")
    documentation: str = ""  # a sentence or two (the reference's description, a doc comment)
    insert: str = ""         # the text that replaces the typed word ("" means the label)
    caret_back: int = 0      # how many characters before the end of ``insert`` the caret goes
    source: str = ""         # the imported file it is declared in ("" for the program, built-ins)

    @property
    def text(self) -> str:
        return self.insert or self.label


@dataclass(frozen=True)
class Completions:
    start: int   # the code-point index where the word being completed begins
    end: int     # the caret: [start, end) is replaced by the chosen item's text
    items: tuple[CompletionItem, ...]


@dataclass(frozen=True)
class CompletionProvider:
    """What the editor asks while Settings > IntelliSense is on (None turns it off)."""
    complete: Callable[[str, int, bool], Optional[Completions]]  # (text, caret, asked with Ctrl+Space)
    signature: Callable[[str, int], Optional[str]]               # rich text of the call around the caret


ImportLoader = hoverdocs.ImportLoader


def _is_name_char(ch: str) -> bool:
    return hoverdocs._is_name_char(ch)


def _matches(label: str, typed: str) -> bool:
    """True when a label starts with the typed word - or, for a name of several words such as
    ``کرښه چاپ``, when one of its words does. Letters are compared folded and case-blind."""
    if not typed:
        return True
    label = fold_letters(label).casefold()
    typed = fold_letters(typed).casefold()
    if label.startswith(typed):
        return True
    return any(word.startswith(typed) for word in re.split(r"[\s_.]+", label)[1:])


def _rank(item: CompletionItem, typed: str, first: Sequence[str] = ()) -> tuple:
    folded = fold_letters(item.label).casefold()
    prefix = 0 if folded.startswith(fold_letters(typed).casefold()) else 1
    group = -1 if item.kind in first else KIND_GROUP.get(item.kind, 5)
    return (prefix, group, folded)


# ---------------------------------------------------------------------------------- the context


def _word_before(text: str, index: int) -> int:
    start = index
    while start > 0 and _is_name_char(text[start - 1]):
        start -= 1
    return start


def _expression_before_dot(text: str, dot: int) -> str:
    """``a.b`` out of ``x = a.b.`` (the dot at index ``dot``)."""
    start = dot
    while start > 0 and (_is_name_char(text[start - 1]) or text[start - 1] == "."):
        start -= 1
    return text[start:dot].strip(".")


def _inside_string_or_comment(line: str) -> bool:
    code, _comment = strip_comment(line)
    if len(code) < len(line.rstrip()):
        return True  # the caret is in the comment
    closing = None
    for ch in line:
        if closing is not None:
            if ch in closing:
                closing = None
        elif ch == '"':
            closing = '"'
        elif ch == "«":
            closing = "»"
        elif ch in "“”„":
            closing = "“”„"
    return closing is not None


class _Context:
    """Everything one completion request looks at, gathered once."""

    def __init__(self, text: str, index: int, info: dict, load_import: Optional[ImportLoader]):
        self.text = text
        self.index = index
        self.info = info
        self.row = text.count("\n", 0, index)
        self.found = declarations(text, info)
        self.imports: list[tuple[str, list[Declaration]]] = []
        self.lines: dict[str, list[str]] = {"": text.split("\n")}  # each file's lines, for doc comments
        if load_import is not None:
            for target in hoverdocs.import_targets(text, info):
                loaded = load_import(target)
                if loaded is not None and loaded[0] not in self.lines:
                    self.imports.append((loaded[0], declarations(loaded[1], info)))
                    self.lines[loaded[0]] = loaded[1].split("\n")

    def everywhere(self) -> list[tuple[str, Declaration]]:
        """The program's declarations, then each imported file's, with the file they are in."""
        result = [("", declaration) for declaration in self.found]
        for name, found in self.imports:
            result.extend((name, declaration) for declaration in found)
        return result

    def keyword(self, kind: str, fallback: str) -> str:
        return keyword_spellings(self.info, kind, fallback)[0]


def complete(text: str, index: int, info: dict, load_import: Optional[ImportLoader] = None,
             module_files: Sequence[str] = (), explicit: bool = False) -> Optional[Completions]:
    """What may be typed at the code-point ``index`` of ``text`` (None: nothing to offer).

    ``module_files`` are the program's own module files as ``import`` names them ("lib/geometry").
    Without ``explicit`` (Ctrl+Space) nothing is offered before a letter is typed, except right
    after a dot; an item that is exactly the word already typed is left out, so the list closes
    once a word is complete.
    """
    index = max(0, min(index, len(text)))
    line_start = text.rfind("\n", 0, index) + 1
    line_before = text[line_start:index]
    start = _word_before(text, index)
    typed = text[start:index]
    if typed and typed[0].isdigit():
        return None  # a number

    imported = _import_completions(line_before, typed, info, module_files)
    if imported is not None:
        items = [item for item in imported if _matches(item.label, typed.strip('"«“'))]
        return _finish(start, index, items, typed, explicit)
    if _inside_string_or_comment(line_before):
        return None

    context = _Context(text, index, info, load_import)
    if start > 0 and text[start - 1] == ".":
        expression = _expression_before_dot(text, start - 1)
        if not expression or expression[0].isdigit():
            return None
        items = _member_completions(context, expression)
        if items is None:
            return None
        return _finish(start, index, [item for item in items if _matches(item.label, typed)], typed, explicit)

    # "متن.پاک ک": the second word of a module's member of several words.
    left = start
    for _ in range(3):
        if not (left >= 2 and text[left - 1] == " " and _is_name_char(text[left - 2])):
            break
        left = _word_before(text, left - 1)
        if left > 0 and text[left - 1] == ".":
            expression = _expression_before_dot(text, left - 1)
            items = _member_completions(context, expression) if expression else None
            several = text[left:index]
            items = [item for item in items or [] if " " in item.label and _matches(item.label, several)]
            if items:
                return _finish(left, index, items, several, explicit)
            break

    if not typed and not explicit:
        return None
    in_type = _after_type_colon(text[line_start:start])
    items = _type_completions(context) if in_type else _general_completions(context)
    # A Pashto built-in of several words: "کرښه چ" completes to "کرښه چاپ" over both words.
    start, typed = _widen_to_several_words(text, start, index, typed, items)
    return _finish(start, index, [item for item in items if _matches(item.label, typed)], typed, explicit,
                   first=("type",) if in_type else ())


def insertion(text: str, completions: Completions, item: CompletionItem) -> tuple[int, int, str, int]:
    """How choosing ``item`` changes ``text``: (start, end, replacement, caret after it).

    The typed word is replaced; a function brings its brackets, with the caret between them when
    it takes arguments - unless a bracket already follows the caret, which is then kept."""
    replacement = item.text
    caret_back = item.caret_back
    following = text[completions.end:completions.end + 1]
    if replacement.endswith("()") and following == "(":
        replacement = replacement[:-2]
        caret_back = 0
    return completions.start, completions.end, replacement, completions.start + len(replacement) - caret_back


def _finish(start: int, end: int, items: list[CompletionItem], typed: str, explicit: bool,
            first: Sequence[str] = ()) -> Optional[Completions]:
    seen: set[tuple[str, str]] = set()
    unique: list[CompletionItem] = []
    for item in items:
        key = (fold_letters(item.label), item.kind if item.kind not in ("function", "builtin") else "callable")
        if key in seen:
            continue
        seen.add(key)
        unique.append(item)
    if typed and not explicit:
        unique = [item for item in unique if fold_letters(item.label) != fold_letters(typed)]
    if not unique:
        return None
    unique.sort(key=lambda item: _rank(item, typed, first))
    return Completions(start, end, tuple(unique))


def _widen_to_several_words(text: str, start: int, index: int, typed: str,
                            items: list[CompletionItem]) -> tuple[int, str]:
    best = (start, typed)
    left = start
    for _ in range(3):
        if left >= 2 and text[left - 1] == " " and _is_name_char(text[left - 2]):
            left = _word_before(text, left - 1)
            candidate = text[left:index]
            folded = fold_letters(candidate).casefold()
            if " " in candidate and any(fold_letters(item.label).casefold().startswith(folded)
                                        for item in items if " " in item.label):
                best = (left, candidate)
        else:
            break
    return best


# ---------------------------------------------------------------------------------- the lists


def _builtin_item(entry: dict, info: dict, label: str) -> CompletionItem:
    signature = hoverdocs.builtin_signature(entry, info)
    parameters = _parameters_of(signature)
    return _callable_item(label, "builtin", signature, entry.get("description", ""), parameters)


def _callable_item(label: str, kind: str, detail: str, documentation: str, parameters: list[str]) -> CompletionItem:
    """``name()`` with the caret between the brackets when there is something to pass."""
    takes = any(parameter.strip() for parameter in parameters)
    return CompletionItem(label, kind, detail, documentation, f"{label}()", 1 if takes else 0)


def _parameters_of(signature: str) -> list[str]:
    open_at = signature.find("(")
    if open_at < 0:
        return []
    depth = 0
    for position in range(open_at, len(signature)):
        if signature[position] == "(":
            depth += 1
        elif signature[position] == ")":
            depth -= 1
            if depth == 0:
                return split_top_level(signature[open_at + 1:position])
    return split_top_level(signature[open_at + 1:])


def _keyword_items(info: dict) -> list[CompletionItem]:
    items = []
    for entry in info.get("keywords") or []:
        if entry.get("category") == "operator" and not _is_name_char((entry.get("text") or " ")[0]):
            continue
        category = entry.get("category", "keyword")
        kind = category if category in ("type", "literal") else "keyword"
        items.append(CompletionItem(entry.get("text", ""), kind, category))
    return items


def _module_items(info: dict) -> list[CompletionItem]:
    return [CompletionItem(entry.get("name", ""), "module", f"module {entry.get('canonical_name', '')}")
            for entry in info.get("modules") or []]


def _declaration_item(declaration: Declaration, source: str, lines_of: Callable[[str], list[str]],
                      label: Optional[str] = None) -> Optional[CompletionItem]:
    label = label or declaration.name
    documentation = ""
    lines = lines_of(source)
    if lines:
        documentation = hoverdocs._comment_block_above(lines, declaration.row) or declaration.trailing
    kind = declaration.kind
    detail = declaration.code.strip()
    if kind in ("func", "ctor"):
        item_kind = "method" if declaration.owner and kind == "func" else "function"
        item = _callable_item(label, item_kind, detail, documentation, list(declaration.params))
    elif kind in ("var", "const"):
        item = CompletionItem(label, "constant" if kind == "const" else "variable", detail, documentation)
    elif kind in ("field", "struct", "interface", "enum", "module"):
        item = CompletionItem(label, kind, detail, documentation)
    elif kind == "member":
        item = CompletionItem(label, "member", declaration.owner, documentation)
    else:
        return None
    return CompletionItem(item.label, item.kind, item.detail, item.documentation, item.insert, item.caret_back,
                          source)


def _lines_reader(context: _Context) -> Callable[[str], list[str]]:
    return lambda source: context.lines.get(source, [])


def _general_completions(context: _Context) -> list[CompletionItem]:
    """The names that may be written at the caret, outside any dot."""
    info = context.info
    lines_of = _lines_reader(context)
    items: list[CompletionItem] = []
    # In scope first: the enclosing function's parameters and the locals declared above.
    function = enclosing_function(context.found, context.row)
    if function is not None:
        for parameter in function.params:
            name = parameter_name(parameter)
            if name:
                items.append(CompletionItem(name, "parameter", parameter.strip()))
        for declaration in context.found:
            if (declaration.local and declaration.kind in ("var", "const")
                    and function.row < declaration.row < context.row <= function.end_row + 1):
                item = _declaration_item(declaration, "", lines_of)
                if item is not None:
                    items.append(item)
        if function.owner:
            # Inside a method a bare member name means the receiver's member (no "self").
            for source, declaration in _struct_members(context, function.owner):
                item = _declaration_item(declaration, source, lines_of)
                if item is not None and declaration.kind != "ctor":
                    items.append(item)
    current_module = function.module if function is not None else _module_at(context)
    for source, declaration in context.everywhere():
        if declaration.local or declaration.owner or declaration.kind in ("member", "field", "ctor"):
            continue
        if declaration.module and not (not source and declaration.module == current_module):
            continue  # a module's members are reached through its name (geometry.area)
        if source and declaration.private:
            continue
        item = _declaration_item(declaration, source, lines_of)
        if item is not None:
            items.append(item)
    for entry in info.get("builtins") or []:
        if entry.get("module"):
            continue  # math.sqrt is offered after "math."
        items.append(_builtin_item(entry, info, entry.get("name", "")))
    items.extend(_module_items(info))
    items.extend(_keyword_items(info))
    return [item for item in items if item.label]


def _module_at(context: _Context) -> str:
    """The module block the caret is in, if any (its members may be used by their short names)."""
    for declaration in context.found:
        if declaration.kind == "module" and declaration.row < context.row <= declaration.end_row:
            return declaration.name
    return ""


def _after_type_colon(before_word: str) -> bool:
    """True after the ``:`` of a declared type (``var x: |``, ``n: |``, ``): |``) - not in a map."""
    stripped = before_word.rstrip()
    if not stripped.endswith(":"):
        return False
    return stripped.count("{") <= stripped.count("}")


def _type_completions(context: _Context) -> list[CompletionItem]:
    info = context.info
    lines_of = _lines_reader(context)
    items = [item for item in _keyword_items(info) if item.kind == "type"]
    items.append(CompletionItem(context.keyword("KwFunc", "func"), "keyword", "a function type: func(int): int"))
    for source, declaration in context.everywhere():
        if declaration.kind in ("struct", "interface", "enum") and not (source and declaration.private):
            label = f"{declaration.module}.{declaration.name}" if declaration.module else declaration.name
            item = _declaration_item(declaration, source, lines_of, label)
            if item is not None:
                items.append(item)
    for enum in info.get("enums") or []:
        module = _module_name(info, enum.get("module", ""))
        label = f"{module}.{enum.get('name', '')}" if module else enum.get("name", "")
        items.append(CompletionItem(label, "enum", "enum", enum.get("description", "")))
    return items


def _module_name(info: dict, canonical: str) -> str:
    """A built-in module's name in the program's language (``graphics`` -> ``ګرافیک``)."""
    for entry in info.get("modules") or []:
        if entry.get("canonical_name") == canonical:
            return entry.get("name", canonical)
    return canonical


def _builtin_module(info: dict, name: str) -> Optional[dict]:
    folded = fold_letters(name)
    for entry in info.get("modules") or []:
        if fold_letters(entry.get("name", "")) == folded or entry.get("canonical_name") == name:
            return entry
    return None


def _import_completions(line_before: str, typed: str, info: dict,
                        module_files: Sequence[str]) -> Optional[list[CompletionItem]]:
    """After ``import``: the modules that need importing, and the module files of the program."""
    folded = fold_letters(line_before)
    import_words = [fold_letters(word) for word in keyword_spellings(info, "KwImport", "import")]
    native_words = [fold_letters(word) for word in keyword_spellings(info, "KwNative", "native")]
    words = "|".join(re.escape(word) for word in import_words)
    natives = "|".join(re.escape(word) for word in native_words)
    matched = re.match(rf'^\s*(?:{words})\s+(?:(?:{natives})\s+)?(["«“]?)[^"»”\s]*$', folded)
    if not matched:
        return None
    quoted = bool(matched.group(1))
    items = []
    if not quoted:
        for entry in info.get("modules") or []:
            if entry.get("needs_import"):
                items.append(CompletionItem(entry.get("name", ""), "module",
                                            f"built-in module {entry.get('canonical_name', '')}"))
    opening, closing = {"«": ("«", "»"), "“": ("“", "”")}.get(matched.group(1), ('"', '"'))
    for name in module_files:
        items.append(CompletionItem(name, "file", "module file",
                                    insert=f"{name}{closing}" if quoted else f"{opening}{name}{closing}"))
    return items


def _member_completions(context: _Context, expression: str) -> Optional[list[CompletionItem]]:
    """What may follow ``expression.`` - None when the expression's members are not known."""
    info = context.info
    lines_of = _lines_reader(context)
    parts = expression.split(".")
    # math. - a built-in module's functions and enums.
    module = _builtin_module(info, parts[0]) if len(parts) == 1 else None
    if module is not None:
        items = []
        for entry in info.get("builtins") or []:
            if entry.get("module") == module.get("canonical_name"):
                items.append(_builtin_item(entry, info, entry.get("member") or entry.get("name", "")))
        for enum in info.get("enums") or []:
            if enum.get("module") == module.get("canonical_name"):
                items.append(CompletionItem(enum.get("name", ""), "enum", "enum", enum.get("description", "")))
        return items
    # Direction. or graphics.Color. - an enum's members, its values and its count.
    enum_items = _enum_completions(context, parts)
    if enum_items is not None:
        return enum_items
    # geometry. - a module block's public members (the program's own, or an imported file's).
    if len(parts) == 1:
        wanted = fold_letters(parts[0])
        members = [(source, declaration) for source, declaration in context.everywhere()
                   if declaration.module and fold_letters(declaration.module) == wanted
                   and not declaration.local and not declaration.owner
                   and declaration.kind not in ("member", "field", "ctor")]
        if members:
            inside = _module_at(context) == parts[0]
            items = []
            for source, declaration in members:
                if declaration.private and not inside:
                    continue
                item = _declaration_item(declaration, source, lines_of)
                if item is not None:
                    items.append(item)
            return items
    # b. - the fields and methods of the struct the variable holds.
    struct = _type_of(context, parts)
    if struct is None:
        return None
    inside = _struct_at(context) == fold_letters(struct)
    items = []
    for source, declaration in _struct_members(context, struct):
        if declaration.kind == "ctor" or (declaration.private and not inside):
            continue
        item = _declaration_item(declaration, source, lines_of)
        if item is not None:
            items.append(item)
    return items


def _enum_completions(context: _Context, parts: list[str]) -> Optional[list[CompletionItem]]:
    info = context.info
    terms = info.get("enum_terms") or {}
    wanted = fold_letters(parts[-1])
    members: Optional[list[str]] = None
    name = parts[-1]
    if len(parts) <= 2:
        for enum in info.get("enums") or []:
            if fold_letters(enum.get("name", "")) == wanted or enum.get("canonical_name") == parts[-1]:
                if len(parts) == 2 and _builtin_module(info, parts[0]) is None:
                    continue
                members = [member.get("name", "") for member in enum.get("members") or []]
                name = enum.get("name", name)
                break
        if members is None:
            module = parts[0] if len(parts) == 2 else ""
            for _source, declaration in context.everywhere():
                if (declaration.kind == "enum" and fold_letters(declaration.name) == wanted
                        and (not module or fold_letters(declaration.module) == fold_letters(module))):
                    members = [other.name for _s, other in context.everywhere()
                               if other.kind == "member" and other.owner == declaration.name]
                    name = declaration.name
                    break
    if members is None:
        return None
    items = [CompletionItem(member, "member", name) for member in members if member]
    items.append(CompletionItem(terms.get("values", "values"), "property", f"{name}[]"))
    items.append(CompletionItem(terms.get("count", "count"), "property", keyword_spellings(info, "KwInt", "int")[0]))
    return items


def _struct_at(context: _Context) -> str:
    """The struct whose body the caret is in (folded), or ""."""
    function = enclosing_function(context.found, context.row)
    return fold_letters(function.owner) if function is not None and function.owner else ""


def _struct_members(context: _Context, struct: str) -> list[tuple[str, Declaration]]:
    """A struct's fields, methods and constructor, and those of the mixins it lists after ':'."""
    wanted = fold_letters(struct)
    everywhere = context.everywhere()
    result: list[tuple[str, Declaration]] = []
    pending = [wanted]
    visited: set[str] = set()
    while pending:
        current = pending.pop(0)
        if current in visited:
            continue
        visited.add(current)
        for source, declaration in everywhere:
            if declaration.kind in ("struct", "interface") and fold_letters(declaration.name) == current:
                code = fold_letters(declaration.code)
                after = code.split(":", 1)[1] if ":" in code else ""
                pending.extend(fold_letters(part.strip().split("<")[0]) for part in split_top_level(after)
                               if part.strip())
            if declaration.owner and fold_letters(declaration.owner) == current and declaration.kind != "member":
                result.append((source, declaration))
    # The struct's own member of a name wins over a mixin's (overriding), so keep the first.
    seen: set[str] = set()
    unique = []
    for source, declaration in result:
        key = fold_letters(declaration.name)
        if key not in seen:
            seen.add(key)
            unique.append((source, declaration))
    return unique


_CALL_TYPE = re.compile(r"^\s*(?:[^\W\d][\w]*\.)?([^\W\d][\w]*)\s*(?:<[^>]*>)?\s*\(")


def _bare_type(written: str) -> str:
    """``Stack<int>`` -> ``Stack``; ``geometry.Rectangle`` -> ``Rectangle``; arrays have no members."""
    written = written.strip()
    if not written or written.endswith("]") or written.startswith(("(", "{")):
        return ""
    written = written.split("<")[0].strip()
    return written.split(".")[-1]


def _declared_type(declaration: Declaration) -> str:
    if declaration.type:
        return _bare_type(declaration.type)
    matched = _CALL_TYPE.match(declaration.value or "")
    return matched.group(1) if matched else ""


def _type_of(context: _Context, parts: list[str]) -> Optional[str]:
    """The struct the expression ``a.b.c`` holds, following fields; None when it is not known."""
    first = fold_letters(parts[0])
    struct = ""
    function = enclosing_function(context.found, context.row)
    if function is not None:
        for parameter in function.params:
            if fold_letters(parameter_name(parameter)) == first and ":" in parameter:
                struct = _bare_type(parameter.split(":", 1)[1])
        if not struct:
            for declaration in context.found:
                if (declaration.local and declaration.kind in ("var", "const") and fold_letters(declaration.name) == first
                        and function.row < declaration.row <= context.row):
                    struct = _declared_type(declaration)
        if not struct and function.owner:
            for _source, member in _struct_members(context, function.owner):
                if member.kind == "field" and fold_letters(member.name) == first:
                    struct = _bare_type(member.type)
    if not struct:
        for _source, declaration in context.everywhere():
            if (declaration.kind in ("var", "const") and not declaration.local and not declaration.owner
                    and fold_letters(declaration.name) == first):
                struct = _declared_type(declaration)
                if struct:
                    break
    for part in parts[1:]:
        if not struct:
            return None
        field = next((member for _source, member in _struct_members(context, struct)
                      if member.kind == "field" and fold_letters(member.name) == fold_letters(part)), None)
        struct = _bare_type(field.type) if field is not None else ""
    if not struct:
        return None
    known = any(declaration.kind in ("struct", "interface") and fold_letters(declaration.name) == fold_letters(struct)
                for _source, declaration in context.everywhere())
    return struct if known else None


# ---------------------------------------------------------------------------------- signature help


def call_at(text: str, index: int) -> Optional[tuple[int, int]]:
    """The call the caret is in: (index of the called name's last character, argument number).

    Counts the brackets backwards from the caret, across at most a few lines (a call may be
    continued on the next line), and the commas at the call's own level.
    """
    depth = 0
    argument = 0
    position = index - 1
    lines = 0
    closing = None
    while position >= 0:
        ch = text[position]
        if ch == "\n":
            lines += 1
            if lines > 10:
                return None
        elif closing is not None:
            if ch == closing:
                closing = None
        elif ch in "\"»”":
            closing = {"\"": "\"", "»": "«", "”": "“"}[ch]
        elif ch in ")]}":
            depth += 1
        elif ch in "([{":
            if depth == 0:
                if ch != "(":
                    return None
                name_end = position
                while name_end > 0 and text[name_end - 1] in " \t":
                    name_end -= 1
                if name_end == 0 or not _is_name_char(text[name_end - 1]):
                    return None
                return name_end - 1, argument
            depth -= 1
        elif ch in ",،" and depth == 0:
            argument += 1
        position -= 1
    return None


def signature_help(text: str, index: int, info: dict, load_import: Optional[ImportLoader] = None) -> Optional[str]:
    """The rich text shown above the caret inside a call's brackets: the called function's
    signature with the parameter being typed in bold, and its description."""
    call = call_at(text, index)
    if call is None:
        return None
    name_index, argument = call
    doc = hoverdocs.describe(text, name_index, info, load_import)
    if doc is None:
        return None
    signature = doc.signature.split("\n")[0]
    parameters = _parameters_of(signature)
    if "(" not in signature:
        return None
    open_at = signature.find("(")
    head = signature[:open_at + 1]
    tail = signature[_closing_of(signature, open_at):]
    comma = next((mark.get("text", ",") for mark in info.get("punctuation", []) if mark.get("means") == ","), ",")
    active = argument
    if parameters and "..." in parameters[-1]:
        active = min(argument, len(parameters) - 1)  # every further argument is the variadic one
    shown = []
    for position, parameter in enumerate(parameters):
        escaped = escape(parameter)
        shown.append(f"<b><u>{escaped}</u></b>" if position == active else escaped)
    html = f"<p style='white-space:pre' dir='auto'>{escape(head)}{(comma + ' ').join(shown)}{escape(tail)}</p>"
    body = " ".join(line.strip() for line in doc.body.split("\n\n")[0].split("\n")).strip()
    if body:
        html += f"<p dir='auto'>{escape(body)}</p>"
    return html


def _closing_of(signature: str, open_at: int) -> int:
    depth = 0
    for position in range(open_at, len(signature)):
        if signature[position] == "(":
            depth += 1
        elif signature[position] == ")":
            depth -= 1
            if depth == 0:
                return position
    return len(signature)
