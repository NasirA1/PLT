"""The Qt main window (MainWindowView): menus, toolbar, dock widgets and status bar.

Docking comes from ``QMainWindow``/``QDockWidget``: every panel can be dragged to another
area, tabbed with another panel, floated in its own window or closed (the View menu shows it
again). The arrangement is saved in the toolkit-neutral ``core.layout.Layout`` together with
Qt's exact state.
"""

from __future__ import annotations

import base64
from typing import TYPE_CHECKING, Optional

from PySide6.QtCore import QByteArray, Qt, QTimer
from PySide6.QtGui import QAction, QCloseEvent, QFont, QKeySequence
from PySide6.QtWidgets import (QComboBox, QDialog, QDockWidget, QFileDialog, QFrame, QHBoxLayout, QInputDialog, QLabel,
                               QLineEdit,
                               QMainWindow, QMessageBox, QPushButton, QScrollArea, QSizePolicy, QTabWidget, QToolBar, QVBoxLayout,
                               QWidget)

from thunder_ide.core import layout as layout_ids
from thunder_ide.core.commands import COMMANDS, MENUS
from thunder_ide.core.i18n import tr
from thunder_ide.core.layout import AREAS, Layout
from thunder_ide.core.viewmodels import tree_specs
from thunder_ide.qt import style
from thunder_ide.qt.editor import CodeEditor
from thunder_ide.qt.findbar import FindBar
from thunder_ide.qt.help_viewer import HelpViewer
from thunder_ide.qt.panels import QtBitsPanel, QtCanvasPanel, QtTextPanel, QtTreePanel, QtVmPanel

if TYPE_CHECKING:  # pragma: no cover
    from thunder_ide.core.controller import IdeController

EDUCATION_TABS = ("overview", "tokens", "ast", "symbols", "bytecode", "binary", "vm", "bits")
OUTPUT_TABS = ("output", "problems", "log")
DOCK_AREAS = {
    "left": Qt.DockWidgetArea.LeftDockWidgetArea,
    "right": Qt.DockWidgetArea.RightDockWidgetArea,
    "bottom": Qt.DockWidgetArea.BottomDockWidgetArea,
}


def key_sequences(shortcut: str) -> list[QKeySequence]:
    """Converts "Ctrl+Shift+F" / "F5" / "Ctrl++" into key sequences (Ctrl++ also accepts Ctrl+=)."""
    if not shortcut:
        return []
    sequences = [QKeySequence(shortcut)]
    if shortcut == "Ctrl++":
        sequences.append(QKeySequence("Ctrl+="))
    if shortcut == "Ctrl+Shift+Z":
        sequences.append(QKeySequence("Ctrl+Y"))  # the other common Redo key
    return sequences


class QtMainWindow(QMainWindow):
    def __init__(self, controller: "IdeController", right_to_left: bool, font_size: int):
        super().__init__()
        self.controller = controller
        self.rtl = right_to_left
        self._closing = False
        self.documents: list[QDialog] = []
        self.setLayoutDirection(Qt.LayoutDirection.RightToLeft if right_to_left else Qt.LayoutDirection.LeftToRight)
        self.setWindowTitle(tr("app.title"))
        self.resize(1400, 860)
        self.setMinimumSize(900, 600)
        self.setDockNestingEnabled(True)
        self.setDockOptions(QMainWindow.DockOption.AnimatedDocks | QMainWindow.DockOption.AllowNestedDocks
                            | QMainWindow.DockOption.AllowTabbedDocks)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.mono_family = style.pick_monospace_family()
        self.mono_font = QFont(self.mono_family, max(8, font_size - 1))
        self.heading_font = QFont(self.font())
        self.heading_font.setBold(True)
        self.heading_font.setPointSize(self.font().pointSize() + 1)
        self.actions_by_id: dict[str, QAction] = {}
        self.docks: dict[str, QDockWidget] = {}
        self.panel_actions: dict[str, QAction] = {}
        self._create_panels(font_size)
        # Every panel is a dock widget, so the window has no real central widget. Without one, Qt
        # lets the separator above the bottom area move only one way: the Output panel could be
        # enlarged but never made smaller again. An empty central widget of zero width (between
        # the left and right areas) restores normal dragging in both directions.
        placeholder = QWidget()
        placeholder.setObjectName("central_placeholder")
        placeholder.setMaximumWidth(0)
        self.setCentralWidget(placeholder)
        self._create_menus()
        self._create_toolbar()
        self.statusBar().showMessage("")

    # ------------------------------------------------------------------ construction

    def _create_menus(self) -> None:
        menus = {key: self.menuBar().addMenu(tr(key)) for key in MENUS}
        for entry in COMMANDS:
            menu = menus[entry.menu]
            if entry.separator_before:
                menu.addSeparator()
            action = QAction(tr(entry.label), self)
            action.setShortcuts(key_sequences(entry.shortcut))
            # Clipboard-style commands keep their shortcut as a label only (WidgetShortcut on a
            # menu action never fires): the focused widget itself handles Ctrl+C and friends, so
            # copying from the Output panel or the find bar still works.
            action.setShortcutContext(Qt.ShortcutContext.ApplicationShortcut if entry.app_shortcut
                                      else Qt.ShortcutContext.WidgetShortcut)
            action.setCheckable(entry.checkable)
            action.triggered.connect(lambda _checked=False, c=entry.id: self.controller.run_command(c))
            menu.addAction(action)
            self.actions_by_id[entry.id] = action
        view = menus["menu.view"]
        view.addSeparator()
        panels = view.addMenu(tr("menu.panels"))
        for panel_id in layout_ids.PANELS:
            action = self.docks[panel_id].toggleViewAction()
            action.setText(tr(f"panel.{panel_id}"))
            panels.addAction(action)
            self.panel_actions[panel_id] = action

    def _create_toolbar(self) -> None:
        bar = QToolBar("main")
        bar.setObjectName("main_toolbar")
        bar.setMovable(False)
        bar.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
        self.addToolBar(bar)
        bar.addWidget(QLabel(tr("toolbar.sample") + " "))
        self.sample_choice = QComboBox()
        self.sample_choice.setMinimumContentsLength(36)
        self.sample_choice.activated.connect(self.controller.on_sample_selected)
        bar.addWidget(self.sample_choice)
        self.toolbar_actions: dict[str, QAction] = {}
        for entry in COMMANDS:
            if not entry.toolbar:
                continue
            if entry.id == "translate":
                bar.addSeparator()
            action = QAction(tr(f"toolbar.{entry.id}"), self)
            action.triggered.connect(lambda _checked=False, c=entry.id: self.controller.run_command(c))
            bar.addAction(action)
            self.toolbar_actions[entry.id] = action
            button = bar.widgetForAction(action)
            if button is not None and entry.id in ("compile", "run"):
                font = QFont(button.font())
                font.setBold(True)
                button.setFont(font)
        self.toolbar_actions["stop"].setEnabled(False)
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        bar.addWidget(spacer)
        self.file_label = QLabel("")
        bar.addWidget(self.file_label)
        bar.addWidget(QLabel("   " + tr("toolbar.language") + " "))
        self.language_choice = QComboBox()
        self.language_choice.activated.connect(self.controller.on_language_selected)
        bar.addWidget(self.language_choice)

    def _dock(self, panel_id: str, widget: QWidget, area: str) -> QDockWidget:
        dock = QDockWidget(tr(f"panel.{panel_id}"), self)
        dock.setObjectName(panel_id)
        dock.setWidget(widget)
        dock.setFeatures(QDockWidget.DockWidgetFeature.DockWidgetMovable | QDockWidget.DockWidgetFeature.DockWidgetFloatable
                         | QDockWidget.DockWidgetFeature.DockWidgetClosable)
        self.addDockWidget(DOCK_AREAS[area], dock)
        self.docks[panel_id] = dock
        return dock

    def _create_panels(self, font_size: int) -> None:
        controller = self.controller
        specs = tree_specs()
        self.editor = CodeEditor(self.mono_family, font_size, controller.on_text_changed)
        self.editor.setObjectName("code_editor")
        # The editor and the find bar share one dock; the bar is hidden until a command opens it.
        self.find_bar = FindBar(controller.find_in_editor, controller.replace_in_editor)
        editor_page = QWidget()
        editor_layout = QVBoxLayout(editor_page)
        editor_layout.setContentsMargins(0, 0, 0, 0)
        editor_layout.setSpacing(0)
        editor_layout.addWidget(self.editor)
        editor_layout.addWidget(self.find_bar)
        self._dock(layout_ids.EDITOR, editor_page, "left")

        self.edu_tabs = QTabWidget()
        self.edu_tabs.setDocumentMode(True)
        overview_styles = {"title": {"bold": True, "size": self.heading_font.pointSize() + 2},
                           "heading": {"bold": True, "size": self.heading_font.pointSize()},
                           "ok": {"foreground": "#067d17"}, "failed": {"foreground": "#c00000"},
                           "skipped": {"foreground": "#8c8c8c"}}
        self.overview = QtTextPanel(self.font(), styles=overview_styles)
        select = lambda panel: (lambda row: controller.on_row_selected(panel, row))  # noqa: E731
        self.tokens = QtTreePanel(specs["tokens"], select("tokens"))
        self.ast = QtTreePanel(specs["ast"], select("ast"))
        self.symbols = QtTreePanel(specs["symbols"], select("symbols"))
        self.bytecode = QtTreePanel(specs["bytecode"], select("bytecode"), self.mono_font)
        self.binary = QtTextPanel(self.mono_font, wrap=False,
                                  styles={f"section{i}": {"background": c} for i, c in enumerate(style.SECTION_COLORS)})
        self.vm = QtVmPanel(self.mono_font, controller.on_vm_navigate)
        self.bits = QtBitsPanel(self.mono_font)
        self.tabs: dict[str, tuple[QTabWidget, QWidget, str]] = {}
        for tab in EDUCATION_TABS:
            widget = getattr(self, tab)
            if tab in ("vm", "bits"):
                # The debugger's stacked tables need about 400 pixels, and the Bits tab's rows of
                # 64 bits are wide; in a scroll area they no longer stop the panels around them
                # from being resized (the Output panel could otherwise not be made taller than
                # about a third of the window, and the right-hand panel could not be narrowed).
                widget = self._scrollable(widget)
            self.edu_tabs.addTab(widget, tr(f"tab.{tab}"))
            self.tabs[tab] = (self.edu_tabs, widget, layout_ids.EDUCATION)
        self._dock(layout_ids.EDUCATION, self.edu_tabs, "right")

        self.bottom_tabs = QTabWidget()
        self.bottom_tabs.setDocumentMode(True)
        self.output = QtTextPanel(self.mono_font, background="#1e1e1e", foreground="#e8e8e8",
                                  styles={"stderr": {"foreground": "#ff8080"}, "info": {"foreground": "#7fb2ff"},
                                          "success": {"foreground": "#8fd18f"},
                                          "input": {"foreground": "#ffd75f", "bold": True}})
        self.output_page = self._output_page(controller)
        self.problems = QtTreePanel(specs["problems"], select("problems"))
        self.log = QtTextPanel(self.mono_font, wrap=False,
                               styles={"stderr": {"foreground": "#c00000"}, "command": {"foreground": "#0033b3"}})
        for tab in OUTPUT_TABS:
            widget = self.output_page if tab == "output" else getattr(self, tab)
            self.bottom_tabs.addTab(widget, tr(f"tab.{tab}"))
            self.tabs[tab] = (self.bottom_tabs, widget, layout_ids.OUTPUT)
        self._dock(layout_ids.OUTPUT, self.bottom_tabs, "bottom")

        # Graphics programs draw on the Canvas panel, a tab next to "Under the bonnet".
        self.canvas = QtCanvasPanel(controller.on_canvas_key, controller.on_canvas_mouse)
        self._dock(layout_ids.CANVAS, self.canvas, "right")
        self.tabifyDockWidget(self.docks[layout_ids.EDUCATION], self.docks[layout_ids.CANVAS])
        self.docks[layout_ids.EDUCATION].raise_()
        self.tabs["canvas"] = (None, self.canvas, layout_ids.CANVAS)

    @staticmethod
    def _scrollable(widget: QWidget) -> QScrollArea:
        """Wraps a widget so that it scrolls instead of imposing its minimum size on the window."""
        area = QScrollArea()
        area.setWidget(widget)
        area.setWidgetResizable(True)
        area.setFrameShape(QFrame.Shape.NoFrame)
        return area

    def _output_page(self, controller: "IdeController") -> QWidget:
        """The program's output with an input line beneath it: what is typed there while the
        program runs is what input() reads."""
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)
        layout.addWidget(self.output, 1)
        row = QHBoxLayout()
        row.setContentsMargins(4, 0, 4, 2)
        row.addWidget(QLabel(tr("output.input_label")))
        self.input_line = QLineEdit()
        self.input_line.setFont(self.mono_font)
        self.input_line.setPlaceholderText(tr("output.input_hint"))
        self.send_button = QPushButton(tr("output.send"))
        self.end_input_button = QPushButton(tr("output.end_input"))

        def send() -> None:
            text = self.input_line.text()
            self.input_line.clear()
            controller.send_input(text)

        self.input_line.returnPressed.connect(send)
        self.send_button.clicked.connect(send)
        self.end_input_button.clicked.connect(controller.end_input)
        row.addWidget(self.input_line, 1)
        row.addWidget(self.send_button)
        row.addWidget(self.end_input_button)
        layout.addLayout(row)
        self._set_input_enabled(False)
        return page

    def _set_input_enabled(self, enabled: bool) -> None:
        for widget in (self.input_line, self.send_button, self.end_input_button):
            widget.setEnabled(enabled)

    # ------------------------------------------------------------------ MainWindowView

    def set_title(self, title: str) -> None:
        self.setWindowTitle(title)

    def set_status(self, text: str) -> None:
        self.statusBar().showMessage(text)

    def set_file_label(self, text: str) -> None:
        self.file_label.setText(text)

    def show_find(self, with_replace: bool) -> None:
        start, end = self.editor.selection()
        self.find_bar.open_bar(with_replace, self.editor.get_text()[min(start, end):max(start, end)])

    def set_command_checked(self, command_id: str, checked: bool) -> None:
        action = self.actions_by_id.get(command_id)
        if action is not None and action.isCheckable() and action.isChecked() != checked:
            action.setChecked(checked)

    def set_running(self, running: bool) -> None:
        for command_id in ("compile", "run", "debug", "translate"):
            self.toolbar_actions[command_id].setEnabled(not running)
            self.actions_by_id[command_id].setEnabled(not running)
        self.toolbar_actions["stop"].setEnabled(running)
        self.actions_by_id["stop"].setEnabled(running)
        self._set_input_enabled(running)
        if running:
            self.input_line.setFocus()  # a program that asks for input can be answered at once

    def set_samples(self, titles: list[str], current: int) -> None:
        self.sample_choice.blockSignals(True)
        self.sample_choice.clear()
        self.sample_choice.addItems(titles)
        self.sample_choice.setCurrentIndex(current if 0 <= current < len(titles) else -1)
        self.sample_choice.blockSignals(False)

    def set_languages(self, names: list[str], current: int) -> None:
        self.language_choice.blockSignals(True)
        self.language_choice.clear()
        self.language_choice.addItems(names)
        self.language_choice.setCurrentIndex(current)
        self.language_choice.blockSignals(False)

    def select_panel_tab(self, tab: str) -> None:
        notebook, widget, panel_id = self.tabs[tab]
        dock = self.docks[panel_id]
        dock.show()
        dock.raise_()
        if notebook is not None:  # (the canvas is a panel of its own)
            notebook.setCurrentWidget(widget)

    def set_panel_visible(self, panel: str, visible: bool) -> None:
        self.docks[panel].setVisible(visible)

    def apply_layout(self, layout: Layout) -> None:
        if layout.toolkit_state:
            try:
                restored = self.restoreState(QByteArray.fromBase64(layout.toolkit_state.encode("ascii")))
            except (ValueError, TypeError):
                restored = False
            if restored:
                for panel_id in layout_ids.PANELS:
                    self.docks[panel_id].setVisible(layout.panels[panel_id].visible)
                return
        for area in AREAS:
            for panel_id in layout.panels_in(area, include_hidden=True):
                dock = self.docks[panel_id]
                dock.setFloating(False)
                self.addDockWidget(DOCK_AREAS[area], dock)
        for panel_id, state in layout.panels.items():
            dock = self.docks[panel_id]
            if state.floating:
                dock.setFloating(True)
            dock.setVisible(state.visible)
        for panel_id, state in layout.panels.items():
            partner = layout.panels.get(state.tab_with)
            if partner is not None and partner.area == state.area and not (partner.floating or state.floating):
                self.tabifyDockWidget(self.docks[state.tab_with], self.docks[panel_id])
                self.docks[state.tab_with].raise_()
        sizes = layout.copy()
        self._apply_sizes(sizes)
        # Qt distributes the space only once the window is laid out: apply the sizes again then.
        QTimer.singleShot(0, lambda: self._apply_sizes(sizes))

    def _apply_sizes(self, layout: Layout) -> None:
        if self._closing:
            return
        width, height = max(self.width(), 900), max(self.height(), 600)
        left = [self.docks[p] for p in layout.panels_in("left")]
        right = [self.docks[p] for p in layout.panels_in("right")]
        bottom = [self.docks[p] for p in layout.panels_in("bottom")]
        if left and right:
            # The fractions describe how the panels share the space (not the whole window).
            shared = left[0].width() + right[0].width() if self.isVisible() else width
            self.resizeDocks([left[0], right[0]], [int(shared * layout.left_fraction),
                                                   int(shared * (1 - layout.left_fraction))],
                             Qt.Orientation.Horizontal)
        top = left or right
        if top and bottom:
            shared = top[0].height() + bottom[0].height() if self.isVisible() else height
            self.resizeDocks([top[0], bottom[0]], [int(shared * layout.top_fraction),
                                                   int(shared * (1 - layout.top_fraction))],
                             Qt.Orientation.Vertical)

    def current_layout(self) -> Layout:
        layout = Layout.default()
        # Panels sharing their space as tabs: each remembers the partner that comes first in PANELS.
        for panel_id in layout_ids.PANELS:
            partners = [other.objectName() for other in self.tabifiedDockWidgets(self.docks[panel_id])]
            earlier = [name for name in partners if layout_ids.PANELS.index(name) < layout_ids.PANELS.index(panel_id)]
            layout.panels[panel_id].tab_with = earlier[0] if earlier else ""

        def position(dock: QDockWidget, area: str) -> tuple:
            # A tab is ordered where its group is, after the group's first panel.
            anchor = self.docks.get(layout.panels[dock.objectName()].tab_with, dock)
            place = (anchor.y(), anchor.x()) if area != "bottom" else (anchor.x(), anchor.y())
            return place, layout_ids.PANELS.index(dock.objectName())

        for area in AREAS:
            docked = []
            for panel_id in layout_ids.PANELS:
                dock = self.docks[panel_id]
                state = layout.panels[panel_id]
                state.visible = not dock.isHidden()
                state.floating = dock.isFloating()
                if state.floating:
                    continue
                for name, value in DOCK_AREAS.items():
                    if self.dockWidgetArea(dock) == value:
                        state.area = name
                if state.area == area:
                    docked.append(dock)
            docked.sort(key=lambda dock, a=area: position(dock, a))
            for order, dock in enumerate(docked):
                layout.panels[dock.objectName()].order = order
        editor, education, output = (self.docks[layout_ids.EDITOR], self.docks[layout_ids.EDUCATION],
                                     self.docks[layout_ids.OUTPUT])
        docked = lambda dock: not dock.isFloating() and not dock.isHidden()  # noqa: E731
        if docked(editor) and docked(education) and editor.width() + education.width() > 0:
            layout.left_fraction = min(0.9, max(0.1, editor.width() / (editor.width() + education.width())))
        if docked(editor) and docked(output) and editor.height() + output.height() > 0:
            layout.top_fraction = min(0.9, max(0.1, editor.height() / (editor.height() + output.height())))
        layout.toolkit_state = base64.b64encode(bytes(self.saveState().data())).decode("ascii")
        return layout

    def ask_yes_no(self, title: str, message: str) -> bool:
        # Not QMessageBox.question: its standard Yes/No buttons are not translated into the
        # interface language (Qt's own translations are not shipped), so the buttons are made
        # by hand with the interface's words (Yes/No, هو/نه).
        box = QMessageBox(QMessageBox.Icon.Question, title, message, QMessageBox.StandardButton.NoButton, self)
        yes = box.addButton(tr("dialog.yes"), QMessageBox.ButtonRole.YesRole)
        box.addButton(tr("dialog.no"), QMessageBox.ButtonRole.NoRole)
        box.setDefaultButton(yes)
        box.exec()
        return box.clickedButton() is yes

    def ask_open_path(self) -> Optional[str]:
        path, _filter = QFileDialog.getOpenFileName(
            self, tr("menu.open"), "", f"{tr('dialog.thunder_files')} (*.th);;{tr('dialog.all_files')} (*)")
        return path or None

    def ask_save_path(self, suggested_name: str, extension: str = ".th") -> Optional[str]:
        # A package has no fixed extension (none at all on Linux and macOS), so it is offered
        # under "all files" rather than the Thunder source filter.
        selector = (f"{tr('dialog.thunder_files')} (*.th)" if extension == ".th"
                    else f"{tr('dialog.all_files')} (*)")
        title = tr("menu.save_as") if extension == ".th" else tr("menu.package")
        path, _filter = QFileDialog.getSaveFileName(self, title, suggested_name, selector)
        if path and extension and "." not in path.rsplit("/", 1)[-1]:
            path += extension
        return path or None

    def ask_go_to_line(self, current: int, last: int) -> Optional[int]:
        value, accepted = QInputDialog.getInt(self, tr("dialog.go_to_line_title"), tr("dialog.go_to_line_prompt", last=last),
                                              max(1, min(current, last)), 1, max(1, last))
        return int(value) if accepted else None

    def show_info(self, title: str, message: str) -> None:
        QMessageBox.information(self, title, message)

    def show_error(self, title: str, message: str) -> None:
        QMessageBox.critical(self, title, message)

    def show_help(self, documents: list, current: str) -> HelpViewer:
        viewer = HelpViewer(self, documents)
        viewer.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        viewer.open_document(current)
        viewer.show()
        viewer.raise_()
        self.documents.append(viewer)
        viewer.destroyed.connect(lambda _object=None, d=viewer: d in self.documents and self.documents.remove(d))
        return viewer

    def closeEvent(self, event: QCloseEvent) -> None:
        if self._closing:
            event.accept()
            return
        event.ignore()
        self.controller.quit()  # asks about unsaved changes, then calls close()

    def close(self) -> bool:  # type: ignore[override]
        self._closing = True
        for dialog in list(self.documents):
            try:
                dialog.close()
            except RuntimeError:  # pragma: no cover - already deleted by Qt
                pass
        self.documents.clear()
        return super().close()
