"""The Qt backend, driven through the real widgets (offscreen when there is no display)."""

import shutil
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from tests.python import support
from thunder_ide import app as app_module
from thunder_ide.core import i18n
from thunder_ide.core.controller import IdeController
from thunder_ide.core.direction import FSI
from thunder_ide.core.layout import EDITOR, EDUCATION, OUTPUT, Layout
from thunder_ide.core.settings import Settings
from thunder_ide.core.toolchain import Toolchain
from thunder_ide.core.viewmodels import Column, Span, StyledText, TreeRow, TreeSpec, VmViewState

if support.qt_available():
    from PySide6.QtCore import QPoint, Qt
    from PySide6.QtGui import QFont, QKeySequence, QTextCursor
    from PySide6.QtTest import QTest
    from PySide6.QtWidgets import QApplication, QFileDialog, QMessageBox

    from thunder_ide.qt import style
    from thunder_ide.qt.backend import QtBackend
    from thunder_ide.qt.editor import span_bounds
    from thunder_ide.qt.main_window import key_sequences
    from thunder_ide.qt.panels import QtTextPanel, QtTreePanel, QtVmPanel

MIXED = 'متغیر name = "Nasir"'


@support.requires_qt
@support.requires_toolchain
class QtTestCase(unittest.TestCase):
    """One IDE window per test class."""

    language = "en"

    @classmethod
    def setUpClass(cls):
        cls.dir = Path(tempfile.mkdtemp(prefix="thunder-qt-"))
        cls.backend = QtBackend()
        cls.app = cls.backend.app
        cls.toolchain = Toolchain.locate(str(support.bin_dir()))
        cls.controller = IdeController(cls.backend, cls.toolchain, Settings(language=cls.language),
                                       cls.dir / "settings.json")
        cls.controller.window.resize(1300, 820)
        cls.pump(0.2)

    @classmethod
    def tearDownClass(cls):
        cls.controller.close()
        cls.pump(0.1)
        i18n.set_language("en")
        shutil.rmtree(cls.toolchain.work_dir, ignore_errors=True)
        shutil.rmtree(cls.dir, ignore_errors=True)

    @classmethod
    def pump(cls, seconds=0.05):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            cls.app.processEvents()
            time.sleep(0.005)
        cls.app.processEvents()

    @property
    def window(self):
        return self.controller.window

    @property
    def editor(self):
        return self.controller.window.editor

    def setUp(self):
        self.editor.set_modified(False)
        self.controller.load_sample(0)
        self.window.apply_layout(Layout.default())
        self.pump(0.1)

    def wait_for_run(self, timeout=60):
        end = time.monotonic() + timeout
        while self.controller.run is not None and self.controller.run.running:
            self.assertLess(time.monotonic(), end)
            self.pump(0.02)
        self.pump(0.15)

    def set_program(self, text, right_to_left=None):
        self.editor.set_text(text)
        self.controller.refresh_highlighting()
        if right_to_left is not None:
            self.editor.set_right_to_left(right_to_left)
        self.pump(0.05)

    def caret_x(self, position):
        return self.editor.caret_point(position).x()


class LiveHighlightingTests(QtTestCase):
    def regions_of(self, tag):
        text = self.editor.get_text()
        return [text[r.start:r.end] for r in self.editor.regions if r.tag == tag]

    def test_highlighting_follows_every_keystroke(self):
        self.set_program("")
        self.editor.setFocus()
        typed = ""
        for word in ("var", " total", " = ", "42", "\n", "while", " ", "(", "true", ")"):
            if word == "\n":
                QTest.keyClick(self.editor, Qt.Key.Key_Return)
            else:
                QTest.keyClicks(self.editor, word)
            typed += word
            self.pump(0.12)
            self.assertEqual(self.editor.get_text(), typed)
            self.assertIn("var", self.regions_of("keyword"))
        self.assertIn("while", self.regions_of("keyword"))
        self.assertIn("42", self.regions_of("number"))
        self.assertIn("true", self.regions_of("literal"))

    def test_deleting_and_undo_also_rehighlight(self):
        self.set_program("var x = 1")
        cursor = self.editor.textCursor()
        cursor.setPosition(0)
        cursor.setPosition(3, QTextCursor.MoveMode.KeepAnchor)
        cursor.removeSelectedText()
        self.pump(0.12)
        self.assertNotIn("var", self.regions_of("keyword"))
        self.editor.undo()
        self.pump(0.12)
        self.assertIn("var", self.regions_of("keyword"))
        self.editor.redo()
        self.pump(0.12)
        self.assertEqual(self.regions_of("keyword"), [])

    def test_strings_and_comments_while_typing(self):
        self.set_program("")
        QTest.keyClicks(self.editor, 'println("if") // while')
        self.pump(0.12)
        self.assertEqual(self.regions_of("string"), ['"if"'])
        self.assertEqual(self.regions_of("comment"), ["// while"])
        self.assertEqual(self.regions_of("builtin"), ["println"])


class EditorRenderingTests(QtTestCase):
    def test_right_to_left_program_switches_the_editor(self):
        self.set_program("// ژبه: ps\n" + MIXED)
        editor = self.editor
        self.assertTrue(editor.right_to_left)
        self.assertEqual(editor.layoutDirection(), Qt.LayoutDirection.RightToLeft)
        self.assertGreater(editor.gutter.x(), editor.viewport().x())  # line numbers on the right
        block = editor.document().findBlockByNumber(1)
        layout = editor.block_layout(block)
        self.assertTrue(layout.line.display.startswith(FSI))
        self.assertIs(editor.block_layout(block), layout)  # cached
        start = block.position()
        # Reading order right to left: the keyword at the right, then "name", "=", "Nasir".
        after_keyword, after_name, after_equals, end = (self.caret_x(start + i) for i in (5, 10, 12, 20))
        right_edge = self.caret_x(start)
        self.assertGreater(right_edge, after_keyword)
        self.assertGreater(after_keyword, after_name)
        self.assertGreater(after_name, after_equals)
        self.assertGreater(after_equals, end)
        # Inside "name" the caret moves left to right, as the word is written.
        self.assertLess(self.caret_x(start + 7), self.caret_x(start + 8))
        self.assertLess(self.caret_x(start + 8), self.caret_x(start + 9))
        self.assertEqual(self.caret_x(start + 6), after_name)  # before and after an English word coincide
        self.set_program("// lang: en\nvar x = 1")
        self.assertFalse(editor.right_to_left)
        self.assertLess(editor.gutter.x(), editor.viewport().x())
        self.assertLess(self.caret_x(12), self.caret_x(15))

    def test_hit_testing_matches_the_caret(self):
        self.set_program("// ژبه: ps\n" + MIXED + "\nvar ټکی = ۱")
        editor = self.editor
        for block_number in (1, 2):
            block = editor.document().findBlockByNumber(block_number)
            layout = editor.block_layout(block)
            for index in range(len(block.text()) + 1):
                position = block.position() + index
                found = editor.position_at(editor.caret_point(position))
                token = layout.line.token_containing(index - 1) if index else None
                ambiguous = any(index in (t.start, t.end) and t.opposite for t in layout.line.tokens)
                if ambiguous:
                    self.assertIn(found - block.position(), [t.start for t in layout.line.tokens]
                                  + [t.end for t in layout.line.tokens], (block_number, index, token))
                else:
                    self.assertEqual(found, position, (block_number, index))
        self.assertIsNotNone(editor.position_at(QPoint(5, 5)))

    def test_mouse_placement_selection_and_double_click(self):
        self.set_program("// ژبه: ps\n" + MIXED)
        editor = self.editor
        block = editor.document().findBlockByNumber(1)
        point = editor.caret_point(block.position() + 8).toPoint() + QPoint(0, 5)
        QTest.mouseClick(editor.viewport(), Qt.MouseButton.LeftButton, pos=point)
        self.assertEqual(editor.textCursor().position(), block.position() + 8)
        shift_point = editor.caret_point(block.position() + 12).toPoint() + QPoint(0, 5)
        QTest.mouseClick(editor.viewport(), Qt.MouseButton.LeftButton, Qt.KeyboardModifier.ShiftModifier, shift_point)
        self.assertEqual(editor.textCursor().selectedText(), "me =")
        QTest.mousePress(editor.viewport(), Qt.MouseButton.LeftButton, pos=point)
        QTest.mouseMove(editor.viewport(), shift_point)
        editor.mouseMoveEvent(mock_mouse_move(shift_point))
        QTest.mouseRelease(editor.viewport(), Qt.MouseButton.LeftButton, pos=shift_point)
        self.assertEqual(editor.textCursor().selectedText(), "me =")
        QTest.mouseDClick(editor.viewport(), Qt.MouseButton.LeftButton, pos=point)
        self.assertEqual(editor.textCursor().selectedText(), "name")
        QTest.mouseClick(editor.viewport(), Qt.MouseButton.RightButton, pos=point)  # handled by Qt
        editor.viewport().grab()  # paints the selection

    def test_marks_are_painted(self):
        self.set_program("var x = 1\nvar y = 2\nvar z = 3\n")
        editor = self.editor
        editor.setFocus()
        editor.show_current_line(2)
        editor.mark_error(Span(1, 5, 1, 6))
        editor.show_span(Span(3, 5, 3, 6))
        self.pump(0.05)
        image = editor.viewport().grab().toImage()
        x = int(editor.caret_point(editor.document().findBlockByNumber(1).position() + 2).x())
        line_height = editor.fontMetrics().height()
        y = int(editor.contentOffset().y()) + line_height + line_height // 2
        self.assertEqual(image.pixelColor(x, y).name(), style.CURRENT_LINE_BACKGROUND)
        self.assertEqual(image.pixelColor(x, y - line_height).name(), style.EDITOR_BACKGROUND)
        editor.clear_marks()
        self.assertEqual(editor.current_line, 0)
        self.assertEqual(editor.error_spans, [])
        self.assertIsNone(editor.selected_span)

    def test_span_bounds(self):
        self.assertEqual(span_bounds(Span(2, 3), 2, 10), (2, 3))
        self.assertEqual(span_bounds(Span(2, 3, 2, 6), 2, 10), (2, 5))
        self.assertEqual(span_bounds(Span(2, 3, 4, 2), 3, 10), (0, 10))
        self.assertEqual(span_bounds(Span(2, 3, 4, 2), 4, 10), (0, 1))
        self.assertIsNone(span_bounds(Span(2, 3), 1, 10))
        self.assertEqual(span_bounds(Span(2, 30), 2, 10), (10, 10))

    def test_wrapped_lines_follow_qt(self):
        long_line = 'کرښه چاپ("' + "a very long string literal " * 6 + '")'
        self.set_program("// ژبه: ps\n" + long_line)
        editor = self.editor
        block = editor.document().findBlockByNumber(1)
        layout = editor.block_layout(block)
        self.assertGreater(layout.layout.lineCount(), 1)
        self.assertEqual(layout.layout.lineCount(), block.layout().lineCount())
        self.assertAlmostEqual(layout.layout.boundingRect().height(), editor.blockBoundingRect(block).height(),
                               delta=1.0)
        first = editor.caret_point(block.position() + 2)
        last = editor.caret_point(block.position() + len(block.text()))
        self.assertGreater(last.y(), first.y())
        self.assertEqual(editor.position_at(last), block.position() + len(block.text()))

    def test_scrolling_to_lines(self):
        self.set_program("\n".join(f"println({i})" for i in range(300)))
        editor = self.editor
        editor.show_current_line(250)
        self.pump(0.05)
        self.assertGreater(editor.firstVisibleBlock().blockNumber(), 200)
        self.assertLessEqual(editor.firstVisibleBlock().blockNumber(), 249)
        editor.show_span(Span(5, 1))
        self.pump(0.05)
        self.assertLessEqual(editor.firstVisibleBlock().blockNumber(), 4)
        editor.show_current_line(5)  # already visible: nothing moves
        editor.show_current_line(9999)  # no such line
        editor.go_to_line(120)
        self.assertEqual(editor.cursor_line(), 120)
        editor.gutter.grab()

    def test_astral_characters(self):
        self.set_program('// ژبه: ps\nکرښه چاپ("😀 x")')
        block = self.editor.document().findBlockByNumber(1)
        end = block.position() + block.length() - 1  # UTF-16 units
        self.assertEqual(self.editor.position_at(self.editor.caret_point(end)), end)
        self.assertEqual(self.editor.position_at(self.editor.caret_point(block.position() + 3)), block.position() + 3)


def mock_mouse_move(point):
    from PySide6.QtCore import QPointF
    from PySide6.QtGui import QMouseEvent

    position = QPointF(point)
    return QMouseEvent(QMouseEvent.Type.MouseMove, position, position, position, Qt.MouseButton.NoButton,
                       Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier)


class EditorEditingTests(QtTestCase):
    def type_line(self, content):
        self.editor.set_text(content)
        self.editor.moveCursor(QTextCursor.MoveOperation.End)

    def key(self, key, modifier=None):
        QTest.keyClick(self.editor, key, Qt.KeyboardModifier.NoModifier if modifier is None else modifier)

    def test_indentation_editing(self):
        self.controller.refresh_highlighting()  # gives the editor the language's block words
        self.type_line("    if x > 0")
        self.key(Qt.Key.Key_Return)
        self.assertEqual(self.editor.get_text(), "    if x > 0\n        ")
        QTest.keyClicks(self.editor, "return x")
        self.key(Qt.Key.Key_Enter)
        self.assertEqual(self.editor.get_text(), "    if x > 0\n        return x\n    ")
        self.key(Qt.Key.Key_Backspace)
        self.assertEqual(self.editor.get_text(), "    if x > 0\n        return x\n")
        self.key(Qt.Key.Key_Tab)
        self.assertEqual(self.editor.get_text(), "    if x > 0\n        return x\n    ")
        self.type_line("ab")
        self.key(Qt.Key.Key_Backspace)  # not in leading whitespace: Qt's normal backspace
        self.assertEqual(self.editor.get_text(), "a")
        self.type_line("abc")
        cursor = self.editor.textCursor()
        cursor.setPosition(1)
        cursor.setPosition(2, QTextCursor.MoveMode.KeepAnchor)
        self.editor.setTextCursor(cursor)
        self.key(Qt.Key.Key_Return)  # Enter replaces the selection
        self.assertEqual(self.editor.get_text(), "a\nc")
        self.key(Qt.Key.Key_Return, Qt.KeyboardModifier.ControlModifier)  # not an editing key: Qt's default
        self.key(Qt.Key.Key_X)
        self.assertIn("x", self.editor.get_text())
        self.set_program("// ژبه: ps\nکه x")
        self.editor.moveCursor(QTextCursor.MoveOperation.End)
        self.key(Qt.Key.Key_Return)
        self.assertTrue(self.editor.get_text().endswith("که x\n    "))  # Pashto block words work too

    def test_marks_navigation_and_undo(self):
        editor = self.editor
        editor.set_text("var x = 1\nvar y = 2\n")
        editor.mark_error(Span(1, 5, 1, 6))
        self.assertEqual(editor.error_spans, [Span(1, 5, 1, 6)])
        editor.mark_error(Span(2, 11))  # past the end: marks the whole line
        self.assertEqual(editor.error_spans[1], Span(2, 1, 2, 10))
        self.assertEqual(editor.gutter.marked_line, 2)
        editor.mark_error(Span(3, 1))  # the empty last line
        editor.show_span(Span(2, 0))
        self.assertEqual(editor.selected_span, Span(2, 1, 2, 10))
        editor.show_span(Span(2, 5, 2, 6))
        self.assertEqual(editor.selected_span, Span(2, 5, 2, 6))
        editor.show_span(None)
        self.assertIsNone(editor.selected_span)
        editor.show_current_line(1)
        self.assertEqual(editor.current_line, 1)
        editor.clear_marks()
        self.assertEqual(editor.error_spans, [])
        self.assertIsNone(editor.gutter.marked_line)
        editor.go_to_line(2)
        self.assertEqual(editor.cursor_line(), 2)
        editor.replace_text("println(1)")
        self.assertEqual(editor.get_text(), "println(1)")
        self.assertTrue(editor.is_modified())
        editor.undo()
        self.assertEqual(editor.get_text(), "var x = 1\nvar y = 2\n")
        editor.set_text("fresh")
        self.assertFalse(editor.is_modified())
        self.assertFalse(editor.document().isUndoAvailable())
        editor.set_modified(True)
        self.assertTrue(editor.is_modified())

    def test_font_size_and_direction(self):
        editor = self.editor
        editor.set_font_size(14)
        self.assertEqual(editor.font().pointSize(), 14)
        self.assertEqual(editor.gutter.width(), editor.gutter_width())
        editor.set_font_size(11)
        editor.set_right_to_left(True)
        editor.set_right_to_left(True)  # no change
        self.assertEqual(editor.viewportMargins().right(), editor.gutter_width())
        self.assertEqual(editor.viewportMargins().left(), 0)
        editor.set_right_to_left(False)
        self.assertEqual(editor.viewportMargins().left(), editor.gutter_width())
        editor.set_text("\n".join(f"println({i})" for i in range(300)))
        editor.verticalScrollBar().setValue(100)
        self.pump(0.05)
        self.assertGreater(editor.firstVisibleBlock().blockNumber(), 50)

    def test_caret_blinks_and_focus(self):
        editor = self.editor
        editor.setFocus()
        self.pump(0.02)
        editor._toggle_caret()
        editor.viewport().grab()
        self.app.processEvents()
        editor._toggle_caret()
        editor.viewport().grab()


class DockingTests(QtTestCase):
    def test_layout_round_trip(self):
        window = self.window
        layout = Layout.default()
        layout.move(EDUCATION, "bottom")
        layout.set_floating(OUTPUT, True)
        window.apply_layout(layout)
        self.pump(0.1)
        current = window.current_layout()
        self.assertEqual(current.panels[EDUCATION].area, "bottom")
        self.assertTrue(current.panels[OUTPUT].floating)
        self.assertTrue(current.panels[OUTPUT].visible)
        self.assertTrue(current.toolkit_state)
        self.assertEqual(current.panels_in("right"), [])

        window.set_panel_visible(EDUCATION, False)
        self.pump(0.05)
        self.assertFalse(window.current_layout().panels[EDUCATION].visible)
        self.assertFalse(window.panel_actions[EDUCATION].isChecked())
        window.select_panel_tab("tokens")  # reveals the hidden panel
        self.pump(0.05)
        self.assertTrue(window.current_layout().panels[EDUCATION].visible)
        self.assertIs(window.edu_tabs.currentWidget(), window.tokens)

        saved = current  # restore the exact Qt state later
        window.apply_layout(Layout.default())
        self.pump(0.05)
        self.assertEqual(window.current_layout().panels[EDUCATION].area, "right")
        window.apply_layout(saved)
        self.pump(0.05)
        self.assertEqual(window.current_layout().panels[EDUCATION].area, "bottom")
        broken = saved.copy()
        broken.toolkit_state = "!!not base64!!"
        window.apply_layout(broken)  # falls back to the semantic arrangement
        self.pump(0.05)
        self.assertEqual(window.current_layout().panels[EDUCATION].area, "bottom")
        self.controller.reset_layout()
        self.pump(0.1)
        self.assertEqual(window.current_layout().panels_in("right"), [EDUCATION])
        fractions = window.current_layout()
        self.assertAlmostEqual(fractions.left_fraction, 0.5, delta=0.1)   # the editor gets half the width
        self.assertAlmostEqual(fractions.top_fraction, 0.72, delta=0.1)

    def test_panel_toggle_action_and_settings(self):
        action = self.window.panel_actions[OUTPUT]
        action.trigger()
        self.pump(0.05)
        self.assertFalse(self.window.current_layout().panels[OUTPUT].visible)
        action.trigger()
        self.pump(0.05)
        self.assertTrue(self.window.current_layout().panels[OUTPUT].visible)
        self.controller.save_settings()
        self.assertTrue(Settings.load(self.dir / "settings.json").layout.toolkit_state)


class MainWindowTests(QtTestCase):
    def test_toolbar_compile_run_and_debug(self):
        window = self.window
        self.controller.load_sample(14)  # 15_structs
        window.toolbar_actions["compile"].trigger()
        self.assertGreater(len(window.tokens.items()), 100)
        item = window.symbols.items()[1]
        window.symbols.tree.setCurrentItem(item)
        self.pump(0.05)
        self.assertIsNotNone(window.editor.selected_span)
        window.toolbar_actions["run"].trigger()
        self.assertFalse(window.toolbar_actions["compile"].isEnabled())
        self.assertTrue(window.toolbar_actions["stop"].isEnabled())
        self.wait_for_run()
        self.assertIn("Point(x: 7, y: 3)", window.output.get_text())
        self.assertTrue(window.toolbar_actions["compile"].isEnabled())
        window.toolbar_actions["debug"].trigger()
        self.wait_for_run()
        vm = window.vm
        self.assertIn("Step 1 of", vm.position.text())
        vm.buttons["forward_line"].click()
        vm.buttons["last"].click()
        self.assertFalse(vm.buttons["forward"].isEnabled())
        self.assertGreater(window.editor.current_line, 0)
        window.bytecode.tree.setCurrentItem(window.bytecode.items()[-1])
        self.pump(0.05)
        self.assertTrue(window.bytecode.footer.text())

    def test_menus_samples_and_shortcuts(self):
        window = self.window
        window.sample_choice.setCurrentIndex(2)
        window.sample_choice.activated.emit(2)
        self.pump(0.05)
        self.assertIn("03_variables.th", window.file_label.text())
        self.assertIn("03_variables.th", window.windowTitle())
        window.actions_by_id["compile"].trigger()
        self.assertIsNotNone(self.controller.last_compile)
        self.controller.last_compile = None
        window.activateWindow()
        window.editor.setFocus()
        self.pump(0.05)
        QTest.keyClick(window.editor, Qt.Key.Key_F7)
        self.pump(0.05)
        self.assertIsNotNone(self.controller.last_compile)  # the application-wide shortcut
        self.assertIn(QKeySequence("F7"), window.actions_by_id["compile"].shortcuts())
        self.assertEqual([m.title() for m in window.menuBar().findChildren(type(window.menuBar().actions()[0].menu()))][:1],
                         ["File"])
        self.assertEqual(window.statusBar().currentMessage(), self.controller.status)
        window.language_choice.activated.emit(0)  # English again: no change
        self.assertIs(window, self.controller.window)

    def test_key_sequences(self):
        self.assertEqual(key_sequences("F5")[0], QKeySequence("F5"))
        self.assertEqual([s.toString() for s in key_sequences("Ctrl++")], ["Ctrl++", "Ctrl+="])
        self.assertEqual(key_sequences(""), [])

    def test_dialogs_and_documents(self):
        window = self.window
        with mock.patch.object(QMessageBox, "question", return_value=QMessageBox.StandardButton.Yes) as ask:
            self.assertTrue(window.ask_yes_no("t", "m"))
        ask.assert_called_once()
        with mock.patch.object(QMessageBox, "question", return_value=QMessageBox.StandardButton.No):
            self.assertFalse(window.ask_yes_no("t", "m"))
        with mock.patch.object(QFileDialog, "getOpenFileName", return_value=("", "")):
            self.assertIsNone(window.ask_open_path())
        with mock.patch.object(QFileDialog, "getOpenFileName", return_value=("/a.th", "")):
            self.assertEqual(window.ask_open_path(), "/a.th")
        with mock.patch.object(QFileDialog, "getSaveFileName", return_value=("/x", "")):
            self.assertEqual(window.ask_save_path("a.th"), "/x.th")
        with mock.patch.object(QFileDialog, "getSaveFileName", return_value=("", "")):
            self.assertIsNone(window.ask_save_path("a.th"))
        with mock.patch.object(QMessageBox, "information") as info, mock.patch.object(QMessageBox, "critical") as error:
            window.show_info("t", "i")
            window.show_error("t", "e")
        info.assert_called_once()
        error.assert_called_once()
        document = window.show_document("language.md", "# Thunder\ntext")
        self.pump(0.05)
        self.assertIn("Thunder", document.findChild(QtTextPanel).get_text())
        self.assertIn(document, window.documents)
        self.controller.run_command("reference")
        self.assertEqual(len(window.documents), 2)
        window.documents[-1].close()

    def test_window_close_button_asks_the_controller(self):
        window = self.window
        window.editor.set_modified(True)
        with mock.patch.object(QMessageBox, "question", return_value=QMessageBox.StandardButton.No) as ask:
            window.closeEvent(mock_close_event := mock.Mock())
        ask.assert_called_once()
        mock_close_event.ignore.assert_called_once()
        window.editor.set_modified(False)


class PashtoInterfaceTests(QtTestCase):
    language = "ps"

    def test_right_to_left_interface(self):
        window = self.window
        self.assertEqual(window.layoutDirection(), Qt.LayoutDirection.RightToLeft)
        self.assertEqual(QApplication.layoutDirection(), Qt.LayoutDirection.RightToLeft)
        self.assertIn("تندر", window.windowTitle())
        self.assertEqual(window.menuBar().actions()[0].text(), i18n.tr("menu.file"))
        self.assertTrue(window.editor.right_to_left)  # the Pashto sample
        self.assertIn("کرښه چاپ", window.editor.get_text())
        self.assertEqual(window.edu_tabs.tabText(0), i18n.tr("tab.overview"))
        self.controller.compile()
        self.assertGreater(len(window.tokens.items()), 5)
        self.controller.run_command("translate")
        self.pump(0.05)
        self.assertFalse(window.editor.right_to_left)
        self.assertIn("println", window.editor.get_text())
        self.controller.run_command("translate")
        self.pump(0.05)
        self.assertTrue(window.editor.right_to_left)
        window.grab()

    def test_switching_language_rebuilds_the_window(self):
        old = self.window
        self.controller.set_language("en")
        self.pump(0.1)
        self.assertIsNot(self.controller.window, old)
        self.assertEqual(self.controller.window.layoutDirection(), Qt.LayoutDirection.LeftToRight)
        self.controller.set_language("ps")
        self.pump(0.1)
        self.assertEqual(self.controller.window.layoutDirection(), Qt.LayoutDirection.RightToLeft)


class WidgetTests(QtTestCase):
    def test_text_panel(self):
        panel = QtTextPanel(QFont("Sans", 10), styles={"red": {"foreground": "#ff0000", "bold": True, "size": 14}},
                            background="#000000", foreground="#ffffff", wrap=False)
        panel.append("plain\n")
        panel.append("coloured", "red")
        self.assertEqual(panel.get_text(), "plain\ncoloured")
        panel.set_lines([StyledText("a", "red"), StyledText("b")])
        self.assertEqual(panel.get_text(), "a\nb\n")
        panel.clear()
        self.assertEqual(panel.get_text(), "")

    def test_tree_panel(self):
        selected = []
        spec = TreeSpec("Name", (Column("kind", "Kind", 80),))
        panel = QtTreePanel(spec, selected.append)
        child = TreeRow("child", ("c",), style="error")
        panel.set_rows([TreeRow("parent", ("p",), children=[child], expanded=False), TreeRow("second", ("s",))])
        items = panel.items()
        self.assertEqual([item.text(0) for item in items], ["parent", "child", "second"])
        self.assertFalse(items[0].isExpanded())
        panel.tree.setCurrentItem(items[1])
        self.assertEqual(selected, [child])
        panel.set_footer("note")
        self.assertTrue(panel.footer.isVisibleTo(panel))
        panel.set_footer("")
        self.assertFalse(panel.footer.isVisibleTo(panel))
        flat = QtTreePanel(TreeSpec(None, (Column("a", "A", 50), Column("b", "B", 50))), selected.append)
        flat.set_rows([TreeRow("x", ("1", "2"))])
        self.assertEqual([flat.items()[0].text(i) for i in range(3)], ["x", "1", "2"])

    def test_vm_panel(self):
        actions = []
        panel = QtVmPanel(QFont("Sans", 10), actions.append)
        panel.show(None)
        self.assertFalse(panel.buttons["first"].isEnabled())
        state = VmViewState("Step 2 of 9", "PUSH 1", True, True, [TreeRow("0", ("1", "local x"))],
                            [TreeRow("main", (0,))], [TreeRow("x", ("1",))], [], "out", 3)
        panel.show(state)
        self.assertTrue(panel.buttons["back"].isEnabled())
        self.assertEqual(panel.output.get_text(), "out")
        panel.buttons["forward"].click()
        self.assertEqual(actions, ["forward"])


class SchedulerAndAppTests(QtTestCase):
    def test_scheduler(self):
        scheduler = self.backend.scheduler
        ran = []
        handle = scheduler.call_later(10_000, lambda: ran.append("late"))
        scheduler.cancel(handle)
        scheduler.cancel("not a timer")
        scheduler.call_later(1, lambda: ran.append("soon"))
        self.pump(0.1)
        self.assertEqual(ran, ["soon"])
        thread = threading.Thread(target=lambda: scheduler.post(lambda: ran.append("posted")))
        thread.start()
        thread.join()
        self.pump(0.1)
        scheduler.flush()
        self.assertEqual(ran, ["soon", "posted"])

    def test_create_backend(self):
        with self.assertRaises(ValueError):
            app_module.create_backend("gtk")
        self.assertIsInstance(app_module.create_backend("qt"), QtBackend)


@support.requires_qt
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
                mock.patch.object(QtBackend, "run", lambda backend: created[0].close()), \
                mock.patch.object(IdeController, "__init__", recording_init):
            self.assertEqual(app_module.main(["--bin", str(support.bin_dir()),
                                              str(support.SAMPLES / "04_strings.th")]), 0)
            self.assertEqual(created[0].display_name, "04_strings.th")
            created.clear()
            with mock.patch("thunder_ide.app.Toolchain.locate", side_effect=app_module.ToolchainError("missing")), \
                    mock.patch.object(QMessageBox, "critical") as error:
                self.assertEqual(app_module.main([]), 0)
            self.assertIn("missing", error.call_args[0][2])
        QApplication.processEvents()


if __name__ == "__main__":
    unittest.main()
