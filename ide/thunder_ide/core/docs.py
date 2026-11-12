"""The SDK reference documents shown by the Help menu.

The catalog lists the documents in the reference's reading order (docs/README.md first).
In a non-English interface a translation from docs/<language>/ replaces the English file
when there is one; the directory of the file that was actually read is kept, so a viewer
can resolve relative links and images.
"""

from __future__ import annotations

from dataclasses import dataclass


from thunder_ide.core.i18n import tr
from thunder_ide.core.toolchain import repository_root

#: The reference, in reading order: file name in docs/.
REFERENCE_ORDER = ("README.md", "language.md", "library.md", "graphics.md", "sockets.md",
                   "native.md", "packaging.md", "display.md", "bytecode.md")


@dataclass(frozen=True)
class ReferenceDoc:
    """One document of the SDK reference, ready to show."""

    name: str       # the file name, e.g. "language.md": what show_reference is asked for
    title: str      # the document's own first heading (localized, since the file is)
    text: str       # the markdown source
    directory: str  # where the file was read from: relative links and images resolve here


def _title_of(text: str, fallback: str) -> str:
    for line in text.splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return fallback


def load_reference(language: str) -> list[ReferenceDoc]:
    """Every document of the reference, in reading order, in the interface language."""
    documents = []
    for name in REFERENCE_ORDER:
        path = repository_root() / "docs" / name
        translated = repository_root() / "docs" / language / name
        if language != "en" and translated.is_file():
            path = translated
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            text = tr("dialog.cannot_open", path=path, message=exc)
        documents.append(ReferenceDoc(name, _title_of(text, name), text, str(path.parent)))
    return documents
