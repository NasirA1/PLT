"""Stepping through a recorded execution trace.

``thundervm --trace-json`` records the machine state before every instruction.
Because the whole run is recorded, students can step backwards as well as
forwards - something a live debugger cannot easily do.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from thunder_ide.core.i18n import tr


@dataclass(frozen=True)
class StackRow:
    """One operand-stack slot, labelled with the frame and role it belongs to."""

    index: int
    value: str
    label: str  # e.g. "fib: local n" or "fib: operand"


class TraceSession:
    """A cursor over the steps of a trace."""

    def __init__(self, trace: dict):
        self.trace = trace
        self.steps: list[dict] = list(trace.get("steps", []))
        self.outputs: list[dict] = list(trace.get("output", []))
        self.error: Optional[dict] = trace.get("error")
        self.truncated: bool = bool(trace.get("truncated"))
        self.index = 0

    # ------------------------------------------------------------------ navigation

    @property
    def total(self) -> int:
        return len(self.steps)

    @property
    def current(self) -> Optional[dict]:
        return self.steps[self.index] if self.steps else None

    @property
    def at_start(self) -> bool:
        return self.index == 0

    @property
    def at_end(self) -> bool:
        return self.index >= self.total - 1

    def go_to(self, index: int) -> None:
        self.index = max(0, min(index, self.total - 1)) if self.steps else 0

    def forward(self) -> None:
        self.go_to(self.index + 1)

    def back(self) -> None:
        self.go_to(self.index - 1)

    def first(self) -> None:
        self.go_to(0)

    def last(self) -> None:
        self.go_to(self.total - 1)

    @staticmethod
    def _location(step: dict) -> tuple:
        return step.get("function"), step.get("line"), len(step.get("frames", []))

    def forward_line(self) -> None:
        """Runs to the first instruction of a different source line (or call depth)."""
        if not self.steps:
            return
        start = self._location(self.steps[self.index])
        index = self.index + 1
        while index < self.total and self._location(self.steps[index]) == start:
            index += 1
        self.go_to(index)

    def back_line(self) -> None:
        """Goes back to the first instruction of the previous source line."""
        if not self.steps:
            return
        index = self.index - 1
        if index < 0:
            return
        target = self._location(self.steps[index])
        while index > 0 and self._location(self.steps[index - 1]) == target:
            index -= 1
        self.go_to(index)

    # ------------------------------------------------------------------ state

    def output_so_far(self) -> str:
        """Everything the program printed before the current instruction."""
        return "".join(entry["text"] for entry in self.outputs if entry["step"] < self.index)

    def stack_rows(self) -> list[StackRow]:
        """The operand stack, top first, with each slot attributed to its frame."""
        step = self.current
        if step is None:
            return []
        stack: list[str] = step.get("stack", [])
        frames: list[dict] = step.get("frames", [])
        local_names = [entry["name"] for entry in step.get("locals", [])]
        rows: list[StackRow] = []
        for index in range(len(stack) - 1, -1, -1):
            owner = None
            for frame_index, frame in enumerate(frames):
                if frame["base"] <= index:
                    owner = (frame_index, frame)
            label = tr("vm.operand")
            if owner is not None:
                frame_index, frame = owner
                offset = index - frame["base"]
                if offset < frame["locals"]:
                    if frame_index == len(frames) - 1 and offset < len(local_names):
                        label = f"{frame['function']}: {tr('vm.local')} {local_names[offset]}"
                    else:
                        label = f"{frame['function']}: {tr('vm.local_slot')} {offset}"
                else:
                    label = f"{frame['function']}: {tr('vm.operand')}"
            rows.append(StackRow(index, stack[index], label))
        return rows
