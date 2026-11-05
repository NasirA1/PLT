import unittest

from tests.python import support
from thunder_ide.core.formatter import format_allman


class FormatterTests(unittest.TestCase):
    def test_moves_braces_onto_their_own_lines(self):
        source = 'if (x > 0) {\nprintln("yes");\n} else {\nprintln("no");\n}\n'
        expected = 'if (x > 0)\n{\n    println("yes");\n}\nelse\n{\n    println("no");\n}\n'
        self.assertEqual(format_allman(source), expected)

    def test_reindents_nested_blocks(self):
        source = "func f()\n{\nwhile (true)\n{\nbreak;\n}\n}\n"
        expected = "func f()\n{\n    while (true)\n    {\n        break;\n    }\n}\n"
        self.assertEqual(format_allman(source), expected)

    def test_one_line_blocks_are_expanded(self):
        self.assertEqual(format_allman("while (x) { x = false; }"), "while (x)\n{\n    x = false;\n}\n")

    def test_braces_in_strings_and_comments_are_kept(self):
        source = 'println("{not a block}"); // { nor this\n'
        self.assertEqual(format_allman(source), source)

    def test_comment_after_a_brace_moves_inside_the_block(self):
        self.assertEqual(format_allman("if (x) { // why\ny();\n}"), "if (x)\n{\n    // why\n    y();\n}\n")

    def test_block_comments_are_left_alone(self):
        source = "{\n/* keep\n   this { exactly\n*/\nx = 1;\n}\n"
        expected = "{\n    /* keep\n   this { exactly\n*/\n    x = 1;\n}\n"
        self.assertEqual(format_allman(source), expected)
        self.assertEqual(format_allman("/* one line */\n"), "/* one line */\n")

    def test_blank_lines_are_preserved_but_collapsed(self):
        self.assertEqual(format_allman("a();\n\n\n\nb();\n\n\n"), "a();\n\nb();\n")

    def test_division_is_not_a_comment(self):
        self.assertEqual(format_allman("x = a / b; {"), "x = a / b;\n{\n")

    def test_unbalanced_closing_braces_do_not_go_negative(self):
        self.assertEqual(format_allman("}\nx();"), "}\nx();\n")

    def test_formatting_is_idempotent_and_samples_are_already_formatted(self):
        for path in sorted(support.SAMPLES.glob("*.th")):
            text = path.read_text(encoding="utf-8")
            with self.subTest(sample=path.name):
                self.assertEqual(format_allman(format_allman(text)), format_allman(text))
                # The e*.th samples contain deliberate syntax errors (e.g. an if without braces).
                if not path.name.startswith("e"):
                    self.assertEqual(format_allman(text), text)


if __name__ == "__main__":
    unittest.main()
