"""The Tk code editor (EditorView): line numbers, live syntax highlighting, Allman auto-indent."""

from __future__ import annotations

import tkinter as tk
import tkinter.font as tkfont
from tkinter import ttk
from typing import Callable, Optional

from thunder_ide.core import editing
from thunder_ide.core.formatter import INDENT
from thunder_ide.core.highlight import Highlight
from thunder_ide.core.viewmodels import Span
from thunder_ide.tk.widgets import SYNTAX_COLORS

MONOSPACE_FAMILIES = ("Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", "Menlo",
                      "Courier New")

# Text widget subcommands that change the text. Tk's <<Modified>> event only fires when the
# modified flag changes, so the editor intercepts these commands to report *every* edit.
_EDITING_COMMANDS = {"insert", "delete", "replace"}


def pick_monospace_family(root: tk.Misc) -> str:
    available = set(tkfont.families(root))
    for family in MONOSPACE_FAMILIES:
        if family in available:
            return family
    return "TkFixedFont"


def line_starts_of(text: str) -> list[int]:
    starts = [0]
    for index, char in enumerate(text):
        if char == "\n":
            starts.append(index + 1)
    return starts


def offset_to_index(line_starts: list[int], offset: int) -> str:
    """Converts a character offset into a Tk text index ("line.column")."""
    low, high = 0, len(line_starts) - 1
    while low < high:
        middle = (low + high + 1) // 2
        if line_starts[middle] <= offset:
            low = middle
        else:
            high = middle - 1
    return f"{low + 1}.{offset - line_starts[low]}"


class LineNumbers(tk.Canvas):
    """Draws the line numbers next to a Text widget."""

    def __init__(self, master: tk.Misc, text: tk.Text, font: tkfont.Font):
        super().__init__(master, width=48, highlightthickness=0, background="#f3f3f3")
        self.text = text
        self.font = font
        self.marked_line: Optional[int] = None

    def redraw(self) -> None:
        self.delete("all")
        width = self.font.measure("0000") + 12
        self.configure(width=width)
        index = self.text.index("@0,0")
        while True:
            info = self.text.dlineinfo(index)
            if info is None:
                break
            line = int(index.split(".")[0])
            color = "#c00000" if line == self.marked_line else "#8a8a8a"
            self.create_text(width - 6, info[1], anchor="ne", text=str(line), font=self.font, fill=color)
            index = self.text.index(f"{index}+1line")
            if self.text.compare(index, "==", "end"):
                break


class TkEditor(ttk.Frame):
    def __init__(self, master: tk.Misc, font_family: str, font_size: int, on_change: Callable[[], None],
                 on_cursor_moved: Callable[[], None]):
        super().__init__(master)
        self.font = tkfont.Font(family=font_family, size=font_size)
        self.on_change = on_change
        self.on_cursor_moved = on_cursor_moved
        self.right_to_left = False

        self.text = tk.Text(self, wrap="none", undo=True, font=self.font, tabs=(self.font.measure(INDENT),),
                            background="#ffffff", insertbackground="#000000", selectbackground="#add6ff",
                            borderwidth=0, padx=6, pady=4)
        self.line_numbers = LineNumbers(self, self.text, self.font)
        self.y_scroll = ttk.Scrollbar(self, orient="vertical", command=self._scroll_y)
        self.x_scroll = ttk.Scrollbar(self, orient="horizontal", command=self.text.xview)
        self.text.configure(yscrollcommand=self._on_y_scroll, xscrollcommand=self.x_scroll.set)
        self._place_widgets()

        for tag, color in SYNTAX_COLORS.items():
            self.text.tag_configure(tag, foreground=color)
        self.text.tag_configure("comment", font=(font_family, font_size, "italic"))
        self.text.tag_configure("error", underline=True, foreground="#c00000", background="#ffe6e6")
        self.text.tag_configure("selection_span", background="#fff2a8")
        self.text.tag_configure("current_line", background="#d8f0d8")
        self.text.tag_configure("rtl", justify="right")
        self.text.tag_raise("sel")

        self._install_change_proxy()
        self.text.bind("<Return>", self._on_return)
        self.text.bind("<KP_Enter>", self._on_return)
        self.text.bind("<Tab>", self._on_tab)
        self.text.bind("<braceleft>", self._on_open_brace)
        self.text.bind("<braceright>", self._on_close_brace)
        self.text.bind("<<TextChanged>>", lambda _event: self._changed())
        self.text.bind("<<CursorMoved>>", lambda _event: self.on_cursor_moved())
        self.text.bind("<Configure>", lambda _event: self.line_numbers.redraw())

    # ------------------------------------------------------------------ change notifications

    def _install_change_proxy(self) -> None:
        """Routes the Text widget's Tcl command through Python so every edit is noticed."""
        widget = str(self.text)
        self._original_command = widget + "_original"
        self.tk.call("rename", widget, self._original_command)
        self.tk.createcommand(widget, self._proxy)
        self.text.bind("<Destroy>", lambda _event: self._remove_proxy(widget), add="+")

    def _remove_proxy(self, widget: str) -> None:
        try:
            self.tk.deletecommand(widget)
        except tk.TclError:  # pragma: no cover - already removed
            pass

    def _proxy(self, command: str, *args: str) -> object:
        result = self.tk.call((self._original_command, command) + args)
        changed = command in _EDITING_COMMANDS or (command == "edit" and args and args[0] in ("undo", "redo"))
        if changed:
            self.text.event_generate("<<TextChanged>>", when="tail")
        if changed or (command == "mark" and args[:2] == ("set", "insert")):
            self.text.event_generate("<<CursorMoved>>", when="tail")
        return result

    def _changed(self) -> None:
        self.line_numbers.redraw()
        self.on_change()

    # ------------------------------------------------------------------ EditorView

    def get_text(self) -> str:
        return self.text.get("1.0", "end-1c")

    def set_text(self, text: str) -> None:
        self.text.delete("1.0", "end")
        self.text.insert("1.0", text)
        self.text.mark_set("insert", "1.0")
        self.text.edit_reset()
        self.text.edit_modified(False)
        self.clear_marks()

    def replace_text(self, text: str) -> None:
        """Replaces everything as a single step that one Undo reverts."""
        self.text.configure(autoseparators=False)
        try:
            self.text.edit_separator()
            self.text.delete("1.0", "end")
            self.text.insert("1.0", text)
            self.text.edit_separator()
        finally:
            self.text.configure(autoseparators=True)

    def is_modified(self) -> bool:
        return bool(self.text.edit_modified())

    def set_modified(self, modified: bool) -> None:
        self.text.edit_modified(modified)

    def apply_highlights(self, regions: list[Highlight]) -> None:
        for tag in SYNTAX_COLORS:
            self.text.tag_remove(tag, "1.0", "end")
        starts = line_starts_of(self.get_text())
        for region in regions:
            self.text.tag_add(region.tag, offset_to_index(starts, region.start), offset_to_index(starts, region.end))
        self._apply_alignment()
        self.line_numbers.redraw()

    def _apply_alignment(self) -> None:
        self.text.tag_remove("rtl", "1.0", "end")
        if self.right_to_left:
            self.text.tag_add("rtl", "1.0", "end")

    def set_right_to_left(self, rtl: bool) -> None:
        if rtl != self.right_to_left:
            self.right_to_left = rtl
            self._place_widgets()
        self._apply_alignment()

    def _place_widgets(self) -> None:
        for widget in (self.line_numbers, self.text, self.y_scroll, self.x_scroll):
            widget.grid_forget()
        if self.right_to_left:  # line numbers on the right, scroll bar on the left
            self.y_scroll.grid(row=0, column=0, sticky="ns")
            self.text.grid(row=0, column=1, sticky="nsew")
            self.line_numbers.grid(row=0, column=2, sticky="ns")
        else:
            self.line_numbers.grid(row=0, column=0, sticky="ns")
            self.text.grid(row=0, column=1, sticky="nsew")
            self.y_scroll.grid(row=0, column=2, sticky="ns")
        self.x_scroll.grid(row=1, column=1, sticky="ew")
        self.rowconfigure(0, weight=1)
        self.columnconfigure(1, weight=1)

    def _span_indices(self, span: Span) -> tuple[str, str]:
        start = f"{span.line}.{max(span.column - 1, 0)}"
        if span.end_line <= 0 or (span.end_line, span.end_column) <= (span.line, span.column):
            return start, f"{start}+1c"
        return start, f"{span.end_line}.{max(span.end_column - 1, 0)}"

    def mark_error(self, span: Span) -> None:
        start, end = self._span_indices(span)
        if self.text.compare(start, "==", "end-1c") or self.text.get(start, end).strip() == "":
            # Nothing visible to underline (e.g. a missing operand at the end of a line): mark the line.
            start, end = f"{span.line}.0", f"{span.line}.end"
        self.text.tag_add("error", start, end)
        self.line_numbers.marked_line = span.line
        self.line_numbers.redraw()

    def show_span(self, span: Optional[Span]) -> None:
        self.text.tag_remove("selection_span", "1.0", "end")
        if span is None or span.line <= 0:
            return
        if span.column <= 0:  # no column: highlight the whole line
            start, end = f"{span.line}.0", f"{span.line}.end"
        else:
            start, end = self._span_indices(span)
        self.text.tag_add("selection_span", start, end)
        self.text.see(start)

    def show_current_line(self, line: int) -> None:
        self.text.tag_remove("current_line", "1.0", "end")
        if line > 0:
            self.text.tag_add("current_line", f"{line}.0", f"{line}.0+1line")
            self.text.see(f"{line}.0")

    def clear_marks(self) -> None:
        for tag in ("error", "selection_span", "current_line"):
            self.text.tag_remove(tag, "1.0", "end")
        self.line_numbers.marked_line = None
        self.line_numbers.redraw()

    def cursor_line(self) -> int:
        return int(self.text.index("insert").split(".")[0])

    def go_to_line(self, line: int) -> None:
        self.text.mark_set("insert", f"{line}.0")
        self.text.see("insert")
        self.text.focus_set()

    def set_font_size(self, size: int) -> None:
        self.font.configure(size=size)
        self.text.tag_configure("comment", font=(self.font.actual("family"), size, "italic"))
        self.text.configure(tabs=(self.font.measure(INDENT),))
        self.line_numbers.redraw()

    # ------------------------------------------------------------------ Allman-style editing (core.editing)

    def _apply(self, edit: editing.Edit) -> str:
        self.text.delete("insert linestart", "insert")
        self.text.insert("insert", edit.before + edit.insert)
        if edit.cursor_back:
            self.text.mark_set("insert", f"insert-{edit.cursor_back}c")
        self.text.see("insert")
        return "break"

    def _on_return(self, _event: Optional[tk.Event] = None) -> str:
        if self.text.tag_ranges("sel"):
            self.text.delete("sel.first", "sel.last")
        before = self.text.get("insert linestart", "insert")
        after = self.text.get("insert", "insert lineend")
        return self._apply(editing.on_return(before, after))

    def _on_tab(self, _event: Optional[tk.Event] = None) -> str:
        self.text.insert("insert", INDENT)
        return "break"

    def _on_open_brace(self, _event: Optional[tk.Event] = None) -> str:
        return self._apply(editing.on_open_brace(self.text.get("insert linestart", "insert")))

    def _on_close_brace(self, _event: Optional[tk.Event] = None) -> str:
        return self._apply(editing.on_close_brace(self.text.get("insert linestart", "insert")))

    # ------------------------------------------------------------------ scrolling

    def _scroll_y(self, *args: str) -> None:
        self.text.yview(*args)
        self.line_numbers.redraw()

    def _on_y_scroll(self, first: str, last: str) -> None:
        self.y_scroll.set(first, last)
        self.line_numbers.redraw()
