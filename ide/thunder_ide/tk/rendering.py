"""How right-to-left text is drawn with Tk.

Tk on X11 (Linux) draws every character left to right and never joins Arabic letters, so the
IDE gives it text that is already in visual order and shaped (see core.bidi). On Windows and
macOS the operating system's text engine reorders and joins each piece of text Tk draws, so
only the order of differently styled pieces needs fixing there.
"""

from __future__ import annotations

import tkinter as tk
from typing import Optional

from thunder_ide.core import bidi


class TextRenderer:
    def __init__(self, root: tk.Misc, right_to_left: bool):
        self.right_to_left = right_to_left
        self.manual = str(root.tk.call("tk", "windowingsystem")) == "x11"

    def display(self, text: str, right_to_left: Optional[bool] = None) -> str:
        """A single-style string as it should be handed to Tk (labels, menus, table cells)."""
        if not bidi.has_rtl(text):
            return text
        if not self.manual:
            return text
        rtl = self.right_to_left if right_to_left is None else right_to_left
        return "\n".join(bidi.visual_text(line, rtl) for line in text.split("\n"))

    def line_chunks(self, text: str, styles: list[Optional[str]],
                    right_to_left: Optional[bool] = None) -> tuple[list[bidi.Chunk], bool]:
        """A styled line split into chunks in display order, and whether it reads right to left."""
        rtl = self.right_to_left if right_to_left is None else right_to_left
        return bidi.render_line(text, styles, rtl, shape_letters=self.manual, reverse_rtl_text=self.manual)
