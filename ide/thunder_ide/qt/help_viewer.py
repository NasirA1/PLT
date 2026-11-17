"""The Help window: the HTML API documentation (docs/api/), shown in a browser view.

The documentation is the static site tools/apidoc.py generates - the language's keywords and
types, every built-in function, the SDK modules, the guides and the instruction set, with its
own search box and a language switch on every page. This window is a small browser around it:
Back, Forward, Home (the site's front page in the interface's language) and "open in the
browser", with the page itself doing the rest. Links that leave the site open in the system's
browser, so the window always shows documentation.

Qt WebEngine does the rendering. If it is not installed, the page opens in the system's browser
instead (see QtMainWindow.show_help).
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QUrl, Qt
from PySide6.QtGui import QDesktopServices, QKeySequence, QShortcut
from PySide6.QtWidgets import QDialog, QHBoxLayout, QPushButton, QToolBar, QVBoxLayout, QWidget

from thunder_ide.core import docs
from thunder_ide.core.i18n import tr

try:  # pragma: no cover - whether WebEngine is installed is the machine's business
    from PySide6.QtWebEngineCore import QWebEnginePage
    from PySide6.QtWebEngineWidgets import QWebEngineView

    WEB_ENGINE_AVAILABLE = True
except ImportError:  # pragma: no cover
    QWebEnginePage = None  # type: ignore[assignment,misc]
    QWebEngineView = None  # type: ignore[assignment,misc]
    WEB_ENGINE_AVAILABLE = False


if WEB_ENGINE_AVAILABLE:

    class DocumentationPage(QWebEnginePage):  # type: ignore[misc,valid-type]
        """Keeps navigation inside the site: anything else goes to the system's browser."""

        def acceptNavigationRequest(self, url: QUrl, kind, is_main_frame: bool) -> bool:  # noqa: N802
            if not is_main_frame or url.isLocalFile() and docs.is_site_page(url.toLocalFile()):
                return True
            if url.scheme() in ("data", "about", "qrc"):
                return True
            QDesktopServices.openUrl(url)
            return False


class HelpViewer(QDialog):
    """A browser window on the documentation site, opened on one page."""

    def __init__(self, parent: QWidget, page: str, language: str):
        super().__init__(parent)
        self.language = language
        self.setWindowTitle(tr("help.title"))
        self.resize(1100, 760)
        self.setLayoutDirection(Qt.LayoutDirection.LeftToRight)  # the page lays itself out

        self.view = QWebEngineView(self)  # type: ignore[misc]
        self.view.setPage(DocumentationPage(self.view))  # type: ignore[misc]

        toolbar = QToolBar()
        toolbar.setMovable(False)
        self.back = QPushButton(tr("help.back"))
        self.forward = QPushButton(tr("help.forward"))
        self.home = QPushButton(tr("help.home"))
        self.external = QPushButton(tr("help.open_browser"))
        for button in (self.back, self.forward, self.home, self.external):
            toolbar.addWidget(button)
        self.back.clicked.connect(self.view.back)
        self.forward.clicked.connect(self.view.forward)
        self.home.clicked.connect(self.go_home)
        self.external.clicked.connect(lambda: QDesktopServices.openUrl(self.view.url()))
        self.view.urlChanged.connect(self._update_buttons)
        self.view.titleChanged.connect(lambda title: self.setWindowTitle(title or tr("help.title")))

        row = QHBoxLayout()
        row.setContentsMargins(0, 0, 0, 0)
        row.addWidget(toolbar)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addLayout(row)
        layout.addWidget(self.view, 1)

        QShortcut(QKeySequence.StandardKey.Back, self, self.view.back)
        QShortcut(QKeySequence.StandardKey.Forward, self, self.view.forward)
        QShortcut(QKeySequence("Alt+Home"), self, self.go_home)
        QShortcut(QKeySequence("Escape"), self, self.close)

        self.open_page(page)

    # ------------------------------------------------------------------ pages

    def open_page(self, page: str) -> None:
        """Shows a page of the site (a file path, with an optional #fragment)."""
        path, _, fragment = str(page).partition("#")
        url = QUrl.fromLocalFile(str(Path(path)))
        if fragment:
            url.setFragment(fragment)
        self.requested = url
        self.view.load(url)
        self._update_buttons()

    def go_home(self) -> None:
        self.open_page(str(docs.home_page(self.language)))

    def current_page(self) -> str:
        """The file shown now, relative to the site ("ps/guides/language.html")."""
        url = self.view.url()
        requested = getattr(self, "requested", None)
        if requested is not None and url != requested:
            url = requested  # the load has not committed yet: the page asked for is the current one
        if not url.isLocalFile():
            return url.toString()
        try:
            return Path(url.toLocalFile()).resolve().relative_to(docs.api_root().resolve()).as_posix()
        except ValueError:
            return url.toLocalFile()

    def _update_buttons(self, *_args) -> None:
        if getattr(self, "requested", None) is not None and self.view.url() == self.requested:
            self.requested = None # the load committed: the view speaks for itself now
        history = self.view.history()
        self.back.setEnabled(history.canGoBack())
        self.forward.setEnabled(history.canGoForward())
