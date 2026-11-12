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
from thunder_ide.core.layout import CANVAS, EDITOR, EDUCATION, OUTPUT, Layout
from thunder_ide.core.settings import Settings
from thunder_ide.core.toolchain import Toolchain
from thunder_ide.core.viewmodels import Column, Span, StyledText, TreeRow, TreeSpec, VmViewState

if support.qt_available():
    from PySide6.QtCore import QEvent, QEventLoop, QPoint, Qt
    from PySide6.QtGui import QFont, QKeyEvent, QKeySequence, QTextCursor
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
        """Delivers Qt's pending events, posted callbacks and timers. The argument is an upper
        bound for timers (the 40 ms highlight delay, the canvas refresh, dock animations): the
        loop blocks *inside Qt* until an event arrives, so it neither spins nor sleeps blindly."""
        end = time.monotonic() + seconds
        cls.app.sendPostedEvents()
        cls.app.processEvents()
        while time.monotonic() < end:
            cls.app.processEvents(QEventLoop.ProcessEventsFlag.WaitForMoreEvents, 10)
        cls.app.sendPostedEvents()
        cls.app.processEvents()

    @classmethod
    def wait_until(cls, condition, timeout=30.0, what="the condition"):
        """Pumps events until ``condition()`` holds; fails (not hangs) after ``timeout`` seconds."""
        end = time.monotonic() + timeout
        while not condition():
            if time.monotonic() > end:
                raise AssertionError(f"timed out after {timeout:.0f}s waiting for {what}")
            cls.app.processEvents(QEventLoop.ProcessEventsFlag.WaitForMoreEvents, 20)
        cls.app.sendPostedEvents()
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
        self.wait_until(lambda: self.controller.run is None or not self.controller.run.running, timeout,
                        "the program to finish")
        # The exit is reported through a posted callback and the window then repaints: two more
        # rounds of events, not a timed sleep.
        self.pump(0.02)

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
                elif found != position:
                    # Another logical position whose caret is drawn at (nearly) the same point is
                    # an equally right answer: with some fonts (Windows especially) neighbouring
                    # carets coincide to within a pixel, and either may win the distance tie.
                    self.assertLess(abs(self.caret_x(found) - self.caret_x(position)), 1.5,
                                    (block_number, index, token))
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

        def colours_of_line(number):
            # Every colour along the middle of a line: independent of fonts and glyph positions.
            block = editor.document().findBlockByNumber(number - 1)
            geometry = editor.blockBoundingGeometry(block).translated(editor.contentOffset())
            y = int(geometry.top() + geometry.height() / 2)
            return {image.pixelColor(x, y).name() for x in range(0, image.width(), 2)}

        self.assertIn(style.CURRENT_LINE_BACKGROUND, colours_of_line(2))     # the debugger's line
        self.assertNotIn(style.CURRENT_LINE_BACKGROUND, colours_of_line(1))
        self.assertIn(style.EDITOR_BACKGROUND, colours_of_line(1))
        self.assertIn(style.ERROR_BACKGROUND, colours_of_line(1))            # the marked error
        self.assertIn(style.SPAN_BACKGROUND, colours_of_line(3))             # the selected span
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
        # The wrapped lines sit exactly where Qt's own layout puts them (block_layout copies the
        # y positions), so the two heights differ by at most one line's leading - the display
        # layout measures its own last line, and some fonts (Windows) give it a pixel or two more.
        last = layout.layout.lineCount() - 1
        self.assertAlmostEqual(layout.layout.lineAt(last).y(), block.layout().lineAt(last).y(), delta=0.01)
        self.assertAlmostEqual(layout.layout.boundingRect().height(), editor.blockBoundingRect(block).height(),
                               delta=editor.fontMetrics().height() / 2)
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


class BitsPanelTests(QtTestCase):
    def test_the_bits_tab_shows_the_operation_bit_by_bit(self):
        panel = self.window.bits
        panel.first.setText("12")
        panel.second.setText("10")
        panel.operator.setCurrentText("&")
        self.assertEqual(panel.result.text(), "8")                 # refreshed by the text changes
        self.assertEqual([row.bits for row in panel.view.rows], ["00001100", "00001010", "00001000"])
        self.assertIn("00001100"[0], panel.table.text())  # the rows are rendered
        panel.operator.setCurrentText("~")                # unary: the second operand goes grey
        self.assertFalse(panel.second.isEnabled())
        self.assertEqual(panel.result.text(), "-13")
        panel.operator.setCurrentText("<<")
        panel.second.setText("64")
        self.assertEqual(panel.result.text(), "")         # out of range: the message shows instead
        self.assertIn("63", panel.table.text())
        self.window.select_panel_tab("bits")              # the tab is reachable like any other


class OccurrenceHighlightTests(QtTestCase):
    def test_double_clicking_a_name_marks_its_other_uses(self):
        self.set_program("var total = 1\ntotal = total + 1\nprintln(total, totals)\n")
        editor = self.editor
        block = editor.document().findBlockByNumber(1)
        point = editor.caret_point(block.position() + 2).toPoint() + QPoint(0, 5)
        QTest.mouseDClick(editor.viewport(), Qt.MouseButton.LeftButton, pos=point)
        self.assertEqual(editor.textCursor().selectedText(), "total")
        text = editor.get_text()
        self.assertEqual([text[a:b] for a, b in editor.occurrences], ["total"] * 4)   # "totals" is another name
        editor.viewport().grab()                                                     # paints the marks
        block_range = editor._occurrence_ranges(1, block.text(), editor.block_layout(block))
        self.assertEqual(len(block_range), 2)                                        # both uses on line 2
        cursor = editor.textCursor()
        cursor.clearSelection()
        editor.setTextCursor(cursor)                                                 # deselecting clears them
        self.assertEqual(editor.occurrences, [])


class HelpViewerTests(QtTestCase):
    def test_markdown_rendering_contents_and_search(self):
        self.controller.run_command("library_reference")
        self.pump(0.05)
        viewer = self.window.documents[-1]
        self.assertEqual(viewer.current_document().name, "library.md")
        # Rendered, not plain text: a heading in the source is gone from the shown text,
        # and the document carries real formatting (more than one font size).
        self.assertNotIn("## ", viewer.browser.toPlainText())
        self.assertIn("Conventions", viewer.browser.toPlainText())
        # The contents list holds the whole reference in reading order and switches documents.
        self.assertEqual(viewer.contents.count(), 9)
        viewer.contents.setCurrentRow(0)
        self.assertEqual(viewer.current_document().name, "README.md")
        # Searching finds and counts matches, Enter steps through them.
        viewer.contents.setCurrentRow(1)                       # language.md
        viewer.search.setText("bitwise")
        self.assertGreater(len(viewer._matches), 2)
        self.assertEqual(viewer.matches_label.text(), f"1 of {len(viewer._matches)}")
        viewer.search.returnPressed.emit()
        self.assertEqual(viewer.matches_label.text(), f"2 of {len(viewer._matches)}")
        self.assertEqual(viewer.browser.textCursor().selectedText().lower(), "bitwise")
        viewer.search.setText("no-such-word-anywhere")
        self.assertEqual(viewer._matches, [])
        # A link to another reference document switches to it.
        from PySide6.QtCore import QUrl
        viewer._follow(QUrl("library.md"))
        self.assertEqual(viewer.current_document().name, "library.md")
        viewer.close()
        self.pump(0.02)


class FoldMarkerTests(QtTestCase):
    def test_gutter_knows_the_fold_state_of_each_block(self):
        self.set_program("func f()\n    println(1)\n    println(2)\nf()\n")
        editor = self.editor
        self.assertEqual(editor.foldable_headers(), {0})   # "-" next to the header
        self.assertEqual(editor.folded_headers, set())
        self.assertGreater(editor.gutter_width(), editor.fold_marker_width())  # room for the mark
        editor.fold([(0, 2)])
        self.assertEqual(editor.folded_headers, {0})       # now drawn as "+"
        editor.unfold_all()
        self.assertEqual(editor.folded_headers, set())
        self.set_program("println(1)\nprintln(2)\n")       # nothing to fold: no marks
        self.assertEqual(editor.foldable_headers(), set())


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
        self.assertEqual(current.panels_in("right"), [CANVAS])
        self.assertEqual(current.panels[CANVAS].tab_with, "")  # it was tabbed with the panel that moved away

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
        self.assertEqual(window.current_layout().panels_in("right"), [EDUCATION, CANVAS])
        self.assertEqual(window.current_layout().panels[CANVAS].tab_with, EDUCATION)
        self.assertIn(window.docks[CANVAS], window.tabifiedDockWidgets(window.docks[EDUCATION]))
        fractions = window.current_layout()
        self.assertAlmostEqual(fractions.left_fraction, 0.5, delta=0.15)  # the editor gets about half the width
        self.assertAlmostEqual(fractions.top_fraction, 0.72, delta=0.15)

    def drag_separator(self, start: "QPoint", delta: "QPoint") -> None:
        """Drags a panel separator with the mouse, as a user would."""
        window = self.window
        QTest.mousePress(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, start)
        for step in range(1, 11):
            QTest.mouseMove(window, start + delta * step / 10)
            self.pump(0.01)
        QTest.mouseRelease(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, start + delta)
        self.pump(0.1)

    def test_the_output_panel_can_be_resized_both_ways(self):
        # Regressions: (1) the debugger tab's minimum height stopped the Output panel at about a third
        # of the window; (2) without a central widget, the separator above the Output panel could
        # only be dragged upwards - the panel could grow but never shrink again.
        window = self.window
        window.resize(1300, 820)
        self.pump(0.1)
        self.assertLess(window.docks[EDUCATION].minimumSizeHint().height(), 200)
        # A tab with a rigid minimum width (the Bits tab once had one) would stop the right-hand
        # panel from being narrowed: every tab must fit in a modest width.
        self.assertLess(window.docks[EDUCATION].minimumSizeHint().width(), 420)
        output, editor = window.docks[OUTPUT], window.docks[EDITOR]

        def separator() -> "QPoint":
            return QPoint(300, output.geometry().top() - 3)

        heights = [output.height()]
        for delta in (-350, 250, -150, 300):
            self.drag_separator(separator(), QPoint(0, delta))
            heights.append(output.height())
        self.assertGreater(heights[1], heights[0] + 300)            # bigger
        self.assertLess(heights[2], heights[1] - 200)               # and smaller again
        self.assertGreater(heights[3], heights[2] + 100)
        self.assertLess(heights[4], heights[3] - 250)
        self.assertGreater(max(heights), window.height() * 0.5)

        # The separator between the Editor and "Under the bonnet" still moves both ways.
        widths = [editor.width()]
        for delta in (200, -350):
            self.drag_separator(QPoint(editor.geometry().right() + 3, 200), QPoint(delta, 0))
            widths.append(editor.width())
        self.assertGreater(widths[1], widths[0] + 150)
        self.assertLess(widths[2], widths[1] - 300)
        window.select_panel_tab("vm")  # the debugger is still reachable inside its scroll area
        self.assertIs(window.vm.parentWidget().parentWidget(), window.edu_tabs.currentWidget())

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

    def test_the_input_line_answers_a_running_program(self):
        window = self.window
        self.assertFalse(window.input_line.isEnabled())  # only while a program runs
        self.set_program('var name = input("Who? ")\nprintln("Hi ", name)\nprintln(end_of_input())\n')
        window.toolbar_actions["run"].trigger()
        self.assertTrue(window.input_line.isEnabled() and window.send_button.isEnabled())
        end = time.monotonic() + 30
        while "Who? " not in window.output.get_text():  # the prompt appears before the program waits
            self.assertLess(time.monotonic(), end)
            self.pump(0.02)
        QTest.keyClicks(window.input_line, "Ada")
        QTest.keyClick(window.input_line, Qt.Key.Key_Return)
        self.assertEqual(window.input_line.text(), "")
        window.end_input_button.click()
        self.wait_for_run()
        text = window.output.get_text()
        self.assertIn("Who? Ada\n", text)  # the typed line is echoed after the prompt
        self.assertIn("Hi Ada\ntrue", text)
        self.assertIn("[end of input]", text)
        self.assertFalse(window.end_input_button.isEnabled())
        self.assertIs(window.bottom_tabs.currentWidget(), window.output_page)

    def test_the_canvas_paints_frames_and_reports_keys_and_the_mouse(self):
        window = self.window
        from thunder_ide.core.canvas import CanvasSnapshot
        window.select_panel_tab("canvas")
        self.pump(0.1)
        self.assertFalse(window.docks[CANVAS].visibleRegion().isEmpty())
        canvas = window.canvas.canvas
        snapshot = CanvasSnapshot(200, 150, [
            ["clear", "#000080"], ["rect", 10, 10, 40, 30, "#ff0000"], ["rect_outline", 60, 10, 40, 30, "#ffffff", 3],
            ["circle", 150, 40, 20, "#00ff00"], ["circle_outline", 150, 100, 20, "#ffff00", 2],
            ["line", 0, 149, 199, 149, "#ffffff", 2], ["pixel", 5, 140, "#ffffff"],
            ["text", 10, 100, "پښتو ژبه", "#ffffff", 14], ["bogus"], ["rect", "x"]], 7)
        window.canvas.show(snapshot)
        window.canvas.set_status("frame 7")
        self.pump(0.05)
        image = canvas.grab().toImage()
        scale, left, top = canvas.transform()
        self.assertAlmostEqual(scale, min(canvas.width() / 200, canvas.height() / 150))  # scaled to fit

        def color_at(x, y):
            return image.pixelColor(int(left + x * scale), int(top + y * scale)).name()

        self.assertEqual(color_at(20, 20), "#ff0000")   # inside the filled rectangle
        self.assertEqual(color_at(150, 40), "#00ff00")  # the centre of the filled circle
        self.assertEqual(color_at(80, 25), "#000080")   # inside the outline: the background shows
        self.assertEqual(color_at(150, 100), "#000080")
        self.assertEqual(window.canvas.status.text(), "frame 7")
        self.assertEqual(canvas.to_canvas(left + 20 * scale, top + 30 * scale), (20, 30))

        keys, mice = [], []
        with mock.patch.object(canvas, "on_key", side_effect=lambda *a: keys.append(a)), \
                mock.patch.object(canvas, "on_mouse", side_effect=lambda *a: mice.append(a)):
            QTest.keyClick(canvas, Qt.Key.Key_Left)
            QTest.keyClick(canvas, Qt.Key.Key_A)
            QTest.keyClick(canvas, Qt.Key.Key_Space)
            QTest.keyClick(canvas, Qt.Key.Key_F1)          # no name: not reported
            QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=QPoint(int(left + 50.5 * scale), int(top + 60.5 * scale)))
        self.assertEqual(keys, [("left", True), ("left", False), ("a", True), ("a", False), ("space", True),
                                ("space", False)])
        self.assertEqual(mice, [(50, 60, True), (50, 60, False)])
        self.assertFalse(canvas.focusNextPrevChild(True))  # Tab belongs to the program

        window.canvas.show(None)
        window.canvas.set_status("Programs that import graphics draw here.")
        self.pump(0.05)
        self.assertEqual(canvas.hint, "Programs that import graphics draw here.")

    def test_a_graphics_sample_runs_in_the_ide(self):
        window = self.window
        self.controller.load_sample([s.name for s in self.controller.samples].index("28_snake.th"))
        window.toolbar_actions["run"].trigger()
        end = time.monotonic() + 30
        while window.canvas.canvas.snapshot is None:
            self.assertLess(time.monotonic(), end)
            self.pump(0.02)
        self.pump(0.2)
        self.assertTrue(window.canvas.canvas.hasFocus())  # ready for the arrow keys
        QTest.keyClick(window.canvas.canvas, Qt.Key.Key_Down)
        self.pump(0.3)
        self.assertIn("score", " ".join(str(shape) for shape in window.canvas.canvas.snapshot.shapes))
        window.toolbar_actions["stop"].trigger()
        self.wait_for_run()

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
        # The Yes/No buttons carry the interface's own words (Qt's standard buttons would be English).
        asked = []

        def fake_exec(box):
            asked.append([button.text() for button in box.buttons()])
            return 0

        with mock.patch.object(QMessageBox, "exec", fake_exec), \
                mock.patch.object(QMessageBox, "clickedButton", lambda box: box.buttons()[0]):
            self.assertTrue(window.ask_yes_no("t", "m"))
        self.assertEqual(asked, [[i18n.tr("dialog.yes"), i18n.tr("dialog.no")]])
        with mock.patch.object(QMessageBox, "exec", fake_exec), \
                mock.patch.object(QMessageBox, "clickedButton", lambda box: box.buttons()[1]):
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
        self.controller.run_command("reference")
        self.pump(0.05)
        viewer = window.documents[-1]
        self.assertEqual(viewer.current_document().name, "language.md")
        self.assertIn(viewer, window.documents)
        viewer.close()

    def test_window_close_button_asks_the_controller(self):
        window = self.window
        window.editor.set_modified(True)
        asked = []
        with mock.patch.object(QMessageBox, "exec", lambda box: asked.append(box.text()) or 0), \
                mock.patch.object(QMessageBox, "clickedButton", lambda box: box.buttons()[1]):  # "No"
            window.closeEvent(mock_close_event := mock.Mock())
        self.assertEqual(len(asked), 1)
        mock_close_event.ignore.assert_called_once()
        window.editor.set_modified(False)


class PashtoInterfaceTests(QtTestCase):
    language = "ps"

    def test_yes_no_dialog_speaks_pashto(self):
        asked = []

        def fake_exec(box):
            asked.append([button.text() for button in box.buttons()])
            return 0

        with mock.patch.object(QMessageBox, "exec", fake_exec), \
                mock.patch.object(QMessageBox, "clickedButton", lambda box: box.buttons()[0]):
            self.assertTrue(self.window.ask_yes_no("سرلیک", "پیغام"))
        self.assertEqual(asked, [["هو", "نه"]])

    def test_negative_numbers_show_their_sign_on_the_left(self):
        # In right-to-left text the bidirectional algorithm would move a lone "-" to the right of
        # Pashto digits; the panels wrap "-۳" in a left-to-right isolate for display only.
        from thunder_ide.qt.panels import directional
        wrapped = "\u2068\u200e-۳\u200e\u2069"
        self.assertEqual(directional("ځواب -۳ دی"), f"ځواب {wrapped} دی")
        self.assertEqual(directional("۷ - ۲"), "۷ - ۲")      # subtraction (spaces) is left alone
        self.assertEqual(directional("a -42"), "a -42")        # Latin digits need no help
        output = self.window.output
        output.clear()
        output.append("وېش: -۳\n")
        self.assertIn(wrapped, output.toPlainText())             # displayed with the marks
        self.assertEqual(output.get_text(), "وېش: -۳\n")         # reported without them
        self.window.problems.set_rows([TreeRow("t", ("-۳",))])
        self.assertEqual(self.window.problems.items()[0].text(1), wrapped)

    def test_decimal_numbers_keep_their_halves_together_and_their_order(self):
        # A negative decimal is one unit, point included: "-۹٫۸۱", not "-۹" with "٫۸۱" loose.
        from thunder_ide.qt.panels import directional, line_direction_mark
        self.assertEqual(directional("جاذبه -۹٫۸۱ ده"), "جاذبه \u2068\u200e-۹٫۸۱\u200e\u2069 ده")
        self.assertEqual(directional("مینه -2.5"), "مینه -2.5")  # Latin digits need no help
        # A line of nothing but numbers would take the panel's own direction and come out
        # backwards; it is started with a left-to-right mark instead.
        self.assertEqual(line_direction_mark("۷٫۰ ۳٫۵ ۳٫۰"), "\u200e")
        self.assertEqual(line_direction_mark("منځنۍ ۷٫۵"), "")   # a word gives the direction
        self.assertEqual(line_direction_mark("average 7.5"), "")
        self.assertEqual(line_direction_mark(""), "")
        output = self.window.output
        output.clear()
        output.append("۷٫۰ ۳٫۵\nمنځنۍ نمره: ۷۷٫۷۵\n")
        shown = output.toPlainText()
        self.assertTrue(shown.startswith("\u200e۷٫۰ ۳٫۵\n"))    # the numbers-only line is marked
        self.assertIn("\nمنځنۍ نمره: ۷۷٫۷۵", shown)              # the line with words is not
        self.assertEqual(output.get_text(), "۷٫۰ ۳٫۵\nمنځنۍ نمره: ۷۷٫۷۵\n")  # marks never leak out
        # The marks demonstrably put the sign on the digits' left in right-to-left text (measured
        # through the same text engine that paints the panels).
        from PySide6.QtGui import QTextLayout, QTextOption
        layout = QTextLayout(directional("وېش: -۳ دی"))
        option = QTextOption()
        option.setTextDirection(Qt.LayoutDirection.RightToLeft)
        layout.setTextOption(option)
        layout.beginLayout()
        line = layout.createLine()
        line.setLineWidth(10000)
        layout.endLayout()

        def centre(char):
            index = layout.text().index(char)
            return (line.cursorToX(index)[0] + line.cursorToX(index + 1)[0]) / 2

        self.assertLess(centre("-"), centre("۳"))

    def test_canvas_text_starts_at_its_position_in_a_right_to_left_interface(self):
        # Regression: in the mirrored interface Qt right-aligned canvas text, moving it off the canvas.
        from thunder_ide.core.canvas import CanvasSnapshot
        window = self.window
        window.select_panel_tab("canvas")
        self.pump(0.1)
        canvas = window.canvas.canvas
        self.assertEqual(canvas.layoutDirection(), Qt.LayoutDirection.LeftToRight)  # x grows to the right
        for message in ("د تندر ګرافیک", "Thunder"):
            window.canvas.show(CanvasSnapshot(200, 100, [["clear", "#000000"], ["text", 10, 40, message, "#ffffff", 20]], 1))
            self.pump(0.05)
            image = canvas.grab().toImage()
            scale, left, top = canvas.transform()
            written = {image.pixelColor(int(left + x * scale), int(top + y * scale)).name()
                       for x in range(10, 80) for y in range(40, 60)}
            self.assertGreater(len(written), 1, message)  # something besides the black background

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
    def test_find_bar(self):
        from thunder_ide.qt.findbar import FindBar
        found, replaced = [], []
        bar = FindBar(found.append, lambda request, everywhere: replaced.append((request, everywhere)))
        self.assertFalse(bar.isVisible())

        bar.open_bar(False, "two")
        self.assertTrue(bar.isVisibleTo(bar))
        self.assertEqual(bar.needle.text(), "two")       # the selection becomes what to look for
        self.assertFalse(bar.replacement.isVisibleTo(bar))
        bar.open_bar(True)
        self.assertTrue(bar.replacement.isVisibleTo(bar))
        self.assertEqual(bar.needle.text(), "two")       # an empty selection keeps the last search

        bar.case.setChecked(True)
        bar.word.setChecked(True)
        bar.replacement.setText("2")
        bar.needle.returnPressed.emit()                  # Enter in the Find field is "find next"
        self.assertEqual(len(found), 1)
        self.assertEqual((found[0].needle, found[0].replacement), ("two", "2"))
        self.assertTrue(found[0].case_sensitive and found[0].whole_word)
        self.assertFalse(found[0].backwards)
        self.assertTrue(bar.request(backwards=True).backwards)

        bar.replacement.returnPressed.emit()             # Enter in Replace replaces this one
        self.assertEqual(replaced[-1][1], False)
        bar._replace(True)
        self.assertEqual(replaced[-1][1], True)

        # An empty search does nothing at all.
        bar.needle.setText("")
        bar._find(False)
        bar._replace(False)
        self.assertEqual(len(found), 1)
        self.assertEqual(len(replaced), 2)

        bar.open_bar(False)
        bar.keyPressEvent(QKeyEvent(QEvent.Type.KeyPress, int(Qt.Key.Key_Escape), Qt.KeyboardModifier.NoModifier))
        self.assertTrue(bar.isHidden())
        bar.close_bar()                                  # closing twice is harmless

    def test_editor_commands(self):
        editor = self.window.editor
        editor.set_text("func f()\n    println(1)\n    println(2)\nf()\n")
        editor.set_selection(9, 19)
        self.assertEqual(editor.selection(), (9, 19))
        editor.replace_range(9, 13, "  ")                 # one undoable edit
        self.assertTrue(editor.get_text().startswith("func f()\n  println"))
        self.assertEqual(editor.selection(), (9, 11))

        self.assertFalse(editor.show_whitespace)
        editor.set_show_whitespace(True)
        self.assertTrue(editor.show_whitespace)
        editor.set_show_whitespace(True)                  # already on: nothing changes
        editor.set_show_whitespace(False)

        editor.set_text("func f()\n    println(1)\n    println(2)\nf()\n")
        self.assertEqual(editor.fold([(0, 2)]), 1)
        self.assertFalse(editor.document().findBlockByNumber(1).isVisible())
        editor.fold([(5, 9)])                             # lines that do not exist are skipped
        editor.go_to_line(2)                              # jumping into a folded block opens it
        self.assertTrue(editor.document().findBlockByNumber(1).isVisible())
        self.assertEqual(editor.fold([(0, 2)]), 1)
        editor.unfold_all()
        editor.unfold_all()                               # nothing folded: nothing to do
        self.assertTrue(all(editor.document().findBlockByNumber(i).isVisible() for i in range(4)))
        # Any edit opens every fold, so no text is ever changed out of sight.
        editor.fold([(0, 2)])
        editor.replace_range(0, 4, "FUNC")
        self.assertEqual(editor.folded_headers, set())

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
