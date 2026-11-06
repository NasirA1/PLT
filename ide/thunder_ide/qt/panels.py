"""Qt implementations of the generic panels (see core.ui).

Qt lays out, shapes and mirrors bidirectional text itself, so these widgets simply hand it
the text: paragraphs of program output take the direction of their first letter, tables and
labels follow the interface language.
"""

from __future__ import annotations

from typing import Callable, Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QFont, QTextCharFormat, QTextCursor
from PySide6.QtWidgets import (QGroupBox, QHBoxLayout, QLabel, QSplitter, QTextEdit, QToolButton, QTreeWidget,
                               QTreeWidgetItem, QVBoxLayout, QWidget)

from thunder_ide.core.i18n import tr
from thunder_ide.core.viewmodels import Column, StyledText, TreeRow, TreeSpec, VmViewState
from thunder_ide.qt import style


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
        item = QTreeWidgetItem([str(value) for value in (row.text, *row.values)])
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
        cursor.insertText(text, fmt)
        self.verticalScrollBar().setValue(self.verticalScrollBar().maximum())

    def set_lines(self, lines: list[StyledText]) -> None:
        self.clear()
        for line in lines:
            self.append(line.text + "\n", line.style)
        self.verticalScrollBar().setValue(0)

    def get_text(self) -> str:
        return self.toPlainText()


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

