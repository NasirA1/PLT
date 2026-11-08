"""The Qt code editor (EditorView): line numbers, live syntax highlighting, indentation-aware
typing and bidirectional rendering that follows the program's language.

``QPlainTextEdit`` keeps the text, the undo history, the cursor, scrolling and all editing
commands. Only the *drawing* and the mouse hit-testing are replaced: every block (line of the
program) is laid out again with ``QTextLayout`` from the display string that ``core.direction``
builds, so that a Pashto program reads right to left token by token while each English name or
string keeps its own direction (see that module for the reasoning). The block geometry is
still Qt's, so scrolling, wrapping and the line numbers stay in step.
"""

from __future__ import annotations

import bisect
from dataclasses import dataclass
from typing import Callable, Optional

from PySide6.QtCore import QPoint, QPointF, QRect, QRectF, QSize, Qt, QTimer
from PySide6.QtGui import (QColor, QFont, QFontMetricsF, QKeyEvent, QMouseEvent, QPainter, QPaintEvent, QResizeEvent,
                           QTextBlock, QTextCharFormat, QTextCursor, QTextLayout, QTextLine, QTextOption)
from PySide6.QtWidgets import QPlainTextEdit, QWidget

from thunder_ide.core import direction, editing
from thunder_ide.core.direction import LineLayout, index_to_utf16, utf16_to_index
from thunder_ide.core.editing import INDENT, BlockWords
from thunder_ide.core.highlight import Highlight
from thunder_ide.core.viewmodels import Span
from thunder_ide.qt import style

BLINK_MS = 530
CARET_WIDTH = 1.5


@dataclass
class BlockLayout:
    """The display layout of one block, cached until the text, direction or colours change."""

    key: tuple
    line: LineLayout
    layout: QTextLayout

    def utf16(self, display_index: int) -> int:
        """Qt counts positions in UTF-16 units; converts a display index."""
        return index_to_utf16(self.line.display, display_index)

    def x_of(self, logical_index: int) -> tuple[QTextLine, float]:
        """The wrapped line and x position (within the block) of the caret at a logical index."""
        display_index, trailing = self.line.caret(logical_index)
        position = self.utf16(display_index)
        line = self.layout.lineForTextPosition(position)
        if not line.isValid():
            line = self.layout.lineAt(max(self.layout.lineCount() - 1, 0))
        edge = QTextLine.Edge.Trailing if trailing else QTextLine.Edge.Leading
        result = line.cursorToX(position, edge)
        return line, float(result[0] if isinstance(result, tuple) else result)


def _char_format(foreground: Optional[str] = None, background: Optional[str] = None, italic: bool = False,
                 underline: bool = False) -> QTextCharFormat:
    fmt = QTextCharFormat()
    if foreground:
        fmt.setForeground(QColor(foreground))
    if background:
        fmt.setBackground(QColor(background))
    if italic:
        fmt.setFontItalic(True)
    if underline:
        fmt.setUnderlineStyle(QTextCharFormat.UnderlineStyle.WaveUnderline)
        fmt.setUnderlineColor(QColor(style.ERROR_FOREGROUND))
    return fmt


def _format_range(start: int, end: int, fmt: QTextCharFormat) -> QTextLayout.FormatRange:
    rng = QTextLayout.FormatRange()
    rng.start = start
    rng.length = end - start
    rng.format = fmt
    return rng


class Gutter(QWidget):
    """Draws the line numbers beside the text (left of it, or right of it for right-to-left)."""

    def __init__(self, editor: "CodeEditor"):
        super().__init__(editor)
        self.editor = editor
        self.marked_line: Optional[int] = None

    def sizeHint(self) -> QSize:
        return QSize(self.editor.gutter_width(), 0)

    def paintEvent(self, event: QPaintEvent) -> None:
        editor = self.editor
        painter = QPainter(self)
        painter.fillRect(event.rect(), QColor(style.GUTTER_BACKGROUND))
        painter.setFont(editor.font())
        line_height = QFontMetricsF(editor.font()).height()
        rtl = editor.right_to_left
        flags = (Qt.AlignmentFlag.AlignLeft if rtl else Qt.AlignmentFlag.AlignRight) | Qt.AlignmentFlag.AlignVCenter
        offset = editor.contentOffset()
        block = editor.firstVisibleBlock()
        while block.isValid():
            geometry = editor.blockBoundingGeometry(block).translated(offset)
            if geometry.top() > event.rect().bottom():
                break
            if block.isVisible() and geometry.bottom() >= event.rect().top():
                number = block.blockNumber() + 1
                color = style.ERROR_FOREGROUND if number == self.marked_line else style.GUTTER_FOREGROUND
                painter.setPen(QColor(color))
                painter.drawText(QRectF(6, geometry.top(), self.width() - 12, line_height), int(flags), str(number))
            block = block.next()
        painter.end()


class CodeEditor(QPlainTextEdit):
    def __init__(self, font_family: str, font_size: int, on_change: Callable[[], None]):
        super().__init__()
        self.on_change = on_change
        self.right_to_left = False
        self.block_words = BlockWords()
        self.regions: list[Highlight] = []
        self._region_ends: list[int] = []
        self.error_spans: list[Span] = []
        self.selected_span: Optional[Span] = None
        self.current_line = 0
        self.show_whitespace = False
        self.folded_headers: set[int] = set()  # blocks whose body is hidden (see fold)
        self._cache: dict[int, BlockLayout] = {}
        self._block_starts: Optional[list[int]] = None
        self._caret_on = True
        self._dragging = False
        self._formats = {tag: _char_format(color, italic=tag == "comment") for tag, color in style.SYNTAX_COLORS.items()}
        self._error_format = _char_format(style.ERROR_FOREGROUND, style.ERROR_BACKGROUND, underline=True)
        self._span_format = _char_format(background=style.SPAN_BACKGROUND)
        self._selection_format = _char_format(background=style.SELECTION_BACKGROUND)

        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.WidgetWidth)
        self.setWordWrapMode(QTextOption.WrapMode.WrapAtWordBoundaryOrAnywhere)
        self.setCursorWidth(0)  # the caret is drawn by paintEvent
        self.setFrameShape(QPlainTextEdit.Shape.NoFrame)
        self.gutter = Gutter(self)
        font = QFont(font_family, font_size)
        font.setStyleHint(QFont.StyleHint.Monospace)
        self.setFont(font)
        self.set_font_size(font_size)

        self.blockCountChanged.connect(lambda _count: self._update_gutter_width())
        self.updateRequest.connect(self._on_update_request)
        self.textChanged.connect(self._on_text_changed)
        self.cursorPositionChanged.connect(self._on_cursor_changed)
        self.selectionChanged.connect(lambda: self.viewport().update())
        self._blink = QTimer(self)
        self._blink.timeout.connect(self._toggle_caret)
        self._blink.start(BLINK_MS)

    # ------------------------------------------------------------------ bookkeeping

    def _on_text_changed(self) -> None:
        if self.folded_headers:
            self.unfold_all()  # folding hides lines; editing must never happen out of sight
        self._cache.clear()
        self._block_starts = None
        self.on_change()

    def _on_cursor_changed(self) -> None:
        self._caret_on = True
        self._blink.start(BLINK_MS)
        self.viewport().update()

    def _toggle_caret(self) -> None:
        self._caret_on = not self._caret_on
        if self.hasFocus():
            self.viewport().update()

    def _starts(self) -> list[int]:
        """Code-point offset of every block in the whole text (highlights use these offsets)."""
        if self._block_starts is None:
            starts = [0]
            for block_text in self.toPlainText().split("\n")[:-1]:
                starts.append(starts[-1] + len(block_text) + 1)
            self._block_starts = starts
        return self._block_starts

    def _regions_in(self, start: int, end: int) -> list[Highlight]:
        first = bisect.bisect_right(self._region_ends, start)
        result = []
        for region in self.regions[first:]:
            if region.start >= end:
                break
            result.append(region)
        return result

    def _text_width(self) -> float:
        return max(self.viewport().width() - 2 * self.document().documentMargin(), 10.0)

    def block_layout(self, block: QTextBlock) -> BlockLayout:
        """The display layout of a block (see the module documentation)."""
        number = block.blockNumber()
        text = block.text()
        start = self._starts()[number] if number < len(self._starts()) else 0
        regions = self._regions_in(start, start + len(text))
        width = self._text_width()
        key = (text, self.right_to_left, tuple(regions), width, self.font().key(), start,
               self.show_whitespace)
        cached = self._cache.get(number)
        if cached is not None and cached.key == key:
            return cached

        line = direction.analyse_line(text, self.right_to_left)
        layout = QTextLayout(line.display, self.font())
        option = QTextOption()
        if self.right_to_left:
            option.setTextDirection(Qt.LayoutDirection.RightToLeft)
            option.setAlignment(Qt.AlignmentFlag.AlignRight)
        else:
            option.setTextDirection(Qt.LayoutDirection.LeftToRight)
            option.setAlignment(Qt.AlignmentFlag.AlignLeft)
        option.setWrapMode(QTextOption.WrapMode.WrapAtWordBoundaryOrAnywhere)
        if self.show_whitespace:
            option.setFlags(QTextOption.Flag.ShowTabsAndSpaces)
        option.setTabStopDistance(self.tabStopDistance())
        layout.setTextOption(option)
        formats = []
        for region in regions:
            first, last = max(region.start - start, 0), min(region.end - start, len(text))
            if first < last:
                formats.append(_format_range(line.offsets[first], line.offsets[last], self._formats[region.tag]))
        layout.setFormats([self._to_utf16(rng, line) for rng in formats])
        margin = self.document().documentMargin()
        qt_layout = block.layout()  # Qt's own layout of the block: its line positions keep the two in step
        layout.beginLayout()
        height = 0.0
        while True:
            text_line = layout.createLine()
            if not text_line.isValid():
                break
            text_line.setLineWidth(width)
            index = text_line.lineNumber()
            if qt_layout is not None and index < qt_layout.lineCount():
                height = qt_layout.lineAt(index).y()
            text_line.setPosition(QPointF(margin, height))
            height += text_line.height()
        layout.endLayout()
        result = BlockLayout(key, line, layout)
        self._cache[number] = result
        return result

    @staticmethod
    def _to_utf16(rng: QTextLayout.FormatRange, line: LineLayout) -> QTextLayout.FormatRange:
        start = index_to_utf16(line.display, rng.start)
        return _format_range(start, index_to_utf16(line.display, rng.start + rng.length), rng.format)

    # ------------------------------------------------------------------ painting

    def paintEvent(self, event: QPaintEvent) -> None:
        painter = QPainter(self.viewport())
        painter.fillRect(event.rect(), QColor(style.EDITOR_BACKGROUND))
        offset = self.contentOffset()
        cursor = self.textCursor()
        caret_visible = self._caret_on and self.hasFocus()
        block = self.firstVisibleBlock()
        while block.isValid():
            geometry = self.blockBoundingGeometry(block).translated(offset)
            if geometry.top() > event.rect().bottom():
                break
            if block.isVisible() and geometry.bottom() >= event.rect().top():
                self._paint_block(painter, block, geometry, cursor, caret_visible)
            block = block.next()
        painter.end()

    def _paint_block(self, painter: QPainter, block: QTextBlock, geometry: QRectF, cursor: QTextCursor,
                     caret_visible: bool) -> None:
        number = block.blockNumber()
        text = block.text()
        layout = self.block_layout(block)
        if number + 1 == self.current_line:
            height = layout.layout.boundingRect().height() or geometry.height()
            painter.fillRect(QRectF(0, geometry.top(), self.viewport().width(), height),
                             QColor(style.CURRENT_LINE_BACKGROUND))
        ranges: list[QTextLayout.FormatRange] = []
        for span in self.error_spans:
            ranges.extend(self._span_ranges(span, number, text, layout, self._error_format))
        if self.selected_span is not None:
            ranges.extend(self._span_ranges(self.selected_span, number, text, layout, self._span_format))
        ranges.extend(self._selection_ranges(block, text, layout, cursor))
        origin = QPointF(geometry.left(), geometry.top())
        layout.layout.draw(painter, origin, ranges)
        if caret_visible and cursor.blockNumber() == number:
            logical = utf16_to_index(text, cursor.positionInBlock())
            line, x = layout.x_of(logical)
            painter.fillRect(QRectF(origin.x() + x - CARET_WIDTH / 2, origin.y() + line.y(), CARET_WIDTH,
                                    line.height()), QColor("#000000"))

    def _span_ranges(self, span: Span, number: int, text: str, layout: BlockLayout,
                     fmt: QTextCharFormat) -> list[QTextLayout.FormatRange]:
        bounds = span_bounds(span, number + 1, len(text))
        if bounds is None or bounds[0] >= bounds[1]:
            return []
        return [_format_range(layout.utf16(layout.line.offsets[bounds[0]]),
                              layout.utf16(layout.line.offsets[bounds[1]]), fmt)]

    def _selection_ranges(self, block: QTextBlock, text: str, layout: BlockLayout,
                          cursor: QTextCursor) -> list[QTextLayout.FormatRange]:
        if not cursor.hasSelection():
            return []
        block_start = block.position()
        block_end = block_start + block.length() - 1
        first = max(cursor.selectionStart(), block_start) - block_start
        last = min(cursor.selectionEnd(), block_end) - block_start
        if first >= last:
            return []
        logical_first, logical_last = utf16_to_index(text, first), utf16_to_index(text, last)
        return [_format_range(layout.utf16(layout.line.offsets[logical_first]),
                              layout.utf16(layout.line.offsets[logical_last]), self._selection_format)]

    def caret_point(self, position: int) -> QPointF:
        """Viewport coordinates of the caret for a document position (used by the tests)."""
        block = self.document().findBlock(position)
        geometry = self.blockBoundingGeometry(block).translated(self.contentOffset())
        layout = self.block_layout(block)
        line, x = layout.x_of(utf16_to_index(block.text(), position - block.position()))
        return QPointF(geometry.left() + x, geometry.top() + line.y())

    # ------------------------------------------------------------------ mouse: hit-testing on the display layout

    def position_at(self, point: QPointF | QPoint) -> Optional[int]:
        """The document position nearest to a point of the viewport."""
        point = QPointF(point)
        block = self.cursorForPosition(point.toPoint()).block()
        if not block.isValid():
            return None
        geometry = self.blockBoundingGeometry(block).translated(self.contentOffset())
        layout = self.block_layout(block)
        text = block.text()
        y = point.y() - geometry.top()
        target = 0
        for index in range(layout.layout.lineCount()):
            if y >= layout.layout.lineAt(index).y():
                target = index
        best: Optional[tuple[float, int]] = None
        for logical in range(len(text) + 1):
            line, x = layout.x_of(logical)
            if line.lineNumber() != target:
                continue
            distance = abs(geometry.left() + x - point.x())
            if best is None or distance < best[0]:
                best = (distance, logical)
        logical = best[1] if best is not None else len(text)
        return block.position() + index_to_utf16(text, logical)

    def _place_cursor(self, point: QPointF, keep_anchor: bool) -> None:
        position = self.position_at(point)
        if position is None:
            return
        cursor = self.textCursor()
        cursor.setPosition(position, QTextCursor.MoveMode.KeepAnchor if keep_anchor else QTextCursor.MoveMode.MoveAnchor)
        self.setTextCursor(cursor)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self.setFocus()
            self._place_cursor(event.position(), bool(event.modifiers() & Qt.KeyboardModifier.ShiftModifier))
            self._dragging = True
            event.accept()
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._dragging and event.buttons() & Qt.MouseButton.LeftButton:
            self._place_cursor(event.position(), True)
            event.accept()
            return
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._dragging = False
            event.accept()
            return
        super().mouseReleaseEvent(event)

    def mouseDoubleClickEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._place_cursor(event.position(), False)
            cursor = self.textCursor()
            cursor.select(QTextCursor.SelectionType.WordUnderCursor)
            self.setTextCursor(cursor)
            event.accept()
            return
        super().mouseDoubleClickEvent(event)

    # ------------------------------------------------------------------ keyboard: indentation-aware editing (core.editing)

    def keyPressEvent(self, event: QKeyEvent) -> None:
        key = event.key()
        modifiers = event.modifiers() & ~Qt.KeyboardModifier.KeypadModifier
        plain = modifiers in (Qt.KeyboardModifier.NoModifier, Qt.KeyboardModifier.ShiftModifier)
        cursor = self.textCursor()
        if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter) and plain:
            self._apply_edit(lambda before, after: editing.on_return(before, after, self.block_words), remove_selection=True)
        elif key == Qt.Key.Key_Tab and modifiers == Qt.KeyboardModifier.NoModifier:
            self._apply_edit(lambda before, _after: editing.on_tab(before))
        elif key == Qt.Key.Key_Backspace and plain and not cursor.hasSelection() and self._backspace_edit() is not None:
            self._apply_edit(lambda before, _after: self._backspace_edit() or editing.Edit(before, ""))
        else:
            super().keyPressEvent(event)
            return
        self.ensureCursorVisible()
        event.accept()

    def _backspace_edit(self) -> Optional[editing.Edit]:
        cursor = self.textCursor()
        text = cursor.block().text()
        return editing.on_backspace(text[:utf16_to_index(text, cursor.positionInBlock())])

    def _apply_edit(self, make: Callable[[str, str], editing.Edit], remove_selection: bool = False) -> None:
        cursor = self.textCursor()
        cursor.beginEditBlock()
        if cursor.hasSelection():
            if remove_selection:
                cursor.removeSelectedText()
            else:
                cursor.setPosition(cursor.position())
        text = cursor.block().text()
        index = utf16_to_index(text, cursor.positionInBlock())
        before, after = text[:index], text[index:]
        edit = make(before, after)
        cursor.movePosition(QTextCursor.MoveOperation.StartOfBlock, QTextCursor.MoveMode.KeepAnchor)
        cursor.insertText(edit.before + edit.insert)
        for _ in range(edit.cursor_back):
            cursor.movePosition(QTextCursor.MoveOperation.PreviousCharacter)
        cursor.endEditBlock()
        self.setTextCursor(cursor)

    # ------------------------------------------------------------------ gutter

    def gutter_width(self) -> int:
        digits = max(3, len(str(max(1, self.blockCount()))))
        return int(QFontMetricsF(self.font()).horizontalAdvance("0" * digits)) + 16

    def _update_gutter_width(self) -> None:
        width = self.gutter_width()
        if self.right_to_left:
            self.setViewportMargins(0, 0, width, 0)
        else:
            self.setViewportMargins(width, 0, 0, 0)
        self._place_gutter()

    def _place_gutter(self) -> None:
        area = self.contentsRect()
        width = self.gutter_width()
        if self.right_to_left:
            self.gutter.setGeometry(QRect(area.right() - width + 1, area.top(), width, area.height()))
        else:
            self.gutter.setGeometry(QRect(area.left(), area.top(), width, area.height()))
        self.gutter.update()

    def resizeEvent(self, event: QResizeEvent) -> None:
        super().resizeEvent(event)
        self._cache.clear()  # the wrap width changed
        self._place_gutter()

    def _on_update_request(self, rect: QRect, dy: int) -> None:
        if dy:
            self.gutter.scroll(0, dy)
        else:
            self.gutter.update(0, rect.y(), self.gutter.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self._update_gutter_width()

    # ------------------------------------------------------------------ scrolling

    def _scroll_to_line(self, line: int) -> None:
        """Scrolls so that a line is visible, without moving the caret (the scroll bar counts
        wrapped lines, as QPlainTextEdit does)."""
        block = self.document().findBlockByNumber(line - 1)
        if not block.isValid():
            return
        geometry = self.blockBoundingGeometry(block).translated(self.contentOffset())
        viewport_height = self.viewport().height()
        if geometry.top() >= 0 and geometry.bottom() <= viewport_height:
            return
        lines_before = 0
        other = self.document().firstBlock()
        while other.isValid() and other.blockNumber() < block.blockNumber():
            lines_before += max(other.layout().lineCount(), 1)
            other = other.next()
        if geometry.top() < 0:
            self.verticalScrollBar().setValue(lines_before)
        else:
            line_height = max(QFontMetricsF(self.font()).height(), 1.0)
            visible = int(viewport_height / line_height)
            self.verticalScrollBar().setValue(max(0, lines_before + max(block.layout().lineCount(), 1) - visible))

    # ------------------------------------------------------------------ EditorView

    def get_text(self) -> str:
        return self.toPlainText()

    def set_text(self, text: str) -> None:
        self.setPlainText(text)  # also clears the undo history
        self.document().setModified(False)
        self.clear_marks()
        self.moveCursor(QTextCursor.MoveOperation.Start)

    def replace_text(self, text: str) -> None:
        """Replaces everything as a single step that one Undo reverts."""
        line = self.cursor_line()
        cursor = QTextCursor(self.document())
        cursor.beginEditBlock()
        cursor.select(QTextCursor.SelectionType.Document)
        cursor.insertText(text)
        cursor.endEditBlock()
        self.go_to_line(min(line, self.blockCount()))

    def is_modified(self) -> bool:
        return self.document().isModified()

    def set_modified(self, modified: bool) -> None:
        self.document().setModified(modified)

    def set_block_words(self, words: BlockWords) -> None:
        self.block_words = words

    def apply_highlights(self, regions: list[Highlight]) -> None:
        self.regions = sorted(regions, key=lambda region: region.start)
        self._region_ends = [region.end for region in self.regions]
        self._cache.clear()
        self.viewport().update()

    def _line_text(self, line: int) -> str:
        block = self.document().findBlockByNumber(line - 1)
        return block.text() if block.isValid() else ""

    def mark_error(self, span: Span) -> None:
        text = self._line_text(span.line)
        bounds = span_bounds(span, span.line, len(text))
        if bounds is None or bounds[0] >= len(text) or text[bounds[0]:bounds[1]].strip() == "":
            # Nothing visible to underline (e.g. a missing operand at the end of a line): mark the line.
            span = Span(span.line, 1, span.line, len(text) + 1)
        self.error_spans.append(span)
        self.gutter.marked_line = span.line
        self.gutter.update()
        self.viewport().update()

    def show_span(self, span: Optional[Span]) -> None:
        self.selected_span = None
        if span is not None and span.line > 0 and self.folded_headers:
            self.unfold_all()  # the line pointed at must be on screen
        if span is not None and span.line > 0:
            if span.column <= 0:  # no column: highlight the whole line
                span = Span(span.line, 1, span.line, len(self._line_text(span.line)) + 1)
            self.selected_span = span
            self._scroll_to_line(span.line)
        self.viewport().update()

    def show_current_line(self, line: int) -> None:
        self.current_line = line
        if line > 0:
            self._scroll_to_line(line)
        self.viewport().update()

    def clear_marks(self) -> None:
        self.error_spans.clear()
        self.selected_span = None
        self.current_line = 0
        self.gutter.marked_line = None
        self.gutter.update()
        self.viewport().update()

    def cursor_line(self) -> int:
        return self.textCursor().blockNumber() + 1

    def go_to_line(self, line: int) -> None:
        if self.folded_headers:
            self.unfold_all()  # never leave the caret on a line that is folded away
        block = self.document().findBlockByNumber(max(line - 1, 0))
        if block.isValid():
            self.setTextCursor(QTextCursor(block))
            self.ensureCursorVisible()
        self.setFocus()

    def set_font_size(self, size: int) -> None:
        font = self.font()
        font.setPointSize(size)
        self.setFont(font)
        self.setTabStopDistance(QFontMetricsF(font).horizontalAdvance(" ") * len(INDENT))
        self._cache.clear()
        self._update_gutter_width()
        self.viewport().update()

    # -- editing commands (the text work itself is in core.edits) --

    def selection(self) -> tuple[int, int]:
        cursor = self.textCursor()
        text = self.toPlainText()
        return (utf16_to_index(text, cursor.selectionStart()), utf16_to_index(text, cursor.selectionEnd()))

    def set_selection(self, start: int, end: int) -> None:
        text = self.toPlainText()
        cursor = self.textCursor()
        cursor.setPosition(index_to_utf16(text, max(0, min(start, len(text)))))
        cursor.setPosition(index_to_utf16(text, max(0, min(end, len(text)))),
                           QTextCursor.MoveMode.KeepAnchor)
        self.setTextCursor(cursor)
        self.ensureCursorVisible()

    def replace_range(self, start: int, end: int, text: str) -> None:
        whole = self.toPlainText()
        cursor = self.textCursor()
        cursor.beginEditBlock()
        cursor.setPosition(index_to_utf16(whole, start))
        cursor.setPosition(index_to_utf16(whole, end), QTextCursor.MoveMode.KeepAnchor)
        cursor.insertText(text)
        cursor.endEditBlock()
        self.set_selection(start, start + len(text))

    def set_show_whitespace(self, show: bool) -> None:
        if show == self.show_whitespace:
            return
        self.show_whitespace = show
        option = self.document().defaultTextOption()
        option.setFlags(QTextOption.Flag.ShowTabsAndSpaces if show else QTextOption.Flag(0))
        self.document().setDefaultTextOption(option)
        self._cache.clear()
        self.viewport().update()

    def fold(self, folds: list[tuple[int, int]]) -> int:
        """Hides the body of each block. Folding is a way of *looking* at the program: any edit
        opens everything again (_on_text_changed), so no text can be changed while out of sight."""
        document = self.document()
        hidden = 0
        for header, last in folds:
            for number in range(header + 1, last + 1):
                block = document.findBlockByNumber(number)
                if block.isValid() and block.isVisible():
                    block.setVisible(False)
            if last > header:
                hidden += 1
                self.folded_headers.add(header)
        if hidden:
            self._after_folding()
        return hidden

    def unfold_all(self) -> None:
        if not self.folded_headers:
            return
        document = self.document()
        for number in range(document.blockCount()):
            block = document.findBlockByNumber(number)
            if block.isValid() and not block.isVisible():
                block.setVisible(True)
        self.folded_headers.clear()
        self._after_folding()

    def _after_folding(self) -> None:
        document = self.document()
        document.markContentsDirty(0, max(document.characterCount() - 1, 1))
        # A hidden line must never hold the caret: move it to the line that folded it away.
        if not self.textCursor().block().isVisible():
            block = self.textCursor().block()
            while block.isValid() and not block.isVisible():
                block = block.previous()
            if block.isValid():
                self.setTextCursor(QTextCursor(block))
        self._cache.clear()
        self.gutter.update()
        self.viewport().update()

    def set_right_to_left(self, rtl: bool) -> None:
        if rtl == self.right_to_left:
            return
        self.right_to_left = rtl
        option = self.document().defaultTextOption()
        option.setTextDirection(Qt.LayoutDirection.RightToLeft if rtl else Qt.LayoutDirection.LeftToRight)
        self.document().setDefaultTextOption(option)  # Qt then also swaps the Left/Right arrow keys
        self.setLayoutDirection(Qt.LayoutDirection.RightToLeft if rtl else Qt.LayoutDirection.LeftToRight)
        self._cache.clear()
        self._update_gutter_width()
        self.viewport().update()


def span_bounds(span: Span, line: int, length: int) -> Optional[tuple[int, int]]:
    """The part [start, end) of ``line`` (1-based, ``length`` characters long) that a span covers."""
    start = (span.line, max(span.column - 1, 0))
    if span.end_line <= 0 or (span.end_line, span.end_column) <= (span.line, span.column):
        end = (span.line, start[1] + 1)
    else:
        end = (span.end_line, max(span.end_column - 1, 0))
    if line < start[0] or line > end[0]:
        return None
    first = start[1] if line == start[0] else 0
    last = end[1] if line == end[0] else length
    return min(first, length), min(last, length)
