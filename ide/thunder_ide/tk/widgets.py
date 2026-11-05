"""Tk implementations of the generic panels (see core.ui)."""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Callable, Optional

from thunder_ide.core import bidi
from thunder_ide.core.highlight import Highlight, styles_per_character
from thunder_ide.core.i18n import tr
from thunder_ide.core.viewmodels import StyledText, TreeRow, TreeSpec, VmViewState
from thunder_ide.tk.rendering import TextRenderer

CATEGORY_COLORS = {
    "keyword": "#0033b3",
    "type": "#008080",
    "literal": "#067d17",
    "identifier": "#000000",
    "operator": "#a0522d",
    "punctuation": "#555555",
    "special": "#8c8c8c",
    "error": "#c00000",
    "warning": "#a06000",
}

SYNTAX_COLORS = {
    "keyword": "#0033b3",
    "type": "#008080",
    "literal": "#871094",
    "builtin": "#7a3e9d",
    "number": "#1750eb",
    "string": "#067d17",
    "comment": "#8c8c8c",
}

SECTION_COLORS = ("#fff4d6", "#e3f2fd", "#e8f5e9", "#fce4ec", "#ede7f6", "#e0f7fa")


class TkTreePanel(ttk.Frame):
    """A table or tree (TreePanelView)."""

    def __init__(self, master: tk.Misc, spec: TreeSpec, renderer: TextRenderer,
                 on_select: Callable[[TreeRow], None], code_font: object = None):
        super().__init__(master)
        self.renderer = renderer
        self.on_select = on_select
        self.rows: dict[str, TreeRow] = {}
        # A flat table hides Tk's tree column, so a row's text becomes the first visible column.
        self.flat = spec.tree_heading is None
        ids = [column.id for column in spec.columns]
        self.tree = ttk.Treeview(self, columns=ids, show="tree headings" if spec.tree_heading else "headings",
                                 selectmode="browse", style="Code.Treeview" if code_font else "Treeview")
        anchor = "e" if renderer.right_to_left else "w"
        if spec.tree_heading:
            self.tree.heading("#0", text=renderer.display(spec.tree_heading), anchor=anchor)
            self.tree.column("#0", width=spec.tree_width, stretch=True, anchor=anchor)
        for column in spec.columns:
            self.tree.heading(column.id, text=renderer.display(column.heading), anchor=anchor)
            self.tree.column(column.id, width=column.width, stretch=False, anchor=anchor)
        if renderer.right_to_left:
            self.tree.configure(displaycolumns=list(reversed(ids)))
        for category, color in CATEGORY_COLORS.items():
            self.tree.tag_configure(category, foreground=color)
        y_scroll = ttk.Scrollbar(self, orient="vertical", command=self.tree.yview)
        x_scroll = ttk.Scrollbar(self, orient="horizontal", command=self.tree.xview)
        self.tree.configure(yscrollcommand=y_scroll.set, xscrollcommand=x_scroll.set)
        self.footer = ttk.Label(self, text="", anchor=anchor, justify="right" if renderer.right_to_left else "left",
                                wraplength=560)
        self.tree.grid(row=0, column=1 if renderer.right_to_left else 0, sticky="nsew")
        y_scroll.grid(row=0, column=0 if renderer.right_to_left else 1, sticky="ns")
        x_scroll.grid(row=1, column=1 if renderer.right_to_left else 0, sticky="ew")
        self.footer.grid(row=2, column=0, columnspan=2, sticky="ew", padx=6)
        self.rowconfigure(0, weight=1)
        self.columnconfigure(1 if renderer.right_to_left else 0, weight=1)
        self.tree.bind("<<TreeviewSelect>>", self._on_select)

    def set_rows(self, rows: list[TreeRow]) -> None:
        self.tree.delete(*self.tree.get_children())
        self.rows.clear()
        for row in rows:
            self._insert("", row)

    def _insert(self, parent: str, row: TreeRow) -> None:
        display = self.renderer.display
        values = (row.text, *row.values) if self.flat else row.values
        item = self.tree.insert(parent, "end", text=display(row.text),
                                values=tuple(display(str(value)) for value in values),
                                tags=(row.style,) if row.style else (), open=row.expanded)
        self.rows[item] = row
        for child in row.children:
            self._insert(item, child)

    def set_footer(self, text: str) -> None:
        self.footer.configure(text=self.renderer.display(text))

    def _on_select(self, _event: Optional[tk.Event] = None) -> None:
        selection = self.tree.selection()
        if selection and selection[0] in self.rows:
            self.on_select(self.rows[selection[0]])

    def items(self, parent: str = "") -> list[str]:
        """All item ids, depth first (for tests)."""
        result = []
        for item in self.tree.get_children(parent):
            result.append(item)
            result.extend(self.items(item))
        return result


class TkTextPanel(ttk.Frame):
    """Read-only styled text (TextPanelView). Right-to-left lines are laid out by core.bidi."""

    def __init__(self, master: tk.Misc, renderer: TextRenderer, font: object, wrap: str = "word",
                 styles: Optional[dict[str, dict]] = None, **options: object):
        super().__init__(master)
        self.renderer = renderer
        self.text = tk.Text(self, wrap=wrap, font=font, state="disabled", borderwidth=0, padx=8, pady=6, **options)
        scroll = ttk.Scrollbar(self, orient="vertical", command=self.text.yview)
        self.text.configure(yscrollcommand=scroll.set)
        rtl = renderer.right_to_left
        self.text.grid(row=0, column=1 if rtl else 0, sticky="nsew")
        scroll.grid(row=0, column=0 if rtl else 1, sticky="ns")
        self.rowconfigure(0, weight=1)
        self.columnconfigure(1 if rtl else 0, weight=1)
        self.text.tag_configure("rtl", justify="right")
        for name, config in (styles or {}).items():
            self.text.tag_configure(name, **config)
        self._lines: list[list[tuple[str, Optional[str]]]] = [[]]  # logical content, one list of runs per line

    def clear(self) -> None:
        self._lines = [[]]
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.configure(state="disabled")

    def append(self, content: str, style: Optional[str] = None) -> None:
        first_changed = len(self._lines) - 1
        pieces = content.split("\n")
        for index, piece in enumerate(pieces):
            if index > 0:
                self._lines.append([])
            if piece:
                self._lines[-1].append((piece, style))
        self.text.configure(state="normal")
        for line_index in range(first_changed, len(self._lines)):
            self._render_line(line_index)
        self.text.configure(state="disabled")
        self.text.see("end")

    def set_lines(self, lines: list[StyledText]) -> None:
        self.clear()
        for line in lines:
            self.append(line.text + "\n", line.style)
        self.text.see("1.0")

    def _render_line(self, index: int) -> None:
        line_number = index + 1
        if int(self.text.index("end-1c").split(".")[0]) < line_number:
            self.text.insert("end", "\n")
        self.text.delete(f"{line_number}.0", f"{line_number}.end")
        runs = self._lines[index]
        logical = "".join(text for text, _ in runs)
        styles: list[Optional[str]] = []
        for text, style in runs:
            styles.extend([style] * len(text))
        chunks, rtl = self.renderer.line_chunks(logical, styles)
        position = f"{line_number}.0"
        for chunk in chunks:
            tags = tuple(tag for tag in (chunk.style, "rtl" if rtl else None) if tag)
            self.text.insert(position, chunk.text, tags)
            position = self.text.index(f"{position}+{len(chunk.text)}c")

    def get_text(self) -> str:
        """The logical text (as written by the program, before any right-to-left layout)."""
        return "\n".join("".join(text for text, _ in runs) for runs in self._lines)


class TkVmPanel(ttk.Frame):
    """The step-by-step debugger (VmPanelView)."""

    ACTIONS = (("first", "vm.first"), ("back_line", "vm.back_line"), ("back", "vm.back"), ("forward", "vm.forward"),
               ("forward_line", "vm.forward_line"), ("last", "vm.last"))

    def __init__(self, master: tk.Misc, renderer: TextRenderer, font: object, on_navigate: Callable[[str], None]):
        super().__init__(master)
        self.renderer = renderer
        rtl = renderer.right_to_left
        controls = ttk.Frame(self)
        controls.grid(row=0, column=0, sticky="ew", padx=4, pady=4)
        self.buttons: dict[str, ttk.Button] = {}
        actions = list(reversed(self.ACTIONS)) if rtl else list(self.ACTIONS)
        for action, key in actions:
            button = ttk.Button(controls, text=renderer.display(tr(key)), width=8,
                                command=lambda a=action: on_navigate(a))
            button.pack(side="right" if rtl else "left", padx=2)
            self.buttons[action] = button
        self.position = ttk.Label(controls, text="")
        self.position.pack(side="right" if rtl else "left", padx=8)
        self.instruction = ttk.Label(self, text="", font=font, anchor="e" if rtl else "w",
                                     justify="right" if rtl else "left")
        self.instruction.grid(row=1, column=0, sticky="ew", padx=6)

        panes = ttk.Panedwindow(self, orient="horizontal")
        panes.grid(row=2, column=0, sticky="nsew")
        left = ttk.Panedwindow(panes, orient="vertical")
        right = ttk.Panedwindow(panes, orient="vertical")
        for pane in ((right, left) if rtl else (left, right)):
            panes.add(pane, weight=1)
        from thunder_ide.core.viewmodels import Column
        two = lambda first, second: TreeSpec(None, (Column("a", first, 120), Column("b", second, 200)))  # noqa: E731
        self.stack = self._titled(left, tr("vm.stack"), TreeSpec(None, (Column("i", "#", 40),
                                                                        Column("v", tr("column.value"), 120),
                                                                        Column("o", tr("vm.owner"), 170))))
        self.frames = self._titled(left, tr("vm.frames"), two(tr("column.name"), tr("vm.base")))
        self.locals = self._titled(right, tr("vm.locals"), two(tr("column.name"), tr("column.value")))
        self.globals = self._titled(right, tr("vm.globals"), two(tr("column.name"), tr("column.value")))
        output_frame = ttk.Labelframe(right, text=renderer.display(tr("vm.output")))
        self.output = TkTextPanel(output_frame, renderer, font, height=4)
        self.output.pack(fill="both", expand=True)
        right.add(output_frame, weight=1)
        self.rowconfigure(2, weight=1)
        self.columnconfigure(0, weight=1)
        self.show(None)

    def _titled(self, parent: ttk.Panedwindow, title: str, spec: TreeSpec) -> TkTreePanel:
        frame = ttk.Labelframe(parent, text=self.renderer.display(title))
        view = TkTreePanel(frame, spec, self.renderer, lambda _row: None)
        view.tree.configure(height=4)
        view.pack(fill="both", expand=True)
        parent.add(frame, weight=1)
        return view

    def show(self, state: Optional[VmViewState]) -> None:
        display = self.renderer.display
        if state is None:
            for view in (self.stack, self.frames, self.locals, self.globals):
                view.set_rows([])
            self.output.clear()
            self.position.configure(text=display(tr("vm.no_trace")))
            self.instruction.configure(text="")
            for button in self.buttons.values():
                button.state(["disabled"])
            return
        self.position.configure(text=display(state.position))
        self.instruction.configure(text=display(state.instruction))
        for action in ("first", "back", "back_line"):
            self.buttons[action].state(["!disabled"] if state.can_go_back else ["disabled"])
        for action in ("forward", "forward_line", "last"):
            self.buttons[action].state(["!disabled"] if state.can_go_forward else ["disabled"])
        self.stack.set_rows(state.stack)
        self.frames.set_rows(state.frames)
        self.locals.set_rows(state.locals)
        self.globals.set_rows(state.globals)
        self.output.clear()
        self.output.append(state.output)


class TkSourceView(ttk.Frame):
    """The program rendered with correct right-to-left layout (SourceView), with line numbers."""

    def __init__(self, master: tk.Misc, renderer: TextRenderer, font: object, on_line_clicked: Callable[[int], None]):
        super().__init__(master)
        self.renderer = renderer
        self.on_line_clicked = on_line_clicked
        self.text = tk.Text(self, wrap="none", font=font, state="disabled", borderwidth=0, padx=8, pady=4,
                            cursor="arrow", background="#fbfbf8")
        self.gutter = tk.Text(self, width=4, font=font, state="disabled", borderwidth=0, pady=4,
                              background="#f0f0f0", foreground="#8a8a8a")
        scroll = ttk.Scrollbar(self, orient="vertical", command=self._scroll)
        self.text.configure(yscrollcommand=lambda first, last: (scroll.set(first, last), self.gutter.yview_moveto(first)))
        self._scrollbar = scroll
        for tag, color in SYNTAX_COLORS.items():
            self.text.tag_configure(tag, foreground=color)
        self.text.tag_configure("rtl", justify="right")
        self.text.tag_configure("current_line", background="#d8f0d8")
        self.gutter.tag_configure("right", justify="right")
        self.text.bind("<Button-1>", self._on_click)
        self._last: Optional[tuple] = None
        self._layout(False)

    def _layout(self, rtl: bool) -> None:
        for widget in (self.text, self.gutter, self._scrollbar):
            widget.grid_forget()
        if rtl:
            self._scrollbar.grid(row=0, column=0, sticky="ns")
            self.text.grid(row=0, column=1, sticky="nsew")
            self.gutter.grid(row=0, column=2, sticky="ns")
            self.columnconfigure(1, weight=1)
            self.columnconfigure(0, weight=0)
        else:
            self.gutter.grid(row=0, column=0, sticky="ns")
            self.text.grid(row=0, column=1, sticky="nsew")
            self._scrollbar.grid(row=0, column=2, sticky="ns")
            self.columnconfigure(1, weight=1)
        self.rowconfigure(0, weight=1)

    def _scroll(self, *args: str) -> None:
        self.text.yview(*args)
        self.gutter.yview(*args)

    def show(self, text: str, regions: list[Highlight], current_line: int, right_to_left: bool) -> None:
        key = (text, tuple(regions), current_line, right_to_left)
        if key == self._last:
            return
        self._last = key
        self._layout(right_to_left)
        styles = styles_per_character(text, regions)
        self.text.configure(state="normal")
        self.gutter.configure(state="normal")
        self.text.delete("1.0", "end")
        self.gutter.delete("1.0", "end")
        offset = 0
        for number, line in enumerate(text.split("\n"), start=1):
            chunks, rtl = self.renderer.line_chunks(line, styles[offset:offset + len(line)], right_to_left)
            offset += len(line) + 1
            for chunk in chunks:
                self.text.insert("end", chunk.text, tuple(tag for tag in (chunk.style, "rtl" if rtl else None) if tag))
            self.text.insert("end", "\n", ("rtl",) if rtl else ())
            self.gutter.insert("end", self.renderer.display(str(number)) + "\n", ("right",))
        if current_line > 0:
            self.text.tag_add("current_line", f"{current_line}.0", f"{current_line}.0+1line")
            self.text.see(f"{current_line}.0")
        self.text.configure(state="disabled")
        self.gutter.configure(state="disabled")

    def _on_click(self, event: tk.Event) -> str:
        line = int(self.text.index(f"@{event.x},{event.y}").split(".")[0])
        self.on_line_clicked(line)
        return "break"
