"""The colours of a test file's transcript in the Output panel (core/testoutput.py)."""

import unittest

from tests.python import support  # noqa: F401 - puts the IDE on the path

from thunder_ide.core.testoutput import FAIL, PASS, segments


class TestOutputTests(unittest.TestCase):
    def test_marks_are_green_and_red_at_the_start_of_a_line(self):
        pieces, at_start = segments("✓ adds\n  ✗ wrong: expected 2, got 1\n", True)
        self.assertEqual(pieces, [("✓", PASS), (" adds\n  ", None), ("✗", FAIL), (" wrong: expected 2, got 1\n", None)])
        self.assertTrue(at_start)
        # A mark in the middle of a line is just text.
        self.assertEqual(segments("it says ✓\n", True)[0], [("it says ✓\n", None)])

    def test_the_summary_colours_its_counts(self):
        self.assertEqual(segments("3 passed, 1 failed", True)[0],
                         [("3 passed", PASS), (", ", None), ("1 failed", FAIL)])
        # No failure is not an alarm: the zero keeps the ordinary colour.
        self.assertEqual(segments("numbers: 2 passed, 0 failed\n", True)[0],
                         [("numbers: ", None), ("2 passed", PASS), (", 0 failed\n", None)])
        self.assertEqual(segments("۲ تېرې شوې، ۱ ناکامې", True)[0],
                         [("۲ تېرې شوې", PASS), ("، ", None), ("۱ ناکامې", FAIL)])

    def test_pieces_keep_the_line_state_and_give_the_text_back(self):
        text = "suite\n  ✓ one\n  ✗ two: no\n1 passed, 1 failed\n"
        first, at_start = segments(text[:9], True)   # "suite\n  ✓"
        second, _ = segments(text[9:], at_start)
        self.assertEqual("".join(piece for piece, _ in first + second), text)
        self.assertIn(("✓", PASS), first)
        # A piece that continues a line has no marks to find.
        self.assertEqual(segments("✓ more", False)[0], [("✓ more", None)])


if __name__ == "__main__":
    unittest.main()
