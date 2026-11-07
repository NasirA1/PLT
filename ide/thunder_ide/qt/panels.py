"""Qt implementations of the generic panels (see core.ui).

Qt lays out, shapes and mirrors bidirectional text itself, so these widgets simply hand it
the text: paragraphs of program output take the direction of their first letter, tables and
labels follow the interface language.
"""

from __future__ import annotations

import math
import re
from typing import Callable, Optional

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPen, QTextCharFormat, QTextCursor
from PySide6.QtWidgets import (QGroupBox, QHBoxLayout, QLabel, QSplitter, QTextEdit, QToolButton, QTreeWidget,
                               QTreeWidgetItem, QVBoxLayout, QWidget)

from thunder_ide.core.canvas import CanvasSnapshot, key_name
from thunder_ide.core.i18n import tr
from thunder_ide.core.viewmodels import Column, StyledText, TreeRow, TreeSpec, VmViewState
from thunder_ide.qt import style

# Negative numbers keep their sign to the LEFT of the digits in every script (an exception to the
# right-to-left reading). In right-to-left text the bidirectional algorithm places a lone "-" to
# the right of Pashto digits, so for display "-۳" is wrapped in an isolate that carries its own
# left-to-right context (FSI, LRM ... LRM, PDI - the marks Qt honours here, found by measurement;
# see core.direction). Only the *displayed* text carries the marks: get_text removes them.
_NEGATIVE_NUMBER = re.compile("-([٠-٩۰-۹]+)")
_MARKS = str.maketrans({"⁨": None, "⁩": None, "‎": None})


def directional(text: str) -> str:
    """The text with negative Pashto/Arabic numbers wrapped so the sign stays on the left."""
    return _NEGATIVE_NUMBER.sub("⁨‎-\\1‎⁩", text)


class QtTreePanel(QWidget):
    """A table or tree (TreePanelView)."""

    def __init__(self, spec: TreeSpec, on_select: Callable[[TreeRow], None], code_font: Optional[QFont] = None):
        super().__init__()
        self.on_select = on_select
        self.rows: list[TreeRow] = []
        self.flat = spec.tree_heading is None  # a table: TreeRow.text is the first column
        self.tree = QTreeWidget()
        headings = ([] if self.flat else [spec.tree_heading]) + [column.heading for column in spec.columns]
        self.tree.setHeaderLabels([str(heading) for heading in headings])
        self.tree.setRootIsDecorated(not self.flat)
        self.tree.setUniformRowHeights(True)
        self.tree.setAlternatingRowColors(self.flat)
        if code_font is not None:
            self.tree.setFont(code_font)
        widths = ([] if self.flat else [spec.tree_width]) + [column.width for column in spec.columns]
        for index, width in enumerate(widths):
            self.tree.setColumnWidth(index, width)
        self.footer = QLabel("")
        self.footer.setWordWrap(True)
        self.footer.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)
        layout.addWidget(self.tree, 1)
        layout.addWidget(self.footer)
        self.footer.hide()
        self.tree.itemSelectionChanged.connect(self._on_select)

    def set_rows(self, rows: list[TreeRow]) -> None:
        self.tree.clear()
        self.rows = []
        for row in rows:
            self._insert(None, row)

    def _insert(self, parent: Optional[QTreeWidgetItem], row: TreeRow) -> None:
        item = QTreeWidgetItem([directional(str(value)) for value in (row.text, *row.values)])
        if row.style and row.style in style.CATEGORY_COLORS:
            for column in range(item.columnCount()):
                item.setForeground(column, QColor(style.CATEGORY_COLORS[row.style]))
        item.setData(0, Qt.ItemDataRole.UserRole, len(self.rows))
        self.rows.append(row)
        if parent is None:
            self.tree.addTopLevelItem(item)
        else:
            parent.addChild(item)
        for child in row.children:
            self._insert(item, child)
        item.setExpanded(row.expanded)

    def set_footer(self, text: str) -> None:
        self.footer.setText(text)
        self.footer.setVisible(bool(text))

    def _on_select(self) -> None:
        items = self.tree.selectedItems()
        if items:
            self.on_select(self.rows[items[0].data(0, Qt.ItemDataRole.UserRole)])

    def items(self) -> list[QTreeWidgetItem]:
        """All items, depth first (for tests)."""
        result: list[QTreeWidgetItem] = []

        def walk(item: QTreeWidgetItem) -> None:
            result.append(item)
            for index in range(item.childCount()):
                walk(item.child(index))

        for index in range(self.tree.topLevelItemCount()):
            walk(self.tree.topLevelItem(index))
        return result


class QtTextPanel(QTextEdit):
    """Read-only styled text (TextPanelView). Each paragraph takes the direction of its text."""

    def __init__(self, font: QFont, styles: Optional[dict[str, dict]] = None, background: Optional[str] = None,
                 foreground: Optional[str] = None, wrap: bool = True):
        super().__init__()
        self.setReadOnly(True)
        self.setFont(font)
        self.setLineWrapMode(QTextEdit.LineWrapMode.WidgetWidth if wrap else QTextEdit.LineWrapMode.NoWrap)
        option = self.document().defaultTextOption()
        option.setTextDirection(Qt.LayoutDirection.LayoutDirectionAuto)
        self.document().setDefaultTextOption(option)
        self.foreground = foreground
        if background or foreground:
            self.setStyleSheet(f"QTextEdit {{ background: {background or 'white'}; color: {foreground or 'black'}; }}")
        self.styles: dict[str, QTextCharFormat] = {}
        for name, config in (styles or {}).items():
            fmt = QTextCharFormat()
            if "foreground" in config:
                fmt.setForeground(QColor(config["foreground"]))
            if "background" in config:
                fmt.setBackground(QColor(config["background"]))
            if config.get("bold"):
                fmt.setFontWeight(QFont.Weight.Bold)
            if config.get("size"):
                fmt.setFontPointSize(config["size"])
            self.styles[name] = fmt

    def clear(self) -> None:  # type: ignore[override]
        super().clear()

    def append(self, text: str, style_name: Optional[str] = None) -> None:  # type: ignore[override]
        cursor = QTextCursor(self.document())
        cursor.movePosition(QTextCursor.MoveOperation.End)
        fmt = self.styles.get(style_name or "", QTextCharFormat())
        if self.foreground and not fmt.hasProperty(QTextCharFormat.Property.ForegroundBrush):
            fmt.setForeground(QColor(self.foreground))
        cursor.insertText(directional(text), fmt)
        self.verticalScrollBar().setValue(self.verticalScrollBar().maximum())

    def set_lines(self, lines: list[StyledText]) -> None:
        self.clear()
        for line in lines:
            self.append(line.text + "\n", line.style)
        self.verticalScrollBar().setValue(0)

    def get_text(self) -> str:
        return self.toPlainText().translate(_MARKS)  # without the marks directional() added


class QtVmPanel(QWidget):
    """The step-by-step debugger (VmPanelView)."""

    ACTIONS = (("first", "vm.first"), ("back_line", "vm.back_line"), ("back", "vm.back"), ("forward", "vm.forward"),
               ("forward_line", "vm.forward_line"), ("last", "vm.last"))

    def __init__(self, font: QFont, on_navigate: Callable[[str], None]):
        super().__init__()
        outer = QVBoxLayout(self)
        outer.setContentsMargins(4, 4, 4, 4)
        controls = QHBoxLayout()
        self.buttons: dict[str, QToolButton] = {}  # tool buttons: narrow, so the panel can be made small
        for action, key in self.ACTIONS:
            button = QToolButton()
            button.setText(tr(key))
            button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
            button.clicked.connect(lambda _checked=False, a=action: on_navigate(a))
            controls.addWidget(button)
            self.buttons[action] = button
        controls.addStretch(1)
        outer.addLayout(controls)
        self.position = QLabel("")
        self.position.setWordWrap(True)
        outer.addWidget(self.position)
        self.instruction = QLabel("")
        self.instruction.setFont(font)
        self.instruction.setWordWrap(True)
        self.instruction.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        outer.addWidget(self.instruction)

        two = lambda first, second: TreeSpec(None, (Column("a", first, 120), Column("b", second, 200)))  # noqa: E731
        left = QSplitter(Qt.Orientation.Vertical)
        right = QSplitter(Qt.Orientation.Vertical)
        self.stack = self._titled(left, tr("vm.stack"), TreeSpec(None, (Column("i", "#", 40),
                                                                        Column("v", tr("column.value"), 120),
                                                                        Column("o", tr("vm.owner"), 170))))
        self.frames = self._titled(left, tr("vm.frames"), two(tr("column.name"), tr("vm.base")))
        self.locals = self._titled(right, tr("vm.locals"), two(tr("column.name"), tr("column.value")))
        self.globals = self._titled(right, tr("vm.globals"), two(tr("column.name"), tr("column.value")))
        output_box = QGroupBox(tr("vm.output"))
        self.output = QtTextPanel(font)
        box_layout = QVBoxLayout(output_box)
        box_layout.setContentsMargins(4, 4, 4, 4)
        box_layout.addWidget(self.output)
        right.addWidget(output_box)
        panes = QSplitter(Qt.Orientation.Horizontal)
        panes.addWidget(left)
        panes.addWidget(right)
        outer.addWidget(panes, 1)
        self.show(None)

    def _titled(self, parent: QSplitter, title: str, spec: TreeSpec) -> QtTreePanel:
        box = QGroupBox(title)
        view = QtTreePanel(spec, lambda _row: None)
        layout = QVBoxLayout(box)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.addWidget(view)
        parent.addWidget(box)
        return view

    def show(self, state: Optional[VmViewState] = None) -> None:  # type: ignore[override]
        if state is None:
            for view in (self.stack, self.frames, self.locals, self.globals):
                view.set_rows([])
            self.output.clear()
            self.position.setText(tr("vm.no_trace"))
            self.instruction.setText("")
            for button in self.buttons.values():
                button.setEnabled(False)
            return
        self.position.setText(state.position)
        self.instruction.setText(state.instruction)
        for action in ("first", "back", "back_line"):
            self.buttons[action].setEnabled(state.can_go_back)
        for action in ("forward", "forward_line", "last"):
            self.buttons[action].setEnabled(state.can_go_forward)
        self.stack.set_rows(state.stack)
        self.frames.set_rows(state.frames)
        self.locals.set_rows(state.locals)
        self.globals.set_rows(state.globals)
        self.output.clear()
        self.output.append(state.output)



class QtCanvas(QWidget):
    """Paints a graphics program's frames, scaled to fit, and reports keys and the mouse."""

    SPECIAL = None  # filled in on first use (Qt key -> program key name)

    def __init__(self, on_key: Callable[[str, bool], None], on_mouse: Callable[[int, int, bool], None]):
        super().__init__()
        self.on_key = on_key
        self.on_mouse = on_mouse
        self.snapshot: Optional[CanvasSnapshot] = None
        self.hint = ""
        self._pressed = False
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setMouseTracking(True)
        self.setMinimumSize(80, 60)
        self.setLayoutDirection(Qt.LayoutDirection.LeftToRight)  # x always grows to the right

    @classmethod
    def special_keys(cls) -> dict:
        if cls.SPECIAL is None:
            key = Qt.Key
            cls.SPECIAL = {key.Key_Left: "left", key.Key_Right: "right", key.Key_Up: "up", key.Key_Down: "down",
                           key.Key_Space: "space", key.Key_Return: "enter", key.Key_Enter: "enter",
                           key.Key_Escape: "escape", key.Key_Backspace: "backspace", key.Key_Tab: "tab"}
        return cls.SPECIAL

    # ------------------------------------------------------------------ geometry

    def transform(self) -> tuple[float, float, float]:
        """(scale, left, top): where the canvas is drawn inside the widget."""
        width = self.snapshot.width if self.snapshot else 400
        height = self.snapshot.height if self.snapshot else 300
        scale = max(0.05, min(self.width() / width, self.height() / height))
        return scale, (self.width() - width * scale) / 2, (self.height() - height * scale) / 2

    def to_canvas(self, x: float, y: float) -> tuple[int, int]:
        scale, left, top = self.transform()
        return math.floor((x - left) / scale), math.floor((y - top) / scale)

    # ------------------------------------------------------------------ painting

    def paintEvent(self, event) -> None:  # noqa: N802 - Qt naming
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#3c3f41"))
        scale, left, top = self.transform()
        snapshot = self.snapshot or CanvasSnapshot()
        painter.translate(left, top)
        painter.scale(scale, scale)
        painter.setClipRect(QRectF(0, 0, snapshot.width, snapshot.height))
        painter.fillRect(QRectF(0, 0, snapshot.width, snapshot.height), QColor("white"))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        for shape in snapshot.shapes:
            try:
                self._paint_shape(painter, shape, snapshot)
            except (IndexError, TypeError, ValueError):  # a malformed shape is skipped, never fatal
                continue
        if self.snapshot is None and self.hint:
            painter.setPen(QColor("#606060"))
            painter.drawText(QRectF(10, 10, snapshot.width - 20, snapshot.height - 20),
                             int(Qt.AlignmentFlag.AlignCenter) | int(Qt.TextFlag.TextWordWrap), self.hint)
        painter.end()

    @staticmethod
    def _paint_shape(painter: QPainter, shape: list, snapshot: CanvasSnapshot) -> None:
        kind = shape[0]
        no_pen = QPen(Qt.PenStyle.NoPen)

        def pen(color: str, width: int) -> QPen:
            outline = QPen(QColor(color))
            outline.setWidth(max(1, int(width)))
            outline.setCapStyle(Qt.PenCapStyle.RoundCap)
            return outline

        if kind == "clear":
            painter.fillRect(QRectF(0, 0, snapshot.width, snapshot.height), QColor(shape[1]))
        elif kind == "pixel":
            painter.fillRect(QRectF(shape[1], shape[2], 1, 1), QColor(shape[3]))
        elif kind == "line":
            painter.setPen(pen(shape[5], shape[6]))
            painter.drawLine(QPointF(shape[1], shape[2]), QPointF(shape[3], shape[4]))
        elif kind == "rect":
            painter.fillRect(QRectF(shape[1], shape[2], shape[3], shape[4]), QColor(shape[5]))
        elif kind == "rect_outline":
            painter.setPen(pen(shape[5], shape[6]))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawRect(QRectF(shape[1], shape[2], shape[3], shape[4]))
        elif kind == "circle":
            painter.setPen(no_pen)
            painter.setBrush(QColor(shape[4]))
            painter.drawEllipse(QPointF(shape[1], shape[2]), shape[3], shape[3])
        elif kind == "circle_outline":
            painter.setPen(pen(shape[4], shape[5]))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawEllipse(QPointF(shape[1], shape[2]), shape[3], shape[3])
        elif kind == "text":
            font = QFont(painter.font())
            font.setPixelSize(max(1, int(shape[5])))
            painter.setFont(font)
            painter.setPen(QColor(shape[4]))
            # Text may be in any script; Qt orders and shapes it (Pashto is joined and right-to-left).
            # (x, y) is the top left corner for every script: AlignAbsolute stops Qt from turning
            # "left" into "right" for right-to-left text, which would move it off the canvas.
            painter.drawText(QRectF(shape[1], shape[2], 10_000, int(shape[5]) * 2),
                             int(Qt.AlignmentFlag.AlignLeft) | int(Qt.AlignmentFlag.AlignAbsolute)
                             | int(Qt.AlignmentFlag.AlignTop), str(shape[3]))

    # ------------------------------------------------------------------ input

    def keyPressEvent(self, event) -> None:  # noqa: N802
        name = key_name(event.text(), self.special_keys().get(event.key()))
        if name is None:
            super().keyPressEvent(event)
            return
        self.on_key(name, True)  # auto-repeated presses count as presses, too

    def keyReleaseEvent(self, event) -> None:  # noqa: N802
        name = key_name(event.text(), self.special_keys().get(event.key()))
        if name is None or event.isAutoRepeat():
            super().keyReleaseEvent(event)
            return
        self.on_key(name, False)

    def focusNextPrevChild(self, _next: bool) -> bool:  # noqa: N802 - Tab is a key for the program
        return False

    def _report_mouse(self, event, pressed: Optional[bool] = None) -> None:
        if pressed is not None:
            self._pressed = pressed
        x, y = self.to_canvas(event.position().x(), event.position().y())
        self.on_mouse(x, y, self._pressed)

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        self._report_mouse(event)

    def mousePressEvent(self, event) -> None:  # noqa: N802
        self.setFocus()
        self._report_mouse(event, True)

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        self._report_mouse(event, False)


class QtCanvasPanel(QWidget):
    """The Canvas panel (CanvasView): a status line above the picture."""

    def __init__(self, on_key: Callable[[str, bool], None], on_mouse: Callable[[int, int, bool], None]):
        super().__init__()
        self.status = QLabel("")
        self.status.setWordWrap(True)
        self.canvas = QtCanvas(on_key, on_mouse)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)
        layout.addWidget(self.status)
        layout.addWidget(self.canvas, 1)

    def show(self, snapshot: Optional[CanvasSnapshot]) -> None:  # type: ignore[override]
        self.canvas.snapshot = snapshot
        self.canvas.update()

    def set_status(self, text: str) -> None:
        self.status.setText(text)
        if self.canvas.snapshot is None:
            self.canvas.hint = text
            self.canvas.update()

    def focus(self) -> None:
        self.canvas.setFocus()
