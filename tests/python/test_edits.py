"""The editor's text operations: comment, tidy, fold, find and replace (core.edits)."""

import unittest

from tests.python import support  # noqa: F401  (sets up sys.path)
from thunder_ide.core import edits

PROGRAM = (
    "func fib(n: int): int\n"
    "    if n < 2\n"
    "        return n\n"
    "\n"
    "    return fib(n - 1) + fib(n - 2)\n"
    "\n"
    "println(fib(10))\n"
)


class LineTests(unittest.TestCase):
    def test_line_bounds_and_selected_lines(self):
        text = "one\ntwo\nthree"
        self.assertEqual(edits.line_bounds(text, 0), (0, 3))
        self.assertEqual(edits.line_bounds(text, 3), (0, 3))     # the end of a line belongs to it
        self.assertEqual(edits.line_bounds(text, 5), (4, 7))
        self.assertEqual(edits.line_bounds(text, 999), (8, 13))
        self.assertEqual(edits.selected_lines(text, 1, 1), (0, 3))
        self.assertEqual(edits.selected_lines(text, 1, 6), (0, 7))
        self.assertEqual(edits.selected_lines(text, 6, 1), (0, 7))   # backwards selection
        self.assertEqual(edits.selected_lines(text, 0, 4), (0, 3))   # ending at a line start

    def test_indent_width_counts_a_tab_as_four(self):
        self.assertEqual(edits.indent_width("    x"), 4)
        self.assertEqual(edits.indent_width("\tx"), 4)
        self.assertEqual(edits.indent_width("  \tx"), 4)   # the tab finishes the group of four
        self.assertEqual(edits.indent_width("x"), 0)
        self.assertEqual(edits.indent_width(""), 0)


class CommentTests(unittest.TestCase):
    def test_comment_and_uncomment_round_trip(self):
        text, start, end = edits.toggle_comment(PROGRAM, 22, 55)
        # Every marker lines up at the shallowest indentation of the block, so it keeps its shape.
        self.assertIn("    // if n < 2\n", text)
        self.assertIn("    //     return n\n", text)
        self.assertEqual(text[start:end].splitlines()[0], "    // if n < 2")
        back, _, _ = edits.toggle_comment(text, start, end)
        self.assertEqual(back, PROGRAM)

    def test_the_marker_goes_at_the_shallowest_indentation(self):
        # The block keeps its shape: every // lines up with the least indented line.
        text, _, _ = edits.toggle_comment("    a\n        b\n", 0, 20)
        self.assertEqual(text, "    // a\n    //     b\n")

    def test_blank_lines_are_left_alone(self):
        text, _, _ = edits.toggle_comment("a\n\nb\n", 0, 5)
        self.assertEqual(text, "// a\n\n// b\n")
        self.assertEqual(edits.toggle_comment("\n\n", 0, 2)[0], "\n\n")   # nothing to comment

    def test_a_partly_commented_block_is_commented_further(self):
        text, _, _ = edits.toggle_comment("// a\nb\n", 0, 6)
        self.assertEqual(text, "// // a\n// b\n")

    def test_uncomment_accepts_a_marker_without_a_space(self):
        self.assertEqual(edits.toggle_comment("//a\n//b\n", 0, 7)[0], "a\nb\n")


class TidyTests(unittest.TestCase):
    def test_tabs_trailing_spaces_and_blank_lines(self):
        self.assertEqual(edits.tidy("a\tb  \n"), "a   b\n")           # the tab reaches column 4
        self.assertEqual(edits.tidy("\tx\n"), "    x\n")
        self.assertEqual(edits.tidy("a\n\n\n\nb"), "a\n\nb\n")        # runs of blanks become one
        self.assertEqual(edits.tidy("a\n\n\n"), "a\n")                # no blank lines at the end
        self.assertEqual(edits.tidy("a\r\nb\r\n"), "a\nb\n")          # Windows line endings
        self.assertEqual(edits.tidy(""), "")
        self.assertEqual(edits.tidy("\n\n"), "")

    def test_indentation_is_never_changed(self):
        # Thunder's indentation *is* its block structure, so tidying must not touch the amount.
        crooked = "func f()\n  println(1)\n      println(2)\n"
        self.assertEqual(edits.tidy(crooked), crooked)


class FoldTests(unittest.TestCase):
    def test_blocks_are_found_by_indentation(self):
        folds = [(fold.header, fold.last) for fold in edits.fold_regions(PROGRAM)]
        self.assertEqual(folds, [(0, 4), (1, 2)])   # the function, and the "if" inside it

    def test_a_blank_line_inside_a_block_belongs_to_it(self):
        self.assertEqual([(f.header, f.last) for f in edits.fold_regions("a\n    b\n\n    c\nd\n")], [(0, 3)])

    def test_a_single_line_is_not_a_block(self):
        self.assertEqual(edits.fold_regions("a\nb\nc\n"), [])
        self.assertEqual(edits.fold_regions(""), [])

    def test_fold_at_finds_the_innermost_block(self):
        self.assertEqual(edits.fold_at(PROGRAM, 2), edits.Fold(1, 2))   # inside the "if"
        self.assertEqual(edits.fold_at(PROGRAM, 4), edits.Fold(0, 4))   # only the function
        self.assertIsNone(edits.fold_at(PROGRAM, 6))                    # the println at the end


class LineCommandTests(unittest.TestCase):
    TEXT = "one\n    two\nthree"

    def test_duplicate_lines(self):
        text, start, end = edits.duplicate_lines(self.TEXT, 5, 5)
        self.assertEqual(text, "one\n    two\n    two\nthree")
        self.assertEqual(text[start:end], "    two")                 # the selection is the copy
        text, start, end = edits.duplicate_lines("a\nb", 0, 3)        # several lines, at the end
        self.assertEqual(text, "a\nb\na\nb")
        self.assertEqual((start, end), (4, 7))

    def test_delete_lines(self):
        self.assertEqual(edits.delete_lines(self.TEXT, 5, 5), ("one\nthree", 4, 4))
        self.assertEqual(edits.delete_lines(self.TEXT, 13, 13), ("one\n    two", 11, 11))  # the last line
        self.assertEqual(edits.delete_lines("only", 2, 2), ("", 0, 0))
        self.assertEqual(edits.delete_lines(self.TEXT, 0, 6), ("three", 0, 0))         # two lines

    def test_move_lines(self):
        text, start, end = edits.move_lines(self.TEXT, 5, 5, -1)
        self.assertEqual(text, "    two\none\nthree")
        self.assertEqual(text[start:end], "    two")
        self.assertEqual(edits.move_lines(self.TEXT, 0, 0, -1), (self.TEXT, 0, 0))     # already first
        text, start, end = edits.move_lines(self.TEXT, 5, 5, 1)
        self.assertEqual(text, "one\nthree\n    two")
        self.assertEqual(text[start:end], "    two")
        self.assertEqual(edits.move_lines(self.TEXT, 13, 13, 1), (self.TEXT, 13, 13))   # already last
        text, start, end = edits.move_lines("a\nb\nc\nd", 0, 3, 1)                      # two lines together
        self.assertEqual(text, "c\na\nb\nd")
        self.assertEqual(text[start:end], "a\nb")

    def test_indent_and_outdent(self):
        text, start, end = edits.indent_lines("a\n\nb", 0, 4)
        self.assertEqual(text, "    a\n\n    b")                      # blank lines are left alone
        self.assertEqual(text[start:end], "    a\n\n    b")
        self.assertEqual(edits.outdent_lines(text, start, end), ("a\n\nb", 0, 4))
        self.assertEqual(edits.outdent_lines("  a\n\tb\nc", 0, 8), ("a\nb\nc", 0, 5))  # 2 spaces, a tab, nothing


class FindTests(unittest.TestCase):
    def test_find_forwards_backwards_and_wrapping(self):
        text = "one two one two"
        self.assertEqual(edits.find(text, "two", 0), (4, 7))
        self.assertEqual(edits.find(text, "two", 7), (12, 15))
        self.assertEqual(edits.find(text, "two", 13), (4, 7))              # wrapped round
        self.assertIsNone(edits.find(text, "two", 13, wrap=False))
        self.assertEqual(edits.find(text, "two", 15, backwards=True), (12, 15))
        self.assertEqual(edits.find(text, "two", 12, backwards=True), (4, 7))
        self.assertEqual(edits.find(text, "two", 0, backwards=True), (12, 15))   # wrapped round
        self.assertIsNone(edits.find(text, "three", 0))
        self.assertIsNone(edits.find(text, "", 0))

    def test_case_and_whole_word(self):
        text = "Count the counter and the count."
        self.assertEqual(edits.find(text, "count", 0), (0, 5))                        # ignores case
        self.assertEqual(edits.find(text, "count", 0, case_sensitive=True), (10, 15))
        self.assertEqual(edits.find(text, "count", 0, whole_word=True), (0, 5))
        self.assertEqual(edits.find(text, "count", 5, whole_word=True), (26, 31))     # not "counter"

    def test_pashto_words_are_found_whole(self):
        text = "متغیر شمېر = شمېرل(۳)"
        self.assertEqual(edits.find(text, "شمېر", 0, whole_word=True), (6, 10))
        self.assertIsNone(edits.find(text, "شمېر", 11, whole_word=True, wrap=False))  # not شمېرل

    def test_replace_all_and_counting(self):
        text = "a + a + b"
        self.assertEqual(edits.replace_all(text, "a", "z"), ("z + z + b", 2))
        self.assertEqual(edits.matches(text, "a"), 2)
        self.assertEqual(edits.replace_all(text, "q", "z"), (text, 0))
        self.assertEqual(edits.replace_all(text, "", "z"), (text, 0))
        self.assertEqual(edits.matches(text, ""), 0)
        # The replacement is literal: a backslash or a group reference is not interpreted.
        self.assertEqual(edits.replace_all("a", "a", r"\1\\"), (r"\1\\", 1))


if __name__ == "__main__":
    unittest.main()
