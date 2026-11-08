"""The interfaces a GUI toolkit implements to host the Thunder IDE.

The application logic (``core.controller.IdeController``) talks to the user interface only
through these protocols, and the interface talks back only by calling the controller's
public methods (its commands and ``on_...`` event handlers). Supporting another toolkit,
such as Qt, therefore means implementing the classes below; nothing in ``core`` changes.

All data passed to views is plain Python (strings, numbers and the dataclasses in
``core.viewmodels``), already translated into the interface language. Every method is
called on the GUI thread; worker threads use ``Scheduler.post``.
"""

from __future__ import annotations

from typing import Any, Callable, Optional, Protocol

from thunder_ide.core.canvas import CanvasSnapshot
from thunder_ide.core.editing import BlockWords
from thunder_ide.core.highlight import Highlight
from thunder_ide.core.layout import Layout
from thunder_ide.core.viewmodels import Span, StyledText, TreeRow, VmViewState


class Scheduler(Protocol):
    def call_later(self, delay_ms: int, callback: Callable[[], None]) -> Any:
        """Runs ``callback`` on the GUI thread after a delay; returns a handle for ``cancel``."""

    def cancel(self, handle: Any) -> None: ...

    def post(self, callback: Callable[[], None]) -> None:
        """Runs ``callback`` on the GUI thread as soon as possible. Safe to call from any thread."""


class EditorView(Protocol):
    """The source code editor."""

    def get_text(self) -> str: ...

    def set_text(self, text: str) -> None:
        """Replaces the whole text, clears the undo history and the modified flag."""

    def replace_text(self, text: str) -> None:
        """Replaces the whole text as one undoable edit (used by Format and Translate)."""

    def is_modified(self) -> bool: ...

    def set_modified(self, modified: bool) -> None: ...

    def apply_highlights(self, regions: list[Highlight]) -> None:
        """Shows syntax highlighting; ``regions`` covers the whole text."""

    def set_block_words(self, words: BlockWords) -> None:
        """The keywords of the program's language that open and leave blocks (for auto-indentation)."""

    def mark_error(self, span: Span) -> None: ...

    def show_span(self, span: Optional[Span]) -> None:
        """Highlights (and scrolls to) a region, e.g. the source of a selected token."""

    def show_current_line(self, line: int) -> None:
        """Marks the line the debugger is on (0 removes the mark)."""

    def clear_marks(self) -> None: ...

    def cursor_line(self) -> int: ...

    def go_to_line(self, line: int) -> None: ...

    def set_font_size(self, size: int) -> None: ...

    def set_right_to_left(self, rtl: bool) -> None:
        """Switches the editor to the direction of the program's language (see core.direction)."""

    # -- editing commands (the text work itself is in core.edits) --

    def selection(self) -> tuple[int, int]:
        """The selected range as code-point indexes; both the same when nothing is selected."""

    def set_selection(self, start: int, end: int) -> None:
        """Selects a range and scrolls it into view."""

    def replace_range(self, start: int, end: int, text: str) -> None:
        """Replaces a range as one undoable edit, leaving the new text selected."""

    def set_show_whitespace(self, show: bool) -> None:
        """Draws spaces and tabs, or stops drawing them."""

    def fold(self, folds: list[tuple[int, int]]) -> int:
        """Hides the body of each block (header line, last line; 0-based). Returns how many were hidden."""

    def unfold_all(self) -> None:
        """Shows every line again."""


class TreePanelView(Protocol):
    """A table or tree of rows (tokens, syntax tree, symbols, bytecode, problems)."""

    def set_rows(self, rows: list[TreeRow]) -> None: ...

    def set_footer(self, text: str) -> None:
        """A line of explanatory text under the rows (e.g. the selected instruction's meaning)."""


class TextPanelView(Protocol):
    """Read-only styled text (pipeline overview, binary dump, output, build log)."""

    def clear(self) -> None: ...

    def append(self, text: str, style: Optional[str] = None) -> None: ...

    def set_lines(self, lines: list[StyledText]) -> None:
        """Replaces the content; each item is one line."""

    def get_text(self) -> str: ...


class VmPanelView(Protocol):
    """The step-by-step debugger."""

    def show(self, state: Optional[VmViewState]) -> None: ...


class CanvasView(Protocol):
    """Where graphics programs draw (see core.canvas). It reports the user's keys and mouse to
    ``controller.on_canvas_key(name, pressed)`` and ``controller.on_canvas_mouse(x, y, pressed)``,
    in canvas pixels and with the key names of core.canvas.key_name."""

    def show(self, snapshot: Optional[CanvasSnapshot]) -> None:
        """Paints a frame (None: the empty canvas with a hint)."""

    def set_status(self, text: str) -> None:
        """The line above the picture (frame number, size, hint)."""

    def focus(self) -> None:
        """Gives the canvas the keyboard, so a game receives the keys."""


class MainWindowView(Protocol):
    """The main window: menus, toolbar, dockable panels and status bar.

    The window creates its menus and toolbar from ``core.commands.COMMANDS`` and calls
    ``controller.run_command(command_id)`` when one is used.
    """

    editor: EditorView
    overview: TextPanelView
    tokens: TreePanelView
    ast: TreePanelView
    symbols: TreePanelView
    bytecode: TreePanelView
    binary: TextPanelView
    vm: VmPanelView
    output: TextPanelView
    problems: TreePanelView
    log: TextPanelView
    canvas: CanvasView

    def set_title(self, title: str) -> None: ...

    def set_status(self, text: str) -> None: ...

    def set_file_label(self, text: str) -> None: ...

    def set_command_checked(self, command_id: str, checked: bool) -> None:
        """Ticks (or unticks) a checkable command in the menu (see core.commands.Command)."""

    def set_running(self, running: bool) -> None:
        """Enables Stop and disables Compile/Run/Debug while a program runs."""

    def set_samples(self, titles: list[str], current: int) -> None: ...

    def set_languages(self, names: list[str], current: int) -> None: ...

    def select_panel_tab(self, tab: str) -> None:
        """Brings a tab (e.g. "vm", "problems", "output", or the "canvas" panel) to the front."""

    def apply_layout(self, layout: Layout) -> None:
        """Arranges the dockable panels."""

    def current_layout(self) -> Layout:
        """The arrangement as the user has left it (including sizes and floating windows)."""

    def set_panel_visible(self, panel: str, visible: bool) -> None:
        """Shows or hides a dockable panel (as the View menu does)."""

    def ask_yes_no(self, title: str, message: str) -> bool: ...

    def ask_open_path(self) -> Optional[str]: ...

    def ask_save_path(self, suggested_name: str, extension: str = ".th") -> Optional[str]: ...

    def show_find(self, with_replace: bool) -> None:
        """Opens the find bar (with its Replace row, if asked) and puts the focus in it."""

    def show_info(self, title: str, message: str) -> None: ...

    def show_error(self, title: str, message: str) -> None: ...

    def show_document(self, title: str, text: str) -> None: ...

    def close(self) -> None:
        """Destroys the window (the controller has already stopped programs and saved settings)."""


class Backend(Protocol):
    """A GUI toolkit."""

    name: str
    scheduler: Scheduler

    def create_main_window(self, controller: Any, right_to_left: bool, font_size: int) -> MainWindowView:
        """Builds the main window in the current interface language. Called again (after
        ``close`` of the previous window) when the interface language changes."""

    def run(self) -> None:
        """Runs the event loop until ``quit`` is called."""

    def quit(self) -> None:
        """Ends the event loop and releases the toolkit (the window has been closed)."""
