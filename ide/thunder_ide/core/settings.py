"""User settings (interface language, font size, panel layout), saved as JSON between sessions."""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Mapping, Optional

from thunder_ide.core.layout import Layout


def default_settings_path(environ: Optional[Mapping[str, str]] = None, windows: Optional[bool] = None) -> Path:
    """%APPDATA%\\Thunder\\ide.json on Windows, ~/.config/thunder/ide.json elsewhere.

    The THUNDER_IDE_SETTINGS environment variable overrides the location (used by the tests).
    """
    environ = os.environ if environ is None else environ
    windows = os.name == "nt" if windows is None else windows
    if environ.get("THUNDER_IDE_SETTINGS"):
        return Path(environ["THUNDER_IDE_SETTINGS"])
    if windows and environ.get("APPDATA"):
        return Path(environ["APPDATA"]) / "Thunder" / "ide.json"
    base = environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "thunder" / "ide.json"


DEFAULT_FONT_SIZE = 11  # the 100% zoom level of the editor font


@dataclass
class Settings:
    language: str = "en"
    font_size: int = DEFAULT_FONT_SIZE
    show_whitespace: bool = False
    layout: Layout = field(default_factory=Layout.default)

    @classmethod
    def load(cls, path: Path) -> "Settings":
        settings = cls()
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return settings
        if not isinstance(data, dict):
            return settings
        if isinstance(data.get("language"), str):
            settings.language = data["language"]
        if isinstance(data.get("font_size"), int) and 8 <= data["font_size"] <= 28:
            settings.font_size = data["font_size"]
        settings.show_whitespace = bool(data.get("show_whitespace", False))
        settings.layout = Layout.from_dict(data.get("layout"))
        return settings

    def save(self, path: Path) -> bool:
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            data = {"language": self.language, "font_size": self.font_size,
                    "show_whitespace": self.show_whitespace, "layout": self.layout.to_dict()}
            path.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
            return True
        except OSError:
            return False
