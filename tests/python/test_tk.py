"""The Tkinter backend, driven through the real widgets (skipped when there is no display)."""

import gc
import shutil
import tempfile
import threading
import time
import tkinter as tk
import unittest
from pathlib import Path
from unittest import mock

from tests.python import support
from thunder_ide import app as app_module
from thunder_ide.core import bidi, i18n
from thunder_ide.core.controller import IdeController
from thunder_ide.core.highlight import Highlight
from thunder_ide.core.layout import EDITOR, EDUCATION, OUTPUT, SOURCE_VIEW, Layout
from thunder_ide.core.settings import Settings
from thunder_ide.core.toolchain import Toolchain
from thunder_ide.core.viewmodels import Column, Span, StyledText, TreeRow, TreeSpec, VmViewState
from thunder_ide.tk.backend import TkBackend
from thunder_ide.tk.main_window import tk_sequence
from thunder_ide.tk.rendering import TextRenderer
from thunder_ide.tk.widgets import TkSourceView, TkTextPanel, TkTreePanel, TkVmPanel


def release_tk_objects():
    """Frees destroyed Tk objects on the main thread (Tk objects must die on the thread that made them)."""
    gc.collect()


@support.requires_display
@support.requires_toolchain
class TkTestCase(unittest.TestCase):
    """One IDE window per test class (creating Tk windows is slow on some displays)."""

    language = "en"

    @classmethod
    def setUpClass(cls):
        cls.dir = Path(tempfile.mkdtemp(prefix="thunder-tk-"))
        cls.backend = TkBackend()
        cls.root = cls.backend.root
        cls.root.geometry("1300x820+30+30")
        cls.toolchain = Toolchain.locate(str(support.bin_dir()))
        cls.controller = IdeController(cls.backend, cls.toolchain, Settings(language=cls.language),
                                       cls.dir / "settings.json")
        cls.pump(0.5)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.controller.close()
        except tk.TclError:
            pass
        i18n.set_language("en")
        release_tk_objects()
        shutil.rmtree(cls.toolchain.work_dir, ignore_errors=True)
        shutil.rmtree(cls.dir, ignore_errors=True)

    @classmethod
    def pump(cls, seconds=0.05):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            cls.root.update()
            time.sleep(0.005)

    @property
    def window(self):
        return self.controller.window

    def setUp(self):
        self.window.editor.set_modified(False)
        self.controller.load_sample(0)
        self.window.apply_layout(Layout.default())
        self.pump(0.1)

    def wait_for_run(self, timeout=60):
        end = time.monotonic() + timeout
        while self.controller.run is not None and self.controller.run.running:
            self.assertLess(time.monotonic(), end)
            self.pump(0.02)
        self.pump(0.15)


class LiveHighlightingTests(TkTestCase):
    def tags_of(self, name):
        text = self.window.editor.text
        ranges = text.tag_ranges(name)
        return [text.get(ranges[i], ranges[i + 1]) for i in range(0, len(ranges), 2)]

    def test_highlighting_follows_every_keystroke(self):
        editor = self.window.editor
        editor.set_text("")
        self.pump(0.1)
        typed = ""
        for word in ("var", " total", " = ", "42", "\n", "while", " ", "(", "true", ")"):
            for char in word:
                editor.text.insert("insert", char)  # exactly what Tk's key bindings do
                typed += char
            self.pump(0.12)
            self.assertEqual(editor.get_text(), typed)
            self.assertIn("var", self.tags_of("keyword"))
        self.assertIn("while", self.tags_of("keyword"))
        self.assertIn("42", self.tags_of("number"))
        self.assertIn("true", self.tags_of("literal"))

    def test_deleting_and_undo_also_rehighlight(self):
        editor = self.window.editor
        editor.set_text("var x = 1")
        self.pump(0.1)
        editor.text.delete("1.0", "1.3")
        self.pump(0.12)
        self.assertNotIn("var", self.tags_of("keyword"))
        editor.text.edit_undo()
        self.pump(0.12)
        self.assertIn("var", self.tags_of("keyword"))
        editor.text.edit_redo()
        self.pump(0.12)
        self.assertEqual(self.tags_of("keyword"), [])

    def test_strings_and_comments_while_typing(self):
        editor = self.window.editor
        editor.set_text("")
        for char in 'println("if") // while':
            editor.text.insert("insert", char)
        self.pump(0.12)
        self.assertEqual(self.tags_of("string"), ['"if"'])
        self.assertEqual(self.tags_of("comment"), ["// while"])
        self.assertEqual(self.tags_of("builtin"), ["println"])


class EditorTests(TkTestCase):
    def setUp(self):
        super().setUp()
        self.editor = self.window.editor
        self.text = self.editor.text

    def type_line(self, content):
        self.editor.set_text(content)
        self.text.mark_set("insert", "end-1c")

    def test_allman_editing(self):
        self.type_line("    if (x > 0) ")
        self.editor._on_open_brace()
        self.assertEqual(self.editor.get_text(), "    if (x > 0)\n    {")
        self.editor._on_return()
        self.assertEqual(self.editor.get_text(), "    if (x > 0)\n    {\n        ")
        self.editor._on_close_brace()
        self.assertEqual(self.editor.get_text(), "    if (x > 0)\n    {\n    }")
        self.type_line("{}")
        self.text.mark_set("insert", "1.1")
        self.editor._on_return()
        self.assertEqual(self.editor.get_text(), "{\n    \n}")
        self.assertEqual(self.text.index("insert"), "2.4")
        self.type_line("abc")
        self.text.tag_add("sel", "1.1", "1.2")
        self.text.mark_set("insert", "1.2")
        self.editor._on_return()
        self.assertEqual(self.editor.get_text(), "a\nc")
        self.type_line("")
        self.editor._on_tab()
        self.assertEqual(self.editor.get_text(), "    ")

    def test_marks_and_navigation(self):
        self.editor.set_text("var x = 1\nvar y = 2\n")
        self.editor.mark_error(Span(1, 5, 1, 6))
        self.assertEqual(str(self.text.tag_ranges("error")[0]), "1.4")
        self.editor.mark_error(Span(2, 11))  # past the end: marks the whole line
        self.editor.show_span(Span(2, 0))
        self.assertEqual([str(i) for i in self.text.tag_ranges("selection_span")], ["2.0", "2.9"])
        self.editor.show_span(Span(2, 5, 2, 6))
        self.editor.show_span(None)
        self.assertFalse(self.text.tag_ranges("selection_span"))
        self.editor.show_current_line(1)
        self.assertTrue(self.text.tag_ranges("current_line"))
        self.editor.clear_marks()
        self.assertFalse(self.text.tag_ranges("error"))
        self.editor.go_to_line(2)
        self.assertEqual(self.editor.cursor_line(), 2)
        self.editor.replace_text("println(1)")
        self.assertEqual(self.editor.get_text(), "println(1)")
        self.text.edit_undo()
        self.assertEqual(self.editor.get_text(), "var x = 1\nvar y = 2\n")

    def test_right_to_left_alignment_and_font_size(self):
        self.editor.set_right_to_left(True)
        self.assertEqual(int(self.editor.line_numbers.grid_info()["column"]), 2)
        self.assertTrue(self.text.tag_ranges("rtl"))
        self.editor.set_right_to_left(False)
        self.assertEqual(int(self.editor.line_numbers.grid_info()["column"]), 0)
        self.editor.set_font_size(14)
        self.assertEqual(self.editor.font.actual("size"), 14)
        self.editor.set_font_size(11)

    def test_scrolling_redraws_line_numbers(self):
        self.editor.set_text("\n".join(f"println({i})" for i in range(200)))
        self.pump(0.2)
        self.editor._scroll_y("moveto", "0.5")
        self.root.update()
        self.assertGreater(len(self.editor.line_numbers.find_all()), 0)


class DockingTests(TkTestCase):
    def test_collapse_float_move_hide_and_reveal(self):
        docks = self.window.docks
        docks.toggle_collapsed(EDUCATION)
        self.pump(0.2)
        self.assertEqual(docks.panels[EDUCATION].mode, "strip")  # the whole right area is collapsed
        self.assertGreater(self.window.editor.winfo_width(), 1000)
        docks.toggle_collapsed(EDUCATION)
        self.pump(0.2)
        self.assertEqual(docks.panels[EDUCATION].mode, "normal")

        docks.toggle_floating(OUTPUT)
        self.pump(0.3)
        panel = docks.panels[OUTPUT]
        self.assertEqual(str(self.root.tk.call("winfo", "toplevel", panel)), str(panel))
        layout = self.window.current_layout()
        self.assertTrue(layout.panels[OUTPUT].floating)
        self.assertRegex(layout.panels[OUTPUT].geometry, r"\d+x\d+\+\d+\+\d+")
        docks.toggle_floating(OUTPUT)
        self.pump(0.3)
        self.assertEqual(str(self.root.tk.call("winfo", "toplevel", panel)), ".")

        docks.move(SOURCE_VIEW, "bottom")
        self.pump(0.2)
        self.assertEqual(self.window.current_layout().panels_in("bottom"), [OUTPUT, SOURCE_VIEW])
        docks.toggle_collapsed(OUTPUT)
        self.pump(0.2)
        self.assertEqual(docks.panels[OUTPUT].mode, "collapsed")  # the bottom area still has an open panel
        docks.toggle_collapsed(SOURCE_VIEW)
        self.pump(0.2)
        docks.set_visible(SOURCE_VIEW, False)
        self.assertFalse(self.window.panel_vars[SOURCE_VIEW].get())
        self.window.select_panel_tab("problems")  # reveals the collapsed output panel
        self.pump(0.2)
        self.assertFalse(self.window.current_layout().panels[OUTPUT].collapsed)

    def test_stacked_panels_and_title_bar_controls(self):
        docks = self.window.docks
        docks.move(EDUCATION, "left")
        self.pump(0.3)
        self.assertEqual(docks.layout.panels_in("left"), [EDITOR, EDUCATION])
        self.assertEqual(len(docks.areas["left"].panes()), 2)
        panel = docks.panels[EDUCATION]
        panel.collapse_button.invoke()
        self.pump(0.2)
        self.assertEqual(panel.mode, "collapsed")
        panel.collapse_button.invoke()
        self.pump(0.2)
        self.assertEqual(panel.mode, "normal")
        with mock.patch.object(tk.Menu, "tk_popup"):
            panel.menu_button.invoke()
        menu = docks.last_menu
        menu.invoke(1)  # dock on the right
        self.pump(0.2)
        self.assertEqual(docks.layout.panels[EDUCATION].area, "right")
        panel.hide_button.invoke()
        self.pump(0.1)
        self.assertFalse(docks.layout.panels[EDUCATION].visible)
        self.window.menus["menu.view"].invoke(self.window.menus["menu.view"].index("end") - 2)  # the Education toggle
        self.pump(0.1)
        self.assertTrue(docks.layout.panels[EDUCATION].visible)
        panel.float_button.invoke()
        self.pump(0.3)
        close = self.root.tk.call("wm", "protocol", panel, "WM_DELETE_WINDOW")
        self.root.tk.call(close)  # closing the floating window docks the panel again
        self.pump(0.3)
        self.assertFalse(docks.layout.panels[EDUCATION].floating)

    def test_tooltips(self):
        from thunder_ide.tk.docking import Tooltip

        button = self.window.docks.panels[EDITOR].float_button
        tip = Tooltip(button, "hint")
        tip.show()
        self.assertIsNotNone(tip.window)
        tip.show()
        tip.hide()
        self.assertIsNone(tip.window)

    def test_layout_is_saved_with_sizes(self):
        layout = self.window.current_layout()
        self.assertGreater(layout.left_fraction, 0.1)
        self.assertGreater(layout.top_fraction, 0.1)


class MainWindowTests(TkTestCase):
    def test_toolbar_compile_run_and_debug(self):
        self.controller.load_sample(14)  # 15_structs
        self.window.toolbar_buttons["compile"].invoke()
        self.assertGreater(len(self.window.tokens.items()), 100)
        item = self.window.symbols.items()[1]
        self.window.symbols.tree.selection_set(item)
        self.window.symbols.tree.event_generate("<<TreeviewSelect>>")
        self.root.update()
        self.assertTrue(self.window.editor.text.tag_ranges("selection_span"))
        self.window.toolbar_buttons["run"].invoke()
        self.assertTrue(self.window.toolbar_buttons["compile"].instate(["disabled"]))
        self.wait_for_run()
        self.assertIn("Point(x: 7, y: 3)", self.window.output.get_text())
        self.window.toolbar_buttons["debug"].invoke()
        self.wait_for_run()
        vm = self.window.vm
        self.assertIn("Step 1 of", vm.position.cget("text"))
        vm.buttons["forward_line"].invoke()
        vm.buttons["last"].invoke()
        self.assertTrue(vm.buttons["forward"].instate(["disabled"]))
        self.assertTrue(self.window.source_view.text.tag_ranges("current_line"))

    def test_menus_samples_and_shortcuts(self):
        self.window.sample_choice.current(2)
        self.window.sample_choice.event_generate("<<ComboboxSelected>>")
        self.root.update()
        self.assertIn("03_variables.th", self.window.file_label.cget("text"))
        build = self.window.menus["menu.build"]
        build.invoke(0)  # Compile
        self.assertIsNotNone(self.controller.last_compile)
        self.window.editor.text.focus_force()
        self.pump(0.1)
        self.controller.last_compile = None
        self.window.editor.text.event_generate("<F7>")
        self.root.update()
        self.assertIsNotNone(self.controller.last_compile)

    def test_shortcut_conversion(self):
        self.assertEqual(tk_sequence("F5"), "<F5>")
        self.assertEqual(tk_sequence("Shift+F5"), "<Shift-F5>")
        self.assertEqual(tk_sequence("Ctrl+S"), "<Control-s>")
        self.assertEqual(tk_sequence("Ctrl+Shift+F"), "<Control-Shift-F>")
        self.assertEqual(tk_sequence("Ctrl++"), "<Control-plus>")
        self.assertEqual(tk_sequence("Ctrl+-"), "<Control-minus>")
        self.assertIsNone(tk_sequence(""))

    def test_dialogs_and_documents(self):
        with mock.patch("thunder_ide.tk.main_window.messagebox.askyesno", return_value=True) as ask:
            self.assertTrue(self.window.ask_yes_no("t", "m"))
        ask.assert_called_once()
        with mock.patch("thunder_ide.tk.main_window.filedialog.askopenfilename", return_value=""):
            self.assertIsNone(self.window.ask_open_path())
        with mock.patch("thunder_ide.tk.main_window.filedialog.asksaveasfilename", return_value="/x.th"):
            self.assertEqual(self.window.ask_save_path("a.th"), "/x.th")
        with mock.patch("thunder_ide.tk.main_window.messagebox.showinfo") as info, \
                mock.patch("thunder_ide.tk.main_window.messagebox.showerror") as error:
            self.window.show_info("t", "i")
            self.window.show_error("t", "e")
        info.assert_called_once()
        error.assert_called_once()
        document = self.window.show_document("language.md", "# Thunder\ntext")
        self.root.update()
        self.assertIn("Thunder", document.winfo_children()[0].get_text())
        document.destroy()


class PashtoInterfaceTests(TkTestCase):
    language = "ps"

    def test_right_to_left_interface(self):
        w = self.window
        self.assertTrue(w.rtl)
        self.assertEqual(str(w.status.cget("anchor")), "e")
        # The editor area is on the right of the education panel.
        self.pump(0.3)
        self.assertGreater(w.docks.areas["left"].winfo_rootx(), w.docks.areas["right"].winfo_rootx())
        self.controller._source_view_shown_automatically = False  # setUp reset the layout
        self.controller.load_sample(1)  # ۰۲ - حساب
        self.pump(0.2)
        self.assertTrue(w.editor.right_to_left)
        self.assertTrue(w.current_layout().panels[SOURCE_VIEW].visible)
        shown = w.source_view.text.get("1.0", "end")
        expected = bidi.visual_text("چاپ_کرښه") if w.renderer.manual else "چاپ_کرښه"
        self.assertIn(expected, shown)  # in visual order (and shaped) where Tk needs it
        self.controller.run_program()
        self.wait_for_run()
        self.assertIn("۱۷۲", w.output.get_text())
        # A line without letters ("۷") follows the interface direction: right-aligned.
        self.assertEqual(w.output.get_text().splitlines()[0], "۷")
        self.assertIn("rtl", w.output.text.tag_names("1.0"))

    def test_source_view_click_moves_the_editor(self):
        view = self.window.source_view
        view.text.event_generate("<Button-1>", x=10, y=40)
        self.root.update()
        self.assertGreater(self.window.editor.cursor_line(), 1)


class WidgetTests(TkTestCase):
    def setUp(self):
        super().setUp()
        self.top = tk.Toplevel(self.root)

    def tearDown(self):
        self.top.destroy()

    def test_text_panel_lays_out_right_to_left_lines(self):
        renderer = TextRenderer(self.root, right_to_left=False)
        renderer.manual = True  # as on Linux, regardless of the platform running the test
        panel = TkTextPanel(self.top, renderer, "TkFixedFont", styles={"s": {"foreground": "red"}})
        panel.pack()
        panel.append("abc ")
        panel.append("سلام", "s")
        panel.append("\nsecond\n")
        self.assertEqual(panel.get_text(), "abc سلام\nsecond\n")
        self.assertEqual(panel.text.get("1.0", "1.end"), "abc ﻡﻼﺳ")
        self.assertIn("s", panel.text.tag_names("1.5"))
        panel.append("ب\n")
        self.assertIn("rtl", panel.text.tag_names("3.0"))
        panel.set_lines([StyledText("x", "s"), StyledText("")])
        self.assertEqual(panel.get_text(), "x\n\n")
        rtl_panel = TkTextPanel(self.top, TextRenderer(self.root, right_to_left=True), "TkFixedFont")
        rtl_panel.append("{\n")  # a line without letters follows the interface direction
        self.assertIn("rtl", rtl_panel.text.tag_names("1.0"))

    def test_renderer_display(self):
        renderer = TextRenderer(self.root, right_to_left=True)
        renderer.manual = True
        self.assertEqual(renderer.display("abc"), "abc")
        self.assertEqual(renderer.display("بب\nپ"), "ﺐﺑ\nﭖ")
        renderer.manual = False
        self.assertEqual(renderer.display("بب"), "بب")

    def test_tree_panel(self):
        selected = []
        spec = TreeSpec("Name", (Column("a", "A", 50), Column("b", "B", 50)))
        rtl = TextRenderer(self.root, right_to_left=True)
        panel = TkTreePanel(self.top, spec, rtl, selected.append)
        panel.set_rows([TreeRow("root", ("1", "2"), Span(1, 1), "keyword", [TreeRow("child")])])
        self.assertEqual(panel.tree.cget("displaycolumns"), ("b", "a"))
        self.assertEqual(panel.tree.item(panel.items()[0], "values"), ("1", "2"))
        flat = TkTreePanel(self.top, TreeSpec(None, (Column("a", "A", 50), Column("b", "B", 50))), rtl, selected.append)
        flat.set_rows([TreeRow("first", ("second",))])
        self.assertEqual(flat.tree.item(flat.items()[0], "values"), ("first", "second"))  # the text is a column
        item = panel.items()[1]
        panel.tree.selection_set(item)
        panel.tree.event_generate("<<TreeviewSelect>>")
        self.root.update()
        self.assertEqual(selected[0].text, "child")
        panel.set_footer("note")
        self.assertEqual(panel.footer.cget("text"), "note")

    def test_vm_panel(self):
        actions = []
        panel = TkVmPanel(self.top, TextRenderer(self.root, right_to_left=True), "TkFixedFont", actions.append)
        state = VmViewState("Step 1 of 2", "Next: CONST", False, True, [TreeRow("0", ("1", "operand"))],
                            [TreeRow("<main>", (0,))], [TreeRow("x", ("1",))], [TreeRow("g", ("2",))], "out", 3)
        panel.show(state)
        self.assertTrue(panel.buttons["back"].instate(["disabled"]))
        self.assertFalse(panel.buttons["forward"].instate(["disabled"]))
        panel.buttons["forward"].invoke()
        self.assertEqual(actions, ["forward"])
        self.assertEqual(panel.output.get_text(), "out")
        panel.show(None)
        self.assertTrue(panel.buttons["forward"].instate(["disabled"]))

    def test_source_view(self):
        clicks = []
        renderer = TextRenderer(self.root, right_to_left=False)
        renderer.manual = True
        view = TkSourceView(self.top, renderer, "TkFixedFont", clicks.append)
        view.pack()
        view.show("که x\nprintln(1)\n", [Highlight("keyword", 0, 2)], 2, True)
        self.assertEqual(view.text.get("1.0", "1.end"), "x ﻪﮐ")
        self.assertIn("keyword", view.text.tag_names("1.2"))
        self.assertTrue(view.text.tag_ranges("current_line"))
        view.show("که x\nprintln(1)\n", [Highlight("keyword", 0, 2)], 2, True)  # unchanged: not redrawn
        view.show("", [], 0, False)
        view._scroll("moveto", "0")
        self.top.update()
        view.text.event_generate("<Button-1>", x=5, y=5)
        self.root.update()
        self.assertEqual(clicks, [1])


class SchedulerAndAppTests(TkTestCase):
    def test_scheduler(self):
        scheduler = self.backend.scheduler
        ran = []
        handle = scheduler.call_later(10_000, lambda: ran.append("late"))
        scheduler.cancel(handle)
        thread = threading.Thread(target=lambda: scheduler.post(lambda: ran.append("posted")))
        thread.start()
        thread.join()
        scheduler.flush()
        self.assertEqual(ran, ["posted"])
        scheduler.post(lambda: ran.append("polled"))
        self.pump(0.1)
        self.assertEqual(ran, ["posted", "polled"])

    def test_create_backend(self):
        with self.assertRaises(ValueError):
            app_module.create_backend("gtk")


@support.requires_display
@support.requires_toolchain
class AppMainTests(unittest.TestCase):
    def test_main(self):
        created = []
        original = IdeController.__init__

        def recording_init(controller, *args, **kwargs):
            original(controller, *args, **kwargs)
            created.append(controller)

        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.dict("os.environ", {"THUNDER_IDE_SETTINGS": str(Path(directory) / "s.json")}), \
                mock.patch.object(TkBackend, "run", lambda backend: created[0].close()), \
                mock.patch.object(IdeController, "__init__", recording_init):
            self.assertEqual(app_module.main(["--bin", str(support.bin_dir()),
                                              str(support.SAMPLES / "04_strings.th")]), 0)
            self.assertEqual(created[0].display_name, "04_strings.th")
            created.clear()
            with mock.patch("thunder_ide.app.Toolchain.locate", side_effect=app_module.ToolchainError("missing")), \
                    mock.patch("thunder_ide.tk.main_window.messagebox.showerror") as error:
                self.assertEqual(app_module.main([]), 0)
            self.assertIn("missing", error.call_args[0][1])
        release_tk_objects()


if __name__ == "__main__":
    unittest.main()
