import unittest

from tests.python import support  # noqa: F401
from thunder_ide.core import bidi


class BidiTests(unittest.TestCase):
    def test_left_to_right_text_is_unchanged(self):
        self.assertEqual(bidi.visual_text("hello (world) 42"), "hello (world) 42")
        self.assertFalse(bidi.has_rtl("abc"))
        self.assertTrue(bidi.has_rtl("abc پ"))

    def test_paragraph_direction(self):
        self.assertTrue(bidi.paragraph_is_rtl("سلام abc"))
        self.assertFalse(bidi.paragraph_is_rtl("abc سلام"))
        self.assertTrue(bidi.paragraph_is_rtl("123 ...", default_rtl=True))
        self.assertFalse(bidi.paragraph_is_rtl("123 ..."))

    def test_right_to_left_runs_are_reversed_and_brackets_mirrored(self):
        self.assertEqual(bidi.visual_text("ab"[::-1]), "ba")
        levels = bidi.bidi_levels("که (x > ۵)", rtl=True)
        self.assertEqual(levels[0], 1)  # Arabic letter
        self.assertEqual(levels[4], 2)  # Latin letter inside an RTL paragraph
        # Pashto digits have class EN; after a Latin letter they read left to right (rule W7),
        # so the parenthesised expression keeps its order inside the right-to-left line.
        self.assertEqual(bidi.visual_text("ب (x > ۵)", default_rtl=True), "(x > ۵) ﺏ")
        # Between right-to-left words, brackets and '>' are mirrored.
        self.assertEqual(bidi.visual_text("ب (پ > ت)"), "(ﺕ < ﭖ) ﺏ")

    def test_numbers_stay_left_to_right_inside_rtl_text(self):
        self.assertEqual(bidi.visual_text("ب 123 ب"), "ﺏ 123 ﺏ")
        self.assertEqual(bidi.visual_text("ب ۱۲۳"), "۱۲۳ ﺏ")

    def test_mixed_text_in_a_left_to_right_paragraph(self):
        self.assertEqual(bidi.visual_text("Pashto: بپ 12"), "Pashto: 12 ﭗﺑ")

    def test_weak_type_rules(self):
        # W2 + W4: after an Arabic letter, digits are Arabic numbers and "1,5" stays together;
        # the "%" is then neutral and ends up at the far (left) end of the line.
        self.assertEqual(bidi.visual_text("ب 1,5 10%"), "%10 1,5 ﺏ")
        # W5: in left-to-right text a terminator next to a number belongs to the number.
        self.assertEqual(bidi.bidi_levels("10% ب", rtl=False), [0, 0, 0, 0, 1])
        # W1: a combining mark takes its base letter's direction.
        self.assertEqual(bidi.bidi_levels("بَ", rtl=False), [1, 1])
        # W2: European digits after an Arabic letter become Arabic numbers.
        self.assertEqual(bidi.bidi_levels("ب1", rtl=False), [1, 2])
        # Explicit embedding controls are ignored, trailing spaces go to the paragraph level (L1).
        self.assertEqual(bidi.bidi_class("\u202b"), "BN")
        self.assertEqual(bidi.bidi_levels("ب  ", rtl=False), [1, 0, 0])

    def test_visual_order(self):
        self.assertEqual(bidi.visual_order([0, 0, 1, 1, 0]), [0, 1, 3, 2, 4])
        self.assertEqual(bidi.visual_order([1, 2, 2, 1]), [3, 1, 2, 0])
        self.assertEqual(bidi.visual_order([]), [])

    def test_shaping_uses_contextual_forms(self):
        self.assertEqual(bidi.shape("بب"), ["ﺑ", "ﺐ"])        # initial, final
        self.assertEqual(bidi.shape("ببب"), ["ﺑ", "ﺒ", "ﺐ"])  # initial, medial, final
        self.assertEqual(bidi.shape("دب"), ["ﺩ", "ﺏ"])        # dal never joins to the next letter
        self.assertEqual(bidi.shape("لا"), ["ﻻ", ""])          # lam-alef ligature
        self.assertEqual(bidi.shape("بلا"), ["ﺑ", "ﻼ", ""])
        self.assertEqual(bidi.shape("ab"), ["a", "b"])

    def test_letters_without_presentation_forms_do_not_join(self):
        self.assertEqual(bidi.joining_type("ښ"), "U")
        self.assertEqual(bidi.shape("بښب"), ["ﺏ", "ښ", "ﺏ"])

    def test_joining_types(self):
        self.assertEqual(bidi.joining_type("ب"), "D")
        self.assertEqual(bidi.joining_type("ا"), "R")
        self.assertEqual(bidi.joining_type("ـ"), "C")
        self.assertEqual(bidi.joining_type("\u064e"), "T")  # fatha
        self.assertEqual(bidi.joining_type("x"), "U")
        self.assertEqual(bidi.shape("ب\u064eب"), ["ﺑ", "\u064e", "ﺐ"])  # marks are transparent

    def test_render_line_with_styles(self):
        chunks, rtl = bidi.render_line("که x", ["keyword", "keyword", None, None])
        self.assertTrue(rtl)
        self.assertEqual([(c.text, c.style, c.rtl) for c in chunks], [("x", None, False), (" ", None, True),
                                                                       ("ﻪﮐ", "keyword", True)])
        self.assertEqual(bidi.render_line("", None), ([], False))
        plain, rtl = bidi.render_line("abc", ["a", "a", "b"])
        self.assertFalse(rtl)
        self.assertEqual([c.text for c in plain], ["ab", "c"])

    def test_native_mode_only_orders_chunks(self):
        chunks, rtl = bidi.render_line("که (x)", ["kw", "kw", None, None, None, None], reverse_rtl_text=False)
        self.assertTrue(rtl)
        # Neutral-only chunks in right-to-left runs are mirrored; the OS reorders "که" itself.
        self.assertEqual([c.text for c in chunks], ["(", "x", ") ", "که"])
        self.assertEqual(bidi.visual_text("سلام", reverse_rtl_text=False), "سلام")


if __name__ == "__main__":
    unittest.main()
