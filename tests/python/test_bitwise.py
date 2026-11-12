"""The Bits tab's model (core.bitwise): parsing, the operations and the bit layout."""

import unittest

from tests.python import support  # noqa: F401  (sets up sys.path)
from thunder_ide.core import bitwise


class ParseTests(unittest.TestCase):
    def test_accepts_decimal_hex_binary_and_pashto_digits(self):
        self.assertEqual(bitwise.parse_int("42"), 42)
        self.assertEqual(bitwise.parse_int(" -7 "), -7)
        self.assertEqual(bitwise.parse_int("0x2A"), 42)
        self.assertEqual(bitwise.parse_int("0b1010"), 10)
        self.assertEqual(bitwise.parse_int("۴۲"), 42)
        self.assertEqual(bitwise.parse_int("١٢"), 12)

    def test_rejects_rubbish_and_numbers_that_do_not_fit(self):
        for text in ("", "x", "1.5", "--3", str(2**63), str(-(2**63) - 1)):
            self.assertIsNone(bitwise.parse_int(text), text)
        self.assertEqual(bitwise.parse_int(str(2**63 - 1)), 2**63 - 1)  # the edges fit
        self.assertEqual(bitwise.parse_int(str(-(2**63))), -(2**63))


class VisualiseTests(unittest.TestCase):
    def test_and_lays_out_three_rows_and_marks_the_changed_bits(self):
        view = bitwise.visualise("12", "&", "10")
        self.assertEqual(view.expression, "12 & 10 = 8")
        self.assertEqual(view.width, 8)
        self.assertEqual([(r.label, r.bits, r.value) for r in view.rows],
                         [("a", "00001100", 12), ("& b", "00001010", 10), ("a & b", "00001000", 8)])
        self.assertEqual(view.highlight, (5,))  # bit 4 (value 4) was switched off

    def test_every_operator_matches_the_language(self):
        self.assertEqual(bitwise.visualise("12", "|", "10").expression, "12 | 10 = 14")
        self.assertEqual(bitwise.visualise("12", "^", "10").expression, "12 ^ 10 = 6")
        self.assertEqual(bitwise.visualise("1", "<<", "4").expression, "1 << 4 = 16")
        self.assertEqual(bitwise.visualise("-16", ">>", "2").expression, "-16 >> 2 = -4")
        self.assertEqual(bitwise.visualise("5", "~", "").expression, "~5 = -6")

    def test_width_grows_with_the_numbers(self):
        self.assertEqual(bitwise.visualise("200", "&", "1").width, 16)
        self.assertEqual(bitwise.visualise("70000", "&", "1").width, 32)
        self.assertEqual(bitwise.visualise("1", "<<", "40").width, 64)
        self.assertEqual(bitwise.visualise("-1", ">>", "1").rows[-1].bits, "1" * 8)  # the sign is kept

    def test_width_boundaries_are_exact(self):
        # The two's complement edges of each width (kills boundary mutants in _fits).
        self.assertEqual(bitwise.visualise("127", "&", "1").width, 8)
        self.assertEqual(bitwise.visualise("128", "&", "1").width, 16)
        self.assertEqual(bitwise.visualise("-128", "&", "1").width, 8)
        self.assertEqual(bitwise.visualise("-129", "&", "1").width, 16)

    def test_error_views_are_empty(self):
        view = bitwise.visualise("x", "&", "1")
        self.assertEqual((view.width, view.rows, view.expression), (0, (), ""))

    def test_bits_above_the_width_fall_away(self):
        self.assertEqual(bitwise.visualise("3", "<<", "63").rows[-1].value, -(2**63))

    def test_shift_by_zero_is_allowed(self):
        self.assertEqual(bitwise.visualise("5", "<<", "0").expression, "5 << 0 = 5")
        self.assertEqual(bitwise.visualise("5", ">>", "0").rows[-1].value, 5)

    def test_shifts_use_the_vm_semantics(self):
        self.assertEqual(bitwise.visualise("1", "<<", "63").rows[-1].value, -(2**63))  # wraps, as the VM
        self.assertEqual(bitwise.visualise("2", "<<", "63").rows[-1].value, 0)         # bits fall off

    def test_errors_name_the_problem(self):
        self.assertEqual(bitwise.visualise("x", "&", "1").error, "bits.error.number")
        self.assertEqual(bitwise.visualise("x", "&", "1").error_detail, "x")
        self.assertEqual(bitwise.visualise("1", "&", "").error, "bits.error.number")
        self.assertEqual(bitwise.visualise("1", "<<", "64").error, "bits.error.shift")
        self.assertEqual(bitwise.visualise("1", "<<", "-1").error, "bits.error.shift")
        self.assertEqual(bitwise.visualise("1", "?", "2").error, "bits.error.operator")


if __name__ == "__main__":
    unittest.main()
