"""Where the Help menu leads: pages of the HTML API documentation (core.docs)."""

import unittest
from pathlib import Path
from unittest import mock

from tests.python import support
from thunder_ide.core import docs
from thunder_ide.core.commands import COMMANDS


class HelpPageTests(unittest.TestCase):
    def test_every_help_command_has_a_page_that_exists_in_both_languages(self):
        for topic in docs.HELP_PAGES:
            for language in ("en", "ps"):
                with self.subTest(topic=topic, language=language):
                    page = docs.help_page(topic, language)
                    self.assertTrue(page.is_file(), f"{page} is missing: run python3 tools/apidoc.py")
                    self.assertEqual(page.parts[-len(Path(docs.HELP_PAGES[topic]).parts) - 1], language)

    def test_every_help_menu_entry_is_a_known_page(self):
        help_commands = [command.id for command in COMMANDS if command.menu == "menu.help" and command.id != "about"]
        self.assertEqual(sorted(help_commands), sorted(docs.HELP_PAGES))

    def test_pashto_pages_are_right_to_left(self):
        text = docs.help_page("help_guides", "ps").read_text(encoding="utf-8")
        self.assertIn('<html lang="ps" dir="rtl">', text)
        self.assertIn('<html lang="en" dir="ltr">', docs.help_page("help_guides", "en").read_text(encoding="utf-8"))

    def test_an_unknown_topic_or_language_falls_back(self):
        self.assertEqual(docs.help_page("no_such_topic", "en"), docs.home_page("en"))
        self.assertEqual(docs.help_page("help_guides", "fr"), docs.help_page("help_guides", "en"))
        self.assertTrue(docs.is_site_page(docs.home_page("ps")))
        self.assertFalse(docs.is_site_page(support.ROOT / "README.md"))

    def test_a_missing_translation_falls_back_to_english_and_a_missing_site_is_still_named(self):
        with mock.patch("thunder_ide.core.docs.api_root", return_value=support.ROOT / "docs" / "no_such_site"):
            page = docs.help_page("help_guides", "ps")
            self.assertEqual(page.parts[-3:], ("ps", "guides", "index.html"))
        real = docs.api_root()
        with mock.patch("thunder_ide.core.docs.api_root", return_value=real):
            # a page only present in English (the Pashto one removed) is answered with the English one
            english = docs.help_page("help_guides", "en")
            with mock.patch("pathlib.Path.is_file", autospec=True, side_effect=lambda self: self == english):
                self.assertEqual(docs.help_page("help_guides", "ps"), english)


if __name__ == "__main__":
    unittest.main()
