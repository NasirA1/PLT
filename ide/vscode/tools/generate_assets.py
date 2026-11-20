#!/usr/bin/env python3
"""Generates the VS Code extension's static assets from the compiler itself.

The reserved words of Thunder live in the language packs inside the compiler, not in the
editor, so everything the extension ships that mentions them is generated from
``thunderc --language-info``:

  syntaxes/thunder.tmLanguage.json    the TextMate grammar (English and Pashto words)
  snippets/thunder.code-snippets      the snippets, in every language
  data/language-info.<code>.json      a copy of the language info, used until the toolchain
                                      has been built (the extension prefers the live one)

Run it after changing a language pack:

    python3 ide/vscode/tools/generate_assets.py            # writes the files
    python3 ide/vscode/tools/generate_assets.py --check    # fails if they are out of date

``tests/python/test_vscode_extension.py`` runs ``--check``, so the committed assets can
never drift away from the compiler.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXTENSION = HERE.parent
ROOT = EXTENSION.parents[1]
sys.path.insert(0, str(ROOT / "ide"))

from thunder_ide.core.toolchain import EXE_SUFFIX, find_bin_dir  # noqa: E402

# Characters that may appear inside a name (identifier) in any language Thunder speaks.
WORD = r"[[:word:]\p{L}\p{M}\p{Nd}_]"
NOT_WORD_BEFORE = rf"(?<!{WORD})"
NOT_WORD_AFTER = rf"(?!{WORD})"


# ---------------------------------------------------------------------- helpers

def language_info(thunderc: Path, code: str) -> dict:
    """``thunderc --language-info`` for one language."""
    completed = subprocess.run([str(thunderc), "--language-info", "--lang", code],
                               capture_output=True, text=True, encoding="utf-8", check=True)
    return json.loads(completed.stdout)


def escape(word: str) -> str:
    """A reserved word as a regular expression: spaces inside a compound name match any run."""
    parts = [part for part in word.split(" ") if part]
    return r"\s+".join(re.escape(part) for part in parts)


def alternation(words: list[str]) -> str:
    """An alternation that prefers the longest match (``کرښه چاپ`` before ``چاپ``)."""
    unique = sorted(set(words), key=lambda w: (-len(w), w))
    return "|".join(escape(word) for word in unique)


def words_by_category(infos: list[dict]) -> dict[str, list[str]]:
    """Every spelling of every reserved word, grouped by what it is."""
    groups: dict[str, list[str]] = {"keyword": [], "operator": [], "type": [], "literal": [],
                                    "builtin": [], "module_builtin": []}
    for info in infos:
        for keyword in info.get("keywords", []):
            category = keyword.get("category", "keyword")
            target = category if category in groups else "keyword"
            groups[target].extend(keyword.get("spellings") or [keyword["text"]])
        for builtin in info.get("builtins", []):
            groups["module_builtin" if builtin.get("module") else "builtin"].append(builtin["name"])
    return groups


# ---------------------------------------------------------------------- the grammar

def grammar(infos: list[dict]) -> dict:
    groups = words_by_category(infos)
    digits = "".join(sorted({digit for info in infos for digit in info.get("digits", [])}))
    digit_class = f"[{digits}]" if digits else "[0-9]"
    # Keywords that open a block, so "func name" and "struct Name" can name what follows.
    func_words = [spelling for info in infos for keyword in info["keywords"]
                  if keyword["kind"] == "KwFunc" for spelling in (keyword.get("spellings") or [keyword["text"]])]
    struct_words = [spelling for info in infos for keyword in info["keywords"]
                    if keyword["kind"] in ("KwStruct", "KwMixin") for spelling in (keyword.get("spellings") or [keyword["text"]])]
    declare_words = [spelling for info in infos for keyword in info["keywords"]
                     if keyword["kind"] in ("KwVar", "KwConst")
                     for spelling in (keyword.get("spellings") or [keyword["text"]])]
    import_words = [spelling for info in infos for keyword in info["keywords"]
                    if keyword["kind"] == "KwImport" for spelling in (keyword.get("spellings") or [keyword["text"]])]
    native_words = [spelling for info in infos for keyword in info["keywords"]
                    if keyword["kind"] == "KwNative" for spelling in (keyword.get("spellings") or [keyword["text"]])]
    type_words = alternation(groups["type"])
    annotation_words = [spelling for info in infos for entry in info.get("annotations", [])
                        for spelling in entry.get("spellings", []) + [entry.get("text", "")] if spelling]

    return {
        "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
        "name": "Thunder",
        "scopeName": "source.thunder",
        "information_for_contributors": [
            "Generated by ide/vscode/tools/generate_assets.py from thunderc --language-info.",
            "Do not edit by hand: edit the language pack (src/core/language_*.cpp) and run the generator.",
        ],
        "patterns": [
            {"include": "#language-directive"},
            {"include": "#comment"},
            {"include": "#string"},
            {"include": "#number"},
            {"include": "#annotation"},
            {"include": "#declaration"},
            {"include": "#import"},
            {"include": "#type-annotation"},
            {"include": "#keyword"},
            {"include": "#call"},
            {"include": "#operator"},
            {"include": "#punctuation"},
        ],
        "repository": {
            # "# lang: ps" on the first line chooses the language of the program.
            "language-directive": {
                "match": r"\A\s*(#)\s*(lang|ژبه)\s*(:)\s*(\w+)\s*$",
                "captures": {
                    "1": {"name": "punctuation.definition.comment.thunder"},
                    "2": {"name": "keyword.control.directive.thunder"},
                    "3": {"name": "punctuation.separator.key-value.thunder"},
                    "4": {"name": "constant.language.thunder"},
                },
            },
            "comment": {
                "patterns": [
                    {"name": "comment.line.number-sign.thunder",
                     "begin": "#", "end": "$",
                     "beginCaptures": {"0": {"name": "punctuation.definition.comment.thunder"}}},
                    {"name": "comment.block.thunder",
                     "begin": "'''", "end": "'''",
                     "beginCaptures": {"0": {"name": "punctuation.definition.comment.begin.thunder"}},
                     "endCaptures": {"0": {"name": "punctuation.definition.comment.end.thunder"}}},
                ],
            },
            # Thunder strings never span lines; the quotes may be ", « », “ ” or „ “.
            "string": {
                "patterns": [
                    {"name": "string.quoted.double.thunder",
                     "begin": '"', "end": r'"|$',
                     "patterns": [{"include": "#escape"}]},
                    {"name": "string.quoted.other.guillemet.thunder",
                     "begin": "«", "end": r"»|$",
                     "patterns": [{"include": "#escape"}]},
                    {"name": "string.quoted.other.curly.thunder",
                     "begin": "[“„]", "end": r"[”“]|$",
                     "patterns": [{"include": "#escape"}]},
                ],
            },
            "escape": {"name": "constant.character.escape.thunder", "match": r"\\."},
            "number": {
                "patterns": [
                    {"name": "constant.numeric.hex.thunder",
                     "match": rf"{NOT_WORD_BEFORE}0[xX](?:[0-9a-fA-F]|{digit_class})+{NOT_WORD_AFTER}"},
                    {"name": "constant.numeric.binary.thunder",
                     "match": rf"{NOT_WORD_BEFORE}0[bB](?:[01]|{digit_class})+{NOT_WORD_AFTER}"},
                    {"name": "constant.numeric.decimal.thunder",
                     "match": rf"{NOT_WORD_BEFORE}(?:[0-9]|{digit_class})+(?:[.٫](?:[0-9]|{digit_class})+)?{NOT_WORD_AFTER}"},
                ],
            },
            # "@test", "@ازموینه(...)": an annotation, coloured like a decorator.
            # A spelling of several words (Pashto's "@پېښه ګر") is matched whole, longest first.
            "annotation": {"name": "entity.name.function.decorator.thunder",
                           "match": rf"@(?:{alternation(annotation_words)}){NOT_WORD_AFTER}|@{WORD}+"},
            # "func name(...)" and "struct Name" name what they declare.
            "declaration": {
                "patterns": [
                    {"match": rf"{NOT_WORD_BEFORE}({alternation(func_words)})\s+({WORD}+)",
                     "captures": {"1": {"name": "storage.type.function.thunder"},
                                  "2": {"name": "entity.name.function.thunder"}}},
                    {"match": rf"{NOT_WORD_BEFORE}({alternation(struct_words)})\s+({WORD}+)",
                     "captures": {"1": {"name": "storage.type.struct.thunder"},
                                  "2": {"name": "entity.name.type.struct.thunder"}}},
                    {"match": rf"{NOT_WORD_BEFORE}({alternation(declare_words)})\s+({WORD}+)",
                     "captures": {"1": {"name": "storage.type.thunder"},
                                  "2": {"name": "variable.other.declaration.thunder"}}},
                ],
            },
            "import": {
                "patterns": [
                    {"match": rf"{NOT_WORD_BEFORE}({alternation(import_words)})\s+({alternation(native_words)})?",
                     "captures": {"1": {"name": "keyword.control.import.thunder"},
                                  "2": {"name": "keyword.control.import.thunder"}}},
                ],
            },
            # ": int" / "): string[]" - the type of a variable, field, parameter or result.
            "type-annotation": {
                "match": rf"(:)\s*({type_words}|[[:alpha:]\p{{L}}]{WORD}*)",
                "captures": {"1": {"name": "punctuation.separator.type.thunder"},
                             "2": {"name": "storage.type.thunder"}},
            },
            "keyword": {
                "patterns": [
                    {"name": "keyword.operator.word.thunder",
                     "match": rf"{NOT_WORD_BEFORE}(?:{alternation(groups['operator'])}){NOT_WORD_AFTER}"},
                    {"name": "constant.language.thunder",
                     "match": rf"{NOT_WORD_BEFORE}(?:{alternation(groups['literal'])}){NOT_WORD_AFTER}"},
                    {"name": "storage.type.thunder",
                     "match": rf"{NOT_WORD_BEFORE}(?:{type_words}){NOT_WORD_AFTER}"},
                    {"name": "keyword.control.thunder",
                     "match": rf"{NOT_WORD_BEFORE}(?:{alternation(groups['keyword'])}){NOT_WORD_AFTER}"},
                    {"name": "support.function.builtin.thunder",
                     "match": rf"{NOT_WORD_BEFORE}(?:{alternation(groups['builtin'])}){NOT_WORD_AFTER}"},
                    {"name": "support.function.module.thunder",
                     "match": rf"{NOT_WORD_BEFORE}(?:{alternation(groups['module_builtin'])}){NOT_WORD_AFTER}"},
                ],
            },
            "call": {
                "match": rf"({WORD}+)\s*(?=\()",
                "captures": {"1": {"name": "entity.name.function.call.thunder"}},
            },
            "operator": {
                "patterns": [
                    # "->" is one token of the language; it must come before the arithmetic
                    # '-' so that the arrow is matched whole rather than as a minus.
                    {"name": "keyword.operator.arrow.thunder", "match": r"->"},
                    {"name": "keyword.operator.assignment.thunder", "match": r"[+\-*/%]?=|=="},
                    {"name": "keyword.operator.comparison.thunder", "match": r"[<>]=?|!="},
                    {"name": "keyword.operator.bitwise.thunder", "match": r"<<|>>|[&|^~]"},
                    {"name": "keyword.operator.arithmetic.thunder", "match": r"[-+*/%×÷٪]"},
                ],
            },
            "punctuation": {
                "patterns": [
                    {"name": "punctuation.separator.comma.thunder", "match": r"[,،]"},
                    {"name": "punctuation.accessor.thunder", "match": r"\."},
                    {"name": "meta.brace.round.thunder", "match": r"[()]"},
                    {"name": "meta.brace.square.thunder", "match": r"[\[\]]"},
                    {"name": "meta.brace.curly.thunder", "match": r"[{}]"},
                ],
            },
        },
    }


# ---------------------------------------------------------------------- the snippets

# Each snippet is written once, with the pieces that have a translated name taken from the
# language pack by the token kind (keywords) or canonical name (built-ins).
SNIPPETS: list[dict] = [
    {"key": "func", "en": "Function", "ps": "دنده",
     "body": ["@KwFunc ${1:name}(${2:n}: @KwInt): @KwInt", "\t@KwReturn ${0:$2}"]},
    {"key": "struct", "en": "Structure", "ps": "جوړښت",
     "body": ["@KwStruct ${1:Name}", "\t${2:field}: @KwInt", "\t$0"]},
    {"key": "class", "en": "Structure with a constructor and a method", "ps": "جوړښت د جوړوونکي او دندې سره",
     "prefix_ps": "جوړښت",
     "body": ["@KwStruct ${1:Name}", "\t${2:field}: @KwInt", "", "\t${1:Name}(${3:value}: @KwInt)",
              "\t\t${2:field} = ${3:value}", "", "\t@KwFunc ${4:method}(): @KwInt",
              "\t\t@KwReturn ${2:field}$0"]},
    {"key": "interface", "en": "Interface", "ps": "برسېر",
     "body": ["@KwInterface ${1:Name}", "\t@KwFunc ${2:method}()", "\t$0"]},
    {"key": "mixin", "en": "Mixin", "ps": "ګډون",
     "body": ["@KwMixin ${1:Name}", "\t@KwFunc ${2:method}()", "\t\t${0:@KwPass}"]},
    {"key": "test", "en": "Test", "ps": "ازموینه",
     "body": ["@ANNOTATION_test", "@KwFunc ${1:does_something}()", "\t${0:@KwPass}"]},
    {"key": "testsuite", "en": "Test suite (a struct)", "ps": "د ازموینو ډله",
     "body": ["@ANNOTATION_test", "@KwStruct ${1:Tests}", "\t${1:Tests}()", "\t\t${2:@KwPass}", "",
              "\t@KwFunc ${3:does_something}()", "\t\t${0:@KwPass}"]},
    {"key": "mock", "en": "Mock of an interface", "ps": "پېښه ګر",
     "body": ["@ANNOTATION_mock", "@KwStruct ${1:Fake}: ${2:Interface}"]},
    {"key": "if", "en": "If", "ps": "که",
     "body": ["@KwIf ${1:condition}", "\t${0:@println(\"yes\")}"]},
    {"key": "ifelse", "en": "If / else", "ps": "که / کنه",
     "body": ["@KwIf ${1:condition}", "\t${2:@KwPass}", "@KwElse", "\t${0:@KwPass}"]},
    {"key": "while", "en": "While loop", "ps": "ترڅو",
     "body": ["@KwWhile ${1:condition}", "\t${0:@KwPass}"]},
    {"key": "for", "en": "Counting for loop", "ps": "تکرار",
     "body": ["@KwFor @KwVar ${1:i} = 0@COMMA ${1:i} < ${2:10}@COMMA ${1:i} = ${1:i} + 1", "\t${0:@println($1)}"]},
    {"key": "forin", "en": "For each", "ps": "تکرار ... په",
     "body": ["@KwFor ${1:item} @KwIn ${2:items}", "\t${0:@println($1)}"]},
    {"key": "var", "en": "Variable", "ps": "متغیر",
     "body": ["@KwVar ${1:name} = ${0:0}"]},
    {"key": "const", "en": "Constant", "ps": "ثابت",
     "body": ["@KwConst ${1:NAME} = ${0:0}"]},
    {"key": "import", "en": "Import a module", "ps": "راوړه",
     "body": ["@KwImport \"${1:module}\""]},
    {"key": "importnative", "en": "Import a C++ library", "ps": "بهرنی راوړه",
     "body": ["@KwImport @KwNative \"${1:library}\"",
              "@KwNative @KwFunc ${2:name}(${3:n}: @KwInt): @KwInt"]},
    {"key": "main", "en": "main function", "ps": "اصلي دنده",
     "body": ["@KwFunc @main()", "\t${0:@println(\"Hello, Thunder\")}", "", "@main()"]},
    {"key": "graphics", "en": "Graphics program", "ps": "ګرافیک پروګرام",
     "body": ["@KwImport @graphics_module", "", "@KwWhile @graphics.frame()",
              "\t@graphics.clear(\"black\")", "\t${0:@graphics.circle(50@COMMA 50@COMMA 10@COMMA \"white\")}"]},
    {"key": "module", "en": "Module", "ps": "ټولګی",
     "body": ["@KwModule ${1:name}", "\t@KwFunc ${2:member}(${3:n}: @KwInt): @KwInt", "\t\t@KwReturn ${0:$3}"]},
    {"key": "task", "en": "Task started from a closure", "ps": "کار", "prefix_ps": "کار",
     "body": ["@KwVar ${1:work} = @KwFunc()", "\t${0:@println(\"in a task\")}",
              "@KwVar ${2:worker} = @task.spawn($1)", "@task.join($2)"]},
    # "with" stands first in English and last in Pashto ("ساکټ سره"): body_after is the
    # body for a language that writes it after the resource.
    {"key": "with", "en": "Resource closed at the end of the block", "ps": "سرچینه ... سره", "prefix_ps": "سره",
     "body": ["@KwWith ${1:name} = ${2:resource}", "\t${0:@KwPass}"],
     "body_after": ["${1:name} @KwWith = ${2:resource}", "\t${0:@KwPass}"]},
    # A critical section: the lock is given back at the end of the block, however it ends.
    {"key": "lock", "en": "Critical section held by a lock", "ps": "حساسه برخه", "prefix_ps": "قلف",
     "body": ["@KwWith ${1:m}.lock()", "\t${0:@KwPass}"],
     "body_after": ["${1:m}.قلف_کړه() @KwWith", "\t${0:@KwPass}"]},
]

# The built-ins the snippets mention, by canonical name.
SNIPPET_BUILTINS = ["println", "pass", "graphics.frame", "graphics.clear", "graphics.circle",
                    "task.spawn", "task.join"]

# The words a snippet leaves for the programmer to replace. Pashto programs use Pashto names
# throughout (single letters follow the Arabic-script maths convention: ن for n, س for x).
PLACEHOLDERS = {
    "ps": {"name": "نوم", "Name": "نوم", "field": "برخه", "condition": "شرط", "item": "توکی",
           "items": "توکي", "module": "ماډیول", "library": "کتابتون", "i": "ن", "n": "ن",
           "NAME": "نوم", "member": "غړی", "resource": "سرچینه", "method": "دنده_نوم", "value": "ارزښت",
           "does_something": "یو_کار_کوي", "work": "چار", "worker": "کارګر", "m": "ق", "Tests": "ازموینې", "Fake": "پېښه_ګر_نوم", "Interface": "برسېر_نوم"},
}


def translate_placeholders(line: str, code: str) -> str:
    """Translates the words inside ``${1:word}`` into the program's language."""
    words = PLACEHOLDERS.get(code)
    if not words:
        return line
    return re.sub(r"\$\{(\d+):([A-Za-z_]+)\}",
                  lambda match: "${%s:%s}" % (match.group(1), words.get(match.group(2), match.group(2))),
                  line)


def snippet_body(body: list[str], info: dict) -> list[str]:
    """Replaces the @-placeholders with this language's words."""
    keywords = {keyword["kind"]: keyword["text"] for keyword in info["keywords"]}
    builtins = {builtin["canonical_name"]: builtin["name"] for builtin in info["builtins"]}
    modules = {module["canonical_name"]: module["name"] for module in info.get("modules", [])}
    comma = next((mark["text"] for mark in info.get("punctuation", []) if mark.get("means") == ","), ",")
    annotations = {entry["canonical_name"]: entry["text"] for entry in info.get("annotations", [])}
    out = []
    for line in body:
        for canonical in ("test", "mock"):
            line = line.replace("@ANNOTATION_" + canonical, "@" + annotations.get(canonical, canonical))
        # Longest kind name first: @KwInt must not be read as @KwIn followed by "t".
        for kind in sorted(keywords, key=len, reverse=True):
            line = line.replace("@" + kind, keywords[kind])
        line = line.replace("@COMMA", comma)
        line = line.replace("@graphics_module", modules.get("graphics", "graphics"))
        line = line.replace("@main", "main" if info["code"] == "en" else "اصلي")
        for canonical in SNIPPET_BUILTINS:
            line = line.replace("@" + canonical, builtins.get(canonical, canonical))
        out.append(translate_placeholders(line, info["code"]))
    return out


def snippets(infos: list[dict]) -> dict:
    result: dict[str, dict] = {}
    for info in infos:
        keywords = {keyword["kind"]: keyword["text"] for keyword in info["keywords"]}
        for snippet in SNIPPETS:
            name = snippet.get(info["code"], snippet["en"])
            title = f"{name} ({info['native_name']})" if info["code"] != "en" else name
            # The prefix is the snippet's first word in that language, so typing "که" offers it.
            after = info.get("with_after_resource") and "body_after" in snippet
            body = snippet_body(snippet["body_after"] if after else snippet["body"], info)
            first_word = body[0].lstrip().split(" ")[0].split("(")[0]
            prefix = snippet["key"] if info["code"] == "en" else first_word
            if info["code"] != "en" and keywords:
                prefix = snippet.get("prefix_" + info["code"], first_word)
            result[f"{snippet['key']}.{info['code']}"] = {
                "scope": "thunder",
                "prefix": prefix,
                "body": body,
                "description": title,
            }
    return result


# ---------------------------------------------------------------------- writing

def write(path: Path, data: dict, check: bool) -> bool:
    """Writes ``data`` as JSON; with ``check`` only reports whether it is up to date."""
    text = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    if check:
        current = path.read_text(encoding="utf-8") if path.is_file() else ""
        if current != text:
            print(f"out of date: {path.relative_to(ROOT)}")
            return False
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {path.relative_to(ROOT)}")
    return True


# ---------------------------------------------------------------------- the spelling dictionary

# Words a spell checker would not know but that every Thunder program contains: the toolchain's
# own names beside the language's words. (The Pashto spellings are in Arabic script, which spell
# checkers leave alone, so only the names a Latin-script checker would underline are listed.)
TOOLCHAIN_WORDS = ["thunder", "thunderc", "thundervm", "thc", "thunderlang", "lang", "ps", "utf"]

WORD_ONLY = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def spelling_words(infos: list[dict]) -> list[str]:
    """Every name a Thunder program uses that an English dictionary does not have."""
    words: set[str] = set(TOOLCHAIN_WORDS)
    for info in infos:
        for keyword in info["keywords"]:
            words.update(spelling for spelling in (keyword.get("spellings") or [keyword["text"]])
                         if WORD_ONLY.match(spelling))
        for builtin in info["builtins"]:
            for name in (builtin["name"], builtin.get("canonical_name", "")):
                # A member of a module is written "math.sqrt": both halves are words of their own.
                words.update(part for part in name.split(".") if WORD_ONLY.match(part))
        for module in info.get("modules", []):
            words.update(part for part in (module["name"], module.get("canonical_name", "")) if WORD_ONLY.match(part))
        for enum in info.get("enums", []):
            words.update(part for part in enum.get("name", "").split(".") if WORD_ONLY.match(part))
            for member in enum.get("members", []):
                name = member if isinstance(member, str) else member.get("name", "")
                if WORD_ONLY.match(name):
                    words.add(name)
    # A spell checker splits names on '_' and on case changes, so only the pieces matter.
    pieces: set[str] = set()
    for word in words:
        for piece in re.split(r"[_\d]+", word):
            if len(piece) > 1:
                pieces.add(piece.lower())
    return sorted(pieces)


def spelling_settings(words: list[str]) -> dict:
    """What the extension contributes so a spell checker leaves Thunder programs alone."""
    return {
        "languageId": "thunder",
        "words": words,
        "allowCompoundWords": True,
    }


def package_json_with_spelling(words: list[str]) -> dict:
    """package.json with the spell-checker defaults refreshed from the compiler."""
    data = json.loads((EXTENSION / "package.json").read_text(encoding="utf-8"))
    defaults = data["contributes"].setdefault("configurationDefaults", {})
    defaults["cSpell.languageSettings"] = [spelling_settings(words)]
    return data


# ---------------------------------------------------------------------- the language configuration

# The IDE's own rules, from thunder_ide.core.editing: these words open a block, and after
# these the next line goes back out again.
BLOCK_OPENING_KINDS = ("KwIf", "KwElse", "KwWhile", "KwFor", "KwFunc", "KwStruct", "KwMixin", "KwInterface", "KwEnum", "KwModule", "KwWith")
BLOCK_LEAVING_KINDS = ("KwReturn", "KwBreak", "KwContinue", "KwPass")


def spellings_of(infos: list[dict], kinds: tuple[str, ...]) -> list[str]:
    return [spelling for info in infos for keyword in info["keywords"] if keyword["kind"] in kinds
            for spelling in (keyword.get("spellings") or [keyword["text"]])]


def language_configuration(infos: list[dict]) -> dict:
    """The brackets, comments and indentation rules of the language, in every language."""
    # Pashto writes "with" after the handle ("لاستی سره = ...") or at the END of its line ("ساکټ
    # سره"): such lines open a block too, and the with-word is left out of the words that open a
    # block from the start of a line.
    leading = [spelling for info in infos for keyword in info["keywords"] if keyword["kind"] in BLOCK_OPENING_KINDS
               and not (keyword["kind"] == "KwWith" and info.get("with_after_resource"))
               for spelling in (keyword.get("spellings") or [keyword["text"]])]
    opening = alternation(leading)
    trailing = alternation([spelling for info in infos if info.get("with_after_resource") for keyword in info["keywords"]
                            if keyword["kind"] == "KwWith" for spelling in (keyword.get("spellings") or [keyword["text"]])])
    leaving = alternation(spellings_of(infos, BLOCK_LEAVING_KINDS))
    # A function value written in place opens a block from the end of its header, wherever the
    # line begins: "var t = Task(func()" and "var step = func(n: int): int". The header's own
    # brackets hold no others, and nothing but the type it answers with may follow, so
    # "println(map(a, func(n: int): int n * n))" - which closes everything - is not matched.
    functions = alternation(spellings_of(infos, ("KwFunc",)))
    lambda_header = rf".*(?:{functions})\s*\([^()]*\)\s*(?::[^()#]*)?\s*(?:#.*)?"
    else_words = alternation(spellings_of(infos, ("KwElse",)))
    pairs = [["{", "}"], ["[", "]"], ["(", ")"]]
    string_pairs = [[pair[0], pair[1]] for info in infos for pair in info.get("quotes", [])]
    seen: list[list[str]] = []
    for pair in string_pairs:
        if pair not in seen and pair[0] != pair[1]:
            seen.append(pair)
    return {
        "comments": {"lineComment": "#", "blockComment": ["'''", "'''"]},
        "brackets": pairs,
        "autoClosingPairs": (
            [{"open": left, "close": right} for left, right in pairs]
            + [{"open": '"', "close": '"', "notIn": ["string", "comment"]}]
            + [{"open": left, "close": right, "notIn": ["string", "comment"]} for left, right in seen]
            + [{"open": "'''", "close": "'''", "notIn": ["string"]}]
        ),
        "surroundingPairs": pairs + [['"', '"']] + seen,
        "folding": {"offSide": True},
        # Pashto letters, their marks and both sets of digits belong to a name.
        "wordPattern": r"[\p{L}\p{M}_][\p{L}\p{M}\p{N}_]*",
        "indentationRules": {
            "increaseIndentPattern": (rf"^(?:\s*(?:{opening})(?:\s|$|\().*|{lambda_header})$" if not trailing
                                      else rf"^(?:\s*(?:{opening})(?:\s|$|\().*|.*\s(?:{trailing})\s*(?:#.*)?"
                                           rf"|\s*[\p{{L}}\p{{M}}_][\p{{L}}\p{{M}}\p{{N}}_]*\s+(?:{trailing})\s*[:=].*"
                                           rf"|{lambda_header})$"),
            "decreaseIndentPattern": rf"^\s*(?:{else_words})(?:\s|$).*$",
        },
        "onEnterRules": [
            {"beforeText": rf"^\s*(?:{leaving})(?:\s|$).*$", "action": {"indent": "outdent"}},
            {"beforeText": r"^\s*#.*$", "action": {"indent": "none", "appendText": "# "}},
        ],
        "_generated_by": "ide/vscode/tools/generate_assets.py",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if a generated file is out of date")
    parser.add_argument("--bin", help="directory containing thunderc")
    arguments = parser.parse_args(argv)

    thunderc = find_bin_dir(arguments.bin or os.environ.get("THUNDER_BIN")) / f"thunderc{EXE_SUFFIX}"
    first = language_info(thunderc, "en")
    codes = [language["code"] for language in first.get("languages", [{"code": "en"}])]
    infos = [first if code == "en" else language_info(thunderc, code) for code in codes]

    ok = write(EXTENSION / "syntaxes" / "thunder.tmLanguage.json", grammar(infos), arguments.check)
    ok &= write(EXTENSION / "language-configuration.json", language_configuration(infos), arguments.check)
    ok &= write(EXTENSION / "snippets" / "thunder.code-snippets", snippets(infos), arguments.check)
    for info in infos:
        ok &= write(EXTENSION / "data" / f"language-info.{info['code']}.json", info, arguments.check)
    # The spell checker's dictionary: shipped as data (a new project copies it into its own
    # settings) and contributed by the extension, so no Thunder program is ever underlined.
    words = spelling_words(infos)
    ok &= write(EXTENSION / "data" / "spelling-words.json", {"words": words}, arguments.check)
    ok &= write(EXTENSION / "package.json", package_json_with_spelling(words), arguments.check)
    if arguments.check and not ok:
        print("Run: python3 ide/vscode/tools/generate_assets.py", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
