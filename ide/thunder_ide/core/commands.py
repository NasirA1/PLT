"""The IDE's commands. GUI toolkits build their menus, toolbars and shortcuts from this table."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Command:
    id: str
    label: str            # translation key
    menu: str             # translation key of the menu it belongs to
    shortcut: str = ""    # toolkit-neutral, e.g. "Ctrl+S", "F5", "Shift+F5"
    toolbar: bool = False
    separator_before: bool = False
    checkable: bool = False  # a setting that is on or off, shown with a tick in the menu


COMMANDS: tuple[Command, ...] = (
    Command("new", "menu.new", "menu.file", "Ctrl+N"),
    Command("open", "menu.open", "menu.file", "Ctrl+O"),
    Command("save", "menu.save", "menu.file", "Ctrl+S"),
    Command("save_as", "menu.save_as", "menu.file"),
    Command("exit", "menu.exit", "menu.file", separator_before=True),
    Command("find", "menu.find", "menu.edit", "Ctrl+F"),
    Command("find_next", "menu.find_next", "menu.edit", "F3"),
    Command("find_previous", "menu.find_previous", "menu.edit", "Shift+F3"),
    Command("replace", "menu.replace", "menu.edit", "Ctrl+H"),
    Command("comment", "menu.comment", "menu.edit", "Ctrl+/", separator_before=True),
    Command("tidy", "menu.tidy", "menu.edit", "Ctrl+Shift+F"),
    Command("translate", "menu.translate", "menu.edit", "Ctrl+T", toolbar=True, separator_before=True),
    Command("compile", "menu.compile", "menu.build", "F7", toolbar=True),
    Command("run", "menu.run", "menu.build", "F5", toolbar=True),
    Command("debug", "menu.debug", "menu.build", "F9", toolbar=True),
    Command("stop", "menu.stop", "menu.build", "Shift+F5", toolbar=True),
    Command("package", "menu.package", "menu.build", separator_before=True),
    Command("fold", "menu.fold", "menu.view", "Ctrl+Shift+["),
    Command("unfold", "menu.unfold", "menu.view", "Ctrl+Shift+]"),
    Command("fold_all", "menu.fold_all", "menu.view"),
    Command("unfold_all", "menu.unfold_all", "menu.view"),
    Command("show_whitespace", "menu.show_whitespace", "menu.view", separator_before=True, checkable=True),
    Command("zoom_in", "menu.zoom_in", "menu.view", "Ctrl++", separator_before=True),
    Command("zoom_out", "menu.zoom_out", "menu.view", "Ctrl+-"),
    Command("reset_layout", "menu.reset_layout", "menu.view", separator_before=True),
    Command("reference", "menu.reference", "menu.help", "F1"),
    Command("library_reference", "menu.library_reference", "menu.help"),
    Command("graphics_reference", "menu.graphics_reference", "menu.help"),
    Command("bytecode_reference", "menu.bytecode_reference", "menu.help"),
    Command("native_reference", "menu.native_reference", "menu.help"),
    Command("packaging_reference", "menu.packaging_reference", "menu.help"),
    Command("display_reference", "menu.display_reference", "menu.help"),
    Command("about", "menu.about", "menu.help", separator_before=True),
)

MENUS = ("menu.file", "menu.edit", "menu.build", "menu.view", "menu.help")


def command(command_id: str) -> Command:
    for entry in COMMANDS:
        if entry.id == command_id:
            return entry
    raise KeyError(command_id)
