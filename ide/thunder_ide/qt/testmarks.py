"""A test file's marks in the code editor: the buttons in the gutter and the failure panel.

As VS Code's text editor shows a test file: beside each test and suite a button whose icon is
its state - a play triangle pointing into the code, a turning arc while it runs, a tick when it
passed, a cross when it failed - and under a failed test a panel with everything it reported,
an arrow on its top edge pointing at the test's button (core.tests holds the model).
"""

from __future__ import annotations

from typing import Callable, Optional

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QPainter, QPainterPath, QPaintEvent, QPen, QPolygonF
from PySide6.QtWidgets import QFrame, QLabel, QToolButton, QVBoxLayout, QHBoxLayout, QWidget

from thunder_ide.core.tests import TestPeek

PLAY = "#388a34"
PASSED = "#388a34"
FAILED = "#e51400"
RUNNING = "#1a85ff"
PEEK_BORDER = "#e51400"
PEEK_BACKGROUND = "#fdf2f2"
PEEK_HEADER = "#fbe0e0"
MESSAGE_FOREGROUND = "#c72e0f"


def paint_mark(painter: QPainter, rect: QRectF, state: str, right_to_left: bool, angle: int = 0) -> None:
    """One button's icon in ``rect`` (a square): its state's symbol."""
    painter.save()
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    size = min(rect.width(), rect.height()) * 0.7
    box = QRectF(rect.center().x() - size / 2, rect.center().y() - size / 2, size, size)
    if state == "passed":
        pen = QPen(QColor(PASSED), max(1.6, size / 7), Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap,
                   Qt.PenJoinStyle.RoundJoin)
        painter.setPen(pen)
        path = QPainterPath(QPointF(box.left() + size * 0.1, box.top() + size * 0.55))
        path.lineTo(box.left() + size * 0.4, box.top() + size * 0.85)
        path.lineTo(box.left() + size * 0.95, box.top() + size * 0.2)
        painter.drawPath(path)
    elif state == "failed":
        pen = QPen(QColor(FAILED), max(1.6, size / 7), Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap)
        painter.setPen(pen)
        inset = size * 0.15
        painter.drawLine(QPointF(box.left() + inset, box.top() + inset), QPointF(box.right() - inset, box.bottom() - inset))
        painter.drawLine(QPointF(box.right() - inset, box.top() + inset), QPointF(box.left() + inset, box.bottom() - inset))
    elif state == "running":
        pen = QPen(QColor(RUNNING), max(1.6, size / 7), Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap)
        painter.setPen(pen)
        painter.drawArc(box.adjusted(1, 1, -1, -1), -angle * 16, 270 * 16)
    else:
        # A play triangle, pointing into the code: right in a left-to-right program, left in a
        # right-to-left one.
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(PLAY))
        if right_to_left:
            points = [QPointF(box.right() - size * 0.1, box.top()), QPointF(box.left() + size * 0.1, box.center().y()),
                      QPointF(box.right() - size * 0.1, box.bottom())]
        else:
            points = [QPointF(box.left() + size * 0.1, box.top()), QPointF(box.right() - size * 0.1, box.center().y()),
                      QPointF(box.left() + size * 0.1, box.bottom())]
        painter.drawPolygon(QPolygonF(points))
    painter.restore()


class TestPeekWidget(QFrame):
    """The panel under a failed test: the test's name, a close button, every message - and an
    arrow on the top edge pointing at the test's button."""

    ARROW = 8

    def __init__(self, parent: QWidget, on_close: Callable[[], None], close_tip: str):
        super().__init__(parent)
        self.setObjectName("test_peek")
        self.setAutoFillBackground(False)
        self.arrow_x = 12.0   # where the arrow points, in the widget's coordinates
        self.peek: Optional[TestPeek] = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, self.ARROW + 2, 0, 2)
        layout.setSpacing(0)
        header = QWidget()
        header.setObjectName("test_peek_header")
        header.setStyleSheet(f"#test_peek_header {{ background: {PEEK_HEADER}; }}")
        row = QHBoxLayout(header)
        row.setContentsMargins(8, 2, 4, 2)
        self.title = QLabel("")
        self.title.setStyleSheet(f"color: {FAILED}; font-weight: bold;")
        self.close_button = QToolButton()
        self.close_button.setText("×")
        self.close_button.setAutoRaise(True)
        self.close_button.setToolTip(close_tip)
        self.close_button.clicked.connect(on_close)
        row.addWidget(self.title, 1)
        row.addWidget(self.close_button)
        layout.addWidget(header)
        self.messages = QLabel("")
        self.messages.setWordWrap(True)
        self.messages.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.messages.setContentsMargins(12, 4, 12, 6)
        self.messages.setStyleSheet("color: #1f1f1f;")
        layout.addWidget(self.messages)
        self.hide()

    def show_peek(self, peek: Optional[TestPeek], right_to_left: bool) -> None:
        self.peek = peek
        if peek is None:
            self.hide()
            return
        direction = Qt.LayoutDirection.RightToLeft if right_to_left else Qt.LayoutDirection.LeftToRight
        self.setLayoutDirection(direction)
        self.title.setText(peek.title)
        self.messages.setText("\n".join(peek.messages))
        alignment = Qt.AlignmentFlag.AlignRight if right_to_left else Qt.AlignmentFlag.AlignLeft
        self.title.setAlignment(alignment | Qt.AlignmentFlag.AlignVCenter)
        self.messages.setAlignment(alignment | Qt.AlignmentFlag.AlignTop)
        self.show()
        self.raise_()

    def paintEvent(self, event: QPaintEvent) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        body = QRectF(0, self.ARROW, self.width(), self.height() - self.ARROW)
        painter.fillRect(body, QColor(PEEK_BACKGROUND))
        pen = QPen(QColor(PEEK_BORDER), 2)
        painter.setPen(pen)
        painter.drawLine(QPointF(body.left(), body.top() + 1), QPointF(body.right(), body.top() + 1))
        painter.drawLine(QPointF(body.left(), body.bottom() - 1), QPointF(body.right(), body.bottom() - 1))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(PEEK_BORDER))
        x = self.arrow_x
        painter.drawPolygon(QPolygonF([QPointF(x - self.ARROW, self.ARROW + 1), QPointF(x, 0),
                                       QPointF(x + self.ARROW, self.ARROW + 1)]))
        painter.end()
        super().paintEvent(event)
