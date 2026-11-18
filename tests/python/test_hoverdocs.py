"""API help on hover (core.hoverdocs): what the tooltip says for the name under the pointer."""

import json
import unittest
from pathlib import Path

from tests.python import support
from thunder_ide.core import hoverdocs
from thunder_ide.core.hoverdocs import HoverDoc, OtherLanguage, as_rich_text, describe, import_targets, word_at

# The bundled language information is a verbatim copy of `thunderc --language-info`, so the
# pure-text tests need no toolchain (test_vscode_extension checks the copy never drifts).
DATA = Path(support.ROOT) / "ide" / "vscode" / "data"
EN = json.loads((DATA / "language-info.en.json").read_text(encoding="utf-8"))
PS = json.loads((DATA / "language-info.ps.json").read_text(encoding="utf-8"))


PROGRAM = """# A program with documented declarations.

# Greets someone by name.
# The name is used as written.
func hello(name: string): string
    return "Hello, " + name + "!"

# How far a fall reaches in a time.
const gravity = 9.81

# A labelled button.
struct Button
    _title: string   # private: the label

    # Makes a button with its label.
    Button(title: string)
        _title = title

    # The label, for the screen.
    func get_title(): string
        return _title

println(hello("world"))
var b = Button("OK")
println(b.get_title())
"""


class WordAtTests(unittest.TestCase):
    def test_the_word_under_and_just_behind_the_pointer(self):
        text = "println(count)"
        self.assertEqual(word_at(text, 0)[0], "println")
        self.assertEqual(word_at(text, 6)[0], "println")
        self.assertEqual(word_at(text, 7)[0], "println")  # just behind the word still means it
        self.assertEqual(word_at(text, 8)[0], "count")
        self.assertEqual(word_at(text, 13)[0], "count")

    def test_a_member_knows_its_qualifier(self):
        word, qualifier, _, _ = word_at("math.sqrt(x)", 6)
        self.assertEqual((word, qualifier), ("sqrt", "math"))
        word, qualifier, _, _ = word_at("s.push(1)", 2)
        self.assertEqual((word, qualifier), ("push", "s"))
        word, qualifier, _, _ = word_at("println(x)", 1)
        self.assertEqual((word, qualifier), ("println", None))

    def test_numbers_strings_and_blanks_are_not_names(self):
        self.assertEqual(word_at("x = 123", 4)[0], "")
        self.assertEqual(word_at("   ", 1)[0], "")
        self.assertEqual(word_at("", 0)[0], "")

    def test_pashto_names_are_words_too(self):
        text = "کرښه چاپ(شمېر)"
        word, _, start, end = word_at(text, 1)
        self.assertEqual(word, "کرښه")
        self.assertEqual(text[start:end], "کرښه")


class BuiltinHoverTests(unittest.TestCase):
    def test_a_builtin_shows_its_api_reference(self):
        doc = describe("println(1)", 2, EN)
        self.assertIsNotNone(doc)
        self.assertEqual(doc.signature, "println(value, ...)")
        self.assertIn("newline", doc.body)
        self.assertEqual(doc.source, "")

    def test_a_module_builtin_needs_its_module(self):
        text = "var r = math.sqrt(2.0)"
        doc = describe(text, text.index("sqrt") + 1, EN)
        self.assertIsNotNone(doc)
        self.assertIn("sqrt", doc.signature)
        # A bare name never means a module's member, so there is nothing to show for it.
        self.assertIsNone(describe("sqrt(2.0)", 1, EN))

    def test_a_pashto_multiword_builtin_is_found_from_either_word(self):
        text = "کرښه چاپ(۱)"
        for index in (0, 5):
            doc = describe(text, index, PS)
            self.assertIsNotNone(doc, f"at {index}")
            self.assertIn("کرښه چاپ", doc.signature)

    def test_a_pashto_module_builtin(self):
        text = "متغیر ر = ریاضي.جذر(۲٫۰)"
        doc = describe(text, text.index("جذر") + 1, PS)
        self.assertIsNotNone(doc)
        self.assertEqual(doc.signature, "ریاضي.جذر(x: اعشاري): اعشاري")

    def test_a_pashto_signature_speaks_pashto_throughout(self):
        """The types, the function word and the comma follow the program's language."""
        text = "متغیر ل = ریاضي.توان(۲٫۰، ۳٫۰)"
        doc = describe(text, text.index("توان") + 1, PS)
        self.assertEqual(doc.signature, "ریاضي.توان(x: اعشاري، y: اعشاري): اعشاري")
        text = "پر هر یو([۱]، دوچنده)"
        doc = describe(text, 1, PS)
        self.assertEqual(doc.signature, "پر هر یو(a: T[]، f: دنده(T): U): U[]")
        # English signatures are left exactly as they are.
        text = "var y = math.pow(2.0, 3.0)"
        doc = describe(text, text.index("pow") + 1, EN)
        self.assertEqual(doc.signature, "math.pow(x: float, y: float): float")


class DeclarationHoverTests(unittest.TestCase):
    def test_a_functions_doc_comment_is_its_tooltip(self):
        index = PROGRAM.index("hello(\"world\")") + 1
        doc = describe(PROGRAM, index, EN)
        self.assertEqual(doc, HoverDoc("func hello(name: string): string",
                                       "Greets someone by name.\nThe name is used as written.", ""))

    def test_a_constant_a_struct_and_a_method(self):
        doc = describe(PROGRAM, PROGRAM.index("gravity") + 1, EN)
        self.assertEqual(doc.signature, "const gravity = 9.81")
        self.assertEqual(doc.body, "How far a fall reaches in a time.")

        doc = describe(PROGRAM, PROGRAM.index("Button(\"OK\")") + 1, EN)
        self.assertEqual(doc.signature, "struct Button")
        self.assertEqual(doc.body, "A labelled button.")

        index = PROGRAM.index("b.get_title()") + 3
        doc = describe(PROGRAM, index, EN)
        self.assertEqual(doc.signature, "func get_title(): string")
        self.assertEqual(doc.body, "The label, for the screen.")

    def test_a_field_after_a_dot_shows_its_trailing_comment(self):
        text = PROGRAM + "println(b._title)\n"
        index = text.rindex("_title") + 1
        doc = describe(text, index, EN)
        self.assertEqual(doc.signature, "_title: string")
        self.assertEqual(doc.body, "private: the label")

    def test_an_undocumented_name_still_shows_its_signature(self):
        text = "func twice(n: int): int\n    return n * 2\nprintln(twice(3))\n"
        doc = describe(text, text.index("twice(3)") + 1, EN)
        self.assertEqual(doc, HoverDoc("func twice(n: int): int", "", ""))

    def test_a_banner_or_directive_is_not_documentation(self):
        text = "# lang: en\n# ----------- section -----------\nfunc f(): int\n    return 1\nprintln(f())\n"
        doc = describe(text, text.index("f()") + 1, EN)
        self.assertEqual(doc.body, "")

    def test_an_unknown_name_shows_nothing(self):
        self.assertIsNone(describe("println(mystery)", 9, EN))

    def test_a_module_member_prefers_its_module(self):
        text = ("# The area module.\nmodule geometry\n    # The area of a circle.\n"
                "    func area(r: float): float\n        return 3.14 * r * r\n"
                "println(geometry.area(2.0))\n")
        doc = describe(text, text.rindex("area") + 1, EN)
        self.assertEqual(doc.signature, "func area(r: float): float")
        self.assertEqual(doc.body, "The area of a circle.")


class ImportHoverTests(unittest.TestCase):
    LIBRARY = "# A helper.\n\n# Doubles a number.\nfunc double(n: int): int\n    return n * 2\n"

    def test_imports_are_listed(self):
        text = 'import "lib/helpers"\nimport os\nimport native "mathx"\nimport "second"\n'
        self.assertEqual(import_targets(text, EN), ["lib/helpers", "second"])

    def test_a_name_from_an_imported_file_names_the_file(self):
        program = 'import "helpers"\nprintln(double(4))\n'
        loader = lambda target: ("helpers.th", self.LIBRARY) if target == "helpers" else None
        doc = describe(program, program.index("double") + 1, EN, loader)
        self.assertEqual(doc, HoverDoc("func double(n: int): int", "Doubles a number.", "helpers.th"))

    def test_a_method_of_an_imported_struct(self):
        library = ("# The stack.\nstruct Stack<T>\n    _items: T[]\n\n    Stack()\n        _items = []\n\n"
                   "    # Puts a value on top.\n    func push(value: T)\n        push(_items, value)\n")
        program = 'import "datastructures"\nvar s: Stack<int> = Stack()\ns.push(1)\n'
        loader = lambda target: ("datastructures.th", library)
        doc = describe(program, program.rindex("push") + 1, EN, loader)
        self.assertEqual(doc.signature, "func push(value: T)")
        self.assertEqual(doc.body, "Puts a value on top.")
        self.assertEqual(doc.source, "datastructures.th")

    def test_the_real_sdk_documents_a_stacks_push(self):
        sdk = Path(support.ROOT) / "lib" / "thunder" / "en" / "datastructures.th"
        program = 'import "datastructures"\nvar s: Stack<int> = Stack()\ns.push(1)\n'
        loader = lambda target: (sdk.name, sdk.read_text(encoding="utf-8"))
        doc = describe(program, program.rindex("push") + 1, EN, loader)
        self.assertEqual(doc.signature, "func push(value: T)")
        self.assertEqual(doc.body, "Puts a value on top.")


def sdk_loader(language):
    def load(target):
        path = Path(support.ROOT) / "lib" / "thunder" / language / f"{target}.th"
        return (path.name, path.read_text(encoding="utf-8")) if path.is_file() else None
    return load


class OtherLanguageTests(unittest.TestCase):
    """The bottom line of a tooltip: what the name is called in the other language."""

    def test_an_english_builtin_names_its_pashto_twin(self):
        doc = describe("println(1)", 2, EN, None, [OtherLanguage(PS)])
        self.assertEqual(doc.translations, (("پښتو", "کرښه چاپ"),))
        text = "var r = math.sqrt(2.0)"
        doc = describe(text, text.index("sqrt") + 1, EN, None, [OtherLanguage(PS)])
        self.assertEqual(doc.translations, (("پښتو", "ریاضي.جذر"),))

    def test_a_pashto_builtin_names_its_english_twin(self):
        text = "متغیر ل = ریاضي.توان(۲٫۰، ۳٫۰)"
        doc = describe(text, text.index("توان") + 1, PS, None, [OtherLanguage(EN)])
        self.assertEqual(doc.translations, (("English", "math.pow"),))
        html = as_rich_text(doc)
        self.assertTrue(html.endswith("<hr><p dir='auto'>English: <b dir='auto'>math.pow</b></p>"), html)

    def test_an_sdk_declaration_names_its_twin_in_the_other_files(self):
        program = 'import "datastructures"\nvar s: Stack<int> = Stack()\ns.push(1)\n'
        doc = describe(program, program.rindex("push") + 1, EN, sdk_loader("en"),
                       [OtherLanguage(PS, sdk_loader("ps"))])
        self.assertEqual(doc.translations, (("پښتو", "ډېرۍ.ورزیات_کړه"),))
        program = 'راوړه "datastructures"\nمتغیر ډ: ډېرۍ<عدد> = ډېرۍ()\nډ.ورزیات_کړه(۱)\n'
        doc = describe(program, program.rindex("ورزیات") + 1, PS, sdk_loader("ps"),
                       [OtherLanguage(EN, sdk_loader("en"))])
        self.assertEqual(doc.translations, (("English", "Stack.push"),))

    def test_the_programs_own_names_have_no_translation(self):
        doc = describe(PROGRAM, PROGRAM.index('hello("world")') + 1, EN, None, [OtherLanguage(PS)])
        self.assertEqual(doc.translations, ())
        self.assertNotIn("<hr>", as_rich_text(doc))


class PashtoTypeWordTests(unittest.TestCase):
    def test_no_pashto_builtin_shows_an_english_type(self):
        for entry in PS["builtins"]:
            if entry.get("module"):
                module = next(m["name"] for m in PS["modules"] if m["canonical_name"] == entry["module"])
                call = f"{module}.{entry['member']}"
            else:
                call = entry["name"]
            text = call + "()"
            doc = describe(text, len(call) - 1, PS)
            self.assertIsNotNone(doc, call)
            self.assertNotRegex(doc.signature, r"(?<!\w)(int|float|string|bool|byte|func|Platform|Color)(?!\w)",
                                call)

    def test_the_librarys_enum_types_are_named_in_pashto(self):
        text = "متغیر پ = سیسټم.عامل سیسټم()"
        doc = describe(text, text.index("عامل") + 1, PS)
        self.assertEqual(doc.signature, "سیسټم.عامل سیسټم(): پلیټفارم")

    def test_arabic_keyboard_letters_find_the_builtin(self):
        doc = describe("كرښه چاپ(۱)", 1, PS)  # Arabic kaf, as some keyboards type it
        self.assertIsNotNone(doc)
        self.assertTrue(doc.signature.startswith("کرښه چاپ("), doc.signature)


class RichTextTests(unittest.TestCase):
    def test_the_tooltip_wraps_the_doc_and_names_the_source(self):
        doc = HoverDoc("func push(value: T)", "Puts a value\non top.\n\nSecond paragraph.", "datastructures.th")
        html = as_rich_text(doc, "from datastructures.th")
        self.assertIn("func push(value: T)", html)
        self.assertIn("<p dir='auto'>Puts a value on top.</p>", html)
        self.assertIn("<p dir='auto'>Second paragraph.</p>", html)
        self.assertIn("from datastructures.th", html)

    def test_markup_in_the_source_is_escaped(self):
        html = as_rich_text(HoverDoc("func f(a: <T>)", "a < b", ""), "")
        self.assertNotIn("<T>", html)
        self.assertIn("&lt;T&gt;", html)


if __name__ == "__main__":
    unittest.main()
