"""The Tkinter backend: creates the main window and runs Tk's event loop."""

from __future__ import annotations

import queue
import tkinter as tk
from typing import Any, Callable

from thunder_ide.tk.main_window import TkMainWindow

POLL_MS = 25


class TkScheduler:
    """Runs callbacks on the Tk thread. ``post`` may be called from any thread."""

    def __init__(self, root: tk.Tk):
        self.root = root
        self._queue: "queue.Queue[Callable[[], None]]" = queue.Queue()
        self._job = root.after(POLL_MS, self._poll)

    def call_later(self, delay_ms: int, callback: Callable[[], None]) -> Any:
        return self.root.after(delay_ms, callback)

    def cancel(self, handle: Any) -> None:
        try:
            self.root.after_cancel(handle)
        except (tk.TclError, ValueError):  # pragma: no cover - already ran
            pass

    def post(self, callback: Callable[[], None]) -> None:
        self._queue.put(callback)

    def _poll(self) -> None:
        while True:
            try:
                callback = self._queue.get_nowait()
            except queue.Empty:
                break
            callback()
        self._job = self.root.after(POLL_MS, self._poll)

    def flush(self) -> None:
        """Runs everything posted so far (used when shutting down and by tests)."""
        while not self._queue.empty():
            self._queue.get_nowait()()

    def stop(self) -> None:
        try:
            self.root.after_cancel(self._job)
        except tk.TclError:  # pragma: no cover
            pass


class TkBackend:
    name = "Tkinter"

    def __init__(self, root: tk.Tk | None = None):
        self.root = root or tk.Tk()
        self.root.geometry("1400x860")
        self.root.minsize(900, 600)
        self.scheduler = TkScheduler(self.root)
        self.window: TkMainWindow | None = None

    def create_main_window(self, controller: Any, right_to_left: bool, font_size: int) -> TkMainWindow:
        self.window = TkMainWindow(self.root, controller, right_to_left, font_size)
        return self.window

    def run(self) -> None:  # pragma: no cover - interactive
        self.root.mainloop()

    def quit(self) -> None:
        self.scheduler.stop()
        try:
            self.root.quit()
            self.root.destroy()
        except tk.TclError:  # pragma: no cover - already destroyed
            pass
