import tempfile
import unittest
from pathlib import Path

from tests.python import support
from thunder_ide.core import explain, i18n
from thunder_ide.core.samples import Sample, counterpart, list_samples, samples_dir

REPORT = {
    "phases": [
        {"name": "lexical analysis", "ran": True, "succeeded": True, "microseconds": 120},
        {"name": "syntax analysis", "ran": True, "succeeded": True, "microseconds": 80},
        {"name": "semantic analysis", "ran": True, "succeeded": False, "microseconds": 50},
        {"name": "code generation", "ran": False, "succeeded": False, "microseconds": 0},
    ],
    "tokens": [{}, {}, {}],
    "ast": {"kind": "Program", "children": [{"kind": "Var", "children": []}]},
    "symbols": {"scopes": {"symbols": [{}], "children": [{"symbols": [{}, {}], "children": []}]},
                "functions": [{}, {}], "structs": [{}]},
    "bytecode": {"functions": [{"instructions": [{}, {}]}, {"instructions": [{}]}]},
    "binary": {"size": 99},
}


class ExplainTests(unittest.TestCase):
    def tearDown(self):
        i18n.set_language("en")

    def test_counts(self):
        self.assertEqual(explain.count_ast_nodes(REPORT["ast"]), 2)
        self.assertEqual(explain.count_ast_nodes(None), 0)
        self.assertEqual(explain.count_symbols(REPORT["symbols"]), 3)
        self.assertEqual(explain.count_symbols(None), 0)
        self.assertEqual(list(explain.iter_scopes(None)), [])

    def test_phase_details(self):
        self.assertEqual(explain.phase_result_detail("lexical analysis", REPORT), "2 tokens")
        self.assertEqual(explain.phase_result_detail("syntax analysis", REPORT), "2 tree nodes")
        self.assertEqual(explain.phase_result_detail("semantic analysis", REPORT), "3 symbols, 1 function(s), 1 struct(s)")
        self.assertEqual(explain.phase_result_detail("code generation", REPORT), "3 instructions, 99 bytes")
        self.assertEqual(explain.phase_result_detail("unknown", REPORT), "")

    def test_phase_info(self):
        self.assertEqual(explain.phase_info("syntax analysis").tab, "Syntax Tree tab")
        self.assertEqual(explain.phase_info("execution").produces, "program output")
        self.assertIsNone(explain.phase_info("nope"))
        i18n.set_language("ps")
        self.assertEqual(explain.phase_info("lexical analysis").title, "لغوي تحلیل")

    def test_pipeline_before_compiling(self):
        lines = explain.pipeline_lines(None)
        self.assertEqual(lines[0], ("The compilation pipeline", "title"))
        self.assertIn("skipped", {style for _, style in lines})

    def test_pipeline_after_a_failed_compile(self):
        text = "\n".join(line for line, _ in explain.pipeline_lines(REPORT))
        self.assertIn("1. Lexical analysis     OK       0.12 ms, 2 tokens", text)
        self.assertIn("3. Semantic analysis    FAILED", text)
        self.assertIn("4. Code generation      not run", text)
        self.assertIn("press Run or Debug", text)
        self.assertIn("   ↓", text)

    def test_pipeline_after_running(self):
        ok = explain.pipeline_lines(REPORT, stats={"steps": 10, "calls": 2, "max_stack_depth": 3})
        self.assertIn(("   |  5. Execution            OK       10 instructions, 2 call(s), max stack depth 3", "ok"), ok)
        failed = explain.pipeline_lines(REPORT, runtime_error={"message": "division by zero"})
        self.assertIn(("   |  5. Execution            FAILED   division by zero", "failed"), failed)

    def test_failure_explanation(self):
        text = explain.failure_explanation(REPORT)
        self.assertTrue(text.startswith("Compilation stopped during Semantic analysis."))
        self.assertIn("these phases did not run: Code generation.", text)
        self.assertEqual(explain.failure_explanation({"phases": []}), "")
        self.assertIn("bug", explain.failure_explanation({"phases": [], "internal_error": "boom"}))
        unknown = {"phases": [{"name": "mystery", "ran": True, "succeeded": False}]}
        self.assertEqual(explain.failure_explanation(unknown), "Compilation stopped during mystery.")

    def test_hex_dump_rows(self):
        rows = explain.hex_dump_rows("54484e44" + "00" * 20, [{"name": "header", "offset": 0, "size": 4},
                                                            {"name": "rest", "offset": 4, "size": 20}])
        self.assertEqual(rows[0].text, "; header (4 bytes)")
        self.assertTrue(rows[1].text.startswith("00000000  54 48 4e 44"))
        self.assertTrue(rows[1].text.endswith("THND"))
        self.assertEqual([row.section for row in rows], [0, 0, 1, 1, 1])

    def test_ast_labels(self):
        self.assertEqual(explain.format_ast_label({"kind": "Binary", "label": "+", "role": "left"}), "left: Binary +")
        self.assertEqual(explain.format_ast_label({"kind": "Block", "label": "", "role": ""}), "Block")


class I18nTests(unittest.TestCase):
    def tearDown(self):
        i18n.set_language("en")

    def test_lookup_and_formatting(self):
        self.assertEqual(i18n.tr("toolbar.run"), "Run")
        self.assertEqual(i18n.tr("vm.step", index=2, total=9), "Step 2 of 9")
        self.assertEqual(i18n.tr("no.such.key"), "no.such.key")
        self.assertFalse(i18n.is_right_to_left())
        self.assertEqual(i18n.current_language(), "en")

    def test_pashto_is_complete_and_right_to_left(self):
        import string

        english = i18n.UI_LANGUAGES["en"]["strings"]
        pashto = i18n.UI_LANGUAGES["ps"]["strings"]
        self.assertEqual(set(english), set(pashto))
        for key, text in english.items():
            fields = lambda t: {f for _, f, _, _ in string.Formatter().parse(t) if f}  # noqa: E731
            self.assertEqual(fields(text), fields(pashto[key]), key)
        i18n.set_language("ps")
        self.assertTrue(i18n.is_right_to_left())
        self.assertEqual(i18n.tr("toolbar.run"), "چلول")

    def test_unknown_languages_fall_back_to_english(self):
        i18n.UI_LANGUAGES["xx"] = {"name": "X", "right_to_left": False, "strings": {"toolbar.run": "Go"}}
        try:
            i18n.set_language("xx")
            self.assertEqual(i18n.tr("toolbar.run"), "Go")
            self.assertEqual(i18n.tr("toolbar.stop"), "Stop")
        finally:
            i18n.set_language("en")
            del i18n.UI_LANGUAGES["xx"]
        with self.assertRaises(KeyError):
            i18n.set_language("zz")


class SampleTests(unittest.TestCase):
    def test_samples_are_listed_per_language(self):
        english = list_samples("en")
        self.assertGreaterEqual(len(english), 20)
        self.assertEqual(english[0].name, "01_hello.th")
        self.assertEqual(english[0].title, "01 - Hello, Thunder!")
        self.assertEqual(samples_dir("en"), support.SAMPLES)
        pashto = list_samples("ps")
        self.assertGreaterEqual(len(pashto), 8)
        self.assertEqual(pashto[0].title, "۰۱ - سلام، تندر!")  # the directive line is skipped
        self.assertEqual(counterpart("15_structs.th", "ps").path, support.PASHTO_SAMPLES / "15_structs.th")
        self.assertIsNone(counterpart("99_missing.th", "ps"))
        # Every lesson exists in both languages, under the same file name.
        self.assertEqual([s.name for s in english], [s.name for s in pashto])

    def test_titles_fall_back_to_the_file_name(self):
        with tempfile.TemporaryDirectory() as directory:
            plain = Path(directory) / "plain.th"
            plain.write_text("\nprintln(1)\n", encoding="utf-8")
            directive_only = Path(directory) / "directive.th"
            directive_only.write_text("# lang: en\n", encoding="utf-8")
            self.assertEqual(Sample(plain).title, "plain")
            self.assertEqual(Sample(directive_only).title, "directive")
            self.assertEqual(len(list_samples(directory=Path(directory))), 2)
            self.assertEqual(list_samples(directory=Path(directory) / "missing"), [])


if __name__ == "__main__":
    unittest.main()
