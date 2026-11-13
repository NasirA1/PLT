"""Fonts: the shipped Pashto and code fonts, the Format menu, and what Qt really draws with.

The point of the Format menu is that Pashto is drawn in a font made for it. Two things in Qt
quietly defeat that, and both are tested here against the real toolkit: a code font with Arabic
glyphs of its own (DejaVu Sans Mono) draws the Pashto itself, and Qt caches the font it fell back
to, so changing the Pashto font has no effect unless that cache is dropped.
"""

import os
import unittest
from pathlib import Path

from tests.python import support
from tests.python.fake_ui import FakeBackend
from thunder_ide.core import fonts
from thunder_ide.core.fonts import FontChoice

PASHTO_LETTERS = "ابپتټثجچځڅحخدډذرړزژږسشښصضطظعغفقکګلمنڼوهیيېۍئ"
PASHTO_EXTRAS = "۰۱۲۳۴۵۶۷۸۹،؛؟٫«»"


class FontModelTests(unittest.TestCase):
    def test_the_code_font_comes_first_and_no_family_twice(self):
        self.assertEqual(FontChoice("JetBrains Mono", "Vazirmatn").families(), ["JetBrains Mono", "Vazirmatn"])
        self.assertEqual(FontChoice("Same", "Same").families(), ["Same"])
        self.assertEqual(FontChoice("", "Vazirmatn").families(), ["Vazirmatn"])

    def test_the_ide_ships_its_fonts_with_their_licences(self):
        files = fonts.bundled_font_files()
        self.assertGreaterEqual(len(files), 6)
        for path in files:
            with self.subTest(font=path.name):
                licence = path.parent / "OFL.txt"
                self.assertTrue(licence.is_file(), "every shipped font carries its licence")
                self.assertIn("SIL Open Font License", licence.read_text(encoding="utf-8"))

    def test_preferred_fonts_come_first_then_the_rest_in_order(self):
        offered = fonts.ordered_choices(["b", "Vazirmatn", "a", "b"], ["Vazirmatn", "missing", "Vazirmatn"])
        self.assertEqual(offered, ["Vazirmatn", "a", "b"], "each family once, preferred first")

    def test_a_font_missing_on_this_machine_falls_back_to_the_default(self):
        resolved = fonts.resolve(FontChoice("Gone Mono", "Gone Naskh"), ["JetBrains Mono", "X"], ["Vazirmatn"])
        self.assertEqual(resolved, FontChoice("JetBrains Mono", "Vazirmatn"))
        kept = fonts.resolve(FontChoice("X", "Vazirmatn"), ["JetBrains Mono", "X"], ["Vazirmatn"])
        self.assertEqual(kept, FontChoice("X", "Vazirmatn"))
        neither = fonts.resolve(FontChoice("A", "B"), ["Only Mono"], ["Only Naskh"])
        self.assertEqual(neither, FontChoice("Only Mono", "Only Naskh"))


class FormatMenuTests(unittest.TestCase):
    """The controller's side of Format > Code font / Pashto font, through the fake UI."""

    def setUp(self):
        import tempfile
        from unittest import mock

        from thunder_ide.core.controller import IdeController

        self.directory = tempfile.mkdtemp(prefix="thunder-fonts-")
        self.settings_path = Path(self.directory) / "ide.json"
        self.patch = mock.patch.dict(os.environ, {"THUNDER_IDE_SETTINGS": str(self.settings_path)})
        self.patch.start()
        self.backend = FakeBackend()
        self.controller = IdeController(self.backend, None, settings_path=self.settings_path)
        self.window = self.backend.windows[-1]

    def tearDown(self):
        import shutil

        self.patch.stop()
        shutil.rmtree(self.directory, ignore_errors=True)

    def test_the_window_offers_the_fonts_and_ticks_the_defaults(self):
        code, pashto, choice = self.window.font_choices
        self.assertIn("JetBrains Mono", code)
        self.assertIn("Vazirmatn", pashto)
        self.assertEqual(choice, FontChoice("JetBrains Mono", "Vazirmatn"))
        self.assertEqual(self.window.fonts[0], choice)

    def test_choosing_a_pashto_font_redraws_and_is_remembered(self):
        self.controller.on_font_selected("pashto", "Scheherazade New")
        self.assertEqual(self.window.fonts[0], FontChoice("JetBrains Mono", "Scheherazade New"))
        self.assertEqual(self.window.font_choices[2].pashto, "Scheherazade New", "the menu tick moves")
        self.assertIn("Scheherazade New", self.window.status)
        self.assertEqual(self.controller.settings.pashto_font, "Scheherazade New")
        # Kept between sessions.
        self.controller.settings.save(self.settings_path)
        from thunder_ide.core.settings import Settings
        self.assertEqual(Settings.load(self.settings_path).pashto_font, "Scheherazade New")

    def test_choosing_a_code_font_keeps_the_pashto_font(self):
        self.controller.on_font_selected("code", "Liberation Mono")
        self.assertEqual(self.window.fonts[0], FontChoice("Liberation Mono", "Vazirmatn"))

    def test_the_size_survives_a_change_of_font(self):
        self.controller.run_command("zoom_in")
        size = self.controller.settings.font_size
        self.controller.on_font_selected("pashto", "Noto Naskh Arabic")
        self.assertEqual(self.window.fonts[1], size)

    def test_default_fonts_and_size_puts_everything_back(self):
        self.controller.on_font_selected("pashto", "Scheherazade New")
        self.controller.run_command("zoom_in")
        self.controller.run_command("reset_fonts")
        self.assertEqual(self.window.fonts, (FontChoice("JetBrains Mono", "Vazirmatn"), 11))

    def test_an_unknown_kind_changes_nothing(self):
        before = self.window.fonts
        self.controller.on_font_selected("emoji", "Anything")
        self.assertEqual(self.window.fonts, before)

    def test_a_saved_font_that_is_not_installed_falls_back(self):
        self.controller.settings.pashto_font = "Not Installed Here"
        self.controller.on_font_selected("code", "JetBrains Mono")
        self.assertEqual(self.window.fonts[0].pashto, "Vazirmatn")


@support.requires_qt
class QtFontTests(unittest.TestCase):
    """What Qt really draws a line of Pashto code with."""

    @classmethod
    def setUpClass(cls):
        from PySide6.QtWidgets import QApplication

        from thunder_ide.qt import fonts as qt_fonts

        cls.app = QApplication.instance() or QApplication([])
        cls.qt_fonts = qt_fonts
        cls.bundled = qt_fonts.load_bundled_fonts()

    def families_used(self, choice: FontChoice, text: str = "کرښه چاپ(x)") -> set[str]:
        from PySide6.QtGui import QTextLayout

        layout = QTextLayout(text, self.qt_fonts.program_font(choice, 14))
        layout.beginLayout()
        layout.createLine()
        layout.endLayout()
        return {run.rawFont().familyName() for run in layout.glyphRuns()}

    def test_every_shipped_pashto_font_has_every_pashto_letter(self):
        from PySide6.QtGui import QRawFont

        for path in fonts.bundled_font_files():
            raw = QRawFont(str(path), 14)
            if not raw.supportsCharacter(ord("ب")):
                continue  # the code font: deliberately no Arabic
            with self.subTest(font=path.name):
                missing = [c for c in PASHTO_LETTERS + PASHTO_EXTRAS if not raw.supportsCharacter(ord(c))]
                self.assertEqual(missing, [])

    def test_the_shipped_code_font_has_no_arabic_so_pashto_reaches_the_pashto_font(self):
        self.assertIn(fonts.DEFAULT_CODE_FONT, self.bundled)
        self.assertFalse(self.qt_fonts.has_arabic(fonts.DEFAULT_CODE_FONT))

    def test_both_lists_offer_the_shipped_fonts_first(self):
        code, pashto = self.qt_fonts.available_fonts()
        self.assertEqual(code[0], fonts.DEFAULT_CODE_FONT)
        self.assertEqual(pashto[0], fonts.DEFAULT_PASHTO_FONT)
        for family in ("Noto Naskh Arabic", "Scheherazade New", "Lateef", "Noto Sans Arabic"):
            self.assertIn(family, pashto)
        # No code font may carry Arabic glyphs: it would draw the Pashto itself.
        self.assertTrue(all(not self.qt_fonts.has_arabic(family) for family in code))
        # Each family is offered once, and only fonts that can draw code at all.
        self.assertEqual(len(code), len(set(code)))
        self.assertEqual(len(pashto), len(set(pashto)))
        self.assertNotIn("Noto Color Emoji", code)

    def test_latin_comes_from_the_code_font_and_pashto_from_the_pashto_font(self):
        self.qt_fonts.reload_bundled_fonts()  # whatever another test left in Qt's fallback cache
        used = self.families_used(FontChoice("JetBrains Mono", "Vazirmatn"))
        self.assertEqual(used, {"JetBrains Mono", "Vazirmatn"})

    def test_changing_the_pashto_font_really_changes_it(self):
        """Qt caches the fallback font; without reloading, the old Pashto font stays in use."""
        self.assertIn("Vazirmatn", self.families_used(FontChoice("JetBrains Mono", "Vazirmatn")))
        self.qt_fonts.reload_bundled_fonts()
        self.assertIn("Scheherazade New", self.families_used(FontChoice("JetBrains Mono", "Scheherazade New")))
        self.qt_fonts.reload_bundled_fonts()
        self.assertIn("Lateef", self.families_used(FontChoice("JetBrains Mono", "Lateef")))


if __name__ == "__main__":
    unittest.main()
