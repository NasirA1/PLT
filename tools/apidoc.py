#!/usr/bin/env python3
"""Generates the Thunder API documentation: a static HTML5 site in docs/api/, in English and
Pashto, with a link on every page to the same page in the other language.

What goes in (nothing is written by hand twice):

* ``ide/vscode/data/language-info.<lang>.json`` - the compiler's own description of each
  language (``thunderc --language-info``, checked against the compiler by the test suite):
  keywords, types, literals, operators, every built-in function with its signature and
  description, the built-in modules and the instruction set.
* ``lib/thunder/<lang>/*.th`` - the SDK modules written in Thunder. Their ``#`` comments are the
  documentation: the block at the top describes the module, a ``# ---- Title`` banner opens a
  section, and the comment right above a ``struct`` or ``func`` describes it (JavaDoc style,
  without inventing a new comment syntax). Names starting with ``_`` are internal and left out.
* ``docs/*.md`` and ``docs/ps/*.md`` - the reference guides, rendered to HTML.

Usage:
    python3 tools/apidoc.py            # write docs/api/
    python3 tools/apidoc.py --check    # exit 1 if docs/api/ is not what this would write
    python3 tools/apidoc.py --output <dir>

The output is committed, like the extension's generated grammar, so it can be opened straight
from a clone (docs/api/index.html) and GitHub Pages can serve it; the test suite runs --check.
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "ide"))

import markdown_html  # noqa: E402
from thunder_ide.core.highlight import WordClasses, highlight  # noqa: E402  (pure Python, no Qt)

FONTS = ROOT / "ide" / "thunder_ide" / "fonts"

# The code editor's colours (ide/thunder_ide/qt/style.py SYNTAX_COLORS - a test keeps the two in
# step), and a brighter set of the same hues for the dark theme.
SYNTAX_COLORS = {
    "keyword": "#0033b3",
    "type": "#008080",
    "literal": "#871094",
    "builtin": "#7a3e9d",
    "number": "#1750eb",
    "string": "#067d17",
    "comment": "#8c8c8c",
}
DARK_SYNTAX_COLORS = {
    "keyword": "#7aa2ff",
    "type": "#4ec9b0",
    "literal": "#d7a6ff",
    "builtin": "#c8a2ff",
    "number": "#9cc4ff",
    "string": "#8fd18f",
    "comment": "#9aa0a6",
}

LANGUAGE_INFO = ROOT / "ide" / "vscode" / "data"
LIBRARY = ROOT / "lib" / "thunder"
DOCS = ROOT / "docs"
DEFAULT_OUTPUT = DOCS / "api"
REPOSITORY = "https://github.com/NasirA1/PLT"

LANGUAGES = ("en", "ps")

#: The guides in reading order (file name in docs/; the Pashto one has the same name in docs/ps/).
GUIDES = ("language.md", "library.md", "datastructures.md", "algorithms.md", "testing.md", "graphics.md",
          "sockets.md", "native.md", "packaging.md", "display.md", "bytecode.md", "debugging.md")

# ---------------------------------------------------------------------- the words of the site
#
# The Pashto strings are a draft for a native speaker to review, as the rest of the
# repository's Pashto is; the documents themselves come from docs/ps/ and the SDK's comments.

TEXT = {
    "en": {
        "site": "Thunder API documentation",
        "tagline": "The language, the built-in functions, the SDK modules and the guides, from the toolchain itself.",
        "overview": "Overview",
        "language": "Language",
        "keywords": "Keywords",
        "types": "Types",
        "literals": "Literals",
        "operators": "Word operators",
        "builtins": "Built-in functions",
        "core": "Core (always available)",
        "module_builtins": "The {module} module",
        "module_builtins_import": "The {module} module (import {module})",
        "modules": "SDK modules",
        "guides": "Guides",
        "instructions": "Instruction set",
        "search": "Search",
        "search_hint": "Search functions, keywords, modules, guides…",
        "no_results": "Nothing found",
        "results_kind": {"builtin": "built-in", "keyword": "keyword", "function": "function", "struct": "struct",
                         "module": "module", "guide": "guide", "instruction": "instruction", "type": "type"},
        "other_language": "په پښتو",
        "other_language_title": "This page in Pashto",
        "on_this_page": "On this page",
        "contents": "Contents",
        "signature": "Signature",
        "description": "Description",
        "module": "Module",
        "parameters": "Parameters",
        "returns": "Returns",
        "fields": "Fields",
        "constructor": "Constructor",
        "methods": "Methods",
        "structs": "Structs",
        "functions": "Functions",
        "type_parameters": "Type parameters",
        "spellings": "Also spelled",
        "counterpart": "In Pashto",
        "english_name": "English name",
        "native": "Native function (implemented in C++)",
        "generic": "Generic",
        "internal_note": "{count} internal names (starting with <code>_</code>) are not listed.",
        "source": "Source",
        "guide": "Guide",
        "guide_link": "Read the full guide",
        "version": "Thunder {version}",
        "generated": "Generated by <code>tools/apidoc.py</code> from the toolchain's language information, the SDK's own comments and the reference guides. MIT licence.",
        "category": {"keyword": "Statements and declarations", "type": "Types", "literal": "Literals",
                     "operator": "Word operators"},
        "kind_label": {"keyword": "keyword", "type": "type", "literal": "literal", "operator": "operator"},
        "opcode": "Code",
        "mnemonic": "Mnemonic",
        "operand_bytes": "Operand bytes",
        "stack_effect": "Stack effect",
        "composite_types": [
            ("<code>T[]</code>", "Arrays of <code>T</code>, e.g. <code>int[]</code>, <code>string[][]</code>; reference type, created with a literal or <code>array(n, value)</code>."),
            ("<code>(T1, T2, …)</code>", "Tuples: a fixed number of values of given types; immutable."),
            ("<code>{K: V}</code>", "Maps from keys of type <code>K</code> (<code>int</code>, <code>bool</code> or <code>string</code>) to values of type <code>V</code>."),
            ("a struct name", "Values of that struct; generic structs take type arguments (<code>Stack&lt;int&gt;</code>)."),
            ("<code>func(T1, …): R</code>", "Function values: a reference to a function with those parameter types and that return type."),
        ],
        "type_defaults": {"int": "64-bit signed integers; default 0", "float": "64-bit IEEE 754 decimals; default 0.0",
                          "bool": "true or false; default false", "string": "immutable Unicode text; default \"\""},
        "builtins_intro": "Every function the compiler knows without an <code>import</code>, and the functions of the built-in modules. The signature and the description are the compiler's own (<code>thunderc --language-info</code>), so they cannot drift from what the toolchain does; the library guide explains each group with examples.",
        "enums": "Built-in enums",
        "members": "Members",
        "position": "Position",
        "language_intro": "The reserved words of Thunder in this language. A keyword may have more than one accepted spelling; every program may also use any script for its own names.",
        "modules_intro": "The modules shipped with the toolchain in <code>lib/thunder/</code>, written in Thunder itself (the sockets module declares its native functions). <code>import \"name\"</code> makes everything below usable; the toolchain finds these files next to <code>thunderc</code>.",
        "instructions_intro": "The virtual machine's instruction set, as the code generator emits it (<code>thunderc --emit asm</code> shows a program's listing). The stack effect reads <em>before → after</em>.",
        "overview_intro": "Thunder is a small, statically typed language for learning how programming languages work, with a compiler, a bytecode virtual machine, an IDE and a VS Code extension. Programs can be written in English or Pashto; this documentation exists in both, and every page links to its counterpart.",
        "overview_sections": [
            ("language.html", "Language", "Keywords, types, literals and word operators, with every accepted spelling and the name in the other language."),
            ("builtins.html", "Built-in functions", "Input and output, text, arrays, maps, maths, higher-order functions, and the os and graphics modules."),
            ("modules/index.html", "SDK modules", "Data structures, algorithms, sockets and testing: structs and functions with their parameters, return types and documentation."),
            ("guides/index.html", "Guides", "The reference guides in reading order: the language, the library, graphics, sockets, the native interface, packaging, the bytecode and the debugger."),
            ("instructions.html", "Instruction set", "Every instruction of the virtual machine with its operands and stack effect."),
        ],
        "guides_intro": "The reference documents in reading order. The same texts are under the IDE's Help menu.",
        "untranslated": "This guide has not been translated yet; the English original is shown.",
        "count_functions": "{count} functions",
        "count_structs": "{count} structs",
        "skip": "Skip to content",
        "menu": "Menu",
        "output_label": "Output",
    },
    "ps": {
        "site": "د تندر API لارښود",
        "tagline": "ژبه، دننۍ دندې، د SDK ماډیولونه او لارښودونه، پخپله له اوزارو څخه.",
        "overview": "عمومي کتنه",
        "language": "ژبه",
        "keywords": "کلیدي کلمې",
        "types": "ډولونه",
        "literals": "لفظي ارزښتونه",
        "operators": "کلمه‌يي عملګر",
        "builtins": "دننۍ دندې",
        "core": "بنسټیزې (تل شته)",
        "module_builtins": "د {module} ټولګی",
        "module_builtins_import": "د {module} ټولګی (راوړه {module})",
        "modules": "د SDK ماډیولونه",
        "guides": "لارښودونه",
        "instructions": "د لارښوونو ټولګه",
        "search": "لټون",
        "search_hint": "دندې، کلیدي کلمې، ماډیولونه، لارښودونه ولټوئ…",
        "no_results": "ونه موندل شول",
        "results_kind": {"builtin": "دننۍ دنده", "keyword": "کلیدي کلمه", "function": "دنده", "struct": "جوړښت",
                         "module": "ماډیول", "guide": "لارښود", "instruction": "لارښوونه", "type": "ډول"},
        "other_language": "In English",
        "other_language_title": "دا پاڼه په انګلیسي",
        "on_this_page": "په دې پاڼه کې",
        "contents": "نيوليک",
        "signature": "بڼه",
        "description": "تشریح",
        "module": "ماډیول",
        "parameters": "پارامترونه",
        "returns": "راګرځوي",
        "fields": "ساحې",
        "constructor": "جوړوونکې دنده",
        "methods": "دندې",
        "structs": "جوړښتونه",
        "functions": "دندې",
        "type_parameters": "د ډول پارامترونه",
        "spellings": "بله لیکبڼه",
        "counterpart": "په انګلیسي",
        "english_name": "انګلیسي نوم",
        "native": "بهرنۍ دنده (په ++C کې لیکل شوې)",
        "generic": "عمومي",
        "internal_note": "{count} داخلي نومونه (چې په <code>_</code> پیلېږي) دلته نه دي ښودل شوي.",
        "source": "سرچینه",
        "guide": "لارښود",
        "guide_link": "بشپړ لارښود ولولئ",
        "version": "تندر {version}",
        "generated": "د <code>tools/apidoc.py</code> له لارې د اوزارو د ژبې له معلوماتو، د SDK له خپلو تبصرو او له لارښودونو جوړ شوی. د MIT جواز.",
        "category": {"keyword": "امرونه او اعلانونه", "type": "ډولونه", "literal": "لفظي ارزښتونه",
                     "operator": "کلمه‌يي عملګر"},
        "kind_label": {"keyword": "کلیدي کلمه", "type": "ډول", "literal": "لفظي ارزښت", "operator": "عملګر"},
        "opcode": "کوډ",
        "mnemonic": "نوم",
        "operand_bytes": "د عملوند بایټونه",
        "stack_effect": "د سټک اغېز",
        "composite_types": [
            ("<code>ت[]</code>", "د <code>ت</code> لړۍ، لکه <code>عدد[]</code>؛ حوالوي ډول، په لفظي بڼه یا د <code>لړۍ(ن، ارزښت)</code> سره جوړېږي."),
            ("<code>(ت۱، ت۲، …)</code>", "ټاپلونه: د ورکړل شویو ډولونو ثابت شمېر ارزښتونه؛ نه بدلېدونکي."),
            ("<code>{ک: ا}</code>", "نقشې: د <code>ک</code> ډول کیلۍ (<code>عدد</code>، <code>بولین</code> یا <code>متن</code>) د <code>ا</code> ډول ارزښتونو ته."),
            ("د جوړښت نوم", "د هغه جوړښت ارزښتونه؛ عمومي جوړښتونه د ډول دلیلونه اخلي (<code>ډېرۍ&lt;عدد&gt;</code>)."),
            ("<code>دنده(ت۱، …): ر</code>", "د دندې ارزښتونه: هغې دندې ته حواله چې دا پارامترونه او دا د راګرځېدو ډول لري."),
        ],
        "type_defaults": {"int": "۶۴-بیټه علامه لرونکي بشپړ عددونه؛ تلواله ۰", "float": "۶۴-بیټه IEEE 754 اعشاري؛ تلواله 0.0",
                          "bool": "رښتیا یا ناسم؛ تلواله ناسم", "string": "نه بدلېدونکی یونیکوډ متن؛ تلواله \"\""},
        "builtins_intro": "هره هغه دنده چې کمپایلر پرته له <code>راوړه</code> پېژني، او د دننیو ماډیولونو دندې. بڼه او تشریح د کمپایلر خپلې دي (<code>thunderc --language-info</code>)، نو له اوزارو سره توپیر نه شي موندلی؛ د کتابتون لارښود هره ډله په بېلګو تشریح کوي.",
        "enums": "دننۍ ګڼنې",
        "members": "غړي",
        "position": "ځای",
        "language_intro": "د تندر خوندي شوې کلمې په دې ژبه کې. یوه کلیدي کلمه کېدای شي له یوې زیاتې منل شوې لیکبڼې ولري؛ هر پروګرام د خپلو نومونو لپاره هر لیکدود کارولی شي.",
        "modules_intro": "هغه ماډیولونه چې له اوزارو سره په <code>lib/thunder/</code> کې راځي، پخپله په تندر لیکل شوي (د ساکټونو ماډیول خپلې بهرنۍ دندې اعلانوي). <code>راوړه \"نوم\"</code> ټول لاندې شیان د کارونې وړ کوي؛ اوزار دا فایلونه د <code>thunderc</code> تر څنګ مومي.",
        "instructions_intro": "د مجازي ماشین د لارښوونو ټولګه، لکه څنګه چې کوډ جوړوونکی یې تولیدوي (<code>thunderc --emit asm</code> د پروګرام لیست ښيي). د سټک اغېز <em>مخکې ← وروسته</em> لوستل کېږي.",
        "overview_intro": "تندر یوه کوچنۍ، ثابت-ډوله ژبه ده چې د پروګرامولۍ ژبو د کار زده کړې لپاره جوړه شوې، له کمپایلر، د بایټ کوډ مجازي ماشین، IDE او د VS Code له غځونې سره. پروګرامونه په انګلیسي یا پښتو لیکل کېدای شي؛ دا لارښود په دواړو ژبو شته او هره پاڼه خپلې بلې ژبې ته تړنه لري.",
        "overview_sections": [
            ("language.html", "ژبه", "کلیدي کلمې، ډولونه، لفظي ارزښتونه او کلمه‌يي عملګر، د هرې منل شوې لیکبڼې او په بله ژبه د نوم سره."),
            ("builtins.html", "دننۍ دندې", "ننوت او وتنه، متن، لړۍ، نقشې، ریاضي، لوړ-رتبه دندې، او د سیسټم او ګرافیک ماډیولونه."),
            ("modules/index.html", "د SDK ماډیولونه", "ډېټا جوړښتونه، الګوریتمونه، ساکټونه او ازموینه: جوړښتونه او دندې د پارامترونو، د راګرځېدو ډولونو او تشریح سره."),
            ("guides/index.html", "لارښودونه", "د لوستلو په ترتیب لارښودونه: ژبه، کتابتون، ګرافیک، ساکټونه، بهرنی اړیکی، بستول، بایټ کوډ او ډیبګر."),
            ("instructions.html", "د لارښوونو ټولګه", "د مجازي ماشین هره لارښوونه د عملوندونو او د سټک اغېز سره."),
        ],
        "guides_intro": "لارښودونه د لوستلو په ترتیب. همدا متنونه د IDE د مرستې په مینو کې هم شته.",
        "untranslated": "دا لارښود لا نه دی ژباړل شوی؛ انګلیسي اصل ښودل کېږي.",
        "count_functions": "{count} دندې",
        "count_structs": "{count} جوړښتونه",
        "skip": "منځپانګې ته ورشئ",
        "menu": "مینو",
        "output_label": "پایله",
    },
}


# ---------------------------------------------------------------------- the SDK modules

@dataclass
class Parameter:
    name: str
    type: str


@dataclass
class Function:
    name: str
    type_parameters: list[str]
    parameters: list[Parameter]
    returns: str
    doc: str
    native: bool
    line: int
    section: str
    signature: str


@dataclass
class Field:
    name: str
    type: str
    doc: str


@dataclass
class Struct:
    name: str
    type_parameters: list[str]
    fields: list[Field]
    doc: str
    line: int
    section: str
    constructor: list[Parameter] | None = None   # the member spelled like the struct, if any
    methods: list[Function] = field(default_factory=list)


@dataclass
class Section:
    title: str
    doc: str


@dataclass
class Module:
    name: str          # the import name: the file name without .th
    language: str
    path: Path
    doc: str
    sections: list[Section]
    structs: list[Struct]
    functions: list[Function]
    internal: int      # names left out


def _spellings(info: dict, kind: str) -> list[str]:
    for keyword in info["keywords"]:
        if keyword["kind"] == kind:
            return list(keyword["spellings"])
    return []


def _split_top_level(text: str, separators: str) -> list[str]:
    """Splits at separators outside (), [], {} and <>."""
    parts: list[str] = []
    depth = 0
    current: list[str] = []
    for character in text:
        if character in "([{<":
            depth += 1
        elif character in ")]}>":
            depth -= 1
        if depth == 0 and character in separators:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(character)
    tail = "".join(current).strip()
    if tail or parts:
        parts.append(tail)
    return [part for part in parts if part]


def _comment_text(lines: list[str]) -> str:
    """The text of a run of ``#`` comment lines, without the markers, as paragraphs."""
    out = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("#"):
            stripped = stripped[1:]
            if stripped.startswith(" "):
                stripped = stripped[1:]
        out.append(stripped.rstrip())
    return "\n".join(out).strip()


def parse_module(path: Path, info: dict, language: str) -> Module:
    """Reads a Thunder module's declarations and the comments that document them."""
    text = path.read_text(encoding="utf-8")
    raw_lines = text.split("\n")
    # "module stack" blocks: their members are read as if written at the top level, under
    # their qualified names (stack.push), with the module recorded for each line.
    module_words = _spellings(info, "KwModule") or ["module"]
    module_re = re.compile(rf"^(?:{'|'.join(re.escape(spelling) for spelling in module_words)})\s+(\S+)\s*(?:#.*)?$")
    lines: list[str] = []
    owners: list[str] = []
    owner = ""
    for raw in raw_lines:
        opened = module_re.match(raw)
        if opened:
            owner = opened.group(1)
            lines.append("")  # the module line itself declares nothing
            owners.append(owner)
            continue
        if owner and raw.strip() and not raw.startswith((" ", "\t")):
            owner = ""  # back at the top level
        lines.append(raw[4:] if owner and raw.startswith("    ") else raw)
        owners.append(owner)
    qualify = lambda name, at: f"{owners[at]}.{name}" if owners[at] else name  # noqa: E731
    struct_words = _spellings(info, "KwStruct") or ["struct"]
    func_words = _spellings(info, "KwFunc") or ["func"]
    native_words = _spellings(info, "KwNative") or ["native"]
    word = lambda words: "|".join(re.escape(spelling) for spelling in words)  # noqa: E731
    name_pattern = r"([^\s(<:]+)"
    func_re = re.compile(rf"^(?:(?P<native>{word(native_words)})\s+)?(?:{word(func_words)})\s+{name_pattern}\s*(?:<([^>]*)>)?\s*\((.*)\)\s*(?::\s*(.+?))?\s*(?:#.*)?$")
    struct_re = re.compile(rf"^(?:{word(struct_words)})\s+{name_pattern}\s*(?:<([^>]*)>)?\s*(?:#.*)?$")
    banner_re = re.compile(r"^#\s*-{4,}\s*(.*?)\s*-*\s*$")
    separators = "," + "".join(p for p in info.get("punctuation", []) if p in ("،", "٬"))
    if "،" not in separators:
        separators += "،"

    # The module's own description: the comment block at the top (after a language directive).
    index = 0
    header: list[str] = []
    while index < len(lines) and lines[index].startswith("#"):
        if not re.match(r"^#\s*(lang|ژبه)\s*:", lines[index]):
            header.append(lines[index])
        index += 1
    module_doc = _comment_text(header)
    # The first line of the header names the file; the description proper follows it.
    module_doc = re.sub(r"^\S+\.th \([^)]*\)\s*-\s*", "", module_doc)

    sections: list[Section] = []
    structs: list[Struct] = []
    functions: list[Function] = []
    internal = 0
    current_section = ""
    pending: list[str] = []   # the comment lines right above the current line
    position = index
    while position < len(lines):
        line = lines[position]
        banner = banner_re.match(line)
        if banner and banner.group(1):
            current_section = banner.group(1)
            # The comment lines after the banner (until a blank line) describe the section.
            look = position + 1
            description: list[str] = []
            while look < len(lines) and lines[look].startswith("#"):
                description.append(lines[look])
                look += 1
            sections.append(Section(current_section, _comment_text(description)))
            pending = []
            position = look
            continue
        if line.startswith("#"):
            pending.append(line)
            position += 1
            continue
        if not line.strip():
            pending = []
            position += 1
            continue
        struct_match = struct_re.match(line)
        if struct_match:
            private = struct_match.group(1).startswith("_")
            bare_name = struct_match.group(1)
            name = qualify(bare_name, position)
            type_parameters = _split_top_level(struct_match.group(2) or "", separators)
            # The constructor is the member spelled exactly like the struct: "Stack()" under
            # "struct Stack<T>", with a deeper-indented body.
            constructor_re = re.compile(rf"^{re.escape(bare_name)}\s*\((.*)\)\s*(?:#.*)?$")
            parse_parameters = lambda text: [Parameter(piece.partition(":")[0].strip(), piece.partition(":")[2].strip())  # noqa: E731
                                             for piece in _split_top_level(text or "", separators)]
            fields: list[Field] = []
            methods: list[Function] = []
            constructor: list[Parameter] | None = None
            look = position + 1
            member_doc: list[str] = []
            member_indent = 0   # the first indent level: fields, the constructor and the methods
            while look < len(lines):
                raw_member = lines[look]
                if not raw_member.strip():
                    member_doc = []
                    look += 1
                    continue
                if not raw_member.startswith((" ", "\t")):
                    break   # back at the top level: the struct is over
                indent = len(raw_member) - len(raw_member.lstrip(" \t"))
                if not member_indent:
                    member_indent = indent
                if indent > member_indent:
                    look += 1   # inside a constructor's or method's body: not a field
                    continue
                body = raw_member.strip()
                if body.startswith("#"):
                    member_doc.append(body)
                    look += 1
                    continue
                method_match = func_re.match(body)
                if method_match:
                    method_name = method_match.group(2)
                    if method_name.endswith("_") or method_name.startswith("_"):
                        internal += 1   # a private method: the struct's own business
                    else:
                        signature = re.sub(r"\s*(#.*)?$", "", body).strip()
                        methods.append(Function(method_name, _split_top_level(method_match.group(3) or "", separators),
                                                parse_parameters(method_match.group(4)),
                                                (method_match.group(5) or "").strip(), _comment_text(member_doc),
                                                bool(method_match.group("native")), look + 1, current_section, signature))
                    member_doc = []
                    look += 1
                    continue
                constructor_match = constructor_re.match(body)
                if constructor_match:
                    constructor = parse_parameters(constructor_match.group(1))
                    member_doc = []
                    look += 1
                    continue
                if ":" in body:
                    field_name, _, field_type = body.partition(":")
                    field_type, _, trailing = field_type.partition("#")
                    doc = _comment_text(member_doc) or trailing.strip()
                    if field_name.strip().startswith("_"):
                        internal += 1   # a private field: the struct's own insides
                    else:
                        fields.append(Field(field_name.strip(), field_type.strip(), doc))
                    member_doc = []
                look += 1
            if private:
                internal += 1
            else:
                structs.append(Struct(name, type_parameters, fields, _comment_text(pending), position + 1,
                                      current_section, constructor, methods))
            pending = []
            position = look
            continue
        func_match = func_re.match(line)
        if func_match:
            private = func_match.group(2).startswith("_")
            name = qualify(func_match.group(2), position)
            type_parameters = _split_top_level(func_match.group(3) or "", separators)
            parameters = []
            for piece in _split_top_level(func_match.group(4) or "", separators):
                parameter_name, _, parameter_type = piece.partition(":")
                parameters.append(Parameter(parameter_name.strip(), parameter_type.strip()))
            returns = (func_match.group(5) or "").strip()
            signature = re.sub(r"\s*(#.*)?$", "", line).strip()
            if owners[position]:
                # The signature as a caller writes it: the member under its qualified name.
                signature = signature.replace(func_match.group(2), name, 1)
            if private:
                internal += 1
            else:
                functions.append(Function(name, type_parameters, parameters, returns, _comment_text(pending),
                                          bool(func_match.group("native")), position + 1, current_section, signature))
            pending = []
            position += 1
            continue
        pending = []
        position += 1
    return Module(path.stem, language, path, module_doc, sections, structs, functions, internal)


# ---------------------------------------------------------------------- the site

@dataclass
class Page:
    path: str               # relative to the language root, e.g. "modules/datastructures.html"
    title: str
    body: str
    toc: list[tuple[str, str]] = field(default_factory=list)   # (id, text) for "On this page"
    section: str = ""       # the navigation entry to highlight


class Site:
    def __init__(self, output: Path):
        self.output = output
        self.version = read_version()
        self.info = {code: json.loads((LANGUAGE_INFO / f"language-info.{code}.json").read_text(encoding="utf-8"))
                     for code in LANGUAGES}
        self.modules = {code: [parse_module(path, self.info[code], code) for path in sorted((LIBRARY / code).glob("*.th"))]
                        for code in LANGUAGES}
        self.files: dict[str, str | bytes] = {}   # relative path -> contents (text, or a font file)
        # The editor's classifier, fed every language's words at once: a guide in either language
        # may quote the other.
        merged: dict[str, str] = {}
        for code in LANGUAGES:
            merged.update(WordClasses.from_language_info(self.info[code]).words)
        self.classes = WordClasses(merged)
        self.search: dict[str, list[dict]] = {code: [] for code in LANGUAGES}

    # ------------------------------------------------------------ helpers

    def text(self, code: str, key: str, **values) -> str:
        value = TEXT[code][key]
        return value.format(**values) if values and isinstance(value, str) else value

    def other(self, code: str) -> str:
        return "ps" if code == "en" else "en"

    @staticmethod
    def escape(value) -> str:
        return html.escape(str(value), quote=True)

    def highlighted(self, text: str) -> str:
        """Thunder source as HTML, coloured as the IDE's editor colours it."""
        out: list[str] = []
        position = 0
        for region in highlight(text, self.classes):
            out.append(html.escape(text[position:region.start]))
            out.append(f"<span class=\"tok-{region.tag}\">{html.escape(text[region.start:region.end])}</span>")
            position = region.end
        out.append(html.escape(text[position:]))
        return "".join(out)

    def code_span(self, text: str) -> str:
        """An inline signature, highlighted."""
        return f"<code dir=\"auto\">{self.highlighted(text)}</code>"

    def fenced(self, code: str, language: str, text: str) -> str | None:
        """A fenced block of a guide: Thunder is highlighted, an untagged block is program output
        shown as the IDE's output panel shows it (each line in the direction of its own first
        letter), and every other language keeps the plain default."""
        if language == "thunder":
            return f"<pre class=\"thunder\"><code>{self.highlighted(text)}</code></pre>"
        if language == "":
            lines = "\n".join(f"<span class=\"out-line\" dir=\"auto\">{html.escape(line)}</span>" for line in text.split("\n"))
            label = self.escape(self.text(code, "output_label"))
            return f"<pre class=\"output\" data-label=\"{label}\"><code>{lines}</code></pre>"
        return None

    def add_search(self, code: str, kind: str, name: str, page: str, anchor: str, summary: str) -> None:
        self.search[code].append({"kind": kind, "name": name, "page": page + (f"#{anchor}" if anchor else ""),
                                  "summary": re.sub(r"<[^>]+>", "", summary)[:160]})

    def counterpart_keyword(self, code: str, kind: str) -> str:
        for keyword in self.info[self.other(code)]["keywords"]:
            if keyword["kind"] == kind:
                return keyword["text"]
        return ""

    def counterpart_builtin(self, code: str, canonical: str) -> str:
        for builtin in self.info[self.other(code)]["builtins"]:
            if builtin["canonical_name"] == canonical:
                return builtin["name"]
        return ""

    def counterpart_declaration(self, code: str, module: Module, kind: str, index: int) -> str:
        """The same declaration in the other language's module, matched by position."""
        twin = next((candidate for candidate in self.modules[self.other(code)] if candidate.name == module.name), None)
        if not twin:
            return ""
        mine = module.functions if kind == "function" else module.structs
        theirs = twin.functions if kind == "function" else twin.structs
        if len(mine) != len(theirs) or index >= len(theirs):
            return ""
        return theirs[index].name

    # ------------------------------------------------------------ pages

    def build(self) -> dict[str, str]:
        self.files["index.html"] = self.redirect_page()
        self.files["assets/style.css"] = STYLE
        self.files["assets/app.js"] = SCRIPT
        # The IDE's own fonts: JetBrains Mono for code (no Arabic glyphs of its own, so the Pashto
        # font draws every Pashto letter, exactly as in the editor) and Vazirmatn for Pashto.
        for folder, file in (("jetbrainsmono", "JetBrainsMono[wght].ttf"), ("vazirmatn", "Vazirmatn[wght].ttf")):
            self.files[f"assets/fonts/{file.replace('[wght]', '')}"] = (FONTS / folder / file).read_bytes()
            self.files[f"assets/fonts/{folder}-OFL.txt"] = (FONTS / folder / "OFL.txt").read_text(encoding="utf-8")
        for code in LANGUAGES:
            pages = [self.overview_page(code), self.language_page(code), self.builtins_page(code),
                     self.modules_index_page(code), self.instructions_page(code), self.guides_index_page(code)]
            pages += [self.module_page(code, module) for module in self.modules[code]]
            pages += [self.guide_page(code, name) for name in GUIDES]
            for page in pages:
                self.files[f"{code}/{page.path}"] = self.layout(code, page)
            self.files[f"{code}/search-index.js"] = (
                "window.THUNDER_INDEX = " + json.dumps(self.search[code], ensure_ascii=False, indent=0) + ";\n")
        return self.files

    def redirect_page(self) -> str:
        return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="0; url=en/index.html">
<title>Thunder API documentation</title>
<link rel="stylesheet" href="assets/style.css">
</head>
<body class="redirect">
<main>
<h1>Thunder API documentation</h1>
<p><a href="en/index.html">English</a> · <a href="ps/index.html" lang="ps" dir="rtl">پښتو</a></p>
</main>
</body>
</html>
"""

    def overview_page(self, code: str) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        parts = [f"<p class=\"lead\">{t('overview_intro')}</p>", "<div class=\"cards\">"]
        for href, title, blurb in t("overview_sections"):
            parts.append(f"<a class=\"card\" href=\"{href}\"><h2>{self.escape(title)}</h2><p>{self.escape(blurb)}</p></a>")
        parts.append("</div>")
        info = self.info[code]
        counts = [
            (len(info["keywords"]), t("keywords")),
            (len(info["builtins"]), t("builtins")),
            (sum(self.callable_count(module) for module in self.modules[code]), t("functions") + " · " + t("modules")),
            (sum(len(module.structs) for module in self.modules[code]), t("structs") + " · " + t("modules")),
            (len(GUIDES), t("guides")),
            (len(info["opcodes"]), t("instructions")),
        ]
        parts.append("<div class=\"stats\">" + "".join(
            f"<div class=\"stat\"><span class=\"number\">{count}</span><span class=\"label\">{self.escape(label)}</span></div>"
            for count, label in counts) + "</div>")
        return Page("index.html", t("overview"), "\n".join(parts), section="overview")

    def language_page(self, code: str) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        info = self.info[code]
        parts = [f"<p class=\"lead\">{t('language_intro')}</p>"]
        toc = []
        guide = f"guides/language.html"
        parts.append(f"<p><a class=\"button\" href=\"{guide}\">{t('guide_link')} →</a></p>")
        for category in ("keyword", "type", "literal", "operator"):
            keywords = [keyword for keyword in info["keywords"] if keyword["category"] == category]
            if not keywords:
                continue
            anchor = f"category-{category}"
            heading = t("category")[category]
            toc.append((anchor, heading))
            parts.append(f"<h2 id=\"{anchor}\">{self.escape(heading)}</h2>")
            if category == "type":
                parts.append("<table class=\"api\"><thead><tr>"
                             f"<th>{t('types')}</th><th>{t('description')}</th><th>{t('spellings')}</th><th>{t('counterpart')}</th>"
                             "</tr></thead><tbody>")
                for keyword in keywords:
                    canonical = self.counterpart_keyword(code, keyword["kind"]) if code == "ps" else keyword["text"]
                    english = keyword["text"] if code == "en" else self.counterpart_keyword(code, keyword["kind"])
                    description = t("type_defaults").get(english, "")
                    parts.append(self.keyword_row(code, keyword, description, extra_cell=True))
                    self.add_search(code, "type", keyword["text"], "language.html", f"kw-{keyword['kind']}", description)
                parts.append("</tbody></table>")
                parts.append("<table class=\"api\"><thead><tr>"
                             f"<th>{t('types')}</th><th>{t('description')}</th></tr></thead><tbody>")
                for name, description in t("composite_types"):
                    parts.append(f"<tr><td>{name}</td><td>{description}</td></tr>")
                parts.append("</tbody></table>")
                continue
            parts.append("<table class=\"api\"><thead><tr>"
                         f"<th>{t('kind_label')[category].capitalize()}</th><th>{t('spellings')}</th><th>{t('counterpart')}</th>"
                         "</tr></thead><tbody>")
            for keyword in keywords:
                parts.append(self.keyword_row(code, keyword, ""))
                self.add_search(code, "keyword", keyword["text"], "language.html", f"kw-{keyword['kind']}",
                                t("kind_label")[category])
            parts.append("</tbody></table>")
        return Page("language.html", t("language"), "\n".join(parts), toc, section="language")

    def keyword_row(self, code: str, keyword: dict, description: str, extra_cell: bool = False) -> str:
        others = [spelling for spelling in keyword["spellings"] if spelling != keyword["text"]]
        spellings = ", ".join(f"<code>{self.escape(s)}</code>" for s in others) or "—"
        counterpart = self.counterpart_keyword(code, keyword["kind"])
        other_code = self.other(code)
        counterpart_cell = (f"<a href=\"../{other_code}/language.html#kw-{keyword['kind']}\" lang=\"{other_code}\""
                            f" dir=\"{'rtl' if other_code == 'ps' else 'ltr'}\"><code>{self.escape(counterpart)}</code></a>"
                            if counterpart else "—")
        name = f"<code class=\"name\">{self.escape(keyword['text'])}</code>"
        middle = f"<td>{description}</td>" if extra_cell else ""
        return f"<tr id=\"kw-{keyword['kind']}\"><td>{name}</td>{middle}<td>{spellings}</td><td>{counterpart_cell}</td></tr>"

    def builtins_page(self, code: str) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        info = self.info[code]
        parts = [f"<p class=\"lead\">{t('builtins_intro')}</p>",
                 f"<p><a class=\"button\" href=\"guides/library.html\">{t('guide_link')} →</a></p>"]
        toc = []
        # The default module first, then the modules in the order the toolchain lists them.
        groups: dict[str, list[dict]] = {"": []}
        for module in info["modules"]:
            groups[module["canonical_name"]] = []
        for builtin in info["builtins"]:
            groups.setdefault(builtin["module"], []).append(builtin)
        module_names = {module["canonical_name"]: module["name"] for module in info["modules"]}
        needs_import = {module["canonical_name"]: module.get("needs_import", False) for module in info["modules"]}
        for module, builtins in groups.items():
            if not builtins:
                continue
            anchor = f"module-{module or 'core'}"
            heading = t("core") if not module else t("module_builtins_import" if needs_import.get(module) else "module_builtins",
                                                     module=module_names.get(module, module))
            toc.append((anchor, heading))
            parts.append(f"<h2 id=\"{anchor}\">{self.escape(heading)}</h2>")
            parts.append("<div class=\"index\">" + " ".join(
                f"<a href=\"#fn-{self.escape(b['canonical_name'])}\"><code>{self.escape(b['name'])}</code></a>" for b in builtins) + "</div>")
            for builtin in builtins:
                parts.append(self.builtin_entry(code, builtin))
        if info.get("enums"):
            toc.append(("enums", t("enums")))
            parts.append(f"<h2 id=\"enums\">{self.escape(t('enums'))}</h2>")
            other_code = self.other(code)
            for enumeration in info["enums"]:
                anchor = f"enum-{enumeration['canonical_name']}"
                module = module_names.get(enumeration["module"], enumeration["module"])
                rows = [f"<dt>{t('module')}</dt><dd><code>{self.escape(module)}</code></dd>"]
                table = "".join(
                    f"<tr><td><code>{self.escape(module)}.{self.escape(enumeration['name'])}.{self.escape(member['name'])}</code></td>"
                    f"<td>{position}</td><td><code dir=\"ltr\">{self.escape(member['canonical_name'])}</code></td></tr>"
                    for position, member in enumerate(enumeration["members"]))
                rows.append(f"<dt>{t('members')}</dt><dd><table class=\"api compact\"><thead><tr><th>{t('members')}</th>"
                            f"<th>{t('position')}</th><th>{t('english_name')}</th></tr></thead><tbody>{table}</tbody></table></dd>")
                rows.append(f"<dt>{t('counterpart')}</dt><dd><a href=\"../{other_code}/builtins.html#{anchor}\" lang=\"{other_code}\""
                            f" dir=\"{'rtl' if other_code == 'ps' else 'ltr'}\"><code>{self.escape(self.counterpart_enum(code, enumeration['canonical_name']))}</code></a></dd>")
                self.add_search(code, "type", enumeration["name"], "builtins.html", anchor, enumeration["description"])
                for member in enumeration["members"]:
                    self.add_search(code, "builtin", f"{enumeration['name']}.{member['name']}", "builtins.html", anchor, enumeration["name"])
                parts.append(f"<article class=\"entry\" id=\"{anchor}\">"
                             f"<h3><span class=\"badge\">{self.escape(t('results_kind')['type'])}</span> <code>{self.escape(enumeration['name'])}</code>"
                             f"<a class=\"anchor\" href=\"#{anchor}\">#</a></h3>"
                             f"<p class=\"description\">{self.escape(enumeration['description'])}</p><dl>{''.join(rows)}</dl></article>")
        return Page("builtins.html", t("builtins"), "\n".join(parts), toc, section="builtins")

    def counterpart_enum(self, code: str, canonical: str) -> str:
        for enumeration in self.info[self.other(code)].get("enums", []):
            if enumeration["canonical_name"] == canonical:
                return enumeration["name"]
        return canonical

    def builtin_entry(self, code: str, builtin: dict) -> str:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        anchor = f"fn-{builtin['canonical_name']}"
        signature = builtin["signature"]
        if code != "en":
            # The compiler words the signature in English; show it with this language's name.
            signature = signature.replace(builtin["canonical_name"], builtin["name"], 1)
        counterpart = self.counterpart_builtin(code, builtin["canonical_name"])
        other_code = self.other(code)
        rows = [f"<dt>{t('signature')}</dt><dd>{self.code_span(signature)}</dd>"]
        if counterpart:
            rows.append(f"<dt>{t('counterpart')}</dt><dd><a href=\"../{other_code}/builtins.html#{anchor}\" lang=\"{other_code}\""
                        f" dir=\"{'rtl' if other_code == 'ps' else 'ltr'}\"><code>{self.escape(counterpart)}</code></a></dd>")
        if code != "en" and builtin["canonical_name"] != builtin["name"]:
            rows.append(f"<dt>{t('english_name')}</dt><dd><code dir=\"ltr\">{self.escape(builtin['canonical_name'])}</code></dd>")
        self.add_search(code, "builtin", builtin["name"], "builtins.html", anchor, builtin["description"])
        return (f"<article class=\"entry\" id=\"{anchor}\">"
                f"<h3><code>{self.escape(builtin['name'])}</code><a class=\"anchor\" href=\"#{anchor}\">#</a></h3>"
                f"<p class=\"description\">{self.escape(builtin['description'])}</p>"
                f"<dl>{''.join(rows)}</dl></article>")

    def modules_index_page(self, code: str) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        parts = [f"<p class=\"lead\">{t('modules_intro')}</p>", "<div class=\"cards\">"]
        for module in self.modules[code]:
            summary = module.doc.split("\n\n")[0]
            parts.append(f"<a class=\"card\" href=\"{module.name}.html\"><h2><code>{self.escape(module.name)}</code></h2>"
                         f"<p>{self.escape(summary)}</p>"
                         f"<p class=\"meta\">{self.escape(t('count_structs', count=len(module.structs)))} · "
                         f"{self.escape(t('count_functions', count=self.callable_count(module)))}</p></a>")
            self.add_search(code, "module", module.name, f"modules/{module.name}.html", "", summary)
        parts.append("</div>")
        return Page("modules/index.html", t("modules"), "\n".join(parts), section="modules")

    def module_page(self, code: str, module: Module) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        other_code = self.other(code)
        parts = [f"<p class=\"import\">{self.code_span(self.import_line(code, module.name))}</p>"]
        parts.append(f"<div class=\"lead\">{self.paragraphs(module.doc)}</div>")
        guide = f"../guides/{module.name}.html" if f"{module.name}.md" in GUIDES else ""
        if guide:
            parts.append(f"<p><a class=\"button\" href=\"{guide}\">{t('guide_link')} →</a></p>")
        toc = []
        # A quick index of everything.
        parts.append("<div class=\"index\">" + " ".join(
            [f"<a href=\"#struct-{self.escape(s.name)}\"><code>{self.escape(s.name)}</code></a>" for s in module.structs]
            + [f"<a href=\"#fn-{self.escape(f.name)}\"><code>{self.escape(f.name)}</code></a>" for f in module.functions]) + "</div>")
        # Grouped by the source's own section banners, in source order.
        order = [section.title for section in module.sections]
        by_section: dict[str, list] = {}
        for struct in module.structs:
            by_section.setdefault(struct.section, []).append(("struct", struct))
        for function in module.functions:
            by_section.setdefault(function.section, []).append(("function", function))
        for section_title in [""] + [s for s in order if s in by_section] + [s for s in by_section if s not in order and s]:
            if section_title not in by_section:
                continue
            items = sorted(by_section[section_title], key=lambda item: item[1].line)
            if section_title:
                anchor = "section-" + markdown_html.slug(section_title)
                toc.append((anchor, section_title))
                parts.append(f"<h2 id=\"{anchor}\">{self.escape(section_title)}</h2>")
                section = next((s for s in module.sections if s.title == section_title), None)
                if section and section.doc:
                    parts.append(f"<div class=\"section-doc\">{self.paragraphs(section.doc)}</div>")
            for kind, item in items:
                if kind == "struct":
                    parts.append(self.struct_entry(code, module, item))
                else:
                    parts.append(self.function_entry(code, module, item))
        if module.internal:
            parts.append(f"<p class=\"note\">{t('internal_note', count=module.internal)}</p>")
        relative = module.path.relative_to(ROOT).as_posix()
        parts.append(f"<p class=\"meta\">{t('source')}: <a href=\"{REPOSITORY}/blob/master/{relative}\" target=\"_blank\" rel=\"noopener\"><code>{relative}</code></a></p>")
        twin = next((m for m in self.modules[other_code] if m.name == module.name), None)
        title = module.name
        return Page(f"modules/{module.name}.html", title, "\n".join(parts), toc, section="modules")

    @staticmethod
    def callable_count(module: Module) -> int:
        """Everything a program can call: the module's functions and its structs' methods."""
        return len(module.functions) + sum(len(struct.methods) for struct in module.structs)

    def import_line(self, code: str, name: str) -> str:
        words = _spellings(self.info[code], "KwImport") or ["import"]
        quotes = self.info[code].get("quotes") or [["\"", "\""]]
        opening, closing = quotes[0]
        return f"{words[0]} {opening}{name}{closing}"

    def struct_entry(self, code: str, module: Module, struct: Struct) -> str:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        anchor = f"struct-{struct.name}"
        generic = f"&lt;{self.escape(', '.join(struct.type_parameters))}&gt;" if struct.type_parameters else ""
        rows = []
        if struct.type_parameters:
            rows.append(f"<dt>{t('type_parameters')}</dt><dd>" + ", ".join(f"<code>{self.escape(p)}</code>" for p in struct.type_parameters) + "</dd>")
        if struct.fields:
            table = "".join(f"<tr><td><code>{self.escape(f.name)}</code></td><td><code dir=\"auto\">{self.escape(f.type)}</code></td>"
                            f"<td>{self.escape(f.doc)}</td></tr>" for f in struct.fields)
            rows.append(f"<dt>{t('fields')}</dt><dd><table class=\"api compact\"><tbody>{table}</tbody></table></dd>")
        if struct.constructor is not None:
            # How a caller creates one: the struct's own name as a call, with the parameters.
            comma = "، " if code == "ps" else ", "
            bare = struct.name.split(".")[-1]
            pieces = comma.join(f"{p.name}: {p.type}" if p.type else p.name for p in struct.constructor)
            rows.append(f"<dt>{t('constructor')}</dt><dd>{self.code_span(f'{bare}({pieces})')}</dd>")
        index = module.structs.index(struct)
        counterpart = self.counterpart_declaration(code, module, "struct", index)
        other_code = self.other(code)
        if counterpart:
            rows.append(f"<dt>{t('counterpart')}</dt><dd><a href=\"../../{other_code}/modules/{module.name}.html#struct-{self.escape(counterpart)}\""
                        f" lang=\"{other_code}\" dir=\"{'rtl' if other_code == 'ps' else 'ltr'}\"><code>{self.escape(counterpart)}</code></a></dd>")
        self.add_search(code, "struct", struct.name, f"modules/{module.name}.html", anchor, struct.doc)
        methods = ""
        if struct.methods:
            methods = (f"<h4>{self.escape(t('methods'))}</h4>"
                       + "".join(self.method_entry(code, module, struct, method) for method in struct.methods))
        return (f"<article class=\"entry struct\" id=\"{anchor}\">"
                f"<h3><span class=\"badge\">{t('structs')[:-1] if code == 'en' else t('results_kind')['struct']}</span> "
                f"<code>{self.escape(struct.name)}{generic}</code><a class=\"anchor\" href=\"#{anchor}\">#</a></h3>"
                f"<div class=\"description\">{self.paragraphs(struct.doc)}</div>"
                f"<dl>{''.join(rows)}</dl>{methods}</article>")

    def method_entry(self, code: str, module: Module, struct: Struct, method: Function) -> str:
        """A struct's method, shown as the functions are, under the struct's heading."""
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        anchor = f"fn-{struct.name}.{method.name}"
        rows = [f"<dt>{t('signature')}</dt><dd>{self.code_span(method.signature)}</dd>"]
        if method.type_parameters:
            rows.append(f"<dt>{t('type_parameters')}</dt><dd>" + ", ".join(f"<code>{self.escape(p)}</code>" for p in method.type_parameters) + "</dd>")
        if method.parameters:
            table = "".join(f"<tr><td><code>{self.escape(p.name)}</code></td><td><code dir=\"auto\">{self.escape(p.type)}</code></td></tr>"
                            for p in method.parameters)
            rows.append(f"<dt>{t('parameters')}</dt><dd><table class=\"api compact\"><tbody>{table}</tbody></table></dd>")
        if method.returns:
            rows.append(f"<dt>{t('returns')}</dt><dd><code dir=\"auto\">{self.escape(method.returns)}</code></dd>")
        if method.native:
            rows.append(f"<dt>{t('native')}</dt><dd>✓</dd>")
        # The same method in the other language, matched by position: the structs come in the
        # same order in both languages, and so do their methods.
        other_code = self.other(code)
        counterpart = ""
        twin = next((candidate for candidate in self.modules[other_code] if candidate.name == module.name), None)
        if twin and len(twin.structs) == len(module.structs):
            twin_struct = twin.structs[module.structs.index(struct)]
            if len(twin_struct.methods) == len(struct.methods):
                counterpart = f"{twin_struct.name}.{twin_struct.methods[struct.methods.index(method)].name}"
        if counterpart:
            rows.append(f"<dt>{t('counterpart')}</dt><dd><a href=\"../../{other_code}/modules/{module.name}.html#fn-{self.escape(counterpart)}\""
                        f" lang=\"{other_code}\" dir=\"{'rtl' if other_code == 'ps' else 'ltr'}\"><code>{self.escape(counterpart)}</code></a></dd>")
        self.add_search(code, "function", f"{struct.name}.{method.name}", f"modules/{module.name}.html", anchor, method.doc)
        return (f"<article class=\"entry function method\" id=\"{anchor}\">"
                f"<h3><code>{self.escape(method.name)}</code><a class=\"anchor\" href=\"#{anchor}\">#</a></h3>"
                f"<div class=\"description\">{self.paragraphs(method.doc)}</div>"
                f"<dl>{''.join(rows)}</dl></article>")

    def function_entry(self, code: str, module: Module, function: Function) -> str:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        anchor = f"fn-{function.name}"
        rows = [f"<dt>{t('signature')}</dt><dd>{self.code_span(function.signature)}</dd>"]
        if function.type_parameters:
            rows.append(f"<dt>{t('type_parameters')}</dt><dd>" + ", ".join(f"<code>{self.escape(p)}</code>" for p in function.type_parameters) + "</dd>")
        if function.parameters:
            table = "".join(f"<tr><td><code>{self.escape(p.name)}</code></td><td><code dir=\"auto\">{self.escape(p.type)}</code></td></tr>"
                            for p in function.parameters)
            rows.append(f"<dt>{t('parameters')}</dt><dd><table class=\"api compact\"><tbody>{table}</tbody></table></dd>")
        if function.returns:
            rows.append(f"<dt>{t('returns')}</dt><dd><code dir=\"auto\">{self.escape(function.returns)}</code></dd>")
        if function.native:
            rows.append(f"<dt>{t('native')}</dt><dd>✓</dd>")
        index = module.functions.index(function)
        counterpart = self.counterpart_declaration(code, module, "function", index)
        other_code = self.other(code)
        if counterpart:
            rows.append(f"<dt>{t('counterpart')}</dt><dd><a href=\"../../{other_code}/modules/{module.name}.html#fn-{self.escape(counterpart)}\""
                        f" lang=\"{other_code}\" dir=\"{'rtl' if other_code == 'ps' else 'ltr'}\"><code>{self.escape(counterpart)}</code></a></dd>")
        self.add_search(code, "function", function.name, f"modules/{module.name}.html", anchor, function.doc)
        return (f"<article class=\"entry function\" id=\"{anchor}\">"
                f"<h3><code>{self.escape(function.name)}</code><a class=\"anchor\" href=\"#{anchor}\">#</a></h3>"
                f"<div class=\"description\">{self.paragraphs(function.doc)}</div>"
                f"<dl>{''.join(rows)}</dl></article>")

    def paragraphs(self, text: str) -> str:
        """Comment text as paragraphs; `code` spans and "quoted" names are kept readable."""
        out = []
        for paragraph in re.split(r"\n\s*\n", text.strip()):
            if not paragraph.strip():
                continue
            joined = " ".join(line.strip() for line in paragraph.split("\n"))
            out.append(f"<p>{markdown_html.Renderer().inline(joined)}</p>")
        return "\n".join(out)

    def instructions_page(self, code: str) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        parts = [f"<p class=\"lead\">{t('instructions_intro')}</p>",
                 f"<p><a class=\"button\" href=\"guides/bytecode.html\">{t('guide_link')} →</a></p>",
                 "<table class=\"api\"><thead><tr>"
                 f"<th>{t('opcode')}</th><th>{t('mnemonic')}</th><th>{t('operand_bytes')}</th><th>{t('stack_effect')}</th><th>{t('description')}</th>"
                 "</tr></thead><tbody>"]
        for opcode in self.info[code]["opcodes"]:
            anchor = f"op-{opcode['mnemonic']}"
            parts.append(f"<tr id=\"{anchor}\"><td><code>{opcode['code']}</code></td><td><code>{self.escape(opcode['mnemonic'])}</code></td>"
                         f"<td>{opcode['operand_bytes']}</td><td><code dir=\"ltr\">{self.escape(opcode['stack_effect'])}</code></td>"
                         f"<td>{self.escape(opcode['description'])}</td></tr>")
            self.add_search(code, "instruction", opcode["mnemonic"], "instructions.html", anchor, opcode["description"])
        parts.append("</tbody></table>")
        return Page("instructions.html", t("instructions"), "\n".join(parts), section="instructions")

    def guide_source(self, code: str, name: str) -> tuple[Path, bool]:
        """The markdown file for a guide in a language, and whether it is a translation."""
        if code != "en":
            translated = DOCS / code / name
            if translated.is_file():
                return translated, True
        return DOCS / name, code == "en"

    def guide_title(self, code: str, name: str) -> str:
        path, _ = self.guide_source(code, name)
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("# "):
                return line[2:].strip()
        return name

    def guides_index_page(self, code: str) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        parts = [f"<p class=\"lead\">{t('guides_intro')}</p>", "<ol class=\"guides\">"]
        for name in GUIDES:
            title = self.guide_title(code, name)
            page = name[:-3] + ".html"
            parts.append(f"<li><a href=\"{page}\">{self.escape(title)}</a></li>")
        parts.append("</ol>")
        return Page("guides/index.html", t("guides"), "\n".join(parts), section="guides")

    def guide_page(self, code: str, name: str) -> Page:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        path, translated = self.guide_source(code, name)
        text = path.read_text(encoding="utf-8")
        page_dir = f"{code}/guides"

        def link(target: str) -> str:
            return self.rewrite_link(target, path, page_dir, code)

        rendered = markdown_html.render(text, link, lambda language, body: self.fenced(code, language, body))
        body = rendered.html
        # The document's own first heading becomes the page title; drop it from the body.
        # (A Pashto document opens with its <div dir="rtl"> wrapper; the heading follows it.)
        body = re.sub(r"^((?:<div[^>]*>\n)?)<h1 [^>]*>.*?</h1>\n?", r"\1", body, count=1, flags=re.S)
        if not translated:
            body = f"<p class=\"note\">{t('untranslated')}</p>\n" + body
        toc = [(heading.id, heading.text) for heading in rendered.headings if heading.level == 2]
        title = rendered.title or name
        self.add_search(code, "guide", title, f"guides/{name[:-3]}.html", "", title)
        for heading in rendered.headings:
            if heading.level in (2, 3):
                self.add_search(code, "guide", f"{title} › {heading.text}", f"guides/{name[:-3]}.html", heading.id, title)
        page = Page(f"guides/{name[:-3]}.html", title, body, toc, section="guides")
        if not translated:
            page.body = f'<div lang="en" dir="ltr">{page.body}</div>'
        return page

    def rewrite_link(self, target: str, source: Path, page_dir: str, code: str) -> str:
        """Where a link in a guide should lead in the generated site."""
        if re.match(r"^[a-z]+:", target) or target.startswith("#"):
            return target
        path_part, _, fragment = target.partition("#")
        fragment = f"#{fragment}" if fragment else ""
        resolved = Path(os.path.normpath(source.parent / path_part))
        try:
            relative = resolved.relative_to(ROOT)
        except ValueError:
            return target
        parts = relative.parts
        # A guide of the reference (in either language) becomes its page.
        if resolved.suffix == ".md" and len(parts) >= 2 and parts[0] == "docs":
            # docs/x.md is the English original; docs/<lang>/x.md a translation.
            language = parts[1] if len(parts) == 3 and parts[1] in LANGUAGES else "en"
            name = parts[-1]
            if name == "README.md":
                page = "guides/index.html"
            elif name in GUIDES:
                page = f"guides/{name[:-3]}.html"
            else:
                return f"{REPOSITORY}/blob/master/{relative.as_posix()}{fragment}"
            if language == code:
                return f"{page[len('guides/'):]}{fragment}"   # a sibling in the same guides/ directory
            return f"../../{language}/{page}{fragment}"
        if resolved.is_file() and parts[0] == "docs" and resolved.suffix.lower() in (".png", ".jpg", ".jpeg", ".gif", ".svg"):
            # An image in docs/: reached from docs/api/<lang>/guides/ by going up three levels.
            return "../../../" + "/".join(parts[1:])
        return f"{REPOSITORY}/blob/master/{relative.as_posix()}{fragment}"

    # ------------------------------------------------------------ layout

    def layout(self, code: str, page: Page) -> str:
        t = lambda key, **values: self.text(code, key, **values)  # noqa: E731
        depth = page.path.count("/")
        root = "../" * depth               # the language root
        site_root = "../" * (depth + 1)    # docs/api/
        other_code = self.other(code)
        other_href = f"{site_root}{other_code}/{page.path}"
        direction = "rtl" if self.info[code]["right_to_left"] else "ltr"
        nav_items = [
            ("overview", "index.html", t("overview")),
            ("language", "language.html", t("language")),
            ("builtins", "builtins.html", t("builtins")),
            ("modules", "modules/index.html", t("modules")),
            ("guides", "guides/index.html", t("guides")),
            ("instructions", "instructions.html", t("instructions")),
        ]
        nav = []
        for key, href, label in nav_items:
            current = ' class="current"' if key == page.section else ""
            nav.append(f"<li{current}><a href=\"{root}{href}\">{self.escape(label)}</a></li>")
            if key == "modules":
                nav.append("<ul class=\"sub\">" + "".join(
                    f"<li><a href=\"{root}modules/{m.name}.html\"><code>{self.escape(m.name)}</code></a></li>"
                    for m in self.modules[code]) + "</ul>")
            if key == "guides":
                nav.append("<ul class=\"sub\">" + "".join(
                    f"<li><a href=\"{root}guides/{name[:-3]}.html\">{self.escape(self.guide_title(code, name))}</a></li>"
                    for name in GUIDES) + "</ul>")
        toc_html = ""
        if page.toc:
            toc_html = (f"<aside class=\"toc\"><h2>{t('on_this_page')}</h2><ul>"
                        + "".join(f"<li><a href=\"#{anchor}\">{self.escape(text)}</a></li>" for anchor, text in page.toc)
                        + "</ul></aside>")
        other_direction = "rtl" if other_code == "ps" else "ltr"
        crumbs = [f"<a href=\"{root}index.html\">{t('overview')}</a>"]
        for key, href, label in nav_items:
            if key == page.section and key != "overview":
                if page.path == href:
                    crumbs.append(self.escape(label))
                else:
                    crumbs.append(f"<a href=\"{root}{href}\">{self.escape(label)}</a>")
                    crumbs.append(self.escape(page.title))
        return f"""<!DOCTYPE html>
<html lang="{code}" dir="{direction}">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{self.escape(page.title)} · {self.escape(t('site'))}</title>
<meta name="description" content="{self.escape(page.title)} - {self.escape(t('site'))}">
<link rel="alternate" hreflang="{other_code}" href="{other_href}">
<link rel="stylesheet" href="{site_root}assets/style.css">
<script src="{root}search-index.js" defer></script>
<script src="{site_root}assets/app.js" defer></script>
</head>
<body>
<a class="skip" href="#content">{self.escape(t('skip'))}</a>
<header class="top">
<button class="menu-button" type="button" aria-label="{self.escape(t('menu'))}" data-menu>☰</button>
<a class="brand" href="{root}index.html"><span class="logo">⚡</span> <span>{self.escape(t('site'))}</span></a>
<form class="search" role="search" onsubmit="return false">
<input type="search" id="search" placeholder="{self.escape(t('search_hint'))}" aria-label="{self.escape(t('search'))}" autocomplete="off"
 data-no-results="{self.escape(t('no_results'))}" data-kinds='{json.dumps(t('results_kind'), ensure_ascii=False)}'>
<ul id="search-results" class="results" hidden></ul>
</form>
<a class="language-switch" href="{other_href}" lang="{other_code}" dir="{other_direction}" title="{self.escape(t('other_language_title'))}">{self.escape(t('other_language'))}</a>
</header>
<div class="shell">
<nav class="side" aria-label="{self.escape(t('contents'))}">
<ul>
{''.join(nav)}
</ul>
<p class="version">{self.escape(t('version', version=self.version))}</p>
</nav>
<main id="content">
<p class="crumbs">{' › '.join(crumbs)}</p>
<h1>{self.escape(page.title)}</h1>
{toc_html}
{page.body}
<footer><p>{t('generated')}</p></footer>
</main>
</div>
</body>
</html>
"""


# ---------------------------------------------------------------------- style and script

STYLE = """/* Thunder API documentation - generated by tools/apidoc.py; edit the STYLE there. */
:root {
  --bg: #ffffff; --fg: #1f2328; --muted: #59636e; --line: #d8dee4; --accent: #0969da; --accent-bg: #ddf4ff;
  --code-bg: #f6f8fa; --side-bg: #f6f8fa; --top-bg: #24292f; --top-fg: #ffffff; --badge: #8250df;
  --font: "Segoe UI", "Noto Sans", "Vazirmatn", system-ui, -apple-system, Roboto, Arial, sans-serif;
  /* The IDE's fonts: JetBrains Mono has no Arabic glyphs, so Pashto letters fall through to Vazirmatn. */
  --mono: "JetBrains Mono", "Vazirmatn", ui-monospace, Consolas, "DejaVu Sans Mono", monospace;
  --tok-keyword: #0033b3; --tok-type: #008080; --tok-literal: #871094; --tok-builtin: #7a3e9d;
  --tok-number: #1750eb; --tok-string: #067d17; --tok-comment: #8c8c8c;
  --editor-bg: #ffffff; --output-bg: #ffffff;
}
@media (prefers-color-scheme: dark) {
  :root { --bg: #0d1117; --fg: #e6edf3; --muted: #9198a1; --line: #30363d; --accent: #58a6ff; --accent-bg: #121d2f;
          --code-bg: #161b22; --side-bg: #0d1117; --top-bg: #010409; --badge: #d2a8ff;
          --tok-keyword: #7aa2ff; --tok-type: #4ec9b0; --tok-literal: #d7a6ff; --tok-builtin: #c8a2ff;
          --tok-number: #9cc4ff; --tok-string: #8fd18f; --tok-comment: #9aa0a6;
          --editor-bg: #0d1117; --output-bg: #0d1117; }
}
@font-face { font-family: "JetBrains Mono"; src: url("fonts/JetBrainsMono.ttf") format("truetype"); font-weight: 100 800; font-display: swap; }
@font-face { font-family: "Vazirmatn"; src: url("fonts/Vazirmatn.ttf") format("truetype"); font-weight: 100 900; font-display: swap; }
html[lang="ps"] { --font: "Vazirmatn", "Segoe UI", "Noto Sans", system-ui, sans-serif; }
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body { margin: 0; font: 16px/1.6 var(--font); color: var(--fg); background: var(--bg); }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
code, pre { font-family: var(--mono); font-size: 0.92em; }
code { background: var(--code-bg); padding: 0.1em 0.35em; border-radius: 4px; }
pre { background: var(--code-bg); padding: 0.9rem 1.1rem; border-radius: 8px; overflow: auto; border: 1px solid var(--line); line-height: 1.45; }
pre code { background: none; padding: 0; }
html[dir="rtl"] pre { direction: rtl; text-align: right; }
/* Thunder source: the editor's colours on the editor's white page. */
pre.thunder { background: var(--editor-bg); }
.tok-keyword { color: var(--tok-keyword); font-weight: 600; }
.tok-type { color: var(--tok-type); }
.tok-literal { color: var(--tok-literal); }
.tok-builtin { color: var(--tok-builtin); }
.tok-number { color: var(--tok-number); }
.tok-string { color: var(--tok-string); }
.tok-comment { color: var(--tok-comment); font-style: italic; }
/* Program output, as the IDE's Output panel shows it: a plain panel with its title, each line
   in the direction of its own first letter, so Pashto and English lines sit on their own sides. */
pre.output { background: var(--output-bg); padding-top: 1.9rem; position: relative; }
pre.output::before { content: attr(data-label); position: absolute; top: 0.35rem; inset-inline-start: 0.9rem; font: 600 0.7rem/1 var(--font);
                     letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); }
pre.output .out-line { display: block; unicode-bidi: plaintext; }
html[dir="rtl"] pre.output .out-line { text-align: right; }
html[dir="ltr"] pre.output .out-line { text-align: left; }
.skip { position: absolute; top: -3rem; inset-inline-start: 0.5rem; background: var(--accent); color: #fff; padding: 0.4rem 0.8rem; border-radius: 0 0 6px 6px; }
.skip:focus { top: 0; }

/* header */
.top { position: sticky; top: 0; z-index: 10; display: flex; align-items: center; gap: 1rem; padding: 0.55rem 1.2rem;
       background: var(--top-bg); color: var(--top-fg); box-shadow: 0 1px 0 rgba(255,255,255,0.08); }
.top a { color: var(--top-fg); }
.brand { font-weight: 600; font-size: 1.05rem; white-space: nowrap; display: flex; align-items: center; gap: 0.4rem; }
.logo { color: #f5c518; }
.menu-button { display: none; background: none; border: 1px solid rgba(255,255,255,0.4); color: var(--top-fg); border-radius: 6px; padding: 0.2rem 0.55rem; font-size: 1.1rem; }
.search { position: relative; flex: 1; max-width: 34rem; margin-inline-start: auto; }
.search input { width: 100%; padding: 0.45rem 0.8rem; border-radius: 6px; border: 1px solid rgba(255,255,255,0.25); background: rgba(255,255,255,0.1); color: var(--top-fg); font: inherit; }
.search input::placeholder { color: rgba(255,255,255,0.6); }
.search input:focus { outline: 2px solid var(--accent); background: rgba(255,255,255,0.16); }
.results { position: absolute; top: calc(100% + 0.3rem); inset-inline: 0; margin: 0; padding: 0.3rem; list-style: none; background: var(--bg); color: var(--fg);
           border: 1px solid var(--line); border-radius: 8px; box-shadow: 0 8px 24px rgba(0,0,0,0.2); max-height: 60vh; overflow: auto; }
.results li a { display: block; padding: 0.4rem 0.7rem; border-radius: 6px; color: var(--fg); }
.results li a:hover, .results li.active a { background: var(--accent-bg); text-decoration: none; }
.results .kind { display: inline-block; min-width: 5.5em; font-size: 0.75rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.03em; }
.results .summary { display: block; color: var(--muted); font-size: 0.85rem; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.results .empty { padding: 0.5rem 0.7rem; color: var(--muted); }
.language-switch { border: 1px solid rgba(255,255,255,0.4); border-radius: 999px; padding: 0.25rem 0.9rem; white-space: nowrap; font-weight: 500; }
.language-switch:hover { background: rgba(255,255,255,0.12); text-decoration: none; }

/* layout */
.shell { display: grid; grid-template-columns: 17rem minmax(0, 1fr); min-height: calc(100vh - 3.2rem); }
.side { background: var(--side-bg); border-inline-end: 1px solid var(--line); padding: 1.2rem 0.9rem; position: sticky; top: 3.2rem; height: calc(100vh - 3.2rem); overflow: auto; }
.side ul { list-style: none; margin: 0; padding: 0; }
.side li a { display: block; padding: 0.3rem 0.7rem; border-radius: 6px; color: var(--fg); }
.side li a:hover { background: var(--accent-bg); text-decoration: none; }
.side li.current > a { background: var(--accent-bg); color: var(--accent); font-weight: 600; }
.side ul.sub { margin: 0 0 0.4rem 0; padding-inline-start: 0.9rem; border-inline-start: 2px solid var(--line); margin-inline-start: 0.9rem; }
.side ul.sub a { font-size: 0.92rem; color: var(--muted); padding: 0.2rem 0.6rem; }
.side .version { margin: 1.2rem 0.7rem 0; color: var(--muted); font-size: 0.85rem; }
main { padding: 1.6rem 3rem 3rem; max-width: 64rem; }
.crumbs { color: var(--muted); font-size: 0.9rem; margin: 0 0 0.3rem; }
h1 { font-size: 2rem; margin: 0.2rem 0 1rem; letter-spacing: -0.01em; }
h2 { font-size: 1.45rem; margin: 2.2rem 0 0.8rem; padding-bottom: 0.3rem; border-bottom: 1px solid var(--line); }
h3 { font-size: 1.15rem; margin: 1.6rem 0 0.5rem; }
h2 .anchor, h3 .anchor, h4 .anchor, h1 .anchor { visibility: hidden; margin-inline-start: 0.4rem; color: var(--muted); font-weight: 400; }
h1:hover .anchor, h2:hover .anchor, h3:hover .anchor, h4:hover .anchor { visibility: visible; }
.lead { font-size: 1.08rem; color: var(--muted); }
.note { background: var(--accent-bg); border-inline-start: 4px solid var(--accent); padding: 0.6rem 1rem; border-radius: 6px; }
.button { display: inline-block; border: 1px solid var(--accent); border-radius: 6px; padding: 0.3rem 0.9rem; font-weight: 500; }
.button:hover { background: var(--accent-bg); text-decoration: none; }
table { border-collapse: collapse; width: 100%; margin: 0.8rem 0 1.2rem; display: block; overflow-x: auto; }
th, td { border: 1px solid var(--line); padding: 0.45rem 0.7rem; text-align: start; vertical-align: top; }
th { background: var(--code-bg); }
table.compact { margin: 0.2rem 0; width: auto; }
table.compact td { padding: 0.25rem 0.6rem; }
blockquote { margin: 0.8rem 0; padding: 0.2rem 1rem; border-inline-start: 4px solid var(--line); color: var(--muted); }
img { max-width: 100%; height: auto; border-radius: 6px; }
hr { border: 0; border-top: 1px solid var(--line); margin: 2rem 0; }
footer { margin-top: 3rem; padding-top: 1rem; border-top: 1px solid var(--line); color: var(--muted); font-size: 0.85rem; }

/* overview */
.cards { display: grid; grid-template-columns: repeat(auto-fill, minmax(16rem, 1fr)); gap: 1rem; margin: 1.5rem 0; }
.card { display: block; border: 1px solid var(--line); border-radius: 10px; padding: 1rem 1.2rem; color: var(--fg); background: var(--bg); transition: border-color 0.15s, transform 0.15s; }
.card:hover { border-color: var(--accent); text-decoration: none; transform: translateY(-1px); }
.card h2 { border: 0; margin: 0 0 0.3rem; font-size: 1.15rem; padding: 0; }
.card p { margin: 0.2rem 0; color: var(--muted); font-size: 0.95rem; }
.card .meta { font-size: 0.85rem; }
.stats { display: flex; flex-wrap: wrap; gap: 1rem; margin: 1rem 0 2rem; }
.stat { border: 1px solid var(--line); border-radius: 10px; padding: 0.7rem 1.2rem; min-width: 9rem; }
.stat .number { display: block; font-size: 1.7rem; font-weight: 600; color: var(--accent); }
.stat .label { color: var(--muted); font-size: 0.85rem; }

/* API entries */
.index { display: flex; flex-wrap: wrap; gap: 0.35rem 0.6rem; margin: 0.6rem 0 1.2rem; }
.index a code { background: var(--code-bg); }
.entry { border: 1px solid var(--line); border-radius: 10px; padding: 0.9rem 1.2rem 0.6rem; margin: 0.9rem 0; scroll-margin-top: 4.2rem; }
.entry:target { border-color: var(--accent); box-shadow: 0 0 0 3px var(--accent-bg); }
.entry h3 { margin: 0 0 0.4rem; font-size: 1.1rem; }
.entry h3 code { background: none; padding: 0; font-size: 1em; color: var(--accent); }
.entry .description p { margin: 0.3rem 0 0.6rem; }
.entry dl { display: grid; grid-template-columns: max-content 1fr; gap: 0.3rem 1.2rem; margin: 0.4rem 0 0.4rem; }
.entry dt { color: var(--muted); font-size: 0.85rem; text-transform: uppercase; letter-spacing: 0.03em; padding-top: 0.15rem; }
.entry dd { margin: 0; }
.badge { display: inline-block; font-size: 0.7rem; text-transform: uppercase; letter-spacing: 0.05em; color: #fff; background: var(--badge); border-radius: 4px; padding: 0.05rem 0.4rem; vertical-align: middle; }
.import code { font-size: 1rem; }
.section-doc { color: var(--muted); }
tr:target td { background: var(--accent-bg); }
.toc { float: inline-end; width: 15rem; margin: 0 0 1rem 1.5rem; padding: 0.6rem 1rem; border: 1px solid var(--line); border-radius: 10px; font-size: 0.9rem; }
html[dir="rtl"] .toc { margin: 0 1.5rem 1rem 0; }
.toc h2 { border: 0; margin: 0 0 0.3rem; font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.05em; color: var(--muted); padding: 0; }
.toc ul { margin: 0; padding-inline-start: 1rem; }
ol.guides li { margin: 0.3rem 0; }
.redirect main { padding: 4rem; text-align: center; }

@media (max-width: 900px) {
  .shell { grid-template-columns: 1fr; }
  .side { position: fixed; inset-inline-start: 0; top: 3.2rem; width: 17rem; transform: translateX(-100%); transition: transform 0.2s; z-index: 9; }
  html[dir="rtl"] .side { transform: translateX(100%); }
  body.menu-open .side { transform: none; }
  .menu-button { display: inline-block; }
  main { padding: 1.2rem 1.2rem 2rem; }
  .toc { float: none; width: auto; margin: 0 0 1rem; }
  .brand span:last-child { display: none; }
}
@media print { .top, .side, .toc { display: none; } .shell { display: block; } main { max-width: none; padding: 0; } }
"""

SCRIPT = """// Thunder API documentation - generated by tools/apidoc.py; edit the SCRIPT there.
(function () {
    "use strict";
    var input = document.getElementById("search");
    var list = document.getElementById("search-results");
    var menu = document.querySelector("[data-menu]");
    if (menu) {
        menu.addEventListener("click", function () { document.body.classList.toggle("menu-open"); });
    }
    if (!input || !list) { return; }
    var kinds = {};
    try { kinds = JSON.parse(input.getAttribute("data-kinds") || "{}"); } catch (e) { kinds = {}; }
    var root = (document.querySelector('script[src$="search-index.js"]') || {}).getAttribute
        ? document.querySelector('script[src$="search-index.js"]').getAttribute("src").replace(/search-index\\.js$/, "") : "";
    var active = -1;

    function fold(text) {
        return String(text).toLowerCase().replace(/[\\u0643]/g, "\\u06a9").replace(/[\\u06af]/g, "\\u06ab").replace(/[\\u064a\\u0649]/g, "\\u06cc").replace(/_/g, " ");
    }
    function score(entry, query) {
        var name = fold(entry.name), summary = fold(entry.summary || "");
        if (name === query) { return 0; }
        if (name.indexOf(query) === 0) { return 1; }
        if (name.indexOf(query) >= 0) { return 2; }
        if (summary.indexOf(query) >= 0) { return 3; }
        return -1;
    }
    function render(results, query) {
        list.innerHTML = "";
        active = -1;
        if (!query) { list.hidden = true; return; }
        if (!results.length) {
            var empty = document.createElement("li");
            empty.className = "empty";
            empty.textContent = input.getAttribute("data-no-results") || "";
            list.appendChild(empty);
            list.hidden = false;
            return;
        }
        results.forEach(function (entry) {
            var item = document.createElement("li");
            var link = document.createElement("a");
            link.href = root + entry.page;
            var kind = document.createElement("span");
            kind.className = "kind";
            kind.textContent = kinds[entry.kind] || entry.kind;
            var name = document.createElement("code");
            name.textContent = entry.name;
            var summary = document.createElement("span");
            summary.className = "summary";
            summary.textContent = entry.summary || "";
            link.appendChild(kind);
            link.appendChild(name);
            link.appendChild(summary);
            item.appendChild(link);
            list.appendChild(item);
        });
        list.hidden = false;
    }
    function search() {
        var query = fold(input.value.trim());
        var index = window.THUNDER_INDEX || [];
        if (!query) { render([], ""); return; }
        var scored = [];
        index.forEach(function (entry) {
            var s = score(entry, query);
            if (s >= 0) { scored.push({ s: s, entry: entry }); }
        });
        scored.sort(function (a, b) { return a.s - b.s || a.entry.name.length - b.entry.name.length; });
        render(scored.slice(0, 30).map(function (x) { return x.entry; }), query);
    }
    input.addEventListener("input", search);
    input.addEventListener("focus", search);
    input.addEventListener("keydown", function (event) {
        var items = list.querySelectorAll("li a");
        if (event.key === "ArrowDown" || event.key === "ArrowUp") {
            if (!items.length) { return; }
            event.preventDefault();
            active = (active + (event.key === "ArrowDown" ? 1 : -1) + items.length) % items.length;
            Array.prototype.forEach.call(list.children, function (li, i) { li.classList.toggle("active", i === active); });
            items[active].scrollIntoView({ block: "nearest" });
        } else if (event.key === "Enter") {
            if (active >= 0 && items[active]) { window.location.href = items[active].href; }
            else if (items.length) { window.location.href = items[0].href; }
        } else if (event.key === "Escape") {
            list.hidden = true;
            input.blur();
        }
    });
    document.addEventListener("click", function (event) {
        if (!list.contains(event.target) && event.target !== input) { list.hidden = true; }
    });
    document.addEventListener("keydown", function (event) {
        if (event.key === "/" && document.activeElement !== input && !/input|textarea/i.test(document.activeElement.tagName)) {
            event.preventDefault();
            input.focus();
        }
    });
}());
"""


# ---------------------------------------------------------------------- driver

def read_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\s*\([^)]*?VERSION\s+(\d+\.\d+\.\d+)", text, re.S)
    return match.group(1) if match else "0.0.0"


def generate() -> dict[str, str | bytes]:
    return Site(DEFAULT_OUTPUT).build()


def write(files: dict[str, str | bytes], output: Path) -> None:
    # Only what the generator writes lives in the output directory: stale pages go.
    if output.is_dir():
        for existing in sorted(output.rglob("*"), reverse=True):
            relative = existing.relative_to(output).as_posix()
            if existing.is_file() and relative not in files:
                existing.unlink()
            elif existing.is_dir() and not any(existing.iterdir()):
                existing.rmdir()
    for relative, contents in files.items():
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(contents, bytes):
            path.write_bytes(contents)
        else:
            path.write_text(contents, encoding="utf-8", newline="\n")


def check(files: dict[str, str | bytes], output: Path) -> list[str]:
    """The files that differ between what would be written and what is there."""
    differences = []
    for relative, contents in files.items():
        path = output / relative
        if not path.is_file():
            differences.append(relative)
            continue
        current = path.read_bytes() if isinstance(contents, bytes) else path.read_text(encoding="utf-8")
        if current != contents:
            differences.append(relative)
    if output.is_dir():
        for existing in output.rglob("*"):
            if existing.is_file() and existing.relative_to(output).as_posix() not in files:
                differences.append(existing.relative_to(output).as_posix() + " (stale)")
    return differences


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate the Thunder API documentation (HTML) in English and Pashto.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help=f"where to write (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--check", action="store_true", help="do not write; fail if the output is out of date")
    arguments = parser.parse_args(argv)
    files = generate()
    if arguments.check:
        differences = check(files, arguments.output)
        if differences:
            print("The API documentation is out of date:", file=sys.stderr)
            for name in differences[:20]:
                print(f"  {name}", file=sys.stderr)
            if len(differences) > 20:
                print(f"  ... and {len(differences) - 20} more", file=sys.stderr)
            print("Run: python3 tools/apidoc.py", file=sys.stderr)
            return 1
        print(f"The API documentation is up to date ({len(files)} files).")
        return 0
    write(files, arguments.output)
    print(f"Wrote {len(files)} files to {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
