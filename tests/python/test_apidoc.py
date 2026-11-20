"""The API documentation generator (tools/apidoc.py) and the site it writes to docs/api/.

The site is committed, like the extension's generated grammar; the first test here fails when
it no longer matches what the generator would write. The others check the pieces: the
Markdown renderer, the parsing of the SDK modules' declarations and comments, and the site
itself - every page in both languages, every internal link and anchor, every built-in and
every public SDK function present, and the language switch on every page leading to the same
page in the other language.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from html.parser import HTMLParser
from pathlib import Path

from tests.python import support

sys.path.insert(0, str(support.ROOT / "tools"))

import apidoc  # noqa: E402
import markdown_html  # noqa: E402

GENERATOR = support.ROOT / "tools" / "apidoc.py"
SITE = support.ROOT / "docs" / "api"


class MarkdownTests(unittest.TestCase):
    """The renderer covers what the documents use, and nothing a document writes becomes markup."""

    def render(self, text: str) -> str:
        return markdown_html.render(text).html

    def test_headings_get_github_style_ids(self):
        rendered = markdown_html.render("# The Thunder Language Reference\n\n## 3. Types\n\n## 3. Types\n")
        self.assertEqual([h.id for h in rendered.headings], ["the-thunder-language-reference", "3-types", "3-types-1"])
        self.assertEqual(rendered.title, "The Thunder Language Reference")

    def test_pashto_headings_keep_their_letters(self):
        rendered = markdown_html.render("## ۸. جوړښتونه، ټاپلونه او نقشې\n")
        self.assertEqual(rendered.headings[0].id, "۸-جوړښتونه-ټاپلونه-او-نقشې")

    def test_inline_markup(self):
        html = self.render("Use `a < b` with **bold**, *it* and [a link](x.md#sec); new_stack stays.")
        self.assertIn("<code>a &lt; b</code>", html)
        self.assertIn("<strong>bold</strong>", html)
        self.assertIn("<em>it</em>", html)
        self.assertIn('<a href="x.md#sec">a link</a>', html)
        self.assertIn("new_stack stays", html, "underscores inside a name are not emphasis")

    def test_emphasis_may_span_a_code_span(self):
        html = self.render("1. **Next to the file that contains the `import`.** Then `x`.")
        self.assertIn("<strong>Next to the file that contains the <code>import</code>.</strong>", html)
        self.assertIn("Then <code>x</code>.", html)

    def test_a_fenced_block_may_be_rendered_by_the_caller(self):
        rendered = markdown_html.render("```thunder\nvar x = 1\n```\n\n```\nout\n```\n",
                                        code=lambda language, text: f"<pre data-lang='{language}'>{text}</pre>" if language == "thunder" else None)
        self.assertIn("<pre data-lang='thunder'>var x = 1</pre>", rendered.html)
        self.assertIn("<pre><code>out</code></pre>", rendered.html, "None keeps the default")

    def test_html_is_escaped_except_a_few_harmless_tags(self):
        html = self.render("e<sup>x</sup> and <script>alert(1)</script>")
        self.assertIn("e<sup>x</sup>", html)
        self.assertIn("&lt;script&gt;", html)

    def test_tables_with_alignment_and_escaped_pipes(self):
        html = self.render("| A | B |\n| --- | ---: |\n| `x` \\| y | 2 |\n")
        self.assertIn('<th style="text-align:right">B</th>', html)
        self.assertIn("<td><code>x</code> | y</td>", html)

    def test_code_fences_keep_their_text_verbatim(self):
        html = self.render("```th\nvar x = 1 < 2  # **not bold**\n```\n")
        self.assertIn('<pre><code class="language-th">var x = 1 &lt; 2  # **not bold**</code></pre>', html)

    def test_lists_nest_by_indentation_and_wrap(self):
        html = self.render("* one\n  continued\n* two\n\n  - nested\n\n1. first\n2. second\n")
        self.assertIn("<li>one continued</li>", html)
        self.assertIn("<ul>\n<li>nested</li>\n</ul></li>", html)
        self.assertIn("<ol>\n<li>first</li>\n<li>second</li>\n</ol>", html)

    def test_block_quotes_and_rules(self):
        html = self.render("> quoted\n\n---\n")
        self.assertIn("<blockquote>\n<p>quoted</p>\n</blockquote>", html)
        self.assertIn("<hr>", html)

    def test_every_reference_document_renders(self):
        for path in sorted((support.ROOT / "docs").glob("*.md")) + sorted((support.ROOT / "docs" / "ps").glob("*.md")):
            with self.subTest(document=path.name):
                rendered = markdown_html.render(path.read_text(encoding="utf-8"))
                self.assertTrue(rendered.title, "every document starts with a heading")
                self.assertNotIn("<script", rendered.html)


class ModuleParsingTests(unittest.TestCase):
    """The SDK modules' declarations and comments, in both languages."""

    @classmethod
    def setUpClass(cls):
        cls.site = apidoc.Site(SITE)

    def module(self, language: str, name: str) -> apidoc.Module:
        return next(module for module in self.site.modules[language] if module.name == name)

    def test_structs_their_members_and_their_comments(self):
        module = self.module("en", "datastructures")
        stack = next(struct for struct in module.structs if struct.name == "Stack")
        self.assertEqual(stack.type_parameters, ["T"])
        self.assertEqual(stack.fields, [], "the stack's _items is private: the site shows the methods")
        self.assertEqual(stack.constructor, [], "Stack() takes no parameters")
        push = next(method for method in stack.methods if method.name == "push")
        self.assertEqual([(p.name, p.type) for p in push.parameters], [("value", "T")])
        self.assertEqual(push.returns, "")
        self.assertEqual(push.doc, "Puts a value on top.")
        self.assertEqual(push.signature, "func push(value: T)")
        pop = next(method for method in stack.methods if method.name == "pop")
        self.assertEqual(pop.returns, "T")
        self.assertTrue(stack.section.startswith("Stack"), "declarations are grouped by the source's banners")
        node = next(struct for struct in module.structs if struct.name == "ListNode")
        self.assertIsNone(node.constructor, "a plain data struct has no constructor")
        self.assertEqual(node.methods, [])
        self.assertEqual([field.name for field in node.fields], ["value", "next"])

    def test_method_bodies_are_not_read_as_fields(self):
        for language in ("en", "ps"):
            for module in self.site.modules[language]:
                for struct in module.structs:
                    for member in struct.fields:
                        with self.subTest(language=language, struct=struct.name, field=member.name):
                            self.assertNotIn("(", member.name, "a body line was taken for a field")
                            self.assertNotIn("=", member.name + member.type)

    def test_internal_names_are_left_out(self):
        module = self.module("en", "datastructures")
        for struct in module.structs:
            self.assertFalse(any(method.name.startswith("_") for method in struct.methods),
                             f"{struct.name}: private methods are not listed")
        self.assertGreater(module.internal, 0, "the private methods are counted")

    def test_native_functions_methods_and_parameter_types_with_commas(self):
        sockets = self.module("en", "sockets")
        # The natives of the socket modules are private (socket._send); host's are public.
        address = next(function for function in sockets.functions if function.name == "host.address")
        self.assertTrue(address.native)
        self.assertEqual(address.returns, "string")
        self.assertFalse(any(function.name.split(".")[-1].startswith("_") for function in sockets.functions))
        # The struct inside a module block carries the module's name; its methods are its own.
        socket = next(struct for struct in sockets.structs if struct.name == "socket.Socket")
        send = next(method for method in socket.methods if method.name == "send")
        self.assertEqual(send.returns, "int")
        self.assertEqual([(p.name, p.type) for p in send.parameters], [("text", "string")])
        algorithms = self.module("en", "algorithms")
        for function in algorithms.functions:
            for parameter in function.parameters:
                self.assertTrue(parameter.name and parameter.type, f"{function.name}: {parameter}")

    def test_the_pashto_modules_parse_like_the_english_ones(self):
        for name in ("datastructures", "algorithms", "sockets", "testing", "concurrency"):
            with self.subTest(module=name):
                english, pashto = self.module("en", name), self.module("ps", name)
                self.assertEqual(len(english.functions), len(pashto.functions))
                self.assertEqual(len(english.structs), len(pashto.structs))
                for mine, theirs in zip(english.functions, pashto.functions):
                    self.assertEqual(len(mine.parameters), len(theirs.parameters), f"{mine.name} / {theirs.name}")
                    self.assertEqual(bool(mine.returns), bool(theirs.returns), f"{mine.name} / {theirs.name}")
                for ours, twins in zip(english.structs, pashto.structs):
                    self.assertEqual(len(ours.fields), len(twins.fields), f"{ours.name} / {twins.name}")
                    self.assertEqual(ours.constructor is None, twins.constructor is None, f"{ours.name} / {twins.name}")
                    self.assertEqual(len(ours.methods), len(twins.methods), f"{ours.name} / {twins.name}")
                    for mine, theirs in zip(ours.methods, twins.methods):
                        self.assertEqual(len(mine.parameters), len(theirs.parameters),
                                         f"{ours.name}.{mine.name} / {twins.name}.{theirs.name}")
                        self.assertEqual(bool(mine.returns), bool(theirs.returns),
                                         f"{ours.name}.{mine.name} / {twins.name}.{theirs.name}")
        stack = self.module("ps", "datastructures").structs[0]
        self.assertEqual(stack.name, "ډېرۍ")
        self.assertEqual(stack.methods[0].name, "ورزیات_کړه")
        self.assertEqual(stack.methods[0].doc, "یو ارزښت سر ته ږدي.")
        self.assertEqual(self.site.counterpart_declaration("ps", self.module("ps", "datastructures"), "struct", 0),
                         "Stack")

    def test_every_public_declaration_has_documentation(self):
        """A JavaDoc without text is a list of names; the comment above each declaration is the doc."""
        for language in ("en", "ps"):
            for module in self.site.modules[language]:
                for function in module.functions:
                    with self.subTest(language=language, module=module.name, function=function.name):
                        self.assertTrue(function.doc, f"{module.path}:{function.line} has no comment above it")
                for struct in module.structs:
                    for method in struct.methods:
                        with self.subTest(language=language, module=module.name,
                                          method=f"{struct.name}.{method.name}"):
                            self.assertTrue(method.doc, f"{module.path}:{method.line} has no comment above it")


class _Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids: set[str] = set()
        self.links: list[str] = []
        self.lang = ""
        self.dir = ""
        self.alternate = ""
        self.switch = ""

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a:
            self.ids.add(a["id"])
        if tag == "html":
            self.lang, self.dir = a.get("lang", ""), a.get("dir", "")
        if tag == "a" and "href" in a:
            self.links.append(a["href"])
            if "language-switch" in (a.get("class") or ""):
                self.switch = a["href"]
        if tag == "link" and a.get("rel") == "alternate":
            self.alternate = a.get("href", "")
        if tag in ("img", "script", "link") and (a.get("src") or a.get("href")):
            self.links.append(a.get("src") or a.get("href"))


class SiteTests(unittest.TestCase):
    """The committed site in docs/api/."""

    @classmethod
    def setUpClass(cls):
        cls.pages = {}
        for path in sorted(SITE.rglob("*.html")):
            parser = _Links()
            parser.feed(path.read_text(encoding="utf-8"))
            cls.pages[path] = parser
        cls.files = apidoc.generate()

    def test_the_committed_site_is_what_the_generator_writes(self):
        result = subprocess.run([sys.executable, str(GENERATOR), "--check"],
                                capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(result.returncode, 0, f"{result.stdout}{result.stderr}\nRun: python3 tools/apidoc.py")

    def test_both_languages_have_the_same_pages(self):
        english = sorted(p.relative_to(SITE / "en") for p in (SITE / "en").rglob("*.html"))
        pashto = sorted(p.relative_to(SITE / "ps") for p in (SITE / "ps").rglob("*.html"))
        self.assertEqual(english, pashto)
        self.assertGreaterEqual(len(english), 20)

    def test_every_page_is_html5_in_its_language_and_direction(self):
        for path, page in self.pages.items():
            with self.subTest(page=str(path.relative_to(SITE))):
                text = path.read_text(encoding="utf-8")
                self.assertTrue(text.startswith("<!DOCTYPE html>"))
                self.assertIn('<meta charset="UTF-8">', text)
                self.assertIn('name="viewport"', text)
                if path.parent == SITE:
                    continue  # the redirect page
                language = path.relative_to(SITE).parts[0]
                self.assertEqual(page.lang, language)
                self.assertEqual(page.dir, "rtl" if language == "ps" else "ltr")

    def test_every_internal_link_and_anchor_leads_somewhere(self):
        broken = []
        for path, page in self.pages.items():
            for href in page.links:
                if re.match(r"^[a-z]+:", href):
                    continue
                target_part, _, fragment = href.partition("#")
                target = path if not target_part else Path(os.path.normpath(path.parent / target_part))
                if not target.exists():
                    broken.append((str(path.relative_to(SITE)), href, "missing"))
                elif fragment and target.suffix == ".html" and fragment not in self.pages[target].ids:
                    broken.append((str(path.relative_to(SITE)), href, "no such anchor"))
        self.assertEqual(broken, [])

    def test_the_language_switch_leads_to_the_same_page_in_the_other_language(self):
        for path, page in self.pages.items():
            if path.parent == SITE:
                continue
            with self.subTest(page=str(path.relative_to(SITE))):
                relative = path.relative_to(SITE)
                other = "ps" if relative.parts[0] == "en" else "en"
                expected = SITE / other / Path(*relative.parts[1:])
                self.assertTrue(page.switch, "every page has a language switch")
                self.assertEqual(Path(os.path.normpath(path.parent / page.switch)), expected)
                self.assertEqual(Path(os.path.normpath(path.parent / page.alternate)), expected, "hreflang alternate too")

    def test_every_built_in_and_every_public_sdk_function_is_documented(self):
        site = apidoc.Site(SITE)
        for language in ("en", "ps"):
            builtins = (SITE / language / "builtins.html").read_text(encoding="utf-8")
            for builtin in site.info[language]["builtins"]:
                self.assertIn(f'id="fn-{builtin["canonical_name"]}"', builtins)
                self.assertIn(f"<code>{builtin['name']}</code>", builtins)
            for module in site.modules[language]:
                page = (SITE / language / "modules" / f"{module.name}.html").read_text(encoding="utf-8")
                for function in module.functions:
                    self.assertIn(f'id="fn-{function.name}"', page)
                for struct in module.structs:
                    self.assertIn(f'id="struct-{struct.name}"', page)
                    for method in struct.methods:
                        self.assertIn(f'id="fn-{struct.name}.{method.name}"', page)
            keywords = (SITE / language / "language.html").read_text(encoding="utf-8")
            for keyword in site.info[language]["keywords"]:
                self.assertIn(f'id="kw-{keyword["kind"]}"', keywords)
            instructions = (SITE / language / "instructions.html").read_text(encoding="utf-8")
            for opcode in site.info[language]["opcodes"]:
                self.assertIn(f'id="op-{opcode["mnemonic"]}"', instructions)

    def test_the_search_index_names_what_the_pages_hold(self):
        for language in ("en", "ps"):
            with self.subTest(language=language):
                text = (SITE / language / "search-index.js").read_text(encoding="utf-8")
                self.assertTrue(text.startswith("window.THUNDER_INDEX = "))
                index = json.loads(text[len("window.THUNDER_INDEX = "):].rstrip().rstrip(";"))
                kinds = {entry["kind"] for entry in index}
                self.assertEqual(kinds, {"builtin", "keyword", "type", "function", "struct", "module", "guide", "instruction"})
                for entry in index[::7]:
                    page, _, anchor = entry["page"].partition("#")
                    target = SITE / language / page
                    self.assertTrue(target.is_file(), entry)
                    if anchor:
                        self.assertIn(anchor, self.pages[target].ids, entry)

    def test_a_pashto_builtin_shows_its_english_name_and_links_to_it(self):
        page = (SITE / "ps" / "builtins.html").read_text(encoding="utf-8")
        self.assertIn("کرښه چاپ", page)
        self.assertIn('href="../en/builtins.html#fn-println"', page)
        english = (SITE / "en" / "builtins.html").read_text(encoding="utf-8")
        self.assertIn('href="../ps/builtins.html#fn-println"', english)

    def test_guides_link_to_each_other_and_to_the_repository(self):
        language = (SITE / "en" / "guides" / "language.html").read_text(encoding="utf-8")
        self.assertIn('href="library.html', language, "a sibling guide is a sibling page")
        self.assertIn('href="index.html"', language, "the SDK index became the guides index")
        index = (SITE / "en" / "guides" / "index.html").read_text(encoding="utf-8")
        self.assertIn("testing.html", index)
        pashto = (SITE / "ps" / "guides" / "language.html").read_text(encoding="utf-8")
        self.assertIn('href="../../en/guides/language.html#12-grammar-ebnf"', pashto,
                      "a translation's link to the English original reaches the English page")
        graphics = (SITE / "en" / "guides" / "graphics.html").read_text(encoding="utf-8")
        self.assertIn('src="../../../images/', graphics, "images stay in docs/images")

    def test_code_is_highlighted_with_the_editors_colours_in_both_languages(self):
        for language, keyword, builtin in (("en", "func", "println"), ("ps", "دنده", "کرښه چاپ")):
            with self.subTest(language=language):
                page = (SITE / language / "guides" / "language.html").read_text(encoding="utf-8")
                self.assertIn('<pre class="thunder">', page)
                self.assertIn(f'<span class="tok-keyword">{keyword}</span>', page)
                self.assertIn(f'<span class="tok-builtin">{builtin}</span>', page)
                self.assertIn('<span class="tok-comment">', page)
                self.assertIn('<span class="tok-string">', page)
        # Signatures on the API pages are highlighted too.
        self.assertIn('<span class="tok-type">int</span>', (SITE / "en" / "modules" / "datastructures.html").read_text(encoding="utf-8"))
        self.assertIn('<span class="tok-keyword">import</span>', (SITE / "en" / "modules" / "sockets.html").read_text(encoding="utf-8"))

    def test_the_palette_is_the_editors(self):
        try:
            from thunder_ide.qt import style
        except ImportError:  # pragma: no cover - no PySide6 in this interpreter
            self.skipTest("PySide6 is not available here")
        self.assertEqual(apidoc.SYNTAX_COLORS, style.SYNTAX_COLORS)
        css = (SITE / "assets" / "style.css").read_text(encoding="utf-8")
        for tag, colour in style.SYNTAX_COLORS.items():
            self.assertIn(f"--tok-{tag}: {colour};", css)

    def test_program_output_is_shown_like_the_output_panel(self):
        english = (SITE / "en" / "guides" / "library.html").read_text(encoding="utf-8")
        self.assertIn('<pre class="output" data-label="Output">', english)
        self.assertIn('<span class="out-line" dir="auto">', english)
        pashto = (SITE / "ps" / "guides" / "bytecode.html").read_text(encoding="utf-8")
        self.assertIn('<pre class="output" data-label="پایله">', pashto)

    def test_the_ides_fonts_travel_with_the_site(self):
        fonts = SITE / "assets" / "fonts"
        for name in ("JetBrainsMono.ttf", "Vazirmatn.ttf", "jetbrainsmono-OFL.txt", "vazirmatn-OFL.txt"):
            self.assertTrue((fonts / name).is_file(), name)
        self.assertEqual((fonts / "Vazirmatn.ttf").read_bytes(),
                         (support.ROOT / "ide" / "thunder_ide" / "fonts" / "vazirmatn" / "Vazirmatn[wght].ttf").read_bytes())
        css = (SITE / "assets" / "style.css").read_text(encoding="utf-8")
        self.assertIn('font-family: "JetBrains Mono"; src: url("fonts/JetBrainsMono.ttf")', css)
        self.assertIn('html[lang="ps"] { --font: "Vazirmatn"', css)

    def test_the_generator_writes_and_cleans_an_output_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "site"
            output.mkdir()
            stale = output / "en" / "old.html"
            stale.parent.mkdir(parents=True)
            stale.write_text("stale", encoding="utf-8")
            result = subprocess.run([sys.executable, str(GENERATOR), "--output", str(output)],
                                    capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(stale.exists(), "what the generator did not write is removed")
            self.assertTrue((output / "index.html").is_file())
            check = subprocess.run([sys.executable, str(GENERATOR), "--check", "--output", str(output)],
                                   capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(check.returncode, 0, check.stderr)
            (output / "en" / "index.html").write_text("changed", encoding="utf-8")
            check = subprocess.run([sys.executable, str(GENERATOR), "--check", "--output", str(output)],
                                   capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(check.returncode, 1)
            self.assertIn("en/index.html", check.stderr)


if __name__ == "__main__":
    unittest.main()
