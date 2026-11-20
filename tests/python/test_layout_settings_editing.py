import json
import tempfile
import unittest
from pathlib import Path

from tests.python import support  # noqa: F401
from thunder_ide.core import editing
from thunder_ide.core.layout import CANVAS, EDITOR, EDUCATION, OUTPUT, Layout
from thunder_ide.core.settings import Settings, default_settings_path


class LayoutTests(unittest.TestCase):
    def test_default_layout(self):
        layout = Layout.default()
        self.assertEqual(layout.panels_in("left"), [EDITOR])
        self.assertEqual(layout.panels_in("right"), [EDUCATION, CANVAS])
        self.assertEqual(layout.panels[CANVAS].tab_with, EDUCATION)  # the canvas is a tab next to it
        self.assertEqual(layout.panels_in("bottom"), [OUTPUT])
        self.assertEqual(layout.panels_in("nowhere"), [])

    def test_moving_floating_and_hiding(self):
        layout = Layout.default()
        layout.toolkit_state = "opaque"
        layout.move(EDUCATION, "bottom")
        self.assertEqual(layout.panels_in("bottom"), [OUTPUT, EDUCATION])
        self.assertEqual(layout.panels_in("right"), [CANVAS])
        layout.move(CANVAS, "left")
        self.assertEqual(layout.panels[CANVAS].tab_with, "")  # moving a panel takes it out of its tab group
        self.assertEqual(layout.panels_in("right"), [])
        self.assertEqual(layout.toolkit_state, "")  # a semantic change invalidates the toolkit's exact state
        with self.assertRaises(ValueError):
            layout.move(EDUCATION, "middle")
        layout.set_floating(OUTPUT, True)
        self.assertEqual(layout.panels_in("bottom"), [EDUCATION])
        layout.set_visible(EDUCATION, False)
        self.assertEqual(layout.panels_in("bottom"), [])
        self.assertEqual(layout.panels_in("bottom", include_hidden=True), [EDUCATION])
        layout.set_visible(EDUCATION, True)
        copy = layout.copy()
        copy.move(OUTPUT, "left")
        self.assertTrue(layout.panels[OUTPUT].floating)
        self.assertFalse(copy.panels[OUTPUT].floating)

    def test_round_trip_and_validation(self):
        layout = Layout.default()
        layout.move(OUTPUT, "right")
        layout.left_fraction = 0.3
        layout.toolkit_state = "AAEC"
        restored = Layout.from_dict(json.loads(json.dumps(layout.to_dict())))
        self.assertEqual(restored.to_dict(), layout.to_dict())

        broken = Layout.from_dict({"panels": {EDITOR: {"visible": False, "area": "nowhere", "order": "x"},
                                              "unknown": {}, OUTPUT: "nonsense"},
                                   "left_fraction": 5, "top_fraction": "big", "toolkit_state": 7})
        self.assertTrue(broken.panels[EDITOR].visible)  # the editor can never be lost
        self.assertEqual(broken.panels[EDITOR].area, "left")
        self.assertEqual(broken.left_fraction, 0.5)
        self.assertEqual(broken.toolkit_state, "")
        self.assertEqual(Layout.from_dict(None).to_dict(), Layout.default().to_dict())
        tabs = Layout.from_dict({"panels": {CANVAS: {"tab_with": CANVAS}, OUTPUT: {"tab_with": "nowhere"},
                                            EDITOR: {"tab_with": OUTPUT}}})
        self.assertEqual((tabs.panels[CANVAS].tab_with, tabs.panels[OUTPUT].tab_with, tabs.panels[EDITOR].tab_with),
                         ("", "", OUTPUT))
        # Settings saved before the canvas existed still load, with the canvas in its default place.
        old = Layout.from_dict({"panels": {EDITOR: {"area": "left"}, EDUCATION: {"area": "right"},
                                           OUTPUT: {"area": "bottom"}}})
        self.assertEqual(old.panels[CANVAS].tab_with, EDUCATION)


class SettingsTests(unittest.TestCase):
    def test_save_and_load(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sub" / "ide.json"
            settings = Settings(language="ps", font_size=14)
            settings.layout.move(OUTPUT, "right")
            self.assertTrue(settings.save(path))
            loaded = Settings.load(path)
            self.assertEqual((loaded.language, loaded.font_size), ("ps", 14))
            self.assertEqual(loaded.layout.panels[OUTPUT].area, "right")
            path.write_text("[1, 2]", encoding="utf-8")
            self.assertEqual(Settings.load(path).language, "en")
            path.write_text('{"font_size": 99, "language": 3}', encoding="utf-8")
            self.assertEqual((Settings.load(path).font_size, Settings.load(path).language), (11, "en"))
            self.assertEqual(Settings.load(Path(directory) / "missing.json").language, "en")
            blocked = Path(directory) / "file"
            blocked.write_text("x", encoding="utf-8")
            self.assertFalse(Settings().save(blocked / "ide.json"))

    def test_default_location(self):
        self.assertEqual(default_settings_path({"THUNDER_IDE_SETTINGS": "/tmp/x.json"}), Path("/tmp/x.json"))
        self.assertEqual(default_settings_path({"APPDATA": "C:/AppData"}, windows=True),
                         Path("C:/AppData") / "Thunder" / "ide.json")
        self.assertEqual(default_settings_path({"XDG_CONFIG_HOME": "/cfg"}, windows=False),
                         Path("/cfg") / "thunder" / "ide.json")
        self.assertEqual(default_settings_path({}, windows=False), Path.home() / ".config" / "thunder" / "ide.json")
        self.assertIsInstance(default_settings_path(), Path)


class EditingTests(unittest.TestCase):
    def test_return_keeps_indents_and_dedents(self):
        self.assertEqual(editing.on_return("    x = 1", ""), editing.Edit("    x = 1", "\n    "))
        self.assertEqual(editing.on_return("if x > 0", ""), editing.Edit("if x > 0", "\n    "))
        self.assertEqual(editing.on_return("    else if y  # note", ""), editing.Edit("    else if y  # note", "\n        "))
        self.assertEqual(editing.on_return("func f(a: int): int", ""), editing.Edit("func f(a: int): int", "\n    "))
        self.assertEqual(editing.on_return("        return x", ""), editing.Edit("        return x", "\n    "))
        self.assertEqual(editing.on_return("    pass", "tail"), editing.Edit("    pass", "\n"))
        self.assertEqual(editing.on_return("break", ""), editing.Edit("break", "\n"))
        self.assertEqual(editing.on_return('s = "if', ""), editing.Edit('s = "if', "\n"))      # inside a string

    def test_return_after_module_and_with_in_both_languages(self):
        words = editing.BlockWords.from_language_info({
            "with_after_resource": True,
            "keywords": [{"kind": "KwModule", "spellings": ["ټولګی"]}, {"kind": "KwWith", "spellings": ["سره"]},
                         {"kind": "KwIf", "spellings": ["که"]}, {"kind": "KwReturn", "spellings": ["راګرځه"]}],
        })
        self.assertEqual(editing.on_return("ټولګی هندسه", "", words), editing.Edit("ټولګی هندسه", "\n    "))
        # Pashto's "with" follows the handle, or ends the line when there is no handle.
        self.assertEqual(editing.on_return("لاستی سره = ټولګی.پرانیزه()", "", words),
                         editing.Edit("لاستی سره = ټولګی.پرانیزه()", "\n    "))
        self.assertEqual(editing.on_return("    ساکټ سره", "", words), editing.Edit("    ساکټ سره", "\n        "))
        self.assertEqual(editing.on_return("سره نښلوه(الف، ب)", "", words), editing.Edit("سره نښلوه(الف، ب)", "\n"))
        english = editing.BlockWords()
        self.assertEqual(editing.on_return("with f = log.open()", "", english), editing.Edit("with f = log.open()", "\n    "))
        self.assertEqual(editing.on_return("module geometry", "", english), editing.Edit("module geometry", "\n    "))
        self.assertEqual(editing.on_return("x = 1 # if", ""), editing.Edit("x = 1 # if", "\n"))  # a comment
        self.assertEqual(editing.on_return("    iffy = 1", ""), editing.Edit("    iffy = 1", "\n    "))
        self.assertEqual(editing.on_return("", ""), editing.Edit("", "\n"))

    def test_return_indents_after_the_header_of_a_function_value(self):
        """A function value written in place opens a block from the end of its header, wherever
        the line begins - the rule the compiler's lexer uses."""
        english = editing.BlockWords()
        for line in ("var later = Task(func()", "var step = func(n: int): int",
                     "var later = Task(func()  # note", "    push(jobs, func(): string"):
            with self.subTest(line=line):
                self.assertEqual(editing.on_return(line, "", english).insert,
                                 "\n" + editing.indent_of(line) + editing.INDENT)
        # A function value whose body is on the same line opens nothing: the call is closed.
        for line in ("println(map(a, func(n: int): int n * n))", "var t = Task(func(", "println(1,",
                     "foo(bar)", 'var s = "func("'):
            with self.subTest(line=line):
                self.assertEqual(editing.on_return(line, "", english).insert, "\n" + editing.indent_of(line))

    def test_the_function_word_comes_from_the_language(self):
        words = editing.BlockWords.from_language_info({
            "keywords": [{"kind": "KwFunc", "spellings": ["دنده"]}, {"kind": "KwIf", "spellings": ["که"]},
                         {"kind": "KwReturn", "spellings": ["راګرځه"]}],
        })
        self.assertEqual(words.function, {"دنده"})
        self.assertEqual(editing.on_return("متغیر و = کار(دنده()", "", words).insert, "\n    ")
        self.assertEqual(editing.on_return("کرښه چاپ(پر هر یو(ل، دنده(ن: عدد): عدد ن))", "", words).insert, "\n")
        # A keyword typed on an Arabic keyboard is folded, as the compiler folds it.
        self.assertEqual(editing.on_return("متغير و = کار(دنده()", "", words).insert, "\n    ")

    def test_block_words_from_the_language(self):
        info = {"keywords": [{"kind": "KwIf", "spellings": ["که"]}, {"kind": "KwElse", "text": "کنه"},
                             {"kind": "KwReturn", "spellings": ["راګرځه", "راگرځه"]}, {"kind": "KwInt", "text": "عدد"}]}
        words = editing.BlockWords.from_language_info(info)
        self.assertEqual(words.opening, {"که", "کنه"})
        self.assertEqual(words.leaving, {"راګرځه", "راگرځه"})
        self.assertEqual(editing.on_return("که x", "", words), editing.Edit("که x", "\n    "))
        self.assertEqual(editing.on_return("    راګرځه x", "", words), editing.Edit("    راګرځه x", "\n"))
        self.assertEqual(editing.on_return("if x", "", words), editing.Edit("if x", "\n"))  # English is not special here
        self.assertEqual(editing.BlockWords.from_language_info({}), editing.BlockWords())

    def test_tab_and_backspace(self):
        self.assertEqual(editing.on_tab("x"), editing.Edit("x", "    "))
        self.assertEqual(editing.on_backspace("        "), editing.Edit("    ", ""))
        self.assertEqual(editing.on_backspace("      "), editing.Edit("    ", ""))  # back to the previous level
        self.assertIsNone(editing.on_backspace("    x"))
        self.assertIsNone(editing.on_backspace(""))
        self.assertEqual(editing.indent_of("\t  x"), "\t  ")
        self.assertEqual(editing.first_word("  کرښه چاپ(1)"), "کرښه")
        self.assertTrue(editing.inside_string_or_comment("x = «a"))
        self.assertTrue(editing.inside_string_or_comment('s = "a\\"b'))
        self.assertFalse(editing.inside_string_or_comment('s = "a" + '))


if __name__ == "__main__":
    unittest.main()
