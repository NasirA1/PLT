"""The graphics canvas as the IDE sees it, independent of any GUI toolkit.

A program that imports ``graphics`` draws shapes; ``thundervm --canvas`` sends each frame it
shows as one record line on standard output: the record separator ``\\x1e``, a JSON object and a
line break. A record keeps the first ``keep`` shapes the IDE already has and adds the rest, so a
frame that only adds a few shapes is small. Key and mouse events go the other way, as record lines
on the program's standard input (see include/thunder/graphics.hpp).

``CanvasModel`` applies the records (on the thread that reads the program's output) and gives the
GUI consistent snapshots to paint. ``split_records`` separates records from ordinary output.
"""

from __future__ import annotations

import json
import threading
from dataclasses import dataclass, field
from typing import Optional

RECORD_SEPARATOR = "\x1e"

# Shape arrays as sent by the VM: ["rect", x, y, width, height, color], ... (see graphics.hpp).
SHAPE_KINDS = ("clear", "pixel", "line", "rect", "rect_outline", "circle", "circle_outline", "text")


@dataclass
class CanvasSnapshot:
    """One frame, ready to paint: the canvas size, the shapes in drawing order, the frame number."""

    width: int = 400
    height: int = 300
    shapes: list[list] = field(default_factory=list)
    frame: int = 0


class CanvasModel:
    """The current picture, built from frame records. Safe to use from two threads."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = CanvasSnapshot()
        self.version = 0  # increases with every applied frame

    def reset(self) -> None:
        with self._lock:
            self._snapshot = CanvasSnapshot()
            self.version += 1

    def apply(self, record: dict) -> bool:
        """Applies a frame record; False (and nothing changes) if it is not a valid frame."""
        if record.get("type") != "frame":
            return False
        try:
            width, height = int(record["width"]), int(record["height"])
            keep, frame = int(record["keep"]), int(record["n"])
            shapes = [shape for shape in record["shapes"] if isinstance(shape, list) and shape
                      and shape[0] in SHAPE_KINDS]
        except (KeyError, TypeError, ValueError):
            return False
        with self._lock:
            kept = self._snapshot.shapes[:max(0, keep)]
            self._snapshot = CanvasSnapshot(width, height, kept + shapes, frame)
            self.version += 1
        return True

    def snapshot(self) -> CanvasSnapshot:
        with self._lock:
            current = self._snapshot
            return CanvasSnapshot(current.width, current.height, list(current.shapes), current.frame)


class RecordSplitter:
    """Separates frame records from a program's ordinary output, which arrives in pieces.

    ``feed`` returns the ordinary text and the complete records found so far; a record that is
    cut off at the end of a piece is completed by the next one.
    """

    def __init__(self) -> None:
        self._pending: Optional[str] = None  # an incomplete record (without the separator)

    def feed(self, text: str) -> tuple[str, list[dict]]:
        output: list[str] = []
        records: list[dict] = []
        while text:
            if self._pending is not None:
                end = text.find("\n")
                if end < 0:
                    self._pending += text
                    break
                line, text = self._pending + text[:end], text[end + 1:]
                self._pending = None
                try:
                    record = json.loads(line)
                except ValueError:
                    record = None
                if isinstance(record, dict):
                    records.append(record)
                continue
            start = text.find(RECORD_SEPARATOR)
            if start < 0:
                output.append(text)
                break
            output.append(text[:start])
            self._pending = ""
            text = text[start + 1:]
        return "".join(output), records


# ---------------------------------------------------------------------- events

# Key names a program sees (key_pressed(), key_down(name)), by Qt-neutral key description.
SPECIAL_KEYS = {"left", "right", "up", "down", "space", "enter", "escape", "backspace", "tab"}


def key_event(name: str, pressed: bool) -> str:
    """The input line that reports a key press or release to the program."""
    return f"{RECORD_SEPARATOR}key {'down' if pressed else 'up'} {name}\n"


def mouse_event(x: int, y: int, pressed: bool) -> str:
    """The input line that reports the mouse position (in canvas pixels) and button."""
    return f"{RECORD_SEPARATOR}mouse {int(x)} {int(y)} {1 if pressed else 0}\n"


def key_name(text: str, special: Optional[str] = None) -> Optional[str]:
    """The name a program sees for a key: a special key's name, or a letter or digit in lower case.

    Returns None for keys a program cannot ask about.
    """
    if special in SPECIAL_KEYS:
        return special
    if len(text) == 1 and text.isascii() and text.isalnum():
        return text.lower()
    return None
