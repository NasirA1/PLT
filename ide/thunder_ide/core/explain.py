"""Explanations shown to students, built from the toolchain's JSON reports.

All wording comes from the translation table (core.i18n), so it follows the interface language.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Optional

from thunder_ide.core.i18n import tr

# The compiler's phase names (as reported by thunderc) and the key used for their texts.
PHASES = [
    ("lexical analysis", "lexical"),
    ("syntax analysis", "syntax"),
    ("semantic analysis", "semantic"),
    ("code generation", "codegen"),
]
EXECUTION = ("execution", "execution")


@dataclass(frozen=True)
class PhaseInfo:
    name: str        # as reported by thunderc
    title: str       # e.g. "Lexical analysis", translated
    produces: str    # what the phase hands to the next one
    tab: str         # where to look in the IDE
    explanation: str


def phase_info(name: str) -> Optional[PhaseInfo]:
    for phase, key in PHASES + [EXECUTION]:
        if phase == name:
            return PhaseInfo(phase, tr(f"phase.{key}.title"), tr(f"phase.{key}.produces"), tr(f"phase.{key}.tab"),
                             tr(f"phase.{key}.explanation"))
    return None


def count_ast_nodes(node: Optional[dict]) -> int:
    if not node:
        return 0
    return 1 + sum(count_ast_nodes(child) for child in node.get("children", []))


def iter_scopes(scope: Optional[dict]) -> Iterable[dict]:
    if not scope:
        return
    yield scope
    for child in scope.get("children", []):
        yield from iter_scopes(child)


def count_symbols(symbols: Optional[dict]) -> int:
    if not symbols:
        return 0
    return sum(len(scope.get("symbols", [])) for scope in iter_scopes(symbols.get("scopes")))


def phase_result_detail(name: str, report: dict) -> str:
    """A one-line summary of what a successful phase produced."""
    if name == "lexical analysis":
        return tr("detail.tokens", count=max(len(report.get("tokens", [])) - 1, 0))
    if name == "syntax analysis":
        return tr("detail.nodes", count=count_ast_nodes(report.get("ast")))
    if name == "semantic analysis":
        symbols = report.get("symbols") or {}
        functions = max(len(symbols.get("functions", [])) - 1, 0)
        return tr("detail.symbols", count=count_symbols(symbols), functions=functions,
                  structs=len(symbols.get("structs", [])))
    if name == "code generation":
        binary = report.get("binary") or {}
        bytecode = report.get("bytecode") or {}
        instructions = sum(len(f.get("instructions", [])) for f in bytecode.get("functions", []))
        return tr("detail.code", count=instructions, size=binary.get("size", 0))
    return ""


def pipeline_lines(report: Optional[dict], stats: Optional[dict] = None,
                   runtime_error: Optional[dict] = None) -> list[tuple[str, str]]:
    """The Pipeline tab: (text, style) lines where style is "title", "heading", "ok", "failed", "skipped" or "body"."""
    lines: list[tuple[str, str]] = [(tr("overview.title"), "title"), ("", "body")]
    infos = [phase_info(name) for name, _ in PHASES]
    if report is None:
        lines.append((tr("overview.not_compiled"), "body"))
        lines.append(("", "body"))
        for index, info in enumerate(infos, start=1):
            assert info is not None
            lines.append((f"{index}. {info.title} -> {info.produces}", "skipped"))
            lines.append((f"   {info.explanation}", "body"))
        return lines

    lines.append((tr("overview.source"), "body"))
    phases = {phase["name"]: phase for phase in report.get("phases", [])}
    for index, info in enumerate(infos, start=1):
        assert info is not None
        phase = phases.get(info.name, {})
        if not phase.get("ran"):
            status, style, detail = tr("phase.skipped"), "skipped", tr("overview.earlier_failed")
        elif phase.get("succeeded"):
            status, style = tr("phase.ok"), "ok"
            detail = tr("overview.timing", ms=f"{phase.get('microseconds', 0) / 1000:.2f}",
                        detail=phase_result_detail(info.name, report))
        else:
            status, style, detail = tr("phase.failed"), "failed", tr("overview.see_problems")
        lines.append(("   |", "body"))
        lines.append((f"   |  {index}. {info.title:<20} {status:<8} {detail}", style))
        lines.append(("   ↓", "body"))
        lines.append((f"{info.produces}   [{info.tab}]", "body"))

    execution = phase_info(EXECUTION[0])
    assert execution is not None
    lines.append(("   |", "body"))
    if runtime_error is not None:
        lines.append((f"   |  5. {execution.title:<20} {tr('phase.failed'):<8} {runtime_error.get('message', '')}",
                      "failed"))
    elif stats is not None:
        lines.append((f"   |  5. {execution.title:<20} {tr('phase.ok'):<8} "
                      + tr("overview.execution", steps=stats.get("steps", 0), calls=stats.get("calls", 0),
                           depth=stats.get("max_stack_depth", 0)), "ok"))
    else:
        lines.append((f"   |  5. {execution.title:<20} {tr('overview.press_run')}", "skipped"))
    lines.append(("   v", "body"))
    lines.append((f"{execution.produces}   [{execution.tab}]", "body"))

    lines.append(("", "body"))
    lines.append((tr("overview.what_phases_do"), "title"))
    for index, info in enumerate(infos + [execution], start=1):
        assert info is not None
        lines.append((f"{index}. {info.title}", "heading"))
        lines.append((info.explanation, "body"))
    return lines


def failure_explanation(report: dict) -> str:
    """Explains, after a failed compilation, which phase stopped it and what that means."""
    if report.get("internal_error"):
        return tr("failure.internal", message=report["internal_error"])
    failed = next((p for p in report.get("phases", []) if p.get("ran") and not p.get("succeeded")), None)
    if failed is None:
        return ""
    info = phase_info(failed["name"])
    text = tr("failure.stopped", phase=info.title if info else failed["name"])
    if info is not None:
        text += " " + info.explanation
    skipped = [phase_info(p["name"]) for p in report.get("phases", []) if not p.get("ran")]
    if skipped:
        text += " " + tr("failure.skipped", phases=", ".join(s.title for s in skipped if s))
    return text


@dataclass(frozen=True)
class HexRow:
    section: int  # index into the sections list (for colouring)
    text: str


def hex_dump_rows(hex_text: str, sections: list[dict], width: int = 16) -> list[HexRow]:
    """Formats a compiled file as a hex dump, one block per section."""
    data = bytes.fromhex(hex_text)
    rows: list[HexRow] = []
    for index, section in enumerate(sections):
        start, size = section["offset"], section["size"]
        rows.append(HexRow(index, f"; {section['name']} ({size} bytes)"))
        for row_start in range(start, start + size, width):
            chunk = data[row_start:min(row_start + width, start + size)]
            hex_part = " ".join(f"{b:02x}" for b in chunk).ljust(width * 3 - 1)
            ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
            rows.append(HexRow(index, f"{row_start:08x}  {hex_part}  {ascii_part}"))
    return rows


def format_ast_label(node: dict) -> str:
    text = f"{node['role']}: " if node.get("role") else ""
    text += node["kind"]
    if node.get("label"):
        text += " " + node["label"]
    return text
