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


COMMANDS: tuple[Command, ...] = (
    Command("new", "menu.new", "menu.file", "Ctrl+N"),
    Command("open", "menu.open", "menu.file", "Ctrl+O"),
    Command("save", "menu.save", "menu.file", "Ctrl+S"),
    Command("save_as", "menu.save_as", "menu.file"),
    Command("exit", "menu.exit", "menu.file", separator_before=True),
    Command("translate", "menu.translate", "menu.edit", "Ctrl+T", toolbar=True),
    Command("compile", "menu.compile", "menu.build", "F7", toolbar=True),
    Command("run", "menu.run", "menu.build", "F5", toolbar=True),
    Command("debug", "menu.debug", "menu.build", "F9", toolbar=True),
    Command("stop", "menu.stop", "menu.build", "Shift+F5", toolbar=True),
    Command("zoom_in", "menu.zoom_in", "menu.view", "Ctrl++"),
    Command("zoom_out", "menu.zoom_out", "menu.view", "Ctrl+-"),
    Command("reset_layout", "menu.reset_layout", "menu.view", separator_before=True),
    Command("reference", "menu.reference", "menu.help", "F1"),
    Command("library_reference", "menu.library_reference", "menu.help"),
    Command("graphics_reference", "menu.graphics_reference", "menu.help"),
    Command("bytecode_reference", "menu.bytecode_reference", "menu.help"),
    Command("about", "menu.about", "menu.help", separator_before=True),
)

MENUS = ("menu.file", "menu.edit", "menu.build", "menu.view", "menu.help")


def command(command_id: str) -> Command:
    for entry in COMMANDS:
        if entry.id == command_id:
            return entry
    raise KeyError(command_id)
