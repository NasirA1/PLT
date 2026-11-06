"""The Qt (PySide6) backend: creates the main window and runs Qt's event loop."""

from __future__ import annotations

import sys
from typing import Any, Callable, Optional

from PySide6.QtCore import QObject, Qt, QTimer, Signal, Slot
from PySide6.QtWidgets import QApplication

from thunder_ide.qt.main_window import QtMainWindow


class _Invoker(QObject):
    """Runs callables on the GUI thread; the signal may be emitted from any thread."""

    requested = Signal(object)

    def __init__(self) -> None:
        super().__init__()
        self.requested.connect(self._run, Qt.ConnectionType.QueuedConnection)

    @Slot(object)
    def _run(self, callback: Callable[[], None]) -> None:
        callback()


class QtScheduler:
    def __init__(self) -> None:
        self._invoker = _Invoker()
        self._timers: set[QTimer] = set()

    def call_later(self, delay_ms: int, callback: Callable[[], None]) -> Any:
        timer = QTimer()
        timer.setSingleShot(True)
        self._timers.add(timer)

        def fire() -> None:
            self._timers.discard(timer)
            callback()

        timer.timeout.connect(fire)
        timer.start(max(0, delay_ms))
        return timer

    def cancel(self, handle: Any) -> None:
        if isinstance(handle, QTimer):
            handle.stop()
            self._timers.discard(handle)

    def post(self, callback: Callable[[], None]) -> None:
        self._invoker.requested.emit(callback)

    def flush(self) -> None:
        """Processes pending events (used by the tests)."""
        QApplication.processEvents()


class QtBackend:
    name = "Qt (PySide6)"

    def __init__(self, app: Optional[QApplication] = None):
        existing = QApplication.instance()
        self.app: QApplication = app or existing or QApplication(sys.argv[:1])  # type: ignore[assignment]
        self.app.setApplicationName("Thunder IDE")
        self.app.setQuitOnLastWindowClosed(False)  # the controller closes the window when switching language
        self.scheduler = QtScheduler()
        self.window: Optional[QtMainWindow] = None

    def create_main_window(self, controller: Any, right_to_left: bool, font_size: int) -> QtMainWindow:
        self.app.setLayoutDirection(Qt.LayoutDirection.RightToLeft if right_to_left else Qt.LayoutDirection.LeftToRight)
        self.window = QtMainWindow(controller, right_to_left, font_size)
        self.window.show()
        return self.window

    def run(self) -> None:  # pragma: no cover - interactive
        self.app.exec()

    def quit(self) -> None:
        self.app.quit()
