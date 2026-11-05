"""Display data built from the toolchain's JSON reports, ready for any GUI toolkit.

Views never interpret reports themselves: the controller converts each report into the
plain dataclasses defined here (rows of a table or tree, styled lines, debugger state).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from thunder_ide.core import explain
from thunder_ide.core.debugger import TraceSession
from thunder_ide.core.i18n import tr


@dataclass(frozen=True)
class Span:
    """A region of the source (1-based; columns count characters)."""

    line: int
    column: int
    end_line: int = 0
    end_column: int = 0

    @classmethod
    def of(cls, item: dict) -> "Span":
        return cls(int(item.get("line", 0)), int(item.get("column", 0)), int(item.get("end_line", 0)),
                   int(item.get("end_column", 0)))


@dataclass(frozen=True)
class StyledText:
    text: str
    style: Optional[str] = None


@dataclass
class TreeRow:
    text: str = ""                      # first column / tree label
    values: tuple = ()                  # remaining columns
    span: Optional[Span] = None         # source region to highlight when selected
    style: Optional[str] = None         # e.g. a token category, "error", "warning"
    children: list["TreeRow"] = field(default_factory=list)
    key: Optional[str] = None           # extra data, e.g. an instruction's mnemonic
    expanded: bool = True


@dataclass(frozen=True)
class Column:
    id: str
    heading: str
    width: int


@dataclass(frozen=True)
class TreeSpec:
    """How a tree panel is laid out: an optional tree column plus value columns."""

    tree_heading: Optional[str]  # None for a flat table (the first column is TreeRow.text)
    columns: tuple[Column, ...]
    tree_width: int = 260


def tree_specs() -> dict[str, TreeSpec]:
    """Column layout of each tree panel, in the current interface language."""
    return {
        "tokens": TreeSpec(None, (Column("index", tr("column.index"), 50), Column("position", tr("column.position"), 80),
                                  Column("kind", tr("column.kind"), 120), Column("category", tr("column.category"), 100),
                                  Column("lexeme", tr("column.lexeme"), 240))),
        "ast": TreeSpec(tr("tab.ast"), (Column("type", tr("column.type"), 110),
                                        Column("position", tr("column.position"), 80)), 340),
        "symbols": TreeSpec(tr("column.name"), (Column("kind", tr("column.kind"), 100), Column("type", tr("column.type"), 120),
                                                 Column("slot", tr("column.slot"), 70), Column("line", tr("column.line"), 60))),
        "bytecode": TreeSpec(tr("column.instruction"), (Column("offset", tr("column.offset"), 60),
                                                         Column("bytes", tr("column.bytes"), 120),
                                                         Column("comment", tr("column.comment"), 160),
                                                         Column("line", tr("column.line"), 50)), 230),
        "problems": TreeSpec(None, (Column("phase", tr("column.phase"), 90), Column("code", tr("column.code"), 60),
                                    Column("position", tr("column.position"), 80),
                                    Column("message", tr("column.message"), 600))),
    }


# ---------------------------------------------------------------------- compiler artefacts

def token_rows(report: dict) -> list[TreeRow]:
    return [TreeRow(str(index), (f"{t['line']}:{t['column']}", t["kind"], t["category"], t["lexeme"]),
                    Span.of(t), t["category"])
            for index, t in enumerate(report.get("tokens", []))]


def ast_rows(report: dict) -> list[TreeRow]:
    def convert(node: dict) -> TreeRow:
        return TreeRow(explain.format_ast_label(node), (node.get("type", ""), f"{node['line']}:{node['column']}"),
                       Span.of(node), children=[convert(child) for child in node.get("children", [])])

    ast = report.get("ast")
    return [convert(ast)] if ast else []


def symbol_rows(report: dict) -> list[TreeRow]:
    symbols = report.get("symbols")
    if not symbols:
        return []
    rows = []
    functions = TreeRow(tr("symbols.functions"))
    for function in symbols.get("functions", []):
        signature = f"{function['name']}({', '.join(function['parameters'])}): {function['returns']}"
        functions.children.append(TreeRow(signature, (tr("symbols.function"), function["returns"],
                                                      tr("symbols.slots", count=function["local_slots"]),
                                                      function["line"]), Span.of(function)))
    rows.append(functions)
    if symbols.get("structs"):
        structs = TreeRow(tr("symbols.structs"))
        for info in symbols["structs"]:
            row = TreeRow(info["name"], (tr("symbols.struct"), "", "", info["line"]), Span.of(info))
            for index, field_info in enumerate(info["fields"]):
                row.children.append(TreeRow(field_info["name"], (tr("symbols.field"), field_info["type"], f"#{index}",
                                                                 field_info["line"]), Span.of(field_info)))
            structs.children.append(row)
        rows.append(structs)

    def scope_row(scope: dict) -> TreeRow:
        title = tr(f"scope.{scope['kind']}") + (f" {scope['name']}" if scope.get("name") else "")
        title += " " + tr("scope.lines", start=scope["line"], end=scope["end_line"])
        row = TreeRow(title, span=Span.of(scope))
        for symbol in scope.get("symbols", []):
            kind = tr(f"symbols.{symbol['kind']}") + (" " + tr("symbols.const") if symbol.get("const") else "")
            row.children.append(TreeRow(symbol["name"], (kind, symbol["type"], symbol["slot"], symbol["line"]),
                                        Span.of(symbol)))
        row.children.extend(scope_row(child) for child in scope.get("children", []))
        return row

    rows.append(scope_row(symbols["scopes"]))
    return rows


def bytecode_rows(report: dict) -> list[TreeRow]:
    bytecode = report.get("bytecode")
    if not bytecode:
        return []
    rows = []
    constants = TreeRow(tr("bytecode.constants", count=len(bytecode["constants"])), expanded=False)
    constants.children = [TreeRow(f"#{c['index']}  {c['value']}", ("", "", c["type"], "")) for c in bytecode["constants"]]
    rows.append(constants)
    if bytecode.get("structs"):
        structs = TreeRow(tr("bytecode.structs", count=len(bytecode["structs"])), expanded=False)
        for info in bytecode["structs"]:
            fields = ", ".join(f"{f['name']}: {f['type']}" for f in info["fields"])
            structs.children.append(TreeRow(f"${info['index']} {info['name']}", ("", "", fields, "")))
        rows.append(structs)
    globals_row = TreeRow(tr("bytecode.globals", count=len(bytecode["globals"])), expanded=False)
    globals_row.children = [TreeRow(f"[{index}] {name}") for index, name in enumerate(bytecode["globals"])]
    rows.append(globals_row)
    for function in bytecode["functions"]:
        title = tr("bytecode.function", index=function["index"], name=function["name"], params=function["arity"],
                   locals=len(function["locals"]), size=function["code_size"])
        row = TreeRow(title)
        for instruction in function["instructions"]:
            line = instruction["line"]
            row.children.append(TreeRow(f"{instruction['mnemonic']} {instruction['operands']}".rstrip(),
                                        (f"{instruction['offset']:04}", instruction["bytes"], instruction["comment"],
                                         line or ""),
                                        Span(line, 0, line, 0) if line else None, key=instruction["mnemonic"]))
        rows.append(row)
    return rows


def problem_rows(diagnostics: list[dict], runtime_error: Optional[dict] = None) -> list[TreeRow]:
    rows = [TreeRow(tr(f"phase.name.{d['phase']}"), (d["code"], f"{d['line']}:{d['column']}",
                                                   f"{tr('severity.' + d['severity'])}: {d['message']}"),
                    Span.of(d), d["severity"])
            for d in diagnostics]
    if runtime_error is not None:
        line, column = runtime_error.get("line", 0), runtime_error.get("column", 0)
        rows.append(TreeRow(tr("phase.name.runtime"), (runtime_error.get("code", ""), f"{line}:{column}",
                                                       f"{tr('severity.error')}: {runtime_error.get('message', '')}"),
                            Span(line, column, line, column), "error"))
    return rows


def binary_lines(report: dict) -> list[StyledText]:
    binary = report.get("binary")
    if not binary:
        return []
    lines = [StyledText(tr("binary.header", size=binary["size"])), StyledText("")]
    lines += [StyledText(row.text, f"section{row.section % 6}")
              for row in explain.hex_dump_rows(binary["hex"], binary["sections"])]
    return lines


def opcode_description(opcodes: dict[str, dict], mnemonic: Optional[str]) -> str:
    op = opcodes.get(mnemonic or "")
    return f"{op['mnemonic']}  [{op['stack_effect']}]  {op['description']}" if op else ""


# ---------------------------------------------------------------------- debugger

@dataclass
class VmViewState:
    position: str
    instruction: str
    can_go_back: bool
    can_go_forward: bool
    stack: list[TreeRow]
    frames: list[TreeRow]
    locals: list[TreeRow]
    globals: list[TreeRow]
    output: str
    line: int


def vm_state(session: Optional[TraceSession]) -> Optional[VmViewState]:
    if session is None or session.current is None:
        return None
    step = session.current
    position = tr("vm.step", index=session.index + 1, total=session.total)
    if session.truncated:
        position += tr("vm.truncated", total=session.total)
    instruction = (f"{tr('vm.next')}  {step['function']}+{step['offset']:04}   {step['instruction']}   "
                   f"({tr('column.line')} {step['line']})")
    if session.at_end and session.error and not session.truncated:
        instruction += "\n" + tr("vm.error", message=session.error.get("message", ""))
    elif session.at_end and step.get("mnemonic") == "HALT":
        instruction += "\n" + tr("vm.finished")
    return VmViewState(
        position=position,
        instruction=instruction,
        can_go_back=not session.at_start,
        can_go_forward=not session.at_end,
        stack=[TreeRow(str(row.index), (row.value, row.label)) for row in session.stack_rows()],
        frames=[TreeRow(frame["function"], (frame["base"],)) for frame in reversed(step.get("frames", []))],
        locals=[TreeRow(local["name"], (local["value"],)) for local in step.get("locals", [])],
        globals=[TreeRow(value["name"], (value["value"],)) for value in step.get("globals", [])],
        output=session.output_so_far(),
        line=int(step.get("line", 0)),
    )
