"""The sample programs shipped in samples/<language>/."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from thunder_ide.core.toolchain import repository_root


@dataclass(frozen=True)
class Sample:
    path: Path

    @property
    def name(self) -> str:
        return self.path.name

    @property
    def title(self) -> str:
        """The first comment line that is not a language directive, e.g. "01 - Hello, Thunder!"."""
        for line in self.path.read_text(encoding="utf-8").splitlines():
            stripped = line.strip()
            if stripped.startswith("#"):
                text = stripped.lstrip("#").strip()
                if not text.split(":")[0].strip() in ("lang", "language", "ژبه"):
                    return text
                continue
            if stripped:
                break
        return self.path.stem

    def read(self) -> str:
        return self.path.read_text(encoding="utf-8")


def samples_dir(language: str = "en") -> Path:
    return repository_root() / "samples" / language


def list_samples(language: str = "en", directory: Path | None = None) -> list[Sample]:
    directory = directory or samples_dir(language)
    if not directory.is_dir():
        return []
    return [Sample(path) for path in sorted(directory.glob("*.th"))]


def counterpart(sample_name: str, language: str) -> Sample | None:
    """The same lesson in another language (samples share file names across languages)."""
    path = samples_dir(language) / sample_name
    return Sample(path) if path.is_file() else None
