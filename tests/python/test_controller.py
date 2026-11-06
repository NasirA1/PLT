"""The IDE's application logic, driven through a fake user interface (no GUI toolkit needed)."""

import shutil
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from tests.python import support
from tests.python.fake_ui import FakeBackend
from thunder_ide.core import i18n
from thunder_ide.core.controller import IdeController, directive_language
from thunder_ide.core.layout import EDUCATION, OUTPUT, Layout
from thunder_ide.core.settings import Settings
from thunder_ide.core.toolchain import RunResult, Toolchain, ToolchainError
from thunder_ide.core.viewmodels import Span, TreeRow


class DirectiveTests(unittest.TestCase):
    def test_directive_language(self):
        self.assertEqual(directive_language("// lang: ps\nx"), "ps")
        self.assertEqual(directive_language("// ژبه: پښتو"), "ps")
        self.assertEqual(directive_language("  //   language:English"), "en")
        self.assertIsNone(directive_language("// lang: tlh"))
        self.assertIsNone(directive_language("println(1)"))


@support.requires_toolchain
class ControllerTestCase(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="thunder-controller-"))
        self.backend = FakeBackend()
        self.toolchain = Toolchain.locate(str(support.bin_dir()))
        self.settings_path = self.dir / "settings.json"
        self.controller = IdeController(self.backend, self.toolchain, Settings(), self.settings_path)
        self.window = self.backend.windows[-1]

    def tearDown(self):
        if self.controller.run is not None and self.controller.run.running:
            self.controller.run.stop()
            self.controller.run.wait(30)
        i18n.set_language("en")
        shutil.rmtree(self.toolchain.work_dir, ignore_errors=True)
        shutil.rmtree(self.dir, ignore_errors=True)

    def load(self, name: str) -> None:
        index = [s.name for s in self.controller.samples].index(name)
        self.controller.load_sample(index)

    def wait_for_run(self) -> None:
        run = self.controller.run
        self.assertIsNotNone(run)
        deadline = time.monotonic() + 60
        while not run.wait(0.05):
            self.assertLess(time.monotonic(), deadline)
            self.backend.scheduler.drain()
        self.backend.scheduler.drain()


class StartupTests(ControllerTestCase):
    def test_initial_state(self):
        w = self.window
        self.assertEqual(w.samples[1], 0)
        self.assertIn("Hello, Thunder!", w.editor.get_text())
        self.assertIn("01_hello.th", w.file_label)
        self.assertEqual(w.languages, (["English", "پښتو"], 0))
        self.assertEqual(w.status, "Opened 01_hello.th.")
        self.assertTrue(w.editor.highlights)  # highlighted immediately
        self.assertIn("Press Compile", w.overview.get_text())

    def test_without_a_toolchain(self):
        backend = FakeBackend()
        controller = IdeController(backend, None, Settings(language="xx"))
        window = backend.windows[-1]
        self.assertIn("Toolchain not found", window.status)
        self.assertIsNone(controller.compile())
        controller.translate_program()
        self.assertTrue(window.editor.highlights)  # falls back to the built-in keyword list
        self.assertEqual(controller.language, "en")


class LiveHighlightingTests(ControllerTestCase):
    def test_every_edit_schedules_highlighting(self):
        editor = self.window.editor
        editor.set_text("")
        self.controller.on_text_changed()
        for text in ("var", " x = ", "1", "\nwhile"):
            editor.type(text)
            self.controller.on_text_changed()
            self.backend.scheduler.run_timers()
            tags = {(r.tag, editor.text[r.start:r.end]) for r in editor.highlights}
            self.assertIn(("keyword", "var"), tags)
        self.assertIn(("keyword", "while"), tags)
        self.assertIn(("number", "1"), tags)
        self.assertIn("(modified)", self.window.file_label)

    def test_rapid_typing_highlights_once(self):
        self.window.editor.type("x")
        self.controller.on_text_changed()
        self.window.editor.type("y")
        self.controller.on_text_changed()
        self.assertEqual(len(self.backend.scheduler.timers), 1)

    def test_pashto_programs_use_pashto_keywords_and_right_to_left(self):
        editor = self.window.editor
        editor.set_text("// ژبه: ps\nمتغیر شمېر = ۵\nکرښه چاپ(«سلام»)\nکرښه  چاپ(\"x\")")
        self.controller.on_text_changed()
        self.backend.scheduler.run_timers()
        tags = {(r.tag, editor.text[r.start:r.end]) for r in editor.highlights}
        self.assertIn(("keyword", "متغیر"), tags)
        self.assertIn(("number", "۵"), tags)
        self.assertIn(("builtin", "کرښه چاپ"), tags)  # a two-word built-in is one region
        self.assertIn(("builtin", "کرښه  چاپ"), tags)
        self.assertIn(("string", "«سلام»"), tags)
        self.assertTrue(editor.rtl)  # the editor switches direction with the program's language
        editor.set_text("// lang: en\nvar x = 1")
        self.controller.refresh_highlighting()
        self.assertFalse(editor.rtl)

    def test_panel_toggles_change_the_layout(self):
        self.controller.on_panel_toggled(OUTPUT, False)
        self.assertFalse(self.window.current_layout().panels[OUTPUT].visible)
        self.controller.on_panel_toggled(OUTPUT, True)
        self.assertTrue(self.window.current_layout().panels[OUTPUT].visible)


class CompileTests(ControllerTestCase):
    def test_compile_fills_every_panel(self):
        self.load("15_structs.th")
        outcome = self.controller.compile()
        self.assertTrue(outcome.success)
        w = self.window
        self.assertGreater(len(w.tokens.rows), 100)
        self.assertEqual(w.ast.rows[0].text, "Program")
        names = [row.text for row in w.symbols.flat()]
        self.assertIn("Rectangle", names)
        self.assertIn("corner", names)
        self.assertTrue(any(row.key == "NEW_STRUCT" for row in w.bytecode.flat()))
        self.assertIn("THND", w.binary.get_text())
        self.assertEqual(w.problems.rows, [])
        self.assertIn("Compiled 15_structs.th", w.output.get_text())
        self.assertIn("Lexical analysis     OK", w.overview.get_text())
        self.assertIn("Compiled successfully", w.status)
        self.assertIn("$ ", w.log.get_text())

    def test_selecting_rows(self):
        self.load("02_arithmetic.th")
        self.controller.compile()
        token = self.window.tokens.rows[0]
        self.controller.on_row_selected("tokens", token)
        self.assertEqual(self.window.editor.span, token.span)
        instruction = next(row for row in self.window.bytecode.flat() if row.key == "CONST")
        self.controller.on_row_selected("bytecode", instruction)
        self.assertIn("CONST  [-> value]", self.window.bytecode.footer)
        self.controller.on_row_selected("bytecode", TreeRow("constant pool"))
        self.assertEqual(self.window.bytecode.footer, "")

    def test_compile_errors(self):
        self.load("e3_type_error.th")
        outcome = self.controller.compile()
        self.assertFalse(outcome.success)
        w = self.window
        self.assertEqual(len(w.problems.rows), 5)
        self.assertEqual(w.problems.rows[0].text, "semantic")
        self.assertEqual(len(w.editor.errors), 5)
        self.assertEqual(w.selected_tab, "problems")
        self.assertIn("Compilation stopped during Semantic analysis", w.output.get_text())
        self.assertIn("Compilation failed: 5 error(s)", w.status)

    def test_warnings(self):
        self.window.editor.set_text("func f()\n    var unused = 1\nf()\n")
        self.assertTrue(self.controller.compile().success)
        self.assertIn("W001", self.window.output.get_text())

    def test_toolchain_failure(self):
        with mock.patch.object(self.toolchain, "compile", side_effect=ToolchainError("boom")):
            self.assertIsNone(self.controller.compile())
        self.assertIn("boom", self.window.log.get_text())
        with mock.patch.object(self.toolchain, "language_info", side_effect=ToolchainError("no info")):
            self.controller.refresh_highlighting()
        self.assertIn("no info", self.window.log.get_text())


class RunTests(ControllerTestCase):
    def test_run(self):
        self.load("02_arithmetic.th")
        self.controller.run_program()
        self.assertTrue(self.window.running)
        self.wait_for_run()
        self.assertFalse(self.window.running)
        self.assertIn("172", self.window.output.get_text())
        self.assertIn("Program finished (exit code 0)", self.window.status)
        self.assertIn("5. Execution            OK", self.window.overview.get_text())
        self.assertIn("executed", self.window.log.get_text())

    def test_run_recompiles_after_an_edit(self):
        self.controller.compile()
        self.window.editor.type('\nprintln("edited")\n')
        self.controller.run_program()
        self.wait_for_run()
        self.assertIn("edited", self.window.output.get_text())

    def test_run_stops_at_compile_errors(self):
        self.load("e2_syntax_error.th")
        self.controller.run_program()
        self.assertIsNone(self.controller.run)

    def test_runtime_error(self):
        self.load("e4_runtime_error.th")
        self.controller.run_program()
        self.wait_for_run()
        self.assertIn("division by zero", self.window.status)
        self.assertEqual(self.window.problems.rows[-1].text, "runtime")
        self.assertEqual(self.window.editor.errors[0], Span(11, 16))

    def test_stop_and_busy(self):
        self.load("e5_infinite_loop.th")
        self.controller.max_steps = 0
        self.controller.run_program()
        time.sleep(0.2)
        self.assertIsNone(self.controller.compile())
        self.controller.run_program()
        self.assertIn("already running", self.window.status)
        self.controller.stop_program()
        self.wait_for_run()
        self.assertIn("Stopped by the user", self.window.status)
        self.controller.stop_program()

    def test_vm_start_failure_and_unreadable_results(self):
        with mock.patch.object(self.toolchain, "start_run", side_effect=OSError("no vm")):
            self.controller.run_program()
        self.assertIn("no vm", self.window.log.get_text())
        self.assertFalse(self.window.running)
        self.controller.last_compile = None
        self.controller._on_exit(RunResult(exit_code=0, error="bad json"), trace=False)
        self.assertIn("bad json", self.window.log.get_text())

    def test_debugging(self):
        self.load("09_recursion.th")
        self.controller.debug_program()
        self.wait_for_run()
        w = self.window
        self.assertEqual(w.selected_tab, "vm")
        self.assertIn("Step 1 of", w.vm.state.position)
        self.assertFalse(w.vm.state.can_go_back)
        for action in ("forward", "forward_line", "forward_line", "back_line", "back", "last"):
            self.controller.on_vm_navigate(action)
        self.assertIn("Program finished", w.vm.state.instruction)
        self.assertIn("5! = 120", w.vm.state.output)
        self.controller.on_vm_navigate("first")
        self.assertGreater(w.editor.current_line, 0)
        self.controller.trace = None
        self.controller.on_vm_navigate("forward")  # nothing to do


class FileAndEditTests(ControllerTestCase):
    def test_open_save_and_save_as(self):
        path = self.dir / "mine.th"
        path.write_text("println(1)\n", encoding="utf-8")
        self.controller.open_file(str(path))
        self.assertEqual(self.controller.current_path, path)
        self.window.editor.type("println(2)\n")
        self.assertTrue(self.controller.save_file())
        self.assertIn("println(2)", path.read_text(encoding="utf-8"))
        copy = self.dir / "copy.th"
        self.assertTrue(self.controller.save_file_as(str(copy)))
        self.assertEqual(self.controller.display_name, "copy.th")
        self.window.answers = [None]
        self.controller.current_path = None
        self.assertFalse(self.controller.save_file())
        self.window.answers = [str(self.dir / "picked.th")]
        self.assertTrue(self.controller.save_file())
        self.window.answers = [None]
        self.controller.open_file()

    def test_unsaved_changes_are_confirmed(self):
        self.window.editor.type("x")
        self.window.answers = [False, False, False, False]
        self.controller.new_file()
        self.controller.load_sample(1)
        self.controller.open_file("never.th")
        self.controller.quit()
        self.assertFalse(self.window.closed)
        self.assertEqual(sum(1 for kind, *_ in self.window.messages if kind == "ask"), 4)
        self.window.answers = [True]
        self.controller.new_file()
        self.assertEqual(self.window.editor.get_text(), "")
        self.controller.load_sample(-1)
        self.controller.on_sample_selected(2)
        self.assertIn("03_variables.th", self.window.file_label)

    def test_block_words_follow_the_program_language(self):
        self.window.editor.set_text("if (true)\n    println(1)\n")
        self.controller.refresh_highlighting()
        self.assertIn("if", self.window.editor.block_words.opening)
        self.assertIn("pass", self.window.editor.block_words.leaving)
        self.window.editor.set_text("// ژبه: ps\nکه رښتیا\n    کرښه چاپ(۱)\n")
        self.controller.refresh_highlighting()
        self.assertIn("که", self.window.editor.block_words.opening)
        self.assertIn("تېر", self.window.editor.block_words.leaving)

    def test_translate_to_pashto_and_back(self):
        self.load("15_structs.th")
        english = self.window.editor.get_text()
        self.controller.run_command("translate")
        pashto = self.window.editor.get_text()
        self.assertTrue(pashto.startswith("// ژبه: ps\n"))
        self.assertIn("جوړښت Point", pashto)
        self.assertIn("پښتو", self.window.status)
        self.controller.run_command("translate")
        self.assertEqual(self.window.editor.get_text(), "// lang: en\n" + english)

    def test_translation_failure(self):
        self.window.editor.set_text("var x = #\n")
        self.controller.translate_program()
        self.assertIn("could not be translated", self.window.status)
        with mock.patch.object(self.toolchain, "translate", side_effect=ToolchainError("gone")):
            self.controller.translate_program()
        self.assertIn("gone", self.window.log.get_text())
        self.window.editor.set_text("var که = 1\n")
        self.controller.translate_program()
        self.assertIn("reserved", self.window.log.get_text())

    def test_view_and_help_commands(self):
        self.controller.run_command("zoom_in")
        self.assertEqual(self.window.editor.font_size, 12)
        for _ in range(30):
            self.controller.run_command("zoom_out")
        self.assertEqual(self.window.editor.font_size, 8)
        layout = Layout.default()
        layout.move(EDUCATION, "bottom")
        self.window.apply_layout(layout)
        self.controller.run_command("reset_layout")
        self.assertEqual(self.window.layout.panels[EDUCATION].area, "right")
        self.controller.run_command("reference")
        self.controller.run_command("bytecode_reference")
        self.controller.run_command("about")
        kinds = [(kind, title) for kind, title, _ in self.window.messages]
        self.assertIn(("document", "language.md"), kinds)
        self.assertIn(("info", "About"), kinds)
        self.assertIn("Thunder", self.window.messages[0][2])
        with mock.patch("thunder_ide.core.controller.repository_root", return_value=self.dir):
            self.controller.show_reference("missing.md")
        self.assertIn("Cannot open", self.window.messages[-1][2])


class LanguageTests(ControllerTestCase):
    def test_switching_to_pashto_rebuilds_the_window(self):
        self.load("15_structs.th")
        self.controller.on_language_selected(1)
        self.assertEqual(len(self.backend.windows), 2)
        self.assertTrue(self.backend.windows[0].closed)
        window = self.backend.windows[-1]
        self.assertTrue(window.right_to_left)
        self.assertEqual(window.languages[1], 1)
        self.assertEqual(window.status, "{} پرانیستل شو.".format("15_structs.th"))
        self.assertIn("جوړښت ټکی", window.editor.get_text())  # the Pashto version of the same lesson
        self.assertTrue(window.samples[0][0].startswith("۰۱"))
        outcome = self.controller.compile()
        self.assertTrue(outcome.success)
        self.assertEqual(window.problems.rows, [])
        self.controller.on_language_selected(1)  # already Pashto: nothing happens
        self.assertEqual(len(self.backend.windows), 2)
        self.controller.on_language_selected(9)

    def test_switching_keeps_an_edited_program(self):
        self.window.editor.type("\n// my change\n")
        self.controller.set_language("ps")
        window = self.backend.windows[-1]
        self.assertIn("// my change", window.editor.get_text())
        self.assertTrue(window.editor.is_modified())
        self.controller.set_language("en")
        self.assertIn("// my change", self.backend.windows[-1].editor.get_text())

    def test_pashto_runtime_output_and_errors(self):
        self.controller.set_language("ps")
        index = [s.name for s in self.controller.samples].index("e4_runtime_error.th")
        self.controller.load_sample(index)
        self.controller.run_program()
        self.wait_for_run()
        window = self.backend.windows[-1]
        self.assertIn("په صفر وېش", window.status)
        self.assertIn("۶", window.output.get_text())


class ShutdownTests(ControllerTestCase):
    def test_close_saves_settings_and_quits(self):
        layout = Layout.default()
        layout.set_floating(EDUCATION, True)
        self.window.apply_layout(layout)
        self.controller.zoom(2)
        self.controller.run_command("exit")
        self.assertTrue(self.window.closed)
        self.assertTrue(self.backend.quit_called)
        saved = Settings.load(self.settings_path)
        self.assertTrue(saved.layout.panels[EDUCATION].floating)
        self.assertEqual(saved.font_size, 13)
        self.controller.close()  # closing twice is harmless
        self.controller._on_output("stdout", "late")  # late events from a finished run are ignored
        self.controller._on_exit(RunResult(exit_code=0), False)

    def test_close_stops_a_running_program(self):
        self.load("e5_infinite_loop.th")
        self.controller.max_steps = 0
        self.controller.run_program()
        self.controller.close()
        self.assertFalse(self.controller.run.running)


if __name__ == "__main__":
    unittest.main()
