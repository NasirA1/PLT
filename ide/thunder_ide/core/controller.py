"""The IDE's application logic, independent of any GUI toolkit.

``IdeController`` owns the state (the open file, the last compilation, the running program,
the recorded trace) and implements every command. It drives a ``MainWindowView`` created by a
``Backend`` (see core.ui) and receives the user's actions through its ``run_command`` and
``on_...`` methods.
"""

from __future__ import annotations

import gc
import re
import sys
from dataclasses import replace
import threading
from pathlib import Path
from typing import Callable, Optional

from thunder_ide import __version__
from thunder_ide.core import docs, edits, explain, fonts, i18n, viewmodels
from thunder_ide.core.canvas import CanvasModel, key_event, mouse_event
from thunder_ide.core.debugger import TraceSession
from thunder_ide.core.editing import BlockWords
from thunder_ide.core.fonts import FontChoice
from thunder_ide.core.highlight import WordClasses, highlight
from thunder_ide.core.i18n import tr
from thunder_ide.core.layout import Layout
from thunder_ide.core.samples import Sample, counterpart, list_samples
from thunder_ide.core.settings import DEFAULT_FONT_SIZE, Settings
from thunder_ide.core.toolchain import (CompileOutcome, ProgramRun, RunResult, Toolchain, ToolchainError,
                                        format_command)
from thunder_ide.core.ui import Backend, MainWindowView
from thunder_ide.core.viewmodels import FindRequest, Span, StyledText, TreeRow

DEFAULT_MAX_STEPS = 50_000_000  # stops runaway programs (a few seconds of execution)
DEBUG_MAX_STEPS = 1_000_000
DEBUG_TRACE_LIMIT = 20_000
HIGHLIGHT_DELAY_MS = 40

_DIRECTIVE = re.compile(r"^﻿?[ \t]*(?:#|//)[ \t]*(?:lang|language|ژبه)[ \t]*:[ \t]*(\S+)", re.IGNORECASE)
_LANGUAGE_NAMES = {"en": "en", "english": "en", "ps": "ps", "pashto": "ps", "پښتو": "ps"}


def directive_language(text: str) -> Optional[str]:
    """The language named by a "# lang: xx" directive on the first line (as thunderc reads it)."""
    match = _DIRECTIVE.match(text.split("\n", 1)[0])
    return _LANGUAGE_NAMES.get(match.group(1).lower()) if match else None


class IdeController:
    def __init__(self, backend: Backend, toolchain: Optional[Toolchain], settings: Optional[Settings] = None,
                 settings_path: Optional[Path] = None):
        self.backend = backend
        self.scheduler = backend.scheduler
        self.toolchain = toolchain
        self.settings = settings or Settings()
        self.settings_path = settings_path
        if self.settings.language not in i18n.UI_LANGUAGES:
            self.settings.language = "en"
        i18n.set_language(self.settings.language)
        if toolchain is not None:
            toolchain.language = self.settings.language

        self.samples: list[Sample] = list_samples(self.language)
        self.current_sample: Optional[Sample] = None
        self.current_path: Optional[Path] = None
        self.display_name = tr("editor.untitled")
        self.last_compile: Optional[CompileOutcome] = None
        self.compiled_text: Optional[str] = None
        self.run: Optional[ProgramRun] = None
        self.trace: Optional[TraceSession] = None
        self.max_steps = DEFAULT_MAX_STEPS
        self.canvas = CanvasModel()            # the picture of the running graphics program
        self._canvas_lock = threading.Lock()
        self._canvas_refresh_pending = False   # one repaint at a time, however fast frames arrive
        self._canvas_shown = False             # brought to the front for this run yet?
        self.status = tr("status.ready")
        self._highlight_job = None
        self._last_find: Optional[FindRequest] = None
        self._closed = False

        self.window: MainWindowView = self._create_window()
        if self.samples:
            self.load_sample(0)
        if self.toolchain is None:  # shown last so that it stays visible
            self.set_status(tr("status.toolchain_missing", message=tr("status.build_first")))

    # ------------------------------------------------------------------ properties

    @property
    def language(self) -> str:
        return i18n.current_language()

    def program_language(self, text: Optional[str] = None) -> str:
        """The language the editor's program is written in: its directive, or the interface language."""
        text = self.window.editor.get_text() if text is None else text
        return directive_language(text) or self.language

    def _language_info(self, language: str) -> dict:
        if self.toolchain is None:
            return {}
        try:
            return self.toolchain.language_info(language)
        except ToolchainError as exc:
            self.log(str(exc), "stderr")
            return {}

    def program_is_rtl(self, text: Optional[str] = None) -> bool:
        info = self._language_info(self.program_language(text))
        return bool(info.get("right_to_left", i18n.UI_LANGUAGES.get(self.program_language(text), {}).get(
            "right_to_left", False)))

    # ------------------------------------------------------------------ window

    def _create_window(self) -> MainWindowView:
        window = self.backend.create_main_window(self, i18n.is_right_to_left(), self.settings.font_size)
        self.window = window
        window.apply_layout(self.settings.layout)
        window.set_languages([info["name"] for info in i18n.UI_LANGUAGES.values()],
                             list(i18n.UI_LANGUAGES).index(self.language))
        window.set_samples([sample.title for sample in self.samples], -1)
        window.set_status(self.status)
        window.overview.set_lines([StyledText(text, style) for text, style in explain.pipeline_lines(None)])
        window.editor.set_show_whitespace(self.settings.show_whitespace)
        window.set_command_checked("show_whitespace", self.settings.show_whitespace)
        self.window = window
        self._apply_fonts()
        return window

    # ------------------------------------------------------------------ fonts

    def _font_choice(self) -> FontChoice:
        """The fonts the settings ask for, with any that are not installed here replaced."""
        code_fonts, pashto_fonts = self.backend.available_fonts()
        choice = fonts.resolve(FontChoice(self.settings.code_font, self.settings.pashto_font),
                               code_fonts, pashto_fonts)
        return choice

    def _apply_fonts(self) -> None:
        code_fonts, pashto_fonts = self.backend.available_fonts()
        choice = self._font_choice()
        self.window.set_font_choices(code_fonts, pashto_fonts, choice)
        self.window.set_fonts(choice, self.settings.font_size)

    def on_font_selected(self, kind: str, family: str) -> None:
        """Format > Code font / Pashto font: draws the program in another font."""
        if kind == "code":
            self.settings.code_font = family
        elif kind == "pashto":
            self.settings.pashto_font = family
        else:
            return
        self._apply_fonts()
        choice = self._font_choice()
        self.set_status(tr("status.fonts", code=choice.code, pashto=choice.pashto))

    def reset_fonts(self) -> None:
        """Format > Default fonts and size: the fonts the IDE ships, at 100%."""
        self.settings.code_font = fonts.DEFAULT_CODE_FONT
        self.settings.pashto_font = fonts.DEFAULT_PASHTO_FONT
        self.settings.font_size = DEFAULT_FONT_SIZE
        self._apply_fonts()
        choice = self._font_choice()
        self.set_status(tr("status.fonts", code=choice.code, pashto=choice.pashto))

    def set_status(self, text: str) -> None:
        self.status = text
        self.window.set_status(text)

    def log(self, text: str, style: Optional[str] = None) -> None:
        self.window.log.append(text.rstrip("\n") + "\n", style)

    def _update_title(self) -> None:
        modified = f" {tr('editor.modified')}" if self.window.editor.is_modified() else ""
        self.window.set_file_label(f"{self.display_name}{modified}")
        self.window.set_title(f"{self.display_name}{modified} - {tr('app.title')}")

    def _confirm_discard(self) -> bool:
        if not self.window.editor.is_modified():
            return True
        return self.window.ask_yes_no(tr("dialog.unsaved_title"), tr("dialog.unsaved_message", name=self.display_name))

    # ------------------------------------------------------------------ commands

    def run_command(self, command_id: str) -> None:
        handlers: dict[str, Callable[[], object]] = {
            "new": self.new_file, "open": self.open_file, "save": self.save_file, "save_as": self.save_file_as,
            "exit": self.quit, "translate": self.translate_program,
            "compile": self.compile, "run": self.run_program, "debug": self.debug_program, "stop": self.stop_program,
            "zoom_in": lambda: self.zoom(1), "zoom_out": lambda: self.zoom(-1), "reset_layout": self.reset_layout,
            "undo": lambda: self.window.editor.undo(), "redo": lambda: self.window.editor.redo(),
            "cut": lambda: self.window.editor.cut(), "copy": lambda: self.window.editor.copy(),
            "paste": lambda: self.window.editor.paste(), "select_all": lambda: self.window.editor.select_all(),
            "go_to_line": self.go_to_line,
            "duplicate_line": lambda: self._line_edit(edits.duplicate_lines),
            "delete_line": lambda: self._line_edit(edits.delete_lines),
            "move_line_up": lambda: self._line_edit(lambda text, start, end: edits.move_lines(text, start, end, -1)),
            "move_line_down": lambda: self._line_edit(lambda text, start, end: edits.move_lines(text, start, end, 1)),
            "indent": lambda: self._line_edit(edits.indent_lines),
            "outdent": lambda: self._line_edit(edits.outdent_lines),
            "find": lambda: self.window.show_find(False), "replace": lambda: self.window.show_find(True),
            "find_next": lambda: self.find_again(False), "find_previous": lambda: self.find_again(True),
            "comment": self.toggle_comment, "tidy": self.tidy_layout,
            "fold": lambda: self.fold(False), "fold_all": lambda: self.fold(True),
            "unfold": self.unfold, "unfold_all": self.unfold,
            "show_whitespace": self.toggle_whitespace,
            "reset_fonts": self.reset_fonts,
            "sdk_contents": lambda: self.show_reference("README.md"),
            "reference": lambda: self.show_reference("language.md"),
            "library_reference": lambda: self.show_reference("library.md"),
            "graphics_reference": lambda: self.show_reference("graphics.md"),
            "bytecode_reference": lambda: self.show_reference("bytecode.md"),
            "native_reference": lambda: self.show_reference("native.md"),
            "packaging_reference": lambda: self.show_reference("packaging.md"),
            "display_reference": lambda: self.show_reference("display.md"),
            "sockets_reference": lambda: self.show_reference("sockets.md"),
            "package": self.package_program, "about": self.show_about,
        }
        handlers[command_id]()

    # ------------------------------------------------------------------ editor events

    def on_text_changed(self) -> None:
        """Called by the editor after every edit: re-highlight shortly (typing stays responsive)."""
        self.window.editor.clear_marks()
        self._update_title()
        if self._highlight_job is not None:
            self.scheduler.cancel(self._highlight_job)
        self._highlight_job = self.scheduler.call_later(HIGHLIGHT_DELAY_MS, self.refresh_highlighting)

    def refresh_highlighting(self) -> None:
        if self._highlight_job is not None:  # a pending refresh is now superfluous
            self.scheduler.cancel(self._highlight_job)
            self._highlight_job = None
        if self._closed:
            return
        text = self.window.editor.get_text()
        language = self.program_language(text)
        info = self._language_info(language)
        classes = WordClasses.from_language_info(info) if info else WordClasses.default()
        self.window.editor.apply_highlights(highlight(text, classes))
        self.window.editor.set_block_words(BlockWords.from_language_info(info) if info else BlockWords())
        # The editor itself switches between left-to-right and right-to-left layout.
        self.window.editor.set_right_to_left(self.program_is_rtl(text))

    def on_row_selected(self, panel: str, row: TreeRow) -> None:
        if row.span is not None and row.span.line > 0:
            self.window.editor.show_span(row.span)
        if panel == "bytecode":
            opcodes = {op["mnemonic"]: op for op in self._language_info("en").get("opcodes", [])}
            self.window.bytecode.set_footer(viewmodels.opcode_description(opcodes, row.key))

    # ------------------------------------------------------------------ files and samples

    def load_text(self, text: str, name: str, path: Optional[Path], sample: Optional[Sample] = None) -> None:
        self.window.editor.set_text(text)
        self.current_path = path
        self.current_sample = sample
        self.display_name = name
        self.clear_results()
        self._update_title()
        self.refresh_highlighting()

    def on_sample_selected(self, index: int) -> None:
        self.load_sample(index)

    def load_sample(self, index: int) -> None:
        if index < 0 or index >= len(self.samples) or not self._confirm_discard():
            return
        sample = self.samples[index]
        self.load_text(sample.read(), sample.name, None, sample)
        self.window.set_samples([s.title for s in self.samples], index)
        self.set_status(tr("status.opened", path=sample.name))

    def new_file(self) -> None:
        if self._confirm_discard():
            self.load_text("", tr("editor.untitled"), None)

    def open_file(self, path: Optional[str] = None) -> None:
        if not self._confirm_discard():
            return
        path = path or self.window.ask_open_path()
        if not path:
            return
        file_path = Path(path)
        self.load_text(file_path.read_text(encoding="utf-8"), file_path.name, file_path)
        self.set_status(tr("status.opened", path=file_path))

    def save_file(self) -> bool:
        if self.current_path is None:
            return self.save_file_as()
        self.current_path.write_text(self.window.editor.get_text(), encoding="utf-8", newline="\n")
        self.window.editor.set_modified(False)
        self._update_title()
        self.set_status(tr("status.saved", path=self.current_path))
        return True

    def save_file_as(self, path: Optional[str] = None) -> bool:
        path = path or self.window.ask_save_path(self.display_name)
        if not path:
            return False
        self.current_path = Path(path)
        self.display_name = self.current_path.name
        self.current_sample = None
        return self.save_file()

    def translate_program(self) -> None:
        """Translates the program between English and Pashto (keywords, built-ins, digits, punctuation)."""
        if self.toolchain is None:
            self.set_status(tr("status.toolchain_missing", message=tr("status.build_first")))
            return
        text = self.window.editor.get_text()
        source = self.program_language(text)
        target = "en" if source != "en" else "ps"
        try:
            self.toolchain.language = source
            outcome = self.toolchain.translate(text, target)
        except ToolchainError as exc:
            self.log(str(exc), "stderr")
            return
        finally:
            self.toolchain.language = self.language
        if not outcome.ok:
            self.log(outcome.messages, "stderr")
            self.set_status(tr("status.translate_failed"))
            return
        if outcome.messages:
            self.log(outcome.messages)
        self.window.editor.replace_text(outcome.text)
        self.clear_results()
        self.refresh_highlighting()
        self.set_status(tr("status.translated", language=i18n.UI_LANGUAGES[target]["name"]))

    # ------------------------------------------------------------------ interface language

    def on_language_selected(self, index: int) -> None:
        codes = list(i18n.UI_LANGUAGES)
        if 0 <= index < len(codes):
            self.set_language(codes[index])

    def set_language(self, code: str) -> None:
        """Switches the interface language (and the default program language), rebuilding the window."""
        if code == self.language or code not in i18n.UI_LANGUAGES:
            return
        self.stop_program()
        layout = self.window.current_layout()
        text = self.window.editor.get_text()
        modified = self.window.editor.is_modified()
        sample_name = self.current_sample.name if self.current_sample and not modified else None

        i18n.set_language(code)
        self.settings.language = code
        if self.toolchain is not None:
            self.toolchain.language = code
        self.samples = list_samples(code)

        self.window.close()
        gc.collect()  # free the old window's widgets on this (the GUI) thread
        self.settings.layout = layout
        self.window = self._create_window()
        self.status = tr("status.ready")
        self.set_status(self.status)

        replacement = counterpart(sample_name, code) if sample_name else None
        if replacement is not None:
            index = [s.name for s in self.samples].index(replacement.name)
            self.load_sample(index)
        else:
            self.load_text(text, self.display_name, self.current_path, self.current_sample)
            self.window.editor.set_modified(modified)
            self._update_title()

    # ------------------------------------------------------------------ compiling

    def clear_results(self) -> None:
        self.trace = None
        self.last_compile = None
        self.compiled_text = None
        w = self.window
        w.overview.set_lines([StyledText(text, style) for text, style in explain.pipeline_lines(None)])
        for panel in (w.tokens, w.ast, w.symbols, w.bytecode, w.problems):
            panel.set_rows([])
        w.bytecode.set_footer("")
        w.binary.set_lines([])
        w.vm.show(None)
        w.output.clear()
        w.editor.clear_marks()
        self._reset_canvas()

    def compile(self) -> Optional[CompileOutcome]:
        """Compiles the editor contents and shows every phase's results. Returns None if the compiler could not run."""
        if self.run is not None and self.run.running:
            self.set_status(tr("status.busy"))
            return None
        if self.toolchain is None:
            self.set_status(tr("status.toolchain_missing", message=tr("status.build_first")))
            return None
        self.set_status(tr("status.compiling"))
        text = self.window.editor.get_text()
        self.clear_results()
        try:
            outcome = self.toolchain.compile(text, self.display_name, self.program_dir())
        except ToolchainError as exc:
            self.log(str(exc), "stderr")
            self.set_status(tr("status.toolchain_missing", message=exc))
            return None
        self.last_compile = outcome
        self.compiled_text = text
        self._show_compile_results(outcome)
        return outcome

    # ------------------------------------------------------------------ editing commands
    #
    # The text work itself is in core.edits; these methods only move it between the editor and
    # the status bar, so the same commands work in any toolkit.

    def toggle_comment(self) -> None:
        """Comments the selected lines, or uncomments them if they are all commented."""
        text = self.window.editor.get_text()
        start, end = self.window.editor.selection()
        new_text, new_start, new_end = edits.toggle_comment(text, start, end)
        if new_text == text:
            return
        self.window.editor.replace_text(new_text)
        self.window.editor.set_selection(new_start, new_end)
        self.refresh_highlighting()

    def _line_edit(self, operation: Callable[[str, int, int], tuple[str, int, int]]) -> None:
        """Applies one of core.edits' line operations to the selected lines (or the caret's line)."""
        editor = self.window.editor
        text = editor.get_text()
        start, end = editor.selection()
        new_text, new_start, new_end = operation(text, start, end)
        if new_text == text and (new_start, new_end) == (start, end):
            return
        if new_text != text:
            editor.replace_text(new_text)
        editor.set_selection(new_start, new_end)
        self.refresh_highlighting()

    def go_to_line(self) -> None:
        """Asks for a line number and puts the caret there."""
        editor = self.window.editor
        last = editor.get_text().count("\n") + 1
        line = self.window.ask_go_to_line(editor.cursor_line(), last)
        if line is not None:
            editor.go_to_line(max(1, min(line, last)))

    def tidy_layout(self) -> None:
        """Tidies tabs, trailing spaces and blank lines, without changing the indentation."""
        text = self.window.editor.get_text()
        tidied = edits.tidy(text)
        if tidied == text:
            self.set_status(tr("status.already_tidy"))
            return
        self.window.editor.replace_text(tidied)
        self.refresh_highlighting()
        self.set_status(tr("status.tidied"))

    def find_in_editor(self, request: FindRequest) -> bool:
        """Selects the next (or previous) match of ``request``; False if there is none."""
        self._last_find = request
        text = self.window.editor.get_text()
        start, end = self.window.editor.selection()
        # Carry on from the far end of what is selected, so "find next" moves forward.
        from_index = start if request.backwards else max(end, start)
        found = edits.find(text, request.needle, from_index, case_sensitive=request.case_sensitive,
                           whole_word=request.whole_word, backwards=request.backwards)
        if found is None:
            self.set_status(tr("status.not_found", needle=request.needle))
            return False
        self.window.editor.set_selection(*found)
        self.set_status(tr("status.ready"))
        return True

    def find_again(self, backwards: bool) -> None:
        """Repeats the last search (F3); opens the find bar when there is nothing to repeat."""
        if self._last_find is None or not self._last_find.needle:
            self.window.show_find(False)
            return
        self.find_in_editor(replace(self._last_find, backwards=backwards))

    def replace_in_editor(self, request: FindRequest, everywhere: bool) -> None:
        """Replaces the selection if it is a match and finds the next one, or replaces them all."""
        text = self.window.editor.get_text()
        if everywhere:
            new_text, count = edits.replace_all(text, request.needle, request.replacement,
                                                case_sensitive=request.case_sensitive,
                                                whole_word=request.whole_word)
            if count:
                self.window.editor.replace_text(new_text)
                self.refresh_highlighting()
            self.set_status(tr("status.replaced", count=count) if count
                            else tr("status.not_found", needle=request.needle))
            return
        start, end = self.window.editor.selection()
        selected = text[min(start, end):max(start, end)]
        same = selected == request.needle or (not request.case_sensitive
                                              and selected.casefold() == request.needle.casefold())
        if selected and same:
            self.window.editor.replace_range(min(start, end), max(start, end), request.replacement)
            self.refresh_highlighting()
        self.find_in_editor(replace(request, backwards=False))

    def fold(self, everything: bool) -> None:
        """Folds every block, or just the one the cursor is in."""
        text = self.window.editor.get_text()
        if everything:
            regions = [(fold.header, fold.last) for fold in edits.fold_regions(text)]
        else:
            here = edits.fold_at(text, max(self.window.editor.cursor_line() - 1, 0))
            if here is None:
                self.set_status(tr("status.no_fold"))
                return
            regions = [(here.header, here.last)]
        self.set_status(tr("status.folded", count=self.window.editor.fold(regions)))

    def unfold(self) -> None:
        self.window.editor.unfold_all()
        self.set_status(tr("status.unfolded"))

    def toggle_whitespace(self) -> None:
        self.settings.show_whitespace = not self.settings.show_whitespace
        self.window.editor.set_show_whitespace(self.settings.show_whitespace)
        self.window.set_command_checked("show_whitespace", self.settings.show_whitespace)
        self.set_status(tr("status.whitespace_on" if self.settings.show_whitespace else "status.whitespace_off"))

    def package_program(self) -> None:
        """Writes the program in the editor as an executable the operating system can run."""
        if self.run is not None and self.run.running:
            self.set_status(tr("status.busy"))
            return
        if self.toolchain is None:
            self.set_status(tr("status.toolchain_missing", message=tr("status.build_first")))
            return
        extension = ".exe" if sys.platform == "win32" else ""
        suggested = Path(self.display_name).stem + extension
        path = self.window.ask_save_path(suggested, extension)
        if not path:
            return
        self.set_status(tr("status.packaging"))
        try:
            outcome = self.toolchain.package(self.window.editor.get_text(), self.display_name, path,
                                             self.program_dir())
        except ToolchainError as exc:
            self.log(str(exc), "stderr")
            self.set_status(tr("status.toolchain_missing", message=exc))
            return
        self.log("$ " + format_command(outcome.command), "command")
        if outcome.message:
            self.log(outcome.message + "\n", "stdout" if outcome.ok else "stderr")
        self.set_status(tr("status.packaged", path=outcome.output) if outcome.ok else tr("status.package_failed"))

    def _show_compile_results(self, outcome: CompileOutcome) -> None:
        w = self.window
        report = outcome.report
        self.log("$ " + format_command(outcome.command), "command")
        for phase in report.get("phases", []):
            state = tr("phase.ok") if phase["succeeded"] else (tr("phase.failed") if phase["ran"] else tr("phase.skipped"))
            self.log(f"  {phase['name']:<20} {state:<8} {phase['microseconds'] / 1000:.2f} ms")
        w.overview.set_lines([StyledText(text, style) for text, style in explain.pipeline_lines(report)])
        w.tokens.set_rows(viewmodels.token_rows(report))
        w.ast.set_rows(viewmodels.ast_rows(report))
        w.symbols.set_rows(viewmodels.symbol_rows(report))
        w.bytecode.set_rows(viewmodels.bytecode_rows(report))
        w.binary.set_lines(viewmodels.binary_lines(report))
        w.problems.set_rows(viewmodels.problem_rows(outcome.diagnostics))
        for diagnostic in outcome.diagnostics:
            if diagnostic["severity"] == "error" and viewmodels.in_program(diagnostic):
                w.editor.mark_error(Span.of(diagnostic))  # (errors in imported modules are listed, not marked)
        if outcome.success:
            self.set_status(tr("status.compiled", warnings=outcome.warning_count))
            w.output.append(tr("output.compiled", name=self.display_name, size=report["binary"]["size"]) + "\n", "success")
        else:
            self.set_status(tr("status.compile_failed", errors=outcome.error_count, warnings=outcome.warning_count))
        for diagnostic in outcome.diagnostics:
            w.output.append(diagnostic["text"] + "\n", "stderr" if diagnostic["severity"] == "error" else "info")
        if not outcome.success:
            w.output.append("\n" + explain.failure_explanation(report) + "\n", "info")
            w.select_panel_tab("problems")

    def program_dir(self) -> Optional[Path]:
        """The directory the program came from: imported files are looked up there, and it is
        the program's working directory when it runs."""
        if self.current_path is not None:
            return self.current_path.parent
        if self.current_sample is not None:
            return self.current_sample.path.parent
        return None

    def _compiled_bytecode(self) -> Optional[Path]:
        """Compiles if the editor changed since the last compilation; returns the bytecode path."""
        if self.last_compile is None or self.compiled_text != self.window.editor.get_text():
            if self.compile() is None:
                return None
        assert self.last_compile is not None
        return self.last_compile.bytecode_path if self.last_compile.success else None

    # ------------------------------------------------------------------ running

    def run_program(self) -> None:
        self._start(trace=False)

    def debug_program(self) -> None:
        self._start(trace=True)

    def _start(self, trace: bool) -> None:
        if self.run is not None and self.run.running:
            self.set_status(tr("status.busy"))
            return
        bytecode = self._compiled_bytecode()
        if bytecode is None or self.toolchain is None:
            return
        w = self.window
        w.output.clear()
        self.trace = None
        w.vm.show(None)
        w.editor.clear_marks()
        self._reset_canvas()
        w.select_panel_tab("output")
        w.set_running(True)
        self.set_status(tr("status.tracing" if trace else "status.running"))
        post = self.scheduler.post
        try:
            self.run = self.toolchain.start_run(
                bytecode,
                on_output=lambda stream, text: post(lambda: self._on_output(stream, text)),
                on_exit=lambda result: post(lambda: self._on_exit(result, trace)),
                max_steps=DEBUG_MAX_STEPS if trace else self.max_steps,
                trace_limit=DEBUG_TRACE_LIMIT if trace else None,
                cwd=self.program_dir(),
                on_graphics=self._on_graphics_record,  # (called on the thread that reads the output)
            )
        except OSError as exc:
            w.set_running(False)
            self.log(tr("log.vm_failed", message=exc), "stderr")
            return
        self.log("$ " + format_command(self.run.command), "command")

    def stop_program(self) -> None:
        if self.run is not None and self.run.running:
            self.run.stop()

    # ------------------------------------------------------------------ canvas (module graphics)

    def _reset_canvas(self) -> None:
        self.canvas.reset()
        self._canvas_shown = False
        self.window.canvas.show(None)
        self.window.canvas.set_status(tr("canvas.hint"))

    def _on_graphics_record(self, record: dict) -> None:
        """A frame from the running program. Runs on a background thread: the frame is applied to
        the model there, and the GUI repaints the newest picture when it is ready."""
        if not self.canvas.apply(record):
            return
        with self._canvas_lock:
            if self._canvas_refresh_pending:
                return
            self._canvas_refresh_pending = True
        self.scheduler.post(self._refresh_canvas)

    def _refresh_canvas(self) -> None:
        with self._canvas_lock:
            self._canvas_refresh_pending = False
        if self._closed:
            return
        snapshot = self.canvas.snapshot()
        if not self._canvas_shown:
            # The first frame of this run: show the canvas and give it the keyboard (for games).
            self._canvas_shown = True
            self.window.select_panel_tab("canvas")
            self.window.canvas.focus()
        self.window.canvas.show(snapshot)
        self.window.canvas.set_status(tr("canvas.status", frame=snapshot.frame, width=snapshot.width,
                                         height=snapshot.height))

    def on_canvas_key(self, name: str, pressed: bool) -> None:
        """A key pressed or released on the canvas: the program's key_pressed() and key_down() see it."""
        if self.run is not None and self.run.running:
            self.run.write_input(key_event(name, pressed))

    def on_canvas_mouse(self, x: int, y: int, pressed: bool) -> None:
        """The mouse moved or clicked on the canvas (in canvas pixels)."""
        if self.run is not None and self.run.running:
            self.run.write_input(mouse_event(x, y, pressed))

    def send_input(self, text: str) -> None:
        """Sends a line typed in the Output panel to the running program (what input() reads)."""
        if self.run is None or not self.run.running:
            self.set_status(tr("status.not_running"))
            return
        self.window.output.append(text + "\n", "input")  # a terminal would echo it, too
        if not self.run.write_input(text + "\n"):
            self.set_status(tr("status.input_closed"))

    def end_input(self) -> None:
        """Ends the running program's input (as Ctrl+D or Ctrl+Z would in a terminal)."""
        if self.run is not None and self.run.running:
            self.run.close_input()
            self.window.output.append(tr("output.input_ended") + "\n", "info")

    def _on_output(self, stream: str, text: str) -> None:
        if not self._closed:
            self.window.output.append(text, "stderr" if stream == "stderr" else None)

    def _on_exit(self, result: RunResult, trace: bool) -> None:
        if self._closed:
            return
        w = self.window
        w.set_running(False)
        if result.stopped:
            w.output.append("\n" + tr("status.stopped") + "\n", "info")
            self.set_status(tr("status.stopped"))
            return
        if result.error:
            self.log(tr("log.results_unreadable", message=result.error), "stderr")
        error = result.runtime_error
        stats = result.stats
        report = self.last_compile.report if self.last_compile else None
        w.overview.set_lines([StyledText(text, style) for text, style in explain.pipeline_lines(report, stats, error)])
        if stats:
            self.log(tr("log.executed", steps=stats["steps"], calls=stats["calls"], stack=stats["max_stack_depth"],
                        depth=stats["max_call_depth"]))
            if stats.get("frames"):
                self.log(tr("log.frames", frames=stats["frames"]))
        if error:
            w.problems.set_rows(viewmodels.problem_rows(self.last_compile.diagnostics if self.last_compile else [],
                                                        error))
            if error.get("line") and not error.get("file_name"):  # (not in an imported module)
                w.editor.mark_error(Span(error["line"], error["column"]))
            self.set_status(tr("status.runtime_error", message=error.get("message", "")))
        else:
            w.output.append("\n" + tr("status.finished", code=result.exit_code) + "\n", "info")
            self.set_status(tr("status.finished", code=result.exit_code))
        if trace and result.trace is not None:
            self.trace = TraceSession(result.trace)
            self._show_trace()
            w.select_panel_tab("vm")
            self.set_status(tr("status.trace_ready", steps=self.trace.total))

    # ------------------------------------------------------------------ debugger

    def on_vm_navigate(self, action: str) -> None:
        if self.trace is None:
            return
        actions: dict[str, Callable[[], None]] = {
            "first": self.trace.first, "back": self.trace.back, "forward": self.trace.forward, "last": self.trace.last,
            "back_line": self.trace.back_line, "forward_line": self.trace.forward_line,
        }
        actions[action]()
        self._show_trace()

    def _show_trace(self) -> None:
        state = viewmodels.vm_state(self.trace)
        self.window.vm.show(state)
        self.window.editor.show_current_line(state.line if state else 0)

    # ------------------------------------------------------------------ view

    def zoom(self, delta: int) -> None:
        self.settings.font_size = max(8, min(28, self.settings.font_size + delta))
        self.window.editor.set_font_size(self.settings.font_size)
        percent = round(self.settings.font_size * 100 / DEFAULT_FONT_SIZE)
        self.set_status(tr("status.zoom", size=self.settings.font_size, percent=percent))

    def reset_layout(self) -> None:
        self.window.apply_layout(Layout.default())

    def on_panel_toggled(self, panel: str, visible: bool) -> None:
        self.window.set_panel_visible(panel, visible)

    def show_reference(self, document: str) -> None:
        """Opens one document of the SDK reference in the Help viewer (all documents are
        loaded, so the viewer can list them and follow the links between them). In a
        non-English interface a translation from docs/<language>/ is shown when there is one."""
        self.window.show_help(docs.load_reference(self.language), document)

    def show_about(self) -> None:
        self.window.show_info(tr("menu.about"), tr("dialog.about", version=__version__, toolkit=self.backend.name))

    # ------------------------------------------------------------------ shutdown

    def save_settings(self) -> None:
        self.settings.layout = self.window.current_layout()
        if self.settings_path is not None:
            self.settings.save(self.settings_path)

    def quit(self) -> None:
        if self._confirm_discard():
            self.close()

    def close(self) -> None:
        """Stops any running program, saves the settings and closes the window."""
        if self._closed:
            return
        if self.run is not None and self.run.running:
            self.run.stop()
            self.run.wait(10)
        if self._highlight_job is not None:
            self.scheduler.cancel(self._highlight_job)
            self._highlight_job = None
        self.save_settings()
        self._closed = True
        self.window.close()
        self.backend.quit()
