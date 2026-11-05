import json
import tempfile
import unittest
from pathlib import Path

from tests.python import support  # noqa: F401
from thunder_ide.core import editing
from thunder_ide.core.layout import EDITOR, EDUCATION, OUTPUT, SOURCE_VIEW, Layout
from thunder_ide.core.settings import Settings, default_settings_path


class LayoutTests(unittest.TestCase):
    def test_default_layout(self):
        layout = Layout.default()
        self.assertEqual(layout.panels_in("left"), [EDITOR])
        self.assertEqual(layout.panels_in("left", include_hidden=True), [EDITOR, SOURCE_VIEW])
        self.assertEqual(layout.panels_in("right"), [EDUCATION])
        self.assertEqual(layout.panels_in("bottom"), [OUTPUT])
        self.assertFalse(layout.area_collapsed("right"))
        self.assertFalse(layout.area_collapsed("nowhere"))

    def test_moving_floating_collapsing_and_hiding(self):
        layout = Layout.default()
        layout.move(EDUCATION, "bottom")
        self.assertEqual(layout.panels_in("bottom"), [OUTPUT, EDUCATION])
        self.assertEqual(layout.panels_in("right"), [])
        with self.assertRaises(ValueError):
            layout.move(EDUCATION, "middle")
        layout.toggle_collapsed(OUTPUT)
        self.assertTrue(layout.panels[OUTPUT].collapsed)
        layout.toggle_collapsed(EDUCATION)
        self.assertTrue(layout.area_collapsed("bottom"))
        layout.set_floating(OUTPUT, True)
        self.assertFalse(layout.panels[OUTPUT].collapsed)
        self.assertEqual(layout.panels_in("bottom"), [EDUCATION])
        layout.set_visible(EDUCATION, False)
        self.assertEqual(layout.panels_in("bottom"), [])
        layout.set_visible(EDUCATION, True)
        self.assertFalse(layout.panels[EDUCATION].collapsed)
        copy = layout.copy()
        copy.move(OUTPUT, "left")
        self.assertTrue(layout.panels[OUTPUT].floating)

    def test_round_trip_and_validation(self):
        layout = Layout.default()
        layout.move(SOURCE_VIEW, "right")
        layout.panels[OUTPUT].geometry = "500x300+1+2"
        layout.left_fraction = 0.3
        restored = Layout.from_dict(json.loads(json.dumps(layout.to_dict())))
        self.assertEqual(restored.to_dict(), layout.to_dict())

        broken = Layout.from_dict({"panels": {EDITOR: {"visible": False, "area": "nowhere", "order": "x"},
                                              "unknown": {}, OUTPUT: "nonsense"},
                                   "left_fraction": 5, "top_fraction": "big"})
        self.assertTrue(broken.panels[EDITOR].visible)  # the editor can never be lost
        self.assertEqual(broken.panels[EDITOR].area, "left")
        self.assertEqual(broken.left_fraction, 0.5)
        self.assertEqual(Layout.from_dict(None).to_dict(), Layout.default().to_dict())


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
    def test_return(self):
        self.assertEqual(editing.on_return("    x = 1", ""), editing.Edit("    x = 1", "\n    "))
        self.assertEqual(editing.on_return("if (x)  {", ""), editing.Edit("if (x)  {", "\n    "))
        self.assertEqual(editing.on_return("{", "}"), editing.Edit("{", "\n    \n", cursor_back=1))
        self.assertEqual(editing.on_return('s = "{', ""), editing.Edit('s = "{', "\n"))

    def test_open_brace_moves_to_its_own_line(self):
        self.assertEqual(editing.on_open_brace("    if (x) "), editing.Edit("    if (x)\n    ", "{"))
        self.assertEqual(editing.on_open_brace("    "), editing.Edit("    ", "{"))
        self.assertEqual(editing.on_open_brace('println("'), editing.Edit('println("', "{"))
        self.assertEqual(editing.on_open_brace("x() // "), editing.Edit("x() // ", "{"))
        self.assertEqual(editing.on_open_brace("x = «a"), editing.Edit("x = «a", "{"))
        self.assertEqual(editing.on_open_brace('s = "a\\"b'), editing.Edit('s = "a\\"b', "{"))

    def test_close_brace_dedents(self):
        self.assertEqual(editing.on_close_brace("        "), editing.Edit("    ", "}"))
        self.assertEqual(editing.on_close_brace("x"), editing.Edit("x", "}"))
        self.assertEqual(editing.indent_of("\t  x"), "\t  ")


if __name__ == "__main__":
    unittest.main()
