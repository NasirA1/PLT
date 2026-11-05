"""Dockable panels for Tk.

Tk has no docking framework, so this module builds one from three pieces:

* ``tk.PanedWindow``s for the dock areas (left and right side by side, bottom underneath),
  with draggable sashes between panels;
* a title bar on each ``DockPanel`` with buttons to collapse/expand, float/dock, move to
  another area and hide the panel;
* Tk's ``wm manage`` / ``wm forget`` commands, which turn a panel's frame into a separate
  top-level window and back again without re-creating its contents.

The arrangement itself is a toolkit-independent ``core.layout.Layout``.
"""

from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Callable, Optional

from thunder_ide.core.i18n import tr
from thunder_ide.core.layout import AREAS, Layout
from thunder_ide.tk.rendering import TextRenderer

HEADER_BACKGROUND = "#e4e7eb"
STRIP_WIDTH = 26


class Tooltip:
    """A small hint shown when the mouse rests on a widget."""

    def __init__(self, widget: tk.Widget, text: str):
        self.widget = widget
        self.text = text
        self.window: Optional[tk.Toplevel] = None
        widget.bind("<Enter>", lambda _event: self.show(), add="+")
        widget.bind("<Leave>", lambda _event: self.hide(), add="+")

    def show(self) -> None:
        self.hide()
        self.window = tk.Toplevel(self.widget)
        self.window.wm_overrideredirect(True)
        x = self.widget.winfo_rootx() + 10
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.window.wm_geometry(f"+{x}+{y}")
        tk.Label(self.window, text=self.text, background="#ffffe0", relief="solid", borderwidth=1,
                 padx=4, pady=2).pack()

    def hide(self) -> None:
        if self.window is not None:
            self.window.destroy()
            self.window = None


class DockPanel(tk.Frame):
    """A panel with a title bar. Put the content in ``body``."""

    def __init__(self, root: tk.Misc, manager: "DockManager", panel_id: str, title: str):
        # A plain tk.Frame (not ttk) because only frames can become windows with "wm manage".
        super().__init__(root, name=f"dock_{panel_id}", borderwidth=1, relief="groove")
        self.manager = manager
        self.panel_id = panel_id
        self.title = title
        renderer = manager.renderer
        rtl = renderer.right_to_left

        self.header = tk.Frame(self, background=HEADER_BACKGROUND)
        side, other = ("right", "left") if rtl else ("left", "right")
        self.collapse_button = self._button(self.header, "▾", lambda: manager.toggle_collapsed(panel_id),
                                            tr("dock.collapse"), side)
        self.title_label = tk.Label(self.header, text=renderer.display(title), background=HEADER_BACKGROUND,
                                    font=("TkDefaultFont", 9, "bold"), anchor="e" if rtl else "w")
        self.title_label.pack(side=side, fill="x", expand=True, padx=4)
        self.title_label.bind("<Double-Button-1>", lambda _event: manager.toggle_collapsed(panel_id))
        self.hide_button = self._button(self.header, "✕", lambda: manager.set_visible(panel_id, False),
                                        tr("dock.hide"), other)
        self.float_button = self._button(self.header, "⧉", lambda: manager.toggle_floating(panel_id),
                                         tr("dock.float"), other)
        self.menu_button = self._button(self.header, "☰", self._show_menu, tr("dock.move"), other)

        self.body = ttk.Frame(self)
        # Shown instead of the header and body when the whole side area is collapsed.
        self.strip = tk.Canvas(self, width=STRIP_WIDTH, background=HEADER_BACKGROUND, highlightthickness=0,
                               cursor="hand2")
        self.strip.bind("<Button-1>", lambda _event: manager.toggle_collapsed(panel_id))
        self.mode = ""
        self.set_mode("normal")

    def _button(self, parent: tk.Misc, text: str, command: Callable[[], None], tip: str, side: str) -> tk.Button:
        button = tk.Button(parent, text=text, command=command, relief="flat", borderwidth=0, padx=4, pady=0,
                           background=HEADER_BACKGROUND, activebackground="#cfd5dc", cursor="hand2")
        button.pack(side=side)
        Tooltip(button, self.manager.renderer.display(tip))
        return button

    def set_mode(self, mode: str) -> None:
        """"normal" (title bar + body), "collapsed" (title bar only) or "strip" (narrow vertical bar)."""
        if mode == self.mode:
            return
        self.mode = mode
        for widget in (self.header, self.body, self.strip):
            widget.pack_forget()
        if mode == "strip":
            self.strip.pack(fill="both", expand=True)
            self.strip.delete("all")
            self.strip.create_text(STRIP_WIDTH // 2, 10, text="▸", anchor="n")
            self.strip.create_text(STRIP_WIDTH // 2, 30, text=self.manager.renderer.display(self.title), angle=90,
                                   anchor="e", font=("TkDefaultFont", 9, "bold"))
            return
        self.header.pack(fill="x", side="top")
        if mode == "normal":
            self.body.pack(fill="both", expand=True)
        self.collapse_button.configure(text="▾" if mode == "normal" else "▸")

    def header_height(self) -> int:
        self.header.update_idletasks()
        return max(self.header.winfo_reqheight(), 22) + 4

    def _show_menu(self) -> None:
        menu = tk.Menu(self, tearoff=False)
        display = self.manager.renderer.display
        for area in AREAS:
            menu.add_command(label=display(tr(f"dock.to_{area}")), command=lambda a=area: self.manager.move(self.panel_id, a))
        menu.add_separator()
        menu.add_command(label=display(tr("dock.float")), command=lambda: self.manager.toggle_floating(self.panel_id))
        menu.add_command(label=display(tr("dock.hide")), command=lambda: self.manager.set_visible(self.panel_id, False))
        x = self.menu_button.winfo_rootx()
        y = self.menu_button.winfo_rooty() + self.menu_button.winfo_height()
        menu.tk_popup(x, y)
        self.manager.last_menu = menu


class DockManager:
    """Arranges DockPanels according to a core.layout.Layout."""

    def __init__(self, root: tk.Tk, parent: tk.Misc, renderer: TextRenderer, on_change: Callable[[], None]):
        self.root = root
        self.renderer = renderer
        self.on_change = on_change
        self.panels: dict[str, DockPanel] = {}
        self.layout = Layout.default()
        self.floating: set[str] = set()
        self.last_menu: Optional[tk.Menu] = None
        self._sash_attempts = 0
        self._sash_job: Optional[str] = None
        options = {"sashwidth": 6, "sashrelief": "flat", "borderwidth": 0, "background": "#d0d4d9"}
        self.outer = tk.PanedWindow(parent, orient="vertical", **options)
        self.row = tk.PanedWindow(root, orient="horizontal", **options)
        self.areas = {name: tk.PanedWindow(root, orient="vertical", **options) for name in AREAS}

    def add_panel(self, panel_id: str, title: str) -> DockPanel:
        panel = DockPanel(self.root, self, panel_id, title)
        self.panels[panel_id] = panel
        return panel

    # ------------------------------------------------------------------ public operations

    def apply(self, layout: Layout) -> None:
        self.layout = layout.copy()
        self._arrange()

    def current_layout(self) -> Layout:
        layout = self.layout.copy()
        self.root.update_idletasks()
        left, right = self.areas["left"], self.areas["right"]
        visible_sides = [a for a in (left, right) if str(a) in map(str, self.row.panes())]
        if len(visible_sides) == 2 and self.row.winfo_width() > 1:
            x = self.row.sash_coord(0)[0] / self.row.winfo_width()
            fraction = 1 - x if self.renderer.right_to_left else x
            if 0.1 <= fraction <= 0.9:
                layout.left_fraction = fraction
        if len(self.outer.panes()) == 2 and self.outer.winfo_height() > 1 and not self.layout.area_collapsed("bottom"):
            y = self.outer.sash_coord(0)[1] / self.outer.winfo_height()
            if 0.1 <= y <= 0.9:
                layout.top_fraction = y
        for panel_id in self.floating:
            layout.panels[panel_id].geometry = str(self.root.tk.call("wm", "geometry", self.panels[panel_id]))
        return layout

    def toggle_collapsed(self, panel_id: str) -> None:
        self.layout = self.current_layout()
        self.layout.toggle_collapsed(panel_id)
        self._arrange()

    def toggle_floating(self, panel_id: str) -> None:
        self.layout = self.current_layout()
        self.layout.set_floating(panel_id, not self.layout.panels[panel_id].floating)
        self._arrange()

    def move(self, panel_id: str, area: str) -> None:
        self.layout = self.current_layout()
        self.layout.move(panel_id, area)
        self._arrange()

    def set_visible(self, panel_id: str, visible: bool) -> None:
        self.layout = self.current_layout()
        self.layout.set_visible(panel_id, visible)
        self._arrange()

    def reveal(self, panel_id: str) -> None:
        """Makes sure a panel can be seen (shown, expanded)."""
        state = self.layout.panels[panel_id]
        if not state.visible or state.collapsed:
            self.layout = self.current_layout()
            self.layout.set_visible(panel_id, True)
            self._arrange()

    # ------------------------------------------------------------------ arranging

    def _arrange(self) -> None:
        """Rebuilds the paned windows from self.layout (cheap: the panels are only re-parented)."""
        for paned in [self.outer, self.row, *self.areas.values()]:
            for pane in list(paned.panes()):
                paned.forget(pane)
        self._update_floating_windows()
        for area_name in AREAS:
            self._fill_area(area_name)
        self._fill_window()
        self._sash_attempts = 0
        self._schedule_sashes(0)
        self.on_change()

    def _update_floating_windows(self) -> None:
        for panel_id, panel in self.panels.items():
            state = self.layout.panels[panel_id]
            wants_float = state.floating and state.visible
            if wants_float and panel_id not in self.floating:
                self._float(panel_id)
            elif not wants_float and panel_id in self.floating:
                self.root.wm_forget(panel)
                self.floating.discard(panel_id)

    def _fill_area(self, area_name: str) -> None:
        """Stacks an area's docked panels; collapsed panels keep only their title bar."""
        area = self.areas[area_name]
        strip = area_name != "bottom" and self.layout.area_collapsed(area_name)
        for name in self.layout.panels_in(area_name):
            panel = self.panels[name]
            collapsed = self.layout.panels[name].collapsed
            panel.set_mode("strip" if strip else ("collapsed" if collapsed else "normal"))
            if collapsed and not strip:
                height = panel.header_height()
                area.add(panel, height=height, minsize=height, stretch="never")
            else:
                area.add(panel, minsize=40 if strip else 60, stretch="always")

    def _fill_window(self) -> None:
        """Puts the side areas in a row (mirrored for right-to-left) above the bottom area."""
        layout = self.layout
        sides = [name for name in ("left", "right") if layout.panels_in(name)]
        if self.renderer.right_to_left:
            sides.reverse()
        for name in sides:
            collapsed = layout.area_collapsed(name)
            self.row.add(self.areas[name], width=STRIP_WIDTH + 4 if collapsed else None,
                         minsize=STRIP_WIDTH if collapsed else 120, stretch="never" if collapsed else "always")
        if sides:
            self.outer.add(self.row, minsize=120, stretch="always")
        if layout.panels_in("bottom"):
            collapsed = layout.area_collapsed("bottom")
            height = sum(self.panels[n].header_height() for n in layout.panels_in("bottom")) if collapsed else None
            self.outer.add(self.areas["bottom"], height=height, minsize=height or 60,
                           stretch="never" if collapsed else "always")

    def _schedule_sashes(self, delay_ms: int) -> None:
        self.cancel_pending()
        self._sash_job = self.root.after(delay_ms, self._place_sashes)

    def cancel_pending(self) -> None:
        """Cancels a scheduled sash placement (before the window is destroyed)."""
        if self._sash_job is not None:
            self.root.after_cancel(self._sash_job)
            self._sash_job = None

    def _float(self, panel_id: str) -> None:
        panel = self.panels[panel_id]
        panel.set_mode("normal")
        self.root.wm_manage(panel)
        tk_call = self.root.tk.call
        tk_call("wm", "title", panel, panel.title)  # window titles are drawn by the window manager
        state = self.layout.panels[panel_id]
        tk_call("wm", "geometry", panel, state.geometry or "700x450")
        command = panel.register(lambda: self.toggle_floating(panel_id))
        tk_call("wm", "protocol", panel, "WM_DELETE_WINDOW", command)
        self.floating.add(panel_id)

    def _place_sashes(self) -> None:
        """Restores the saved proportions between the areas (and between stacked panels)."""
        self._sash_job = None
        layout = self.layout
        try:
            self.root.update_idletasks()
        except tk.TclError:  # pragma: no cover - window closed meanwhile
            return
        if self.outer.winfo_height() < 100 and self._sash_attempts < 40:
            # The window is not on the screen yet, so its size is unknown: try again shortly.
            self._sash_attempts += 1
            self._schedule_sashes(50)
            return
        if len(self.row.panes()) == 2:
            names = [name for name in ("left", "right") if layout.panels_in(name)]
            if not any(layout.area_collapsed(name) for name in names):
                fraction = 1 - layout.left_fraction if self.renderer.right_to_left else layout.left_fraction
                self.row.sash_place(0, int(self.row.winfo_width() * fraction), 0)
        if len(self.outer.panes()) == 2 and not layout.area_collapsed("bottom"):
            self.outer.sash_place(0, 0, int(self.outer.winfo_height() * layout.top_fraction))
        for area_name, area in self.areas.items():
            names = layout.panels_in(area_name)
            if len(names) > 1 and not any(layout.panels[n].collapsed for n in names):
                height = area.winfo_height()
                for index in range(len(names) - 1):
                    area.sash_place(index, 0, int(height * (index + 1) / len(names)))
