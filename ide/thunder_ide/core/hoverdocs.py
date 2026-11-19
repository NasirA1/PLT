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
from typing import Callable, Optional, Sequence

from thunder_ide.core.declarations import Declaration, declarations, keyword_spellings, strip_comment, twin
from thunder_ide.core.editing import fold_letters


@dataclass(frozen=True)
class HoverDoc:
    signature: str  # the declaration as written, e.g. "func push(value: T)" (may be two lines)
    body: str       # the documentation paragraphs ("" when there are none)
    source: str     # the file the declaration came from ("" for the program itself and built-ins)
    # What the name is called in the other languages: (language's own name, the name), e.g.
    # ("پښتو", "کرښه چاپ") under println - shown at the bottom of the tooltip.
    translations: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True)
class OtherLanguage:
    """A language the tooltip names the hovered name in, too (its language information and a
    loader that finds the files of *its* SDK, for the twin of an imported declaration)."""
    info: dict
    load_import: Optional["ImportLoader"] = None


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


def _qualifier_before(text: str, start: int) -> Optional[str]:
    """The word before a '.' right in front of ``start`` (``math`` in ``math.sqrt``), or None."""
    if start > 0 and text[start - 1] == ".":
        before = start - 1
        while before > 0 and _is_name_char(text[before - 1]):
            before -= 1
        if before < start - 1:
            return text[before:start - 1]
    return None


def _candidate_names(text: str, start: int, end: int) -> list[tuple[str, Optional[str]]]:
    """What the name under [start, end) may be, the longest first: Pashto built-ins such as
    ``کرښه چاپ`` or ``پر هر یو`` are several space-separated words the lexer joins into one.

    Each candidate comes with its own qualifier: on ``کړه`` of ``متن.پاک کړه`` the two-word
    candidate ``پاک کړه`` belongs to ``متن``, while ``کړه`` alone has none."""
    words = [text[start:end]]
    starts = [start]
    left = start
    for _ in range(3):  # the longest built-in is four words
        if left >= 2 and text[left - 1] == " " and _is_name_char(text[left - 2]):
            stop = left - 1
            begin = stop
            while begin > 0 and _is_name_char(text[begin - 1]):
                begin -= 1
            words.insert(0, text[begin:stop])
            starts.insert(0, begin)
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
            starts.append(begin)
            right = stop
        else:
            break
    candidates = []
    for first in range(hovered + 1):
        for last in range(len(words) - 1, hovered - 1, -1):
            candidates.append((" ".join(words[first:last + 1]), _qualifier_before(text, starts[first]), last - first))
    candidates.sort(key=lambda entry: -entry[2])  # the most words first
    seen: list[tuple[str, Optional[str]]] = []
    for name, qualifier, _count in candidates:
        if (name, qualifier) not in seen:
            seen.append((name, qualifier))
    return seen


def _builtin_entry(info: dict, candidates: list[tuple[str, Optional[str]]]) -> Optional[dict]:
    """The built-in the name under the pointer is, or None (letters folded, as the compiler does)."""
    builtins = info.get("builtins") or []
    modules: dict[str, str] = {}
    for entry in info.get("modules", []):
        modules[fold_letters(entry.get("name", ""))] = entry.get("canonical_name", "")
        modules[entry.get("canonical_name", "")] = entry.get("canonical_name", "")
    by_member: dict[str, list[dict]] = {}
    for entry in builtins:
        by_member.setdefault(fold_letters(entry.get("member") or entry.get("name", "")), []).append(entry)
    # The joined forms come first: on either word of "کرښه چاپ" the two-word name wins over "چاپ".
    matches = [(entry, qualifier) for candidate, qualifier in candidates
               for entry in by_member.get(fold_letters(candidate), [])]
    for entry, qualifier in matches:
        module = entry.get("module", "")
        if qualifier is not None:
            # math.sqrt: only when the qualifier really is that module (s.push is not push).
            folded = fold_letters(qualifier)
            if not module or modules.get(folded, folded) != module:
                continue
        elif module:
            continue  # a module's member needs its module in front
        return entry
    return None


def builtin_signature(entry: dict, info: dict) -> str:
    """A built-in's signature the way the program writes it.

    The compiler reports it in the program's language (a Pashto one is Pashto through and
    through: "ریاضي.جذر(س: اعشاري): اعشاري"); an older toolchain's English signature is put into
    the language here - its name, its types and its commas."""
    display = entry.get("name") or entry.get("member", "")
    canonical = entry.get("canonical_name", display)
    signature = entry.get("signature", display)
    if display != canonical and canonical and canonical in signature:
        signature = signature.replace(canonical, display, 1)
    return localize_signature(signature, info)


def _builtin_doc(info: dict, candidates: list[tuple[str, Optional[str]]],
                 others: Sequence[OtherLanguage] = ()) -> Optional[HoverDoc]:
    """The API reference of a built-in: its signature and description in the program's language."""
    entry = _builtin_entry(info, candidates)
    if entry is None:
        return None
    translations = []
    for other in others:
        twin_entry = next((candidate for candidate in other.info.get("builtins") or []
                           if candidate.get("canonical_name") == entry.get("canonical_name")), None)
        if twin_entry is not None and twin_entry.get("name") and twin_entry.get("name") != entry.get("name"):
            translations.append((_language_name(other.info), twin_entry["name"]))
    return HoverDoc(builtin_signature(entry, info), entry.get("description", ""), "", tuple(translations))


def _language_name(info: dict) -> str:
    return info.get("native_name") or info.get("name") or info.get("code", "")


def _keyword_spellings(info: dict, kind: str, fallback: str) -> list[str]:
    return keyword_spellings(info, kind, fallback)


# The reference keeps signatures in the canonical spellings; these are the words of them that a
# language pack respells (the type names and the `func` of a function type).
CANONICAL_KEYWORDS = (("KwInt", "int"), ("KwFloat", "float"), ("KwBool", "bool"),
                      ("KwString", "string"), ("KwByte", "byte"), ("KwFunc", "func"))


def localize_signature(signature: str, info: dict) -> str:
    """A canonical signature in the program's language: ``math.pow(x: float, y: float): float``
    becomes ``ریاضي.توان(x: اعشاري، y: اعشاري): اعشاري`` for a Pashto program, and the enum
    types of the library are named the same way (``os.platform(): Platform`` -> ``پلیټفارم``)."""
    words: dict[str, str] = {}
    for kind, canonical in CANONICAL_KEYWORDS:
        spelling = _keyword_spellings(info, kind, canonical)[0]
        if spelling != canonical:
            words[canonical] = spelling
    for enum in info.get("enums") or []:
        if enum.get("canonical_name") and enum.get("name") and enum["name"] != enum["canonical_name"]:
            words[enum["canonical_name"]] = enum["name"]
    if words:
        pattern = "|".join(re.escape(word) for word in sorted(words, key=len, reverse=True))
        # One pass, so a replacement is never replaced again.
        signature = re.sub(rf"(?<!\w)(?:{pattern})(?!\w)", lambda found: words[found.group(0)], signature)
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
    return strip_comment(line)


def _find_declaration(found: list[Declaration], word: str, qualifier: Optional[str]) -> Optional[Declaration]:
    """The declaration a name under the pointer means, among a file's declarations.

    Functions and methods, structs, interfaces, enums, variables and constants are found
    wherever they are; a struct's fields only for a name after a dot (``b._title``) and a
    constructor only under its struct's name. A qualified use prefers the declaration inside
    that module (``geometry.area``); otherwise the first one written wins.
    """
    wanted = fold_letters(word)
    fallback: Optional[Declaration] = None
    for declaration in found:
        if fold_letters(declaration.name) != wanted or declaration.kind in ("member", "module"):
            continue
        if declaration.kind == "field" and qualifier is None:
            continue
        if qualifier is not None and declaration.module and fold_letters(declaration.module) == fold_letters(qualifier):
            return declaration
        if fallback is None:
            fallback = declaration
    return fallback


def _declaration_doc(text: str, word: str, qualifier: Optional[str], info: dict, source: str) -> Optional[HoverDoc]:
    """The signature and doc comment of a declaration named ``word`` in ``text``, or None."""
    declaration = _find_declaration(declarations(text, info), word, qualifier)
    if declaration is None:
        return None
    return _declaration_hover(text.split("\n"), declaration, source)


def _declaration_hover(lines: list[str], declaration: Declaration, source: str) -> HoverDoc:
    body = _comment_block_above(lines, declaration.row) or declaration.trailing
    return HoverDoc(declaration.code.strip(), body, source)


def _qualified_name(declaration: Declaration) -> str:
    """How a declaration is reached: ``Stack.push``, ``geometry.area``, ``hello``."""
    if declaration.owner and declaration.kind != "ctor":
        return f"{declaration.owner}.{declaration.name}"
    if declaration.module:
        return f"{declaration.module}.{declaration.name}"
    return declaration.name


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
             load_import: Optional[ImportLoader] = None,
             others: Sequence[OtherLanguage] = ()) -> Optional[HoverDoc]:
    """What to show for the name at ``index`` of ``text`` (None: nothing to show).

    The order mirrors how the compiler reads a name: a built-in first for a bare name (built-in
    names are reserved), then the program's own declarations, then each imported file's. A name
    after a dot tries the module's built-in (``math.sqrt``) and otherwise prefers a declared
    method or field, so ``s.push(1)`` on a stack explains the stack's push.

    ``others`` are the languages to name it in as well: a built-in has a name in each, and so has
    a declaration of the SDK, which is written once per language, declaration for declaration.
    A name the program declares itself is the same in every language and gets no translation.
    """
    word, qualifier, start, end = word_at(text, index)
    if not word:
        return None
    builtin = _builtin_doc(info, _candidate_names(text, start, end), others)
    if builtin is not None and qualifier is None:
        return builtin
    found = _declaration_doc(text, word, qualifier, info, "")
    if found is None and load_import is not None:
        for target in import_targets(text, info):
            loaded = load_import(target)
            if loaded is None:
                continue
            name, imported_text = loaded
            imported = declarations(imported_text, info)
            declaration = _find_declaration(imported, word, qualifier)
            if declaration is None:
                continue
            found = _declaration_hover(imported_text.split("\n"), declaration, name)
            translations = _twin_names(imported, declaration, imported_text, target, others)
            if translations:
                found = HoverDoc(found.signature, found.body, found.source, translations)
            break
    return found or builtin


def _twin_names(imported: list[Declaration], declaration: Declaration, imported_text: str, target: str,
                others: Sequence[OtherLanguage]) -> tuple[tuple[str, str], ...]:
    """The names of an imported declaration's twins in the other languages' copies of the file."""
    result = []
    for other in others:
        if other.load_import is None:
            continue
        loaded = other.load_import(target)
        if loaded is None or loaded[1] == imported_text:
            continue  # the same file (the program's own module): it has no translation
        match = twin(imported, declaration, declarations(loaded[1], other.info))
        if match is not None and match.name != declaration.name:
            result.append((_language_name(other.info), _qualified_name(match)))
    return tuple(result)


def as_rich_text(doc: HoverDoc, source_label: str = "") -> str:
    """The tooltip as simple rich text: the signature verbatim, the doc wrapped, the source, and
    last what the name is called in the other languages ("پښتو: کرښه چاپ")."""
    parts = [f"<p style='white-space:pre'><b>{escape(doc.signature)}</b></p>"]
    for paragraph in re.split(r"\n\s*\n", doc.body):
        flowed = " ".join(line.strip() for line in paragraph.split("\n")).strip()
        if flowed:
            parts.append(f"<p dir='auto'>{escape(flowed)}</p>")
    if source_label:
        parts.append(f"<p dir='auto'><i>{escape(source_label)}</i></p>")
    if doc.translations:
        parts.append("<hr>")
        for language, name in doc.translations:
            # Each part in its own direction: "English: println" under a Pashto tooltip too.
            parts.append(f"<p dir='auto'>{escape(language)}: <b dir='auto'>{escape(name)}</b></p>")
    return "".join(parts)
