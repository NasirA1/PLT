"""Token-level bidirectional layout of source lines (core.direction)."""

import unittest

from tests.python import support  # noqa: F401  (sets up sys.path)
from thunder_ide.core import direction
from thunder_ide.core.direction import FSI, LRM, PDI, Token, analyse_line, index_to_utf16, utf16_to_index


class DirectionTests(unittest.TestCase):
    def test_pure_left_to_right_text_is_untouched(self):
        layout = analyse_line('var x = "a"', False)
        self.assertEqual(layout.display, 'var x = "a"')
        self.assertEqual(layout.offsets, tuple(range(12)))
        self.assertEqual(layout.tokens, ())
        self.assertEqual(layout.caret(0), (0, False))
        self.assertEqual(layout.caret(4), (3, True))
        self.assertEqual(layout.caret(99), (10, True))

    def test_tokens_are_isolated_in_right_to_left_programs(self):
        text = 'متغیر name = "Nasir"'
        layout = analyse_line(text, True)
        self.assertEqual(layout.display, f'{FSI}متغیر{PDI} {FSI}name{PDI} = {FSI}"Nasir"{PDI}')
        self.assertEqual(layout.tokens, (Token(0, 5, False), Token(6, 10, True), Token(13, 20, True)))
        # every logical index maps to its own character in the display string
        for index, char in enumerate(text):
            self.assertEqual(layout.display[layout.offsets[index]], char)
        self.assertEqual(layout.offsets[len(text)], len(layout.display))
        self.assertEqual([layout.to_logical(i) for i in range(len(layout.display) + 1)],
                         [0, 0, 1, 2, 3, 4, 5, 5, 6, 6, 7, 8, 9, 10, 10, 11, 12, 13, 13, 14, 15, 16, 17, 18, 19, 20, 20])

    def test_token_kinds(self):
        layout = analyse_line('که (x < ۱۸) // ژبه: ps "«a»" /* c', True)
        self.assertEqual([layout.text[t.start:t.end] for t in layout.tokens],
                         ["که", "x", "۱۸", '// ژبه: ps "«a»" /* c'])
        strings = analyse_line('x = "a\\"b" + «c»', True)
        self.assertEqual([strings.text[t.start:t.end] for t in strings.tokens], ["x", '"a\\"b"', "«c»"])
        unterminated = analyse_line('"open', True)
        self.assertEqual([unterminated.text[t.start:t.end] for t in unterminated.tokens], ['"open'])

    def test_token_direction(self):
        self.assertTrue(direction.is_right_to_left_text("متغیر"))
        self.assertFalse(direction.is_right_to_left_text("name"))
        self.assertFalse(direction.is_right_to_left_text("۱۸"))   # digits alone read left to right (as FSI decides)
        self.assertTrue(direction.is_right_to_left_text('"ته."'))
        self.assertFalse(direction.is_right_to_left_text("(x)"))
        self.assertTrue(direction.has_right_to_left_text("x = ١"))    # Arabic-Indic digits are Arabic numbers
        self.assertFalse(direction.has_right_to_left_text("x = ۱"))   # Pashto digits are European numbers
        self.assertFalse(direction.has_right_to_left_text("x = 1"))
        # In an English program a Pashto name is the opposite-direction token.
        layout = analyse_line("var ټکی = 1", False)
        self.assertEqual(layout.tokens, (Token(0, 3, False), Token(4, 7, True), Token(10, 11, False)))

    def test_negative_numbers_keep_their_sign_on_the_left(self):
        # Negative numbers are written sign-first in every script (an exception to the
        # right-to-left reading): "-۳" is one unit, so the minus never jumps to the right.
        layout = analyse_line("متغیر س = -۳", True)
        self.assertIn(FSI + LRM + "-۳" + LRM + PDI, layout.display)  # sign-first, inside the isolate
        calls = analyse_line("دایره(-۷، ص - ۲، -x)", True)
        self.assertIn(FSI + LRM + "-۷" + LRM + PDI, calls.display)  # after '(' the minus is a sign
        self.assertIn(FSI + "ص" + PDI + " - " + FSI + LRM + "۲" + LRM + PDI, calls.display)  # subtraction is not merged
        self.assertNotIn(FSI + "-x" + PDI, calls.display)   # only numbers get the sign
        latin = analyse_line("print(-42, a - 1)", True)
        self.assertIn(FSI + LRM + "-42" + LRM + PDI, latin.display)
        self.assertIn(FSI + "a" + PDI + " - " + FSI + LRM + "1" + LRM + PDI, latin.display)
        start = analyse_line("-۵", True)
        self.assertIn(FSI + LRM + "-۵" + LRM + PDI, start.display)
        # Inside comments and strings (prose for the bidirectional algorithm) the sign gets a
        # small left-to-right isolate of its own; ranges such as ۵-۳ are left alone.
        comment = analyse_line("// پایله: -۳ ده، ۵-۳ نه", True)
        self.assertIn(LRM + "-۳" + LRM, comment.display)
        self.assertIn("۵-۳", comment.display)
        quoted = analyse_line('"تاوان -۴۲"', True)
        self.assertIn(LRM + "-۴۲" + LRM, quoted.display)
        # The mapping between logical and display positions still round-trips.
        for logical in range(len(comment.text) + 1):
            self.assertEqual(comment.to_logical(comment.offsets[logical]), logical)

    def test_caret_rule(self):
        text = 'متغیر name = "Nasir"'
        layout = analyse_line(text, True)
        d = layout.offsets
        self.assertEqual(layout.caret(0), (0, False))                 # start edge of the line
        self.assertEqual(layout.caret(1), (d[1], False))              # inside a token: before its next character
        self.assertEqual(layout.caret(5), (d[4], True))               # after a token's last character
        self.assertEqual(layout.caret(6), (d[5], True))               # after the space
        self.assertEqual(layout.caret(7), (d[7], False))              # after "n": before "a"
        self.assertEqual(layout.caret(10), (d[9], True))              # after "name"
        self.assertEqual(layout.caret(len(text)), (d[len(text) - 1], True))
        self.assertEqual(layout.caret(len(text) + 5), layout.caret(len(text)))
        self.assertEqual(layout.caret(-1), (0, False))
        # One-character tokens use their isolate marks: the mark before an opposite-direction
        # token, the mark after a same-direction one (Qt cannot be asked about the first character).
        mixed = analyse_line("متغیر x = ۱", True)
        self.assertEqual(mixed.caret(7), (mixed.offsets[6] - 1, False))
        self.assertEqual(mixed.display[mixed.offsets[6] - 1], FSI)
        same = analyse_line("x د y", False)
        self.assertEqual(same.caret(1), (same.offsets[0] + 1, False))
        self.assertEqual(same.display[same.offsets[0] + 1], PDI)
        self.assertEqual(same.caret(3), (same.offsets[2] - 1, False))
        self.assertIsNone(same.token_containing(1))
        self.assertEqual(analyse_line("", True).caret(0), (0, False))
        self.assertEqual(analyse_line("", True).caret(3), (0, False))

    def test_utf16_conversion(self):
        text = "a😀b"
        self.assertEqual([index_to_utf16(text, i) for i in range(4)], [0, 1, 3, 4])
        self.assertEqual([utf16_to_index(text, u) for u in range(6)], [0, 1, 1, 2, 3, 3])
        self.assertEqual(index_to_utf16("abc", 2), 2)
        self.assertEqual(utf16_to_index("abc", 7), 3)
        self.assertEqual(utf16_to_index("abc", -2), 0)


if __name__ == "__main__":
    unittest.main()
