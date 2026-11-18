"""IntelliSense (core.completion) and the declaration scanner it shares with the hover help
(core.declarations): pure text work, tested without a toolchain or a GUI."""

import json
import unittest
from pathlib import Path

from tests.python import support
from thunder_ide.core.completion import Completions, call_at, complete, insertion, signature_help
from thunder_ide.core.declarations import declarations, enclosing_function, twin

# The bundled language information is a verbatim copy of `thunderc --language-info`.
DATA = Path(support.ROOT) / "ide" / "vscode" / "data"
EN = json.loads((DATA / "language-info.en.json").read_text(encoding="utf-8"))
PS = json.loads((DATA / "language-info.ps.json").read_text(encoding="utf-8"))
SDK = Path(support.ROOT) / "lib" / "thunder"


def sdk_loader(language):
    def load(target):
        path = SDK / language / f"{target}.th"
        return (path.name, path.read_text(encoding="utf-8")) if path.is_file() else None
    return load


def at_bar(text):
    """The text without its '|' and the index the bar marked (the caret)."""
    index = text.index("|")
    return text.replace("|", "", 1), index


def labels(text, info=EN, explicit=False, loader=None, files=()):
    text, index = at_bar(text)
    found = complete(text, index, info, loader, files, explicit)
    return [item.label for item in found.items] if found is not None else []


def item(text, label, info=EN, loader=None):
    text, index = at_bar(text)
    found = complete(text, index, info, loader, (), True)
    return next(entry for entry in found.items if entry.label == label)


OBJECTS = """struct Bordered
    func frame(inner: int): string
        return ""

# A button.
struct Button: Bordered
    _title: string
    width: int

    Button(title: string)
        _title = title

    # The label.
    func get_title(): string
        return _title

enum Direction
    north
    east, south, west

module geometry
    func area(r: float): float
        return r
    func _helper(): int
        return 1

func paint(b: Button, times: int)
    var total = 0
    b.|
"""


class DeclarationTests(unittest.TestCase):
    def test_every_kind_of_declaration(self):
        text = OBJECTS.replace("|", "")
        found = {(d.kind, d.name, d.owner, d.module) for d in declarations(text, EN)}
        for expected in [("struct", "Button", "", ""), ("field", "_title", "Button", ""),
                         ("ctor", "Button", "Button", ""), ("func", "get_title", "Button", ""),
                         ("enum", "Direction", "", ""), ("member", "south", "Direction", ""),
                         ("member", "west", "Direction", ""), ("module", "geometry", "", ""),
                         ("func", "area", "", "geometry"), ("func", "paint", "", ""),
                         ("var", "total", "", "")]:
            self.assertIn(expected, found)

    def test_parameters_types_locals_and_bodies(self):
        text = OBJECTS.replace("|", "")
        found = declarations(text, EN)
        paint = next(d for d in found if d.name == "paint")
        self.assertEqual(paint.params, ("b: Button", "times: int"))
        total = next(d for d in found if d.name == "total")
        self.assertTrue(total.local)
        self.assertEqual(total.value, "0")
        row = text.split("\n").index("    b.")
        self.assertEqual(enclosing_function(found, row), paint)

    def test_arabic_keyboard_letters_declare_too(self):
        # متغير with Arabic yeh is the same keyword as متغیر, as the compiler reads it.
        found = declarations("متغير شمېر = ۱\n", PS)
        self.assertEqual([(d.kind, d.name) for d in found], [("var", "شمېر")])

    def test_the_sdk_twins_line_up(self):
        english = (SDK / "en" / "datastructures.th").read_text(encoding="utf-8")
        pashto = (SDK / "ps" / "datastructures.th").read_text(encoding="utf-8")
        mine, theirs = declarations(english, EN), declarations(pashto, PS)
        push = next(d for d in mine if d.name == "push" and d.owner == "Stack")
        match = twin(mine, push, theirs)
        self.assertEqual((match.owner, match.name), ("ډېرۍ", "ورزیات_کړه"))


class CompletionTests(unittest.TestCase):
    def test_builtins_and_keywords_from_the_first_letter(self):
        self.assertEqual(labels("prin|")[:2], ["print", "println"])
        self.assertIn("while", labels("whi|"))

    def test_nothing_before_a_letter_unless_asked(self):
        self.assertEqual(labels("|"), [])
        self.assertIn("println", labels("|", explicit=True))

    def test_a_finished_word_closes_the_list(self):
        self.assertNotIn("println", labels("println|"))

    def test_nothing_inside_strings_comments_or_numbers(self):
        self.assertEqual(labels('println("pri|'), [])
        self.assertEqual(labels("# pri|"), [])
        self.assertEqual(labels("var x = 12|"), [])

    def test_a_struct_variables_fields_and_methods(self):
        found = labels(OBJECTS)
        self.assertIn("get_title", found)
        self.assertIn("width", found)
        self.assertIn("frame", found)  # from the mixin
        self.assertNotIn("_title", found)  # private outside the struct's own methods

    def test_private_members_inside_the_struct(self):
        text = OBJECTS.replace("    b.|", "    b.").replace("        return _title", "        return _t|")
        self.assertIn("_title", labels(text))
        text = OBJECTS.replace("    b.|", "    b.").replace("        _title = title", "        _title = title\n        var o: Button = Button(\"x\")\n        o.|")
        self.assertIn("_title", labels(text))  # another instance's, inside the struct's own code

    def test_bare_members_inside_a_method(self):
        text = "struct P\n    x: int\n    func size(): int\n        return |\n"
        found = labels(text, explicit=True)
        self.assertEqual(found[:2], ["size", "x"])

    def test_parameters_and_locals_in_scope(self):
        text = OBJECTS.replace("    b.|", "    t|")
        found = labels(text)
        self.assertEqual(found[:2], ["times", "total"])

    def test_the_type_of_an_initialiser(self):
        text = "struct P\n    x: int\nvar p = P(1)\np.|"
        self.assertEqual(labels(text), ["x"])

    def test_an_sdk_struct_from_its_declared_type(self):
        text = 'import "datastructures"\nvar s: Stack<int> = Stack()\ns.|'
        found = labels(text, loader=sdk_loader("en"))
        self.assertIn("push", found)
        self.assertNotIn("_items", found)
        push = item(text, "push", loader=sdk_loader("en"))
        self.assertEqual(push.detail, "func push(value: T)")
        self.assertEqual(push.documentation, "Puts a value on top.")
        self.assertEqual(push.source, "datastructures.th")

    def test_modules_and_enums_after_a_dot(self):
        self.assertIn("sqrt", labels("math.|"))
        self.assertEqual(labels("math.sq|"), ["sqrt"])
        self.assertEqual(labels(OBJECTS.replace("    b.|", "    geometry.|")), ["area"])  # _helper is private
        found = labels(OBJECTS.replace("    b.|", "    Direction.|"))
        self.assertEqual(found[:4], ["east", "north", "south", "west"])
        self.assertIn("count", found)
        self.assertIn("red", labels("graphics.Color.|"))

    def test_types_after_a_colon(self):
        found = labels(OBJECTS.replace("    b.|", "    var c: |"), explicit=True)
        self.assertEqual(found[:5], ["bool", "byte", "float", "int", "string"])
        self.assertIn("Button", found)
        self.assertIn("Direction", found)

    def test_imports(self):
        self.assertEqual(labels("import gr|"), ["graphics"])
        self.assertIn("lib/geometry", labels("import |", files=["lib/geometry"]))
        found = complete('import "l', 9, EN, None, ["lib/geometry"])
        self.assertEqual(found.items[0].text, 'lib/geometry"')

    def test_pashto_completes_in_pashto(self):
        self.assertEqual(labels("کرښه چ|", PS), ["کرښه چاپ"])
        self.assertIn("کرښه چاپ", labels("چا|", PS))  # any word of a multi-word name
        self.assertIn("جذر", labels("متغیر ر = ریاضي.ج|", PS))
        self.assertIn("ورزیات_کړه", labels('راوړه "datastructures"\nمتغیر ډ: ډېرۍ<عدد> = ډېرۍ()\nډ.|', PS,
                                             loader=sdk_loader("ps")))
        detail = item("ریاضي.|", "توان", PS).detail
        self.assertEqual(detail, "ریاضي.توان(x: اعشاري، y: اعشاري): اعشاري")

    def test_a_module_member_of_several_words(self):
        text, index = at_bar("متن.پاک ک|")
        found = complete(text, index, PS)
        self.assertEqual([entry.label for entry in found.items], ["پاک کړه"])
        self.assertEqual(found.start, text.index("پاک"))

    def test_arabic_keyboard_letters_match(self):
        self.assertIn("کرښه چاپ", labels("كرښه|", PS))

    def test_choosing_an_item(self):
        text, index = at_bar("prin|")
        found = complete(text, index, EN)
        println = next(entry for entry in found.items if entry.label == "println")
        self.assertEqual(insertion(text, found, println), (0, 4, "println()", 8))
        pi = Completions(0, 2, (item("math.|", "pi"),))
        self.assertEqual(insertion("pi", pi, pi.items[0])[2:], ("pi()", 4))  # nothing to pass
        # A bracket that is already there is kept, not doubled.
        text = "prin(1)"
        found = complete(text, 4, EN)
        println = next(entry for entry in found.items if entry.label == "println")
        self.assertEqual(insertion(text, found, println), (0, 4, "println", 7))


class SignatureHelpTests(unittest.TestCase):
    def test_the_call_and_argument_around_the_caret(self):
        text = "println(math.pow(2.0, "
        name_end, argument = call_at(text, len(text))
        self.assertEqual(text[name_end - 2:name_end + 1], "pow")
        self.assertEqual(argument, 1)
        self.assertIsNone(call_at("var x = [1, ", len("var x = [1, ")))
        self.assertIsNone(call_at("println(1)", len("println(1)")))

    def test_the_current_parameter_is_marked(self):
        hint = signature_help("math.pow(2.0, ", len("math.pow(2.0, "), EN)
        self.assertIn("math.pow(x: float, <b><u>y: float</u></b>): float", hint)
        hint = signature_help("ریاضي.توان(۲، ", len("ریاضي.توان(۲، "), PS)
        self.assertIn("<b><u>y: اعشاري</u></b>", hint)

    def test_a_declared_function(self):
        text = "# Doubles.\nfunc twice(n: int): int\n    return n * 2\nprintln(twice("
        hint = signature_help(text, len(text), EN)
        self.assertIn("func twice(<b><u>n: int</u></b>): int", hint)
        self.assertIn("Doubles.", hint)

    def test_every_further_argument_is_the_variadic_one(self):
        hint = signature_help('println(1, 2, 3, ', len('println(1, 2, 3, '), EN)
        self.assertIn("<b><u>...</u></b>", hint)


if __name__ == "__main__":
    unittest.main()
