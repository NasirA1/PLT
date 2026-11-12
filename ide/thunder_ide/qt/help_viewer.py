"""The Help viewer: the SDK reference documents, rendered from markdown, with a search.

A contents list on one side, the rendered document on the other, and a search line above
the text. Qt's own markdown support does the rendering (headings, tables, code, images);
links to the other reference documents switch the contents list, and the search highlights
every match in the current document and walks them with Enter / Shift+Enter.
"""

from __future__ import annotations

from PySide6.QtCore import QUrl, Qt
from PySide6.QtGui import QColor, QDesktopServices, QFont, QKeySequence, QShortcut, QTextCursor, QTextDocument
from PySide6.QtWidgets import (QDialog, QHBoxLayout, QLabel, QLineEdit, QListWidget, QPushButton, QSplitter,
                               QTextBrowser, QVBoxLayout, QWidget)

from thunder_ide.core.docs import ReferenceDoc
from thunder_ide.core.i18n import tr
from thunder_ide.qt import style


class HelpViewer(QDialog):
    def __init__(self, parent: QWidget, documents: list[ReferenceDoc]):
        super().__init__(parent)
        self.documents = documents
        self.setWindowTitle(tr("help.title"))
        self.resize(980, 720)
        self._matches: list[QTextCursor] = []
        self._current_match = -1

        self.contents = QListWidget()
        self.contents.addItems([doc.title for doc in documents])
        self.contents.setMaximumWidth(280)
        self.contents.currentRowChanged.connect(self._show_row)

        self.search = QLineEdit()
        self.search.setPlaceholderText(tr("help.search_hint"))
        self.search.textChanged.connect(self._search)
        self.search.returnPressed.connect(lambda: self._step(1))
        self.matches_label = QLabel("")
        previous = QPushButton(tr("find.previous"))
        following = QPushButton(tr("find.next"))
        previous.clicked.connect(lambda: self._step(-1))
        following.clicked.connect(lambda: self._step(1))

        self.browser = QTextBrowser()
        self.browser.setOpenLinks(False)  # .md links switch the document; the rest go to the system
        self.browser.anchorClicked.connect(self._follow)
        font = QFont(self.font())
        font.setPointSize(font.pointSize() + 1)
        self.browser.setFont(font)

        search_row = QHBoxLayout()
        search_row.addWidget(self.search, 1)
        search_row.addWidget(previous)
        search_row.addWidget(following)
        search_row.addWidget(self.matches_label)
        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.addLayout(search_row)
        right_layout.addWidget(self.browser, 1)

        splitter = QSplitter()
        splitter.addWidget(self.contents)
        splitter.addWidget(right)
        splitter.setStretchFactor(1, 1)
        layout = QVBoxLayout(self)
        layout.addWidget(splitter)

        QShortcut(QKeySequence("Ctrl+F"), self, self.search.setFocus)
        QShortcut(QKeySequence("F3"), self, lambda: self._step(1))
        QShortcut(QKeySequence("Shift+F3"), self, lambda: self._step(-1))

    # ------------------------------------------------------------------ documents

    def open_document(self, name: str) -> None:
        for row, doc in enumerate(self.documents):
            if doc.name == name:
                self.contents.setCurrentRow(row)
                return
        self.contents.setCurrentRow(0)

    def current_document(self) -> ReferenceDoc:
        return self.documents[max(self.contents.currentRow(), 0)]

    def _show_row(self, row: int) -> None:
        doc = self.documents[max(row, 0)]
        # The base URL makes relative links and images (docs/images/...) resolve next to the file.
        self.browser.document().setBaseUrl(QUrl.fromLocalFile(doc.directory + "/"))
        self.browser.document().setMarkdown(doc.text, QTextDocument.MarkdownFeature.MarkdownDialectGitHub)
        self.browser.moveCursor(QTextCursor.MoveOperation.Start)
        self._search(self.search.text())

    def _follow(self, url: QUrl) -> None:
        if url.scheme() in ("http", "https", "mailto"):
            QDesktopServices.openUrl(url)
            return
        name = url.fileName()
        if name.endswith(".md"):
            self.open_document(name)
        if url.hasFragment():
            self.browser.scrollToAnchor(url.fragment())

    # ------------------------------------------------------------------ search

    def _search(self, needle: str) -> None:
        """Finds and highlights every match in the current document."""
        self._matches = []
        self._current_match = -1
        document = self.browser.document()
        if needle.strip():
            cursor = QTextCursor(document)
            while True:
                cursor = document.find(needle, cursor)
                if cursor.isNull():
                    break
                self._matches.append(QTextCursor(cursor))
        selections = []
        for match in self._matches:
            selection = QTextBrowser.ExtraSelection()
            selection.cursor = match
            selection.format.setBackground(QColor(style.SPAN_BACKGROUND))
            selections.append(selection)
        self.browser.setExtraSelections(selections)
        if self._matches:
            self._step(1, from_top=True)
        else:
            self.matches_label.setText("" if not needle.strip() else tr("help.no_matches"))

    def _step(self, direction: int, from_top: bool = False) -> None:
        """Scrolls to the next (or previous) match and says which one it is."""
        if not self._matches:
            return
        if from_top:
            self._current_match = 0
        else:
            self._current_match = (self._current_match + direction) % len(self._matches)
        target = self._matches[self._current_match]
        shown = QTextCursor(target)
        self.browser.setTextCursor(shown)  # selects the match, so it is visible and marked
        self.browser.ensureCursorVisible()
        self.matches_label.setText(tr("help.matches", current=self._current_match + 1, total=len(self._matches)))
