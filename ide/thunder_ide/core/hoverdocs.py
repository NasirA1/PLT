"""What the editor says when the pointer rests on a name (Settings > API help on hover).

For a built-in - ``println``, ``math.sqrt``, the Pashto ``کرښه چاپ`` - the tooltip is the API
reference itself: the signature and description the compiler carries for the program's language
(``thunderc --language-info``). For a name the program (or a file it imports) declares, it is the
declaration's signature and the ``#`` comment block written right above it - the "JavaDoc" of
Thunder, exactly what the API site shows for the SDK's own files.

Everything here is pure text work on the editor's contents, so it needs no compilation, is never
stale while the user types, and is tested without a GUI. Resolving *which* ``push`` a value's
type selects would need the semantic analyser; instead a member after a dot prefers a declared
method of that name (the import's file is named in the tooltip), and falls back to the built-in.
"""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass
from html import escape
from typing import Callable, Optional


@dataclass(frozen=True)
class HoverDoc:
    signature: str  # the declaration as written, e.g. "func push(value: T)" (may be two lines)
    body: str       # the documentation paragraphs ("" when there are none)
    source: str     # the file the declaration came from ("" for the program itself and built-ins)


#: Resolves an import target ("datastructures", "lib/greeting") to (file name, text), or None.
ImportLoader = Callable[[str], Optional[tuple[str, str]]]


def _is_name_char(ch: str) -> bool:
    """True for the characters an identifier may contain (letters, marks, digits, '_')."""
    return ch == "_" or unicodedata.category(ch)[0] in ("L", "M", "N")


def word_at(text: str, index: int) -> tuple[str, Optional[str], int, int]:
    """The identifier at a code-point index: (word, qualifier, start, end).

    ``qualifier`` is the word before a '.' right in front of it ("math" in ``math.sqrt``,
    "s" in ``s.push``), or None. ("", None, index, index) when the position is not on a name.
    """
    if index > 0 and (index >= len(text) or not _is_name_char(text[index])) and _is_name_char(text[index - 1]):
        index -= 1  # the position just behind a word still means the word
    if index < 0 or index >= len(text) or not _is_name_char(text[index]):
        return "", None, index, index
    start = index
    while start > 0 and _is_name_char(text[start - 1]):
        start -= 1
    end = index
    while end < len(text) and _is_name_char(text[end]):
        end += 1
    word = text[start:end]
    if word and word[0].isdigit():
        return "", None, start, end  # a number, not a name
    qualifier = None
    if start > 0 and text[start - 1] == ".":
        before = start - 1
        q_end = before
        while before > 0 and _is_name_char(text[before - 1]):
            before -= 1
        if before < q_end:
            qualifier = text[before:q_end]
    return word, qualifier, start, end


def _candidate_names(text: str, start: int, end: int) -> list[str]:
    """What the name under [start, end) may be, the longest first: Pashto built-ins such as
    ``کرښه چاپ`` or ``پر هر یو`` are several space-separated words the lexer joins into one."""
    words = [text[start:end]]
    left = start
    for _ in range(3):  # the longest built-in is four words
        if left >= 2 and text[left - 1] == " " and _is_name_char(text[left - 2]):
            stop = left - 1
            begin = stop
            while begin > 0 and _is_name_char(text[begin - 1]):
                begin -= 1
            words.insert(0, text[begin:stop])
            left = begin
        else:
            break
    hovered = len(words) - 1  # the index of the hovered word within 'words'
    right = end
    for _ in range(3):
        if right + 1 < len(text) and text[right] == " " and _is_name_char(text[right + 1]):
            begin = right + 1
            stop = begin
            while stop < len(text) and _is_name_char(text[stop]):
                stop += 1
            words.append(text[begin:stop])
            right = stop
        else:
            break
    candidates = []
    for first in range(hovered + 1):
        for last in range(len(words) - 1, hovered - 1, -1):
            candidates.append((" ".join(words[first:last + 1]), last - first))
    candidates.sort(key=lambda pair: -pair[1])  # the most words first
    seen: list[str] = []
    for name, _count in candidates:
        if name not in seen:
            seen.append(name)
    return seen


def _builtin_doc(info: dict, candidates: list[str], qualifier: Optional[str]) -> Optional[HoverDoc]:
    """The API reference of a built-in: its signature and description in the program's language."""
    builtins = info.get("builtins") or []
    modules = {entry.get("name", ""): entry.get("canonical_name", "") for entry in info.get("modules", [])}
    modules.update({entry.get("canonical_name", ""): entry.get("canonical_name", "") for entry in info.get("modules", [])})
    by_member: dict[str, list[dict]] = {}
    for entry in builtins:
        by_member.setdefault(entry.get("member") or entry.get("name", ""), []).append(entry)
    # The joined forms come first: on either word of "کرښه چاپ" the two-word name wins over "چاپ".
    matches = [entry for candidate in candidates for entry in by_member.get(candidate, [])]
    for entry in matches:
        module = entry.get("module", "")
        member = entry.get("member") or entry.get("name", "")
        if qualifier is not None:
            # math.sqrt: only when the qualifier really is that module (s.push is not push).
            if not module or modules.get(qualifier, qualifier) != module:
                continue
        elif module:
            continue  # a module's member needs its module in front
        # The signature is kept under the canonical (English) name and types; show it the way
        # the program writes it ("کرښه چاپ(value, ...)", "ریاضي.جذر(x: اعشاري): اعشاري").
        display = entry.get("name") or member
        canonical = entry.get("canonical_name", display)
        signature = entry.get("signature", display)
        if display != canonical and canonical and canonical in signature:
            signature = signature.replace(canonical, display, 1)
        return HoverDoc(_localize_signature(signature, info), entry.get("description", ""), "")
    return None


def _keyword_spellings(info: dict, kind: str, fallback: str) -> list[str]:
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


# The reference keeps signatures in the canonical spellings; these are the words of them that a
# language pack respells (the type names and the `func` of a function type).
_CANONICAL_KEYWORDS = (("KwInt", "int"), ("KwFloat", "float"), ("KwBool", "bool"),
                       ("KwString", "string"), ("KwByte", "byte"), ("KwFunc", "func"))


def _localize_signature(signature: str, info: dict) -> str:
    """A canonical signature in the program's language: ``math.pow(x: float, y: float): float``
    becomes ``ریاضي.توان(x: اعشاري، y: اعشاري): اعشاري`` for a Pashto program."""
    for kind, canonical in _CANONICAL_KEYWORDS:
        spelling = _keyword_spellings(info, kind, canonical)[0]
        if spelling != canonical:
            signature = re.sub(rf"(?<!\w){canonical}(?!\w)", spelling, signature)
    comma = next((mark.get("text", ",") for mark in info.get("punctuation", []) if mark.get("means") == ","), ",")
    if comma != ",":
        signature = signature.replace(", ", f"{comma} ")
    return signature


_BANNER = re.compile(r"^\s*#\s*-{4,}")
_DIRECTIVE = re.compile(r"^\s*#\s*(?:lang|language|ژبه)\s*:", re.IGNORECASE)


def _comment_block_above(lines: list[str], at: int) -> str:
    """The contiguous ``#`` comment lines right above line ``at`` - the declaration's "JavaDoc"."""
    block: list[str] = []
    row = at - 1
    while row >= 0:
        stripped = lines[row].strip()
        if not stripped.startswith("#") or _BANNER.match(lines[row]) or _DIRECTIVE.match(lines[row]):
            break
        text = stripped[1:]
        block.append(text[1:] if text.startswith(" ") else text)
        row -= 1
    block.reverse()
    return "\n".join(block).strip()


def _strip_comment(line: str) -> tuple[str, str]:
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


def _declaration_doc(text: str, word: str, qualifier: Optional[str], info: dict, source: str) -> Optional[HoverDoc]:
    """The signature and doc comment of a declaration named ``word`` in ``text``, or None.

    Finds functions and methods (``func``), structs, interfaces, enums, constructors, module
    blocks' members, top-level variables and constants, and a struct's fields - wherever the
    name right after the keyword (or before ``:`` for a field) is the word itself.
    """
    func_words = _keyword_spellings(info, "KwFunc", "func")
    struct_words = _keyword_spellings(info, "KwStruct", "struct")
    interface_words = _keyword_spellings(info, "KwInterface", "interface")
    enum_words = _keyword_spellings(info, "KwEnum", "enum")
    var_words = _keyword_spellings(info, "KwVar", "var") + _keyword_spellings(info, "KwConst", "const")
    native_words = _keyword_spellings(info, "KwNative", "native")
    module_words = _keyword_spellings(info, "KwModule", "module")

    def words_pattern(words: list[str]) -> str:
        return "|".join(re.escape(spelling) for spelling in words)

    name = re.escape(word)
    func_re = re.compile(rf"^\s*(?:(?:{words_pattern(native_words)})\s+)?(?:{words_pattern(func_words)})\s+{name}\s*\(")
    type_re = re.compile(rf"^\s*(?:{words_pattern(struct_words + interface_words + enum_words)})\s+{name}\s*(?:<|:|\s|#|$)")
    var_re = re.compile(rf"^\s*(?:{words_pattern(var_words)})\s+{name}\s*(?::|=|#|$)")
    ctor_re = re.compile(rf"^\s+{name}\s*\(")        # the constructor: indented, spelled like its struct
    field_re = re.compile(rf"^\s+{name}\s*(?:\(\s*\))?\s*:\s*\S")  # a field (or a callable field)
    module_re = re.compile(rf"^\s*(?:{words_pattern(module_words)})\s+([^\s#]+)")
    any_struct_re = re.compile(rf"^(\s*)(?:{words_pattern(struct_words + interface_words)})\s+([^\s<:(#]+)")

    lines = text.split("\n")
    fallback: Optional[tuple[int, str]] = None
    current_module = ""
    current_struct = ""   # the struct or interface whose body this line is in ("" outside one)
    struct_indent = 0
    for row, line in enumerate(lines):
        opened = module_re.match(line)
        if opened:
            current_module = opened.group(1)
            continue
        if current_module and line.strip() and not line.startswith((" ", "\t")):
            current_module = ""  # back at the top level
        code, trailing = _strip_comment(line)
        if not code.strip():
            continue
        indent = len(code) - len(code.lstrip(" \t"))
        if current_struct and code.strip() and indent <= struct_indent:
            current_struct = ""  # the struct's body has ended
        in_struct = any_struct_re.match(code)
        matched = func_re.match(code) or type_re.match(code) or var_re.match(code)
        if not matched and qualifier is not None and current_struct:
            # A field, or the constructor - members of the struct whose body this is.
            matched = field_re.match(code)
        if not matched and current_struct == word and ctor_re.match(code):
            matched = ctor_re.match(code)
        if in_struct:
            current_struct = in_struct.group(2)
            struct_indent = len(in_struct.group(1))
        if not matched:
            continue
        # A qualified use prefers the declaration inside that module (geometry.area).
        if qualifier is not None and current_module == qualifier:
            fallback = None
            return _declaration_hover(lines, row, code, trailing, source)
        if fallback is None:
            fallback = (row, code)
    if fallback is None:
        return None
    row, code = fallback
    _, trailing = _strip_comment(lines[row])
    return _declaration_hover(lines, row, code, trailing, source)


def _declaration_hover(lines: list[str], row: int, code: str, trailing: str, source: str) -> HoverDoc:
    body = _comment_block_above(lines, row) or trailing
    return HoverDoc(code.strip(), body, source)


_IMPORT = re.compile(r'^\s*\S+\s+["«“”„]([^"»“”„]+)["»“”„]')


def import_targets(text: str, info: dict) -> list[str]:
    """The files the program imports (``import "datastructures"``), in order, without repeats."""
    import_words = _keyword_spellings(info, "KwImport", "import")
    native_words = _keyword_spellings(info, "KwNative", "native")
    targets: list[str] = []
    for line in text.split("\n"):
        stripped = line.strip()
        if not any(stripped.startswith(word + " ") or stripped.startswith(word + '"') for word in import_words):
            continue
        if any(f" {word} " in f" {stripped} " or f" {word}\"" in stripped for word in native_words):
            continue  # import native "...": a C++ library, no Thunder source to read
        matched = _IMPORT.match(line)
        if matched and matched.group(1) not in targets:
            targets.append(matched.group(1))
    return targets


def describe(text: str, index: int, info: dict,
             load_import: Optional[ImportLoader] = None) -> Optional[HoverDoc]:
    """What to show for the name at ``index`` of ``text`` (None: nothing to show).

    The order mirrors how the compiler reads a name: a built-in first for a bare name (built-in
    names are reserved), then the program's own declarations, then each imported file's. A name
    after a dot tries the module's built-in (``math.sqrt``) and otherwise prefers a declared
    method or field, so ``s.push(1)`` on a stack explains the stack's push.
    """
    word, qualifier, start, end = word_at(text, index)
    if not word:
        return None
    builtin = _builtin_doc(info, _candidate_names(text, start, end), qualifier)
    if builtin is not None and qualifier is None:
        return builtin
    found = _declaration_doc(text, word, qualifier, info, "")
    if found is None and load_import is not None:
        for target in import_targets(text, info):
            loaded = load_import(target)
            if loaded is None:
                continue
            name, imported_text = loaded
            found = _declaration_doc(imported_text, word, qualifier, info, name)
            if found is not None:
                break
    return found or builtin


def as_rich_text(doc: HoverDoc, source_label: str = "") -> str:
    """The tooltip as simple rich text: the signature verbatim, the doc wrapped, the source last."""
    parts = [f"<p style='white-space:pre'><b>{escape(doc.signature)}</b></p>"]
    for paragraph in re.split(r"\n\s*\n", doc.body):
        flowed = " ".join(line.strip() for line in paragraph.split("\n")).strip()
        if flowed:
            parts.append(f"<p dir='auto'>{escape(flowed)}</p>")
    if source_label:
        parts.append(f"<p dir='auto'><i>{escape(source_label)}</i></p>")
    return "".join(parts)
