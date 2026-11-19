"""The widgets of Settings > IntelliSense in the Qt editor: the completion list and the
parameter hint.

Both are child widgets of the editor's viewport rather than separate windows, so they never take
the keyboard focus from the text (the editor forwards the keys that steer the list), they need
no window manager - which keeps them working under Wayland and in the offscreen tests - and
they move and disappear with the editor itself.
"""

from __future__ import annotations

from html import escape
from typing import Callable, Optional

from PySide6.QtCore import QPoint, QRect, QSize, Qt
from PySide6.QtGui import QColor, QFont, QFontMetrics
from PySide6.QtWidgets import QFrame, QLabel, QListWidget, QListWidgetItem, QVBoxLayout, QWidget

from thunder_ide.core import i18n
from thunder_ide.core.completion import CompletionItem, Completions
from thunder_ide.qt import style

# The kind of each item, shown as a small mark in front of it (as VS Code shows an icon).
KIND_MARKS = {
    "function": "ƒ", "builtin": "ƒ", "method": "ƒ", "variable": "▫", "constant": "▪", "parameter": "▫",
    "field": "◇", "property": "◇", "member": "◆", "struct": "▣", "interface": "◈", "enum": "▤",
    "module": "▦", "file": "▦", "keyword": "≡", "type": "T", "literal": "≡",
}
KIND_COLORS = {
    "function": style.SYNTAX_COLORS["builtin"], "builtin": style.SYNTAX_COLORS["builtin"],
    "method": style.SYNTAX_COLORS["builtin"], "keyword": style.SYNTAX_COLORS["keyword"],
    "type": style.SYNTAX_COLORS["type"], "literal": style.SYNTAX_COLORS["literal"],
    "struct": style.SYNTAX_COLORS["type"], "interface": style.SYNTAX_COLORS["type"],
    "enum": style.SYNTAX_COLORS["type"], "member": style.SYNTAX_COLORS["number"],
}
VISIBLE_ROWS = 9


class CompletionPopup(QFrame):
    """The list of completions under the caret, with the chosen item's signature and
    documentation in a panel beside it - on the side away from the caret's text (right of the
    list, or left of it in a right-to-left program), as VS Code shows it, so it is never pushed
    out of view at the top or the bottom of the editor."""

    DETAILS_WIDTH = 360
    MIN_SIDE_WIDTH = 200

    def __init__(self, parent: QWidget, on_accept: Callable[[CompletionItem], None]):
        super().__init__(parent)
        self.on_accept = on_accept
        self.completions: Optional[Completions] = None
        self.setObjectName("completionPopup")
        self.setFrameShape(QFrame.Shape.Box)
        self.setStyleSheet("#completionPopup { background: #f8f8f8; border: 1px solid #a0a0a0; }")
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(1, 1, 1, 1)
        layout.setSpacing(0)
        self.list = QListWidget(self)
        self.list.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.list.setFrameShape(QFrame.Shape.NoFrame)
        self.list.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.list.setUniformItemSizes(True)
        self.list.itemClicked.connect(lambda _item: self.accept())
        self.list.currentRowChanged.connect(lambda _row: self._show_details())
        layout.addWidget(self.list)
        # A sibling in the viewport, not a child: it stands beside the list (see _place_details).
        self.details = QLabel(parent)
        self.details.setObjectName("completionDetails")
        self.details.setWordWrap(True)
        self.details.setTextFormat(Qt.TextFormat.RichText)
        self.details.setAlignment(Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignLeading)
        self.details.setStyleSheet("#completionDetails { background: #ffffff; border: 1px solid #a0a0a0;"
                                   " padding: 6px; }")
        self.details.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.details.hide()
        self._area = QSize(0, 0)
        self._right_to_left = False
        self.hide()

    def hideEvent(self, event) -> None:  # noqa: ANN001 - QHideEvent
        self.details.hide()  # the panel goes with its list
        super().hideEvent(event)

    # -- content --

    def show_completions(self, completions: Completions, font: QFont, right_to_left: bool) -> None:
        self.completions = completions
        self.list.setFont(font)
        self.details.setFont(font)
        direction = Qt.LayoutDirection.RightToLeft if right_to_left else Qt.LayoutDirection.LeftToRight
        self.setLayoutDirection(direction)
        self.list.clear()
        for item in completions.items:
            row = QListWidgetItem(f"{KIND_MARKS.get(item.kind, '·')}  {item.label}")
            color = KIND_COLORS.get(item.kind)
            if color:
                row.setForeground(QColor(color))
            row.setToolTip(item.detail)
            self.list.addItem(row)
        self.list.setCurrentRow(0)
        self._show_details()

    def _show_details(self) -> None:
        item = self.current_item()
        if item is None:
            self.details.setText("")
            return
        parts = []
        if item.detail:
            parts.append(f"<p style='white-space:pre' dir='auto'><b>{escape(item.detail)}</b></p>")
        for paragraph in item.documentation.split("\n\n"):
            flowed = " ".join(line.strip() for line in paragraph.split("\n")).strip()
            if flowed:
                parts.append(f"<p dir='auto'>{escape(flowed)}</p>")
        if item.source:
            parts.append(f"<p dir='auto'><i>{escape(i18n.tr('hover.from', file=item.source))}</i></p>")
        self.details.setText("".join(parts))
        self.details.setProperty("has_text", bool(parts))
        if self.isVisible():
            self._place_details()

    def current_item(self) -> Optional[CompletionItem]:
        if self.completions is None:
            return None
        row = self.list.currentRow()
        if 0 <= row < len(self.completions.items):
            return self.completions.items[row]
        return None

    def labels(self) -> list[str]:
        """The labels in the list, in order (used by the tests)."""
        return [item.label for item in self.completions.items] if self.completions is not None else []

    # -- steering (the editor forwards these keys while the list is open) --

    def step(self, rows: int) -> None:
        count = self.list.count()
        if not count:
            return
        row = self.list.currentRow() + rows
        if rows in (-1, 1):
            row %= count  # the arrow keys wrap round, as in VS Code
        self.list.setCurrentRow(max(0, min(row, count - 1)))

    def accept(self) -> None:
        item = self.current_item()
        self.hide()
        if item is not None:
            self.on_accept(item)

    # -- placement --

    def place(self, caret: QRect, area: QSize, right_to_left: bool) -> None:
        """Under the caret (or above it when there is no room below), inside the viewport."""
        metrics = QFontMetrics(self.list.font())
        widest = max((metrics.horizontalAdvance(self.list.item(row).text())
                      for row in range(self.list.count())), default=100)
        width = min(max(widest + 40, 260), max(area.width() - 8, 120))
        rows = min(self.list.count(), VISIBLE_ROWS)
        list_height = rows * max(self.list.sizeHintForRow(0), metrics.height() + 4) + 6
        self.list.setFixedHeight(list_height)
        height = list_height + 4
        x = caret.right() - width if right_to_left else caret.left()
        x = max(2, min(x, area.width() - width - 2))
        y = caret.bottom() + 2
        if y + height > area.height() and caret.top() - height - 2 >= 0:
            y = caret.top() - height - 2
        self.setGeometry(x, y, width, height)
        self.raise_()
        self.show()
        self._area = area
        self._right_to_left = right_to_left
        self._place_details()

    def _place_details(self) -> None:
        """Beside the list, its top level with the list's: right of it (left in a right-to-left
        program); on the other side when that one has no room; under or over the list only when
        neither side has room (a narrow editor)."""
        details, area, popup = self.details, self._area, self.geometry()
        if not details.property("has_text"):
            details.hide()
            return
        # The room on each side of the list; the panel narrows to fit (down to MIN_SIDE_WIDTH).
        room_after = area.width() - 2 - (popup.right() + 3)
        room_before = popup.left() - 3 - 2
        preferred, other = (room_before, room_after) if self._right_to_left else (room_after, room_before)
        side = None
        if preferred >= self.MIN_SIDE_WIDTH:
            side = "before" if self._right_to_left else "after"
        elif other >= self.MIN_SIDE_WIDTH:
            side = "after" if self._right_to_left else "before"
        if side is not None:
            width = min(self.DETAILS_WIDTH, room_after if side == "after" else room_before)
        else:
            width = min(self.DETAILS_WIDTH, max(area.width() - 8, 120))
        details.setFixedWidth(width)
        height = min(details.heightForWidth(width), max(area.height() - 4, 40))
        if side == "after":
            x, y = popup.right() + 3, popup.top()
        elif side == "before":
            x, y = popup.left() - 3 - width, popup.top()
        else:
            # Neither side has room (a narrow editor): under the list, or over it.
            x = max(2, min(popup.left(), area.width() - width - 2))
            y = popup.bottom() + 3 if popup.bottom() + 3 + height <= area.height() else popup.top() - 3 - height
        y = max(2, min(y, area.height() - height - 2))
        details.setGeometry(x, y, width, height)
        details.raise_()
        details.show()


class SignatureHint(QLabel):
    """The signature of the call the caret is in, above the caret, the current parameter in bold."""

    def __init__(self, parent: QWidget):
        super().__init__(parent)
        self.setObjectName("signatureHint")
        self.setTextFormat(Qt.TextFormat.RichText)
        self.setWordWrap(True)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setStyleSheet("#signatureHint { background: #ffffe6; border: 1px solid #b0b0a0; padding: 3px; }")
        self.hide()

    def show_hint(self, html: str, caret: QRect, area: QSize, font: QFont, right_to_left: bool) -> None:
        self.setFont(font)
        self.setText(html)
        self.setLayoutDirection(Qt.LayoutDirection.RightToLeft if right_to_left else Qt.LayoutDirection.LeftToRight)
        width = min(max(self.sizeHint().width(), 200), max(area.width() - 8, 120))
        height = self.heightForWidth(width) if self.hasHeightForWidth() else self.sizeHint().height()
        height = max(height, self.sizeHint().height() if width >= self.sizeHint().width() else height)
        x = caret.right() - width if right_to_left else caret.left()
        x = max(2, min(x, area.width() - width - 2))
        y = caret.top() - height - 2
        if y < 0:
            y = caret.bottom() + 2
        self.setGeometry(x, y, width, height)
        self.raise_()
        self.show()


def caret_rect(point: QPoint, line_height: int) -> QRect:
    return QRect(point.x(), point.y(), 1, line_height)
