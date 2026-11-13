"""The fonts a program is drawn in, as Qt sees them (the model is core.fonts).

Two things about Qt's fonts shape this module:

* **Fallback goes by the first font that has the character.** A QFont with the families
  ``[code font, Pashto font]`` draws Latin from the code font and Pashto from the Pashto font
  - but only if the code font has no Arabic glyphs; otherwise it draws Pashto itself. So the code
  fonts offered are the monospaced ones *without* the Arabic writing system.
* **Qt remembers which font it fell back to.** It caches the fallback per primary family and
  script, so after ``[JetBrains Mono, Vazirmatn]`` has been used once, ``[JetBrains Mono,
  Scheherazade New]`` still draws Pashto in Vazirmatn. Registering the application fonts again
  makes Qt drop that cache, which is what ``reload_bundled_fonts`` is for: it runs whenever the
  Pashto font is changed.
"""

from __future__ import annotations

from PySide6.QtGui import QFont, QFontDatabase

from thunder_ide.core.fonts import DEFAULT_CODE_FONT, DEFAULT_PASHTO_FONT, FontChoice, bundled_font_files, ordered_choices

_loaded: list[int] = []
_bundled_families: list[str] = []


def load_bundled_fonts() -> list[str]:
    """Registers the fonts the IDE ships (once) and returns their family names."""
    if not _loaded:
        for path in bundled_font_files():
            identifier = QFontDatabase.addApplicationFont(str(path))
            if identifier >= 0:
                _loaded.append(identifier)
                for family in QFontDatabase.applicationFontFamilies(identifier):
                    if family not in _bundled_families:
                        _bundled_families.append(family)
    return list(_bundled_families)


def reload_bundled_fonts() -> None:
    """Registers the shipped fonts again, which makes Qt forget the fallbacks it cached."""
    for identifier in _loaded:
        QFontDatabase.removeApplicationFont(identifier)
    _loaded.clear()
    _bundled_families.clear()
    load_bundled_fonts()


def has_arabic(family: str) -> bool:
    return QFontDatabase.WritingSystem.Arabic in QFontDatabase.writingSystems(family)


def has_latin(family: str) -> bool:
    """False for symbol and emoji fonts, which the font database may call fixed-pitch."""
    return QFontDatabase.WritingSystem.Latin in QFontDatabase.writingSystems(family)


def available_fonts() -> tuple[list[str], list[str]]:
    """(code fonts, Pashto fonts) that are installed or shipped, best first."""
    bundled = load_bundled_fonts()
    families = QFontDatabase.families()
    shipped_pashto = [family for family in bundled if has_arabic(family)]
    # The shipped code fonts are monospaced by design. (Qt does not flag the variable build of
    # JetBrains Mono as fixed-pitch, so they are taken for what they are, not by that flag.)
    shipped_code = [family for family in bundled if not has_arabic(family)]
    code = shipped_code + [family for family in families
                           if QFontDatabase.isFixedPitch(family) and not has_arabic(family) and has_latin(family)
                           and not family.startswith("@")]
    pashto = shipped_pashto + [family for family in families
                               if has_arabic(family) and not QFontDatabase.isFixedPitch(family)
                               and not family.startswith("@")]
    return (ordered_choices(code, [DEFAULT_CODE_FONT] + shipped_code),
            ordered_choices(pashto, [DEFAULT_PASHTO_FONT] + shipped_pashto))


def program_font(choice: FontChoice, size: int) -> QFont:
    """The font a program is drawn in: the code font, then the Pashto font for what it lacks."""
    font = QFont()
    font.setFamilies(choice.families())
    font.setPointSize(max(6, size))
    # No monospace style hint: it would steer Qt's fallback for Arabic towards monospaced fonts
    # (DejaVu Sans Mono), which is exactly what the explicit Pashto font is there to avoid.
    return font
