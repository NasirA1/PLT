"""The Tk main window (MainWindowView)."""

from __future__ import annotations

import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, messagebox, ttk
from typing import TYPE_CHECKING, Optional

from thunder_ide.core import layout as layout_ids
from thunder_ide.core.commands import COMMANDS, MENUS, Command
from thunder_ide.core.i18n import tr
from thunder_ide.core.layout import Layout
from thunder_ide.core.viewmodels import tree_specs
from thunder_ide.tk.docking import DockManager
from thunder_ide.tk.editor import TkEditor, pick_monospace_family
from thunder_ide.tk.rendering import TextRenderer
from thunder_ide.tk.widgets import SECTION_COLORS, TkSourceView, TkTextPanel, TkTreePanel, TkVmPanel

if TYPE_CHECKING:  # pragma: no cover
    from thunder_ide.core.controller import IdeController

EDUCATION_TABS = ("overview", "tokens", "ast", "symbols", "bytecode", "binary", "vm")
OUTPUT_TABS = ("output", "problems", "log")


def tk_sequence(shortcut: str) -> Optional[str]:
    """Converts "Ctrl+Shift+F" / "F5" / "Ctrl++" into a Tk event sequence."""
    if not shortcut:
        return None
    parts = shortcut.replace("++", "+plus").split("+")
    key = parts[-1]
    modifiers = [{"Ctrl": "Control", "Shift": "Shift", "Alt": "Alt"}[part] for part in parts[:-1]]
    if len(key) == 1 and key.isalpha():
        key = key.upper() if "Shift" in modifiers else key.lower()
    key = {"-": "minus"}.get(key, key)
    return "<" + "-".join(modifiers + [key]) + ">"


class TkMainWindow:
    def __init__(self, root: tk.Tk, controller: "IdeController", right_to_left: bool, font_size: int):
        self.root = root
        self.controller = controller
        self.rtl = right_to_left
        self.renderer = TextRenderer(root, right_to_left)
        self.frame = ttk.Frame(root)
        self.frame.pack(fill="both", expand=True)
        self._create_fonts(font_size)
        self._create_style()
        self.toolbar_buttons: dict[str, ttk.Button] = {}
        self._create_menu()
        self._create_toolbar()
        self._create_statusbar()
        self.docks = DockManager(root, self.frame, self.renderer, self._sync_view_menu)
        self.docks.outer.pack(side="top", fill="both", expand=True)
        self._create_panels(font_size)
        self._bind_shortcuts()
        root.title(tr("app.title"))
        root.protocol("WM_DELETE_WINDOW", controller.quit)

    # ------------------------------------------------------------------ construction

    def _create_fonts(self, font_size: int) -> None:
        self.mono_family = pick_monospace_family(self.root)
        self.mono_font = tkfont.Font(root=self.root, family=self.mono_family, size=max(8, font_size - 1))
        self.ui_font = tkfont.nametofont("TkDefaultFont")
        self.heading_font = tkfont.Font(root=self.root, family=self.ui_font.actual("family"), size=11, weight="bold")

    def _create_style(self) -> None:
        style = ttk.Style(self.root)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure("Code.Treeview", font=self.mono_font, rowheight=self.mono_font.metrics("linespace") + 4)
        style.configure("Treeview", rowheight=self.ui_font.metrics("linespace") + 4)
        style.configure("Accent.TButton", font=(self.ui_font.actual("family"), 10, "bold"))

    def _label(self, key: str) -> str:
        return self.renderer.display(tr(key))

    def _create_menu(self) -> None:
        self.menu = tk.Menu(self.root)
        self.menus: dict[str, tk.Menu] = {}
        for menu_key in MENUS:
            submenu = tk.Menu(self.menu, tearoff=False)
            self.menus[menu_key] = submenu
            self.menu.add_cascade(label=self._label(menu_key), menu=submenu)
        for entry in COMMANDS:
            submenu = self.menus[entry.menu]
            if entry.separator_before:
                submenu.add_separator()
            submenu.add_command(label=self._label(entry.label), accelerator=entry.shortcut,
                                command=lambda c=entry.id: self.controller.run_command(c))
        view = self.menus["menu.view"]
        view.add_separator()
        self.panel_vars: dict[str, tk.BooleanVar] = {}
        for panel_id in layout_ids.PANELS:
            var = tk.BooleanVar(self.root, value=True)
            self.panel_vars[panel_id] = var
            view.add_checkbutton(label=self._label(f"panel.{panel_id}"), variable=var,
                                 command=lambda p=panel_id, v=var: self.docks.set_visible(p, v.get()))
        self.root.configure(menu=self.menu)

    def _create_toolbar(self) -> None:
        bar = ttk.Frame(self.frame, padding=(6, 4))
        bar.pack(side="top", fill="x")
        side, other = ("right", "left") if self.rtl else ("left", "right")
        ttk.Label(bar, text=self._label("toolbar.sample")).pack(side=side)
        self.sample_choice = ttk.Combobox(bar, state="readonly", width=40)
        self.sample_choice.pack(side=side, padx=(4, 12))
        self.sample_choice.bind("<<ComboboxSelected>>",
                                lambda _event: self.controller.on_sample_selected(self.sample_choice.current()))
        for entry in COMMANDS:
            if entry.toolbar:
                if entry.id == "format":
                    ttk.Separator(bar, orient="vertical").pack(side=side, fill="y", padx=8)
                self._toolbar_button(bar, entry, side)
        self.language_choice = ttk.Combobox(bar, state="readonly", width=10)
        self.language_choice.pack(side=other, padx=4)
        self.language_choice.bind("<<ComboboxSelected>>",
                                  lambda _event: self.controller.on_language_selected(self.language_choice.current()))
        ttk.Label(bar, text=self._label("toolbar.language")).pack(side=other)
        self.file_label = ttk.Label(bar, text="", anchor="center")
        self.file_label.pack(side=other, padx=12)

    def _toolbar_button(self, bar: ttk.Frame, entry: Command, side: str) -> None:
        accent = entry.id in ("compile", "run")
        button = ttk.Button(bar, text=self._label(f"toolbar.{entry.id}"), style="Accent.TButton" if accent else "TButton",
                            command=lambda c=entry.id: self.controller.run_command(c))
        button.pack(side=side, padx=2)
        self.toolbar_buttons[entry.id] = button
        if entry.id == "stop":
            button.state(["disabled"])

    def _create_statusbar(self) -> None:
        self.status = ttk.Label(self.frame, text="", anchor="e" if self.rtl else "w", padding=(6, 2), relief="sunken")
        self.status.pack(side="bottom", fill="x")

    def _create_panels(self, font_size: int) -> None:
        controller = self.controller
        renderer = self.renderer
        specs = tree_specs()

        editor_panel = self.docks.add_panel(layout_ids.EDITOR, tr("panel.editor"))
        self.editor = TkEditor(editor_panel.body, self.mono_family, font_size, controller.on_text_changed,
                               controller.on_cursor_moved)
        self.editor.pack(fill="both", expand=True)

        education = self.docks.add_panel(layout_ids.EDUCATION, tr("panel.education"))
        self.edu_tabs = ttk.Notebook(education.body)
        self.edu_tabs.pack(fill="both", expand=True)
        styles = {"title": {"font": self.heading_font, "spacing3": 4}, "heading": {"font": self.heading_font, "spacing1": 6},
                  "ok": {"foreground": "#067d17"}, "failed": {"foreground": "#c00000"}, "skipped": {"foreground": "#8c8c8c"}}
        self.overview = TkTextPanel(self.edu_tabs, renderer, self.ui_font, styles=styles)
        select = lambda panel: (lambda row: controller.on_row_selected(panel, row))  # noqa: E731
        self.tokens = TkTreePanel(self.edu_tabs, specs["tokens"], renderer, select("tokens"))
        self.ast = TkTreePanel(self.edu_tabs, specs["ast"], renderer, select("ast"))
        self.symbols = TkTreePanel(self.edu_tabs, specs["symbols"], renderer, select("symbols"))
        self.bytecode = TkTreePanel(self.edu_tabs, specs["bytecode"], renderer, select("bytecode"), self.mono_font)
        self.binary = TkTextPanel(self.edu_tabs, renderer, self.mono_font, wrap="none",
                                  styles={f"section{i}": {"background": c} for i, c in enumerate(SECTION_COLORS)})
        self.vm = TkVmPanel(self.edu_tabs, renderer, self.mono_font, controller.on_vm_navigate)
        self.tabs: dict[str, tuple[ttk.Notebook, tk.Widget, str]] = {}
        for tab in (reversed(EDUCATION_TABS) if self.rtl else EDUCATION_TABS):  # tabs read right to left
            widget = getattr(self, tab)
            self.edu_tabs.add(widget, text=self._label(f"tab.{tab}"))
            self.tabs[tab] = (self.edu_tabs, widget, layout_ids.EDUCATION)

        output_panel = self.docks.add_panel(layout_ids.OUTPUT, tr("panel.output"))
        self.bottom_tabs = ttk.Notebook(output_panel.body)
        self.bottom_tabs.pack(fill="both", expand=True)
        self.output = TkTextPanel(self.bottom_tabs, renderer, self.mono_font, background="#1e1e1e",
                                  foreground="#e8e8e8", insertbackground="#e8e8e8",
                                  styles={"stderr": {"foreground": "#ff8080"}, "info": {"foreground": "#7fb2ff"},
                                          "success": {"foreground": "#8fd18f"}})
        self.problems = TkTreePanel(self.bottom_tabs, specs["problems"], renderer, select("problems"))
        self.log = TkTextPanel(self.bottom_tabs, renderer, self.mono_font, wrap="none",
                               styles={"stderr": {"foreground": "#c00000"}, "command": {"foreground": "#0033b3"}})
        for tab in (reversed(OUTPUT_TABS) if self.rtl else OUTPUT_TABS):
            widget = getattr(self, tab)
            self.bottom_tabs.add(widget, text=self._label(f"tab.{tab}"))
            self.tabs[tab] = (self.bottom_tabs, widget, layout_ids.OUTPUT)

        source_panel = self.docks.add_panel(layout_ids.SOURCE_VIEW, tr("panel.source_view"))
        self.source_view = TkSourceView(source_panel.body, renderer, self.editor.font, controller.on_source_line_clicked)
        self.source_view.pack(fill="both", expand=True)

    def _bind_shortcuts(self) -> None:
        for entry in COMMANDS:
            sequence = tk_sequence(entry.shortcut)
            if sequence:
                handler = lambda _event, c=entry.id: (self.controller.run_command(c), "break")[1]  # noqa: E731
                self.root.bind_all(sequence, handler)
                self.editor.text.bind(sequence, handler)  # before the Text class bindings (e.g. Ctrl+O)
        self.root.bind_all("<Control-equal>", lambda _event: (self.controller.run_command("zoom_in"), "break")[1])

    def _sync_view_menu(self) -> None:
        for panel_id, var in self.panel_vars.items():
            var.set(self.docks.layout.panels[panel_id].visible)

    # ------------------------------------------------------------------ MainWindowView

    def set_title(self, title: str) -> None:
        self.root.title(title)  # drawn by the window manager, which handles right-to-left text itself

    def set_status(self, text: str) -> None:
        self.status.configure(text=self.renderer.display(text))

    def set_file_label(self, text: str) -> None:
        self.file_label.configure(text=self.renderer.display(text))

    def set_running(self, running: bool) -> None:
        for command_id in ("compile", "run", "debug", "translate"):
            self.toolbar_buttons[command_id].state(["disabled"] if running else ["!disabled"])
        self.toolbar_buttons["stop"].state(["!disabled"] if running else ["disabled"])

    def set_samples(self, titles: list[str], current: int) -> None:
        self.sample_choice.configure(values=[self.renderer.display(title) for title in titles])
        if 0 <= current < len(titles):
            self.sample_choice.current(current)
        else:
            self.sample_choice.set("")

    def set_languages(self, names: list[str], current: int) -> None:
        self.language_choice.configure(values=[self.renderer.display(name) for name in names])
        self.language_choice.current(current)

    def select_panel_tab(self, tab: str) -> None:
        notebook, widget, panel_id = self.tabs[tab]
        self.docks.reveal(panel_id)
        notebook.select(widget)

    def apply_layout(self, layout: Layout) -> None:
        self.docks.apply(layout)

    def current_layout(self) -> Layout:
        return self.docks.current_layout()

    def ask_yes_no(self, title: str, message: str) -> bool:
        return bool(messagebox.askyesno(self.renderer.display(title), self.renderer.display(message), parent=self.root))

    def ask_open_path(self) -> Optional[str]:
        return filedialog.askopenfilename(parent=self.root, filetypes=[(tr("dialog.thunder_files"), "*.th"),
                                                                        (tr("dialog.all_files"), "*.*")]) or None

    def ask_save_path(self, suggested_name: str) -> Optional[str]:
        return filedialog.asksaveasfilename(parent=self.root, defaultextension=".th", initialfile=suggested_name,
                                            filetypes=[(tr("dialog.thunder_files"), "*.th")]) or None

    def show_info(self, title: str, message: str) -> None:
        messagebox.showinfo(self.renderer.display(title), self.renderer.display(message), parent=self.root)

    def show_error(self, title: str, message: str) -> None:
        messagebox.showerror(self.renderer.display(title), self.renderer.display(message), parent=self.root)

    def show_document(self, title: str, text: str) -> tk.Toplevel:
        window = tk.Toplevel(self.root)
        window.title(title)
        window.geometry("820x700")
        viewer = TkTextPanel(window, self.renderer, self.mono_font)
        viewer.pack(fill="both", expand=True)
        viewer.append(text)
        viewer.text.see("1.0")
        return window

    def close(self) -> None:
        self.docks.cancel_pending()
        for panel_id in list(self.docks.floating):
            self.root.wm_forget(self.docks.panels[panel_id])
        self.docks.floating.clear()
        for child in self.root.winfo_children():
            child.destroy()
        self.root.configure(menu="")
