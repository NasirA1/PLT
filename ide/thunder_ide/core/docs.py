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


def load_document(path) -> ReferenceDoc:
    """A markdown file that is not part of the reference (a README the reference links to)."""
    from pathlib import Path

    file = Path(path)
    try:
        text = file.read_text(encoding="utf-8")
    except OSError as exc:
        text = tr("dialog.cannot_open", path=file, message=exc)
    return ReferenceDoc(file.name, _title_of(text, file.name), text, str(file.parent))


@dataclass(frozen=True)
class Link:
    """Where a link in a document leads.

    kind is one of:
      "document"  another document already in the viewer (index says which)
      "markdown"  a markdown file outside the reference, to be opened in the viewer (path)
      "external"  a web address or a file that is not markdown, for the system to open (target)
      "anchor"    a heading in the document being shown (fragment)
    """

    kind: str
    index: int = -1
    path: str = ""
    target: str = ""
    fragment: str = ""


def resolve_link(href: str, current: ReferenceDoc, documents: list[ReferenceDoc]) -> Link:
    """Works out where a link in ``current`` leads.

    Links are resolved against the directory of the document they are in, and documents are
    compared by their full path - not by their file name, because the reference's own index and
    every README the reference links to are all called README.md.
    """
    import os
    from pathlib import Path
    from urllib.parse import unquote, urlsplit

    parts = urlsplit(href)
    if parts.scheme in ("http", "https", "mailto"):
        return Link("external", target=href)
    fragment = parts.fragment
    if not parts.path:
        return Link("anchor", fragment=fragment)
    if parts.scheme == "file":
        target = Path(unquote(parts.path))
    else:
        target = Path(current.directory) / unquote(parts.path)
    target = Path(os.path.normpath(target))
    for index, doc in enumerate(documents):
        if Path(os.path.normpath(Path(doc.directory) / doc.name)) == target:
            return Link("document", index=index, path=str(target), fragment=fragment)
    if target.suffix.lower() == ".md" and target.is_file():
        return Link("markdown", path=str(target), fragment=fragment)
    if target.suffix.lower() == ".md":
        # A translation may link to a sibling that was never translated: the catalog then holds
        # the English original, which is what the reader should get.
        for index, doc in enumerate(documents):
            if doc.name == target.name:
                return Link("document", index=index, path=str(target), fragment=fragment)
    return Link("external", target=str(target), fragment=fragment)
