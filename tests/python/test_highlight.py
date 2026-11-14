import unittest

from tests.python import support  # noqa: F401  (sets up sys.path)
from thunder_ide.core.highlight import Highlight, WordClasses, highlight


class CompoundWordTests(unittest.TestCase):
    def test_compound_firsts_takes_the_first_word_only(self):
        classes = WordClasses({"کرښه چاپ": "builtin", "چاپ": "builtin", "a b c": "keyword"})
        self.assertEqual(classes.compound_firsts, {"کرښه", "a"})


class HighlightTests(unittest.TestCase):
    def setUp(self):
        self.classes = WordClasses.default()

    def tags(self, text):
        return [(h.tag, text[h.start:h.end]) for h in highlight(text, self.classes)]

    def test_keywords_types_literals_and_builtins(self):
        self.assertEqual(self.tags("var x: int = true; println(len(s))"), [
            ("keyword", "var"), ("type", "int"), ("literal", "true"), ("builtin", "println"), ("builtin", "len"),
        ])

    def test_numbers_strings_and_comments(self):
        self.assertEqual(self.tags('x = 42; # note\ns = "a \\" b"; /* block\ncomment */'), [
            ("number", "42"), ("comment", "# note"), ("string", '"a \\" b"'), ("comment", "/* block\ncomment */"),
        ])

    def test_arabic_keyboard_letters_are_folded(self):
        # كرښه چاپ typed with the Arabic kaf (U+0643) means کرښه چاپ (Pashto keheh, U+06A9), and
        # راگرځه with the Persian gaf means راګرځه; the compiler folds the same way.
        info = {"keywords": [{"kind": "KwIf", "category": "keyword", "text": "که", "spellings": ["که"]},
                             {"kind": "KwReturn", "category": "keyword", "text": "راګرځه", "spellings": ["راګرځه"]}],
                "builtins": [{"name": "کرښه چاپ"}]}
        classes = WordClasses.from_language_info(info)
        tagged = [(h.tag, text[h.start:h.end]) for text in ["كه كرښه چاپ(۱) راگرځه"]
                  for h in highlight(text, classes)]
        self.assertEqual(tagged, [("keyword", "كه"), ("builtin", "كرښه چاپ"), ("number", "۱"), ("keyword", "راگرځه")])

    def test_keywords_inside_strings_and_comments_are_not_keywords(self):
        self.assertEqual(self.tags('"while" # if'), [("string", '"while"'), ("comment", "# if")])

    def test_unterminated_string_and_comment(self):
        self.assertEqual(self.tags('"open\nvar'), [("string", '"open'), ("keyword", "var")])
        self.assertEqual(self.tags("/* never closed var"), [("comment", "/* never closed var")])

    def test_identifiers_in_other_scripts(self):
        self.assertEqual(self.tags("var شمیر = 1"), [("keyword", "var"), ("number", "1")])

    def test_word_classes_from_language_info(self):
        info = {
            "keywords": [
                {"kind": "KwWhile", "category": "keyword", "text": "repeat"},
                {"kind": "KwInt", "category": "type", "text": "integer"},
                {"kind": "KwTrue", "category": "literal", "text": "yes"},
                {"kind": "Odd", "category": "special", "text": "odd"},
            ],
            "builtins": [{"name": "show"}],
        }
        classes = WordClasses.from_language_info(info)
        self.assertEqual(classes.words, {"repeat": "keyword", "integer": "type", "yes": "literal", "odd": "keyword",
                                         "show": "builtin"})
        self.assertEqual(highlight("repeat while", classes), [Highlight("keyword", 0, 6)])

    def test_multi_word_names(self):
        classes = WordClasses({"کرښه چاپ": "builtin", "چاپ": "builtin", "متن کړه": "builtin", "var": "keyword"})
        self.assertEqual(classes.compound_firsts, {"کرښه", "متن"})
        text = "کرښه چاپ(x) کرښه  \tچاپ کرښه\nچاپ متن کړه کرښه x"
        self.assertEqual([(h.tag, text[h.start:h.end]) for h in highlight(text, classes)],
                         [("builtin", "کرښه چاپ"), ("builtin", "کرښه  \tچاپ"), ("builtin", "چاپ"), ("builtin", "متن کړه")])
        self.assertEqual(self.tags("var متن کړه"), [("keyword", "var")])  # unknown to the default classes


if __name__ == "__main__":
    unittest.main()
