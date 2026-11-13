"""The fonts a program is drawn in: one for code, one for Pashto.

A Thunder program mixes two scripts. Latin names, digits and operators want a monospaced code
font; Pashto wants a font made for the Arabic script, with every Pashto letter and proper
joining. The editor therefore uses a *pair*: the toolkit is asked for the code font first and
the Pashto font second, and draws each character in the first one that has it.

That only works if the code font has **no Arabic glyphs** of its own - otherwise it draws Pashto
too, and the Pashto font is never reached (DejaVu Sans Mono and Courier New both have them; their
monospaced Arabic is what made Pashto hard to read). The toolkit decides which installed fonts
qualify; this module holds the choice and the fonts the IDE ships (``ide/thunder_ide/fonts``).
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

FONTS_DIRECTORY = Path(__file__).resolve().parent.parent / "fonts"

#: The fonts chosen when the user has not chosen any (both ship with the IDE).
DEFAULT_CODE_FONT = "JetBrains Mono"
DEFAULT_PASHTO_FONT = "Vazirmatn"


@dataclass(frozen=True)
class FontChoice:
    """The two fonts the editor and the code panels are drawn in."""

    code: str = DEFAULT_CODE_FONT
    pashto: str = DEFAULT_PASHTO_FONT

    def families(self) -> list[str]:
        """The families in the order the toolkit should try them: code first, then Pashto."""
        result = []
        for family in (self.code, self.pashto):
            if family and family not in result:
                result.append(family)
        return result


def bundled_font_files() -> list[Path]:
    """Every font file the IDE ships, in a stable order."""
    if not FONTS_DIRECTORY.is_dir():
        return []
    return sorted(path for path in FONTS_DIRECTORY.rglob("*") if path.suffix.lower() in (".ttf", ".otf"))


def ordered_choices(available: list[str], preferred: list[str]) -> list[str]:
    """The fonts to offer: the ones the IDE prefers (when present) first, then the rest, sorted.

    ``available`` is what the toolkit found installed (including the bundled fonts); duplicates
    are dropped and the order is stable, so the menu does not reshuffle between sessions.
    """
    present = set(available)
    first: list[str] = []
    for family in preferred:
        if family in present and family not in first:
            first.append(family)
    rest = sorted(present - set(first), key=str.casefold)
    return first + rest


def resolve(choice: FontChoice, code_fonts: list[str], pashto_fonts: list[str]) -> FontChoice:
    """The choice, with a font that is no longer installed replaced by the default (or the first).

    Settings travel between machines; a font chosen on one may be missing on another.
    """
    code = choice.code if choice.code in code_fonts else (
        DEFAULT_CODE_FONT if DEFAULT_CODE_FONT in code_fonts else (code_fonts[0] if code_fonts else ""))
    pashto = choice.pashto if choice.pashto in pashto_fonts else (
        DEFAULT_PASHTO_FONT if DEFAULT_PASHTO_FONT in pashto_fonts else (pashto_fonts[0] if pashto_fonts else ""))
    return FontChoice(code, pashto)
