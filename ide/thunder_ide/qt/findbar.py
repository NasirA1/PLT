"""The find and replace bar under the editor.

The bar only collects what the user typed and hands it to the controller as a
``FindRequest``; all the text work is in ``core.edits``, so it is the same in any toolkit.
"""

from __future__ import annotations

from typing import Callable

from PySide6.QtCore import Qt
from PySide6.QtGui import QKeyEvent
from PySide6.QtWidgets import QCheckBox, QGridLayout, QLabel, QLineEdit, QPushButton, QWidget

from thunder_ide.core.i18n import tr
from thunder_ide.core.viewmodels import FindRequest


class FindBar(QWidget):
    """A row (two rows with Replace) of fields and buttons; hidden until a command opens it."""

    def __init__(self, on_find: Callable[[FindRequest], None],
                 on_replace: Callable[[FindRequest, bool], None]) -> None:
        super().__init__()
        self.on_find = on_find
        self.on_replace = on_replace

        self.needle = QLineEdit()
        self.needle.setClearButtonEnabled(True)
        self.replacement = QLineEdit()
        self.replacement.setClearButtonEnabled(True)
        self.case = QCheckBox(tr("find.case"))
        self.word = QCheckBox(tr("find.word"))
        self.find_label = QLabel(tr("find.find"))
        self.replace_label = QLabel(tr("find.replace"))

        next_button = QPushButton(tr("find.next"))
        previous_button = QPushButton(tr("find.previous"))
        replace_button = QPushButton(tr("find.replace_one"))
        replace_all_button = QPushButton(tr("find.replace_all"))
        close_button = QPushButton(tr("find.close"))
        next_button.clicked.connect(lambda: self._find(False))
        previous_button.clicked.connect(lambda: self._find(True))
        replace_button.clicked.connect(lambda: self._replace(False))
        replace_all_button.clicked.connect(lambda: self._replace(True))
        close_button.clicked.connect(self.close_bar)
        self.needle.returnPressed.connect(lambda: self._find(False))
        self.replacement.returnPressed.connect(lambda: self._replace(False))

        grid = QGridLayout(self)
        grid.setContentsMargins(6, 4, 6, 4)
        grid.setSpacing(6)
        grid.addWidget(self.find_label, 0, 0)
        grid.addWidget(self.needle, 0, 1)
        grid.addWidget(next_button, 0, 2)
        grid.addWidget(previous_button, 0, 3)
        grid.addWidget(self.case, 0, 4)
        grid.addWidget(self.word, 0, 5)
        grid.addWidget(close_button, 0, 6)
        grid.addWidget(self.replace_label, 1, 0)
        grid.addWidget(self.replacement, 1, 1)
        grid.addWidget(replace_button, 1, 2)
        grid.addWidget(replace_all_button, 1, 3)
        grid.setColumnStretch(1, 1)
        self._replace_widgets = (self.replace_label, self.replacement, replace_button, replace_all_button)
        self.hide()

    # ------------------------------------------------------------------ use

    def request(self, backwards: bool = False) -> FindRequest:
        return FindRequest(self.needle.text(), self.replacement.text(), self.case.isChecked(),
                           self.word.isChecked(), backwards)

    def open_bar(self, with_replace: bool, selected: str = "") -> None:
        """Shows the bar; ``selected`` (the editor's selection) becomes what to look for."""
        for widget in self._replace_widgets:
            widget.setVisible(with_replace)
        if selected and "\n" not in selected:
            self.needle.setText(selected)
        self.show()
        field = self.replacement if with_replace and self.needle.text() else self.needle
        field.setFocus()
        field.selectAll()

    def close_bar(self) -> None:
        self.hide()
        parent = self.parentWidget()
        editor = parent.findChild(QWidget, "code_editor") if parent is not None else None
        if editor is not None:
            editor.setFocus()

    def _find(self, backwards: bool) -> None:
        if self.needle.text():
            self.on_find(self.request(backwards))

    def _replace(self, everywhere: bool) -> None:
        if self.needle.text():
            self.on_replace(self.request(), everywhere)

    def keyPressEvent(self, event: QKeyEvent) -> None:  # noqa: N802  (Qt's name)
        if event.key() == Qt.Key.Key_Escape:
            self.close_bar()
            return
        super().keyPressEvent(event)
