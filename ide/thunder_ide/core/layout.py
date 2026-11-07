"""The arrangement of the IDE's dockable panels, independent of any GUI toolkit.

The window has three dock areas: ``left`` and ``right`` side by side, and ``bottom`` below
them. Each panel lives in one area (stacked in ``order``) or floats in its own window, and can
be hidden. A GUI toolkit turns a ``Layout`` into widgets and reports changes back; the layout
is saved with the user's settings. A toolkit with its own docking framework may additionally
store its exact arrangement (sizes, tab order, floating window positions) in ``toolkit_state``
and prefer that when it is present.
"""

from __future__ import annotations

import copy
from dataclasses import asdict, dataclass, field

AREAS = ("left", "right", "bottom")

# Panel identifiers.
EDITOR = "editor"
EDUCATION = "education"
OUTPUT = "output"
CANVAS = "canvas"
PANELS = (EDITOR, EDUCATION, OUTPUT, CANVAS)


@dataclass
class PanelState:
    area: str = "left"
    order: int = 0
    visible: bool = True
    floating: bool = False
    tab_with: str = ""  # another panel in the same area: the two share the space as tabs


@dataclass
class Layout:
    panels: dict[str, PanelState] = field(default_factory=dict)
    # Relative sizes: how much of the width the left area takes, and of the height the top row.
    left_fraction: float = 0.5
    top_fraction: float = 0.72
    toolkit_state: str = ""  # opaque, toolkit-specific (e.g. Qt's saveState, base64)

    @classmethod
    def default(cls) -> "Layout":
        return cls(panels={
            EDITOR: PanelState("left", 0),
            EDUCATION: PanelState("right", 0),
            OUTPUT: PanelState("bottom", 0),
            # The canvas (graphics programs draw there) is a tab next to "Under the bonnet".
            CANVAS: PanelState("right", 1, tab_with=EDUCATION),
        })

    def copy(self) -> "Layout":
        return copy.deepcopy(self)

    def panels_in(self, area: str, include_hidden: bool = False) -> list[str]:
        """Docked panels of an area, in stacking order."""
        names = [name for name, state in self.panels.items()
                 if state.area == area and not state.floating and (state.visible or include_hidden)]
        return sorted(names, key=lambda name: (self.panels[name].order, PANELS.index(name)))

    # ------------------------------------------------------------------ changes

    def move(self, panel: str, area: str) -> None:
        """Docks a panel at the end of another area."""
        if area not in AREAS:
            raise ValueError(f"unknown dock area {area!r}")
        state = self.panels[panel]
        others = self.panels_in(area, include_hidden=True)
        state.area = area
        state.order = max((self.panels[name].order for name in others if name != panel), default=-1) + 1
        state.floating = False
        state.visible = True
        state.tab_with = ""
        self.toolkit_state = ""

    def set_floating(self, panel: str, floating: bool) -> None:
        state = self.panels[panel]
        state.floating = floating
        state.visible = True
        self.toolkit_state = ""

    def set_visible(self, panel: str, visible: bool) -> None:
        self.panels[panel].visible = visible
        self.toolkit_state = ""

    # ------------------------------------------------------------------ persistence

    def to_dict(self) -> dict:
        return {"panels": {name: asdict(state) for name, state in self.panels.items()},
                "left_fraction": self.left_fraction, "top_fraction": self.top_fraction,
                "toolkit_state": self.toolkit_state}

    @classmethod
    def from_dict(cls, data: object) -> "Layout":
        """Builds a layout from saved data, ignoring anything invalid (falls back to defaults)."""
        layout = cls.default()
        if not isinstance(data, dict):
            return layout
        panels = data.get("panels")
        if isinstance(panels, dict):
            for name, saved in panels.items():
                if name not in layout.panels or not isinstance(saved, dict):
                    continue
                state = layout.panels[name]
                if saved.get("area") in AREAS:
                    state.area = saved["area"]
                if isinstance(saved.get("order"), int):
                    state.order = saved["order"]
                for key in ("visible", "floating"):
                    if isinstance(saved.get(key), bool):
                        setattr(state, key, saved[key])
                if "tab_with" in saved:
                    tab_with = saved["tab_with"]
                    state.tab_with = tab_with if tab_with in layout.panels and tab_with != name else ""
        for key in ("left_fraction", "top_fraction"):
            value = data.get(key)
            if isinstance(value, (int, float)) and 0.1 <= value <= 0.9:
                setattr(layout, key, float(value))
        if isinstance(data.get("toolkit_state"), str):
            layout.toolkit_state = data["toolkit_state"]
        layout.panels[EDITOR].visible = True  # the editor can never be lost
        return layout
