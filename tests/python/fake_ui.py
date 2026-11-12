"""A fake user interface that records what the controller shows (no GUI toolkit needed).

It implements the protocols in thunder_ide.core.ui, which also documents them by example:
this is everything a new toolkit backend has to provide.
"""

from __future__ import annotations

import queue
from typing import Any, Callable, Optional

from thunder_ide.core.editing import BlockWords
from thunder_ide.core.highlight import Highlight
from thunder_ide.core.layout import Layout
from thunder_ide.core.viewmodels import Span, StyledText, TreeRow, VmViewState


class FakeScheduler:
    def __init__(self) -> None:
        self.timers: dict[int, Callable[[], None]] = {}
        self.posted: "queue.Queue[Callable[[], None]]" = queue.Queue()
        self._next = 0

    def call_later(self, delay_ms: int, callback: Callable[[], None]) -> Any:
        self._next += 1
        self.timers[self._next] = callback
        return self._next

    def cancel(self, handle: Any) -> None:
        self.timers.pop(handle, None)

    def post(self, callback: Callable[[], None]) -> None:
        self.posted.put(callback)

    def run_timers(self) -> None:
        timers, self.timers = self.timers, {}
        for callback in timers.values():
            callback()

    def drain(self) -> None:
        while not self.posted.empty():
            self.posted.get_nowait()()


class FakeEditor:
    def __init__(self) -> None:
        self.text = ""
        self.modified = False
        self.highlights: list[Highlight] = []
        self.block_words = BlockWords()
        self.errors: list[Span] = []
        self.span: Optional[Span] = None
        self.current_line = 0
        self.cursor = 1
        self.font_size = 0
        self.rtl = False
        self.undo_stack: list[str] = []
        self.redo_stack: list[str] = []
        self.clipboard = ""
        self.selected = (0, 0)
        self.show_whitespace = False
        self.folded: list[tuple[int, int]] = []

    def get_text(self) -> str:
        return self.text

    def set_text(self, text: str) -> None:
        self.text = text
        self.modified = False
        self.selected = (0, 0)
        self.undo_stack.clear()

    def replace_text(self, text: str) -> None:
        self.undo_stack.append(self.text)
        self.text = text
        self.modified = True

    def type(self, text: str) -> None:
        """Simulates the user typing at the end (the toolkit would report the change)."""
        self.text += text
        self.modified = True

    def is_modified(self) -> bool:
        return self.modified

    def set_modified(self, modified: bool) -> None:
        self.modified = modified

    def apply_highlights(self, regions: list[Highlight]) -> None:
        self.highlights = regions

    def set_block_words(self, words: BlockWords) -> None:
        self.block_words = words

    def mark_error(self, span: Span) -> None:
        self.errors.append(span)

    def show_span(self, span: Optional[Span]) -> None:
        self.span = span

    def show_current_line(self, line: int) -> None:
        self.current_line = line

    def clear_marks(self) -> None:
        self.errors.clear()
        self.span = None
        self.current_line = 0

    def cursor_line(self) -> int:
        return self.cursor

    def go_to_line(self, line: int) -> None:
        self.cursor = line

    def set_font_size(self, size: int) -> None:
        self.font_size = size

    def set_right_to_left(self, rtl: bool) -> None:
        self.rtl = rtl


    # -- the standard editing commands of the Edit menu --

    def undo(self) -> None:
        if self.undo_stack:
            self.redo_stack.append(self.text)
            self.text = self.undo_stack.pop()

    def redo(self) -> None:
        if self.redo_stack:
            self.undo_stack.append(self.text)
            self.text = self.redo_stack.pop()

    def cut(self) -> None:
        start, end = sorted(self.selected)
        self.clipboard = self.text[start:end]
        if start != end:
            self.replace_range(start, end, "")

    def copy(self) -> None:
        start, end = sorted(self.selected)
        self.clipboard = self.text[start:end]

    def paste(self) -> None:
        start, end = sorted(self.selected)
        self.replace_range(start, end, self.clipboard)

    def select_all(self) -> None:
        self.selected = (0, len(self.text))

    # -- editing commands --

    def selection(self) -> tuple[int, int]:
        return self.selected

    def set_selection(self, start: int, end: int) -> None:
        self.selected = (start, end)

    def replace_range(self, start: int, end: int, text: str) -> None:
        self.undo_stack.append(self.text)
        self.text = self.text[:start] + text + self.text[end:]
        self.selected = (start, start + len(text))
        self.modified = True

    def set_show_whitespace(self, show: bool) -> None:
        self.show_whitespace = show

    def fold(self, folds: list[tuple[int, int]]) -> int:
        self.folded = list(folds)
        return len(self.folded)

    def unfold_all(self) -> None:
        self.folded = []


class FakeTree:
    def __init__(self) -> None:
        self.rows: list[TreeRow] = []
        self.footer = ""

    def set_rows(self, rows: list[TreeRow]) -> None:
        self.rows = rows

    def set_footer(self, text: str) -> None:
        self.footer = text

    def flat(self) -> list[TreeRow]:
        result: list[TreeRow] = []

        def walk(rows: list[TreeRow]) -> None:
            for row in rows:
                result.append(row)
                walk(row.children)

        walk(self.rows)
        return result


class FakeText:
    def __init__(self) -> None:
        self.chunks: list[tuple[str, Optional[str]]] = []

    def clear(self) -> None:
        self.chunks.clear()

    def append(self, text: str, style: Optional[str] = None) -> None:
        self.chunks.append((text, style))

    def set_lines(self, lines: list[StyledText]) -> None:
        self.chunks = [(line.text + "\n", line.style) for line in lines]

    def get_text(self) -> str:
        return "".join(text for text, _ in self.chunks)

    def text_with_style(self, style: str) -> str:
        return "".join(text for text, chunk_style in self.chunks if chunk_style == style)


class FakeVm:
    def __init__(self) -> None:
        self.state: Optional[VmViewState] = None

    def show(self, state: Optional[VmViewState]) -> None:
        self.state = state


class FakeCanvas:
    def __init__(self) -> None:
        self.snapshot = None
        self.status = ""
        self.focused = 0

    def show(self, snapshot) -> None:
        self.snapshot = snapshot

    def set_status(self, text: str) -> None:
        self.status = text

    def focus(self) -> None:
        self.focused += 1


class FakeWindow:
    def __init__(self, right_to_left: bool, font_size: int) -> None:
        self.right_to_left = right_to_left
        self.font_size = font_size
        self.editor = FakeEditor()
        self.overview, self.binary, self.output, self.log = FakeText(), FakeText(), FakeText(), FakeText()
        self.canvas = FakeCanvas()
        self.tokens, self.ast, self.symbols, self.bytecode, self.problems = (FakeTree() for _ in range(5))
        self.vm = FakeVm()
        self.title = self.status = self.file_label = ""
        self.running = False
        self.samples: tuple[list[str], int] = ([], -1)
        self.languages: tuple[list[str], int] = ([], -1)
        self.selected_tab = ""
        self.layout = Layout.default()
        self.closed = False
        self.answers: list[object] = []  # answers for ask_* dialogs, in order
        self.messages: list[tuple[str, str, str]] = []
        self.checked: dict[str, bool] = {}
        self.find_bar: Optional[bool] = None  # None: closed; True/False: open, with/without Replace
        self.help_documents: list = []

    def set_title(self, title: str) -> None:
        self.title = title

    def set_status(self, text: str) -> None:
        self.status = text

    def set_file_label(self, text: str) -> None:
        self.file_label = text

    def set_running(self, running: bool) -> None:
        self.running = running

    def set_command_checked(self, command_id: str, checked: bool) -> None:
        self.checked[command_id] = checked

    def show_find(self, with_replace: bool) -> None:
        self.find_bar = with_replace

    def set_samples(self, titles: list[str], current: int) -> None:
        self.samples = (titles, current)

    def set_languages(self, names: list[str], current: int) -> None:
        self.languages = (names, current)

    def select_panel_tab(self, tab: str) -> None:
        self.selected_tab = tab

    def apply_layout(self, layout: Layout) -> None:
        self.layout = layout.copy()

    def current_layout(self) -> Layout:
        return self.layout.copy()

    def set_panel_visible(self, panel: str, visible: bool) -> None:
        self.layout.set_visible(panel, visible)

    def _answer(self, default: object) -> object:
        return self.answers.pop(0) if self.answers else default

    def ask_yes_no(self, title: str, message: str) -> bool:
        self.messages.append(("ask", title, message))
        return bool(self._answer(True))

    def ask_open_path(self) -> Optional[str]:
        return self._answer(None)  # type: ignore[return-value]

    def ask_save_path(self, suggested_name: str, extension: str = ".th") -> Optional[str]:
        return self._answer(None)  # type: ignore[return-value]

    def ask_go_to_line(self, current: int, last: int) -> Optional[int]:
        self.messages.append(("go_to_line", str(current), str(last)))
        return self._answer(None)  # type: ignore[return-value]

    def show_info(self, title: str, message: str) -> None:
        self.messages.append(("info", title, message))

    def show_error(self, title: str, message: str) -> None:
        self.messages.append(("error", title, message))

    def show_help(self, documents: list, current: str) -> None:
        self.help_documents = documents
        self.messages.append(("help", current, ", ".join(doc.name for doc in documents)))

    def close(self) -> None:
        self.closed = True


class FakeBackend:
    name = "Fake"

    def __init__(self) -> None:
        self.scheduler = FakeScheduler()
        self.windows: list[FakeWindow] = []
        self.quit_called = False

    def create_main_window(self, controller: Any, right_to_left: bool, font_size: int) -> FakeWindow:
        window = FakeWindow(right_to_left, font_size)
        self.windows.append(window)
        return window

    def run(self) -> None:  # pragma: no cover - not used by the tests
        pass

    def quit(self) -> None:
        self.quit_called = True
