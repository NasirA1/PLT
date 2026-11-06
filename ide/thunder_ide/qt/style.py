"""Colours and fonts shared by the Qt widgets."""

from __future__ import annotations

from PySide6.QtGui import QFont, QFontDatabase

MONOSPACE_FAMILIES = ("Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono",
                      "Ubuntu Mono", "Menlo", "Courier New")

# Colours of the token categories reported by the compiler (tables).
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

# Colours of the editor's highlight classes (core.highlight).
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

EDITOR_BACKGROUND = "#ffffff"
GUTTER_BACKGROUND = "#f3f3f3"
GUTTER_FOREGROUND = "#8a8a8a"
CURRENT_LINE_BACKGROUND = "#d8f0d8"
SPAN_BACKGROUND = "#fff2a8"
ERROR_BACKGROUND = "#ffe6e6"
ERROR_FOREGROUND = "#c00000"
SELECTION_BACKGROUND = "#add6ff"


def pick_monospace_family() -> str:
    available = set(QFontDatabase.families())
    for family in MONOSPACE_FAMILIES:
        if family in available:
            return family
    return QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont).family()


def monospace_font(size: int) -> QFont:
    font = QFont(pick_monospace_family(), size)
    font.setStyleHint(QFont.StyleHint.Monospace)
    return font
