"""User-interface text in every supported language.

Every string the IDE shows comes from this table. Each language also says whether it is
written right to left; the GUI mirrors its layout for such languages.

NOTE FOR REVIEWERS: the Pashto (ps) texts were drafted for the first release and should be
checked by a native speaker familiar with computing terminology.
"""

from __future__ import annotations

_ENGLISH = {
    "app.title": "Thunder IDE",
    # menus and commands
    "menu.file": "File", "menu.edit": "Edit", "menu.build": "Build", "menu.view": "View", "menu.help": "Help",
    "menu.new": "New", "menu.open": "Open...", "menu.save": "Save", "menu.save_as": "Save As...", "menu.exit": "Exit",
    "menu.translate": "Translate program (English / Pashto)",
    "menu.compile": "Compile", "menu.run": "Run", "menu.debug": "Debug (record trace)", "menu.stop": "Stop",
    "menu.find": "Find...", "menu.find_next": "Find next", "menu.find_previous": "Find previous",
    "menu.replace": "Find and replace...", "menu.comment": "Comment / uncomment lines",
    "menu.tidy": "Tidy layout (tabs, trailing spaces)",
    "menu.fold": "Fold block", "menu.unfold": "Unfold block",
    "menu.fold_all": "Fold all blocks", "menu.unfold_all": "Unfold all blocks",
    "menu.show_whitespace": "Show spaces and tabs",
    "menu.package": "Package as an application...",
    "menu.zoom_in": "Larger font", "menu.zoom_out": "Smaller font", "menu.reset_layout": "Reset panel layout",
    "menu.reference": "Language reference", "menu.library_reference": "Library reference (built-in functions)",
    "menu.graphics_reference": "Graphics reference (canvas, animation, games)",
    "menu.bytecode_reference": "Bytecode reference",
    "menu.native_reference": "Native interface reference (calling C++)",
    "menu.packaging_reference": "Packaging reference (standalone applications)",
    "menu.display_reference": "Display reference (a window outside the IDE)", "menu.about": "About",
    "menu.panels": "Panels",
    "toolbar.sample": "Sample:", "toolbar.language": "Language:", "toolbar.compile": "Compile", "toolbar.run": "Run",
    "toolbar.debug": "Debug", "toolbar.stop": "Stop", "toolbar.translate": "Translate",
    # panels and tabs
    "panel.editor": "Editor", "panel.education": "Under the bonnet", "panel.output": "Output",
    "tab.overview": "Pipeline", "tab.tokens": "Tokens", "tab.ast": "Syntax Tree", "tab.symbols": "Symbols",
    "tab.bytecode": "Bytecode", "tab.binary": "Binary", "tab.vm": "VM", "tab.output": "Output",
    "tab.problems": "Problems", "tab.log": "Build Log",
    # status bar and messages
    "status.ready": "Ready.", "status.compiling": "Compiling...",
    "find.find": "Find:", "find.replace": "Replace with:", "find.case": "Match case",
    "find.word": "Whole word", "find.next": "Next", "find.previous": "Previous",
    "find.replace_one": "Replace", "find.replace_all": "Replace all", "find.close": "Close",
    "status.not_found": "\"{needle}\" was not found.", "status.replaced": "{count} replaced.",
    "status.folded": "{count} block(s) folded.", "status.unfolded": "All blocks unfolded.",
    "status.no_fold": "The cursor is not inside a block that can be folded.",
    "status.whitespace_on": "Spaces and tabs are shown.",
    "status.whitespace_off": "Spaces and tabs are hidden.",
    "status.tidied": "Layout tidied.", "status.already_tidy": "The layout was already tidy.",
    "status.packaging": "Packaging...", "status.packaged": "Packaged as {path}.",
    "status.package_failed": "The program could not be packaged.",
    "status.compiled": "Compiled successfully ({warnings} warning(s)).",
    "status.compile_failed": "Compilation failed: {errors} error(s), {warnings} warning(s).",
    "status.running": "Running...", "status.finished": "Program finished (exit code {code}).",
    "status.runtime_error": "Runtime error: {message}", "status.stopped": "Stopped by the user.",
    "status.tracing": "Recording an execution trace...",
    "status.trace_ready": "Trace recorded: {steps} step(s). Use the VM tab to step through.",
    "status.saved": "Saved {path}.", "status.opened": "Opened {path}.",
    "status.busy": "A program is already running. Press Stop first.",
    "status.toolchain_missing": "Toolchain not found: {message}",
    "status.build_first": "build the C++ tools first (./run.sh or run.ps1)",
    "status.translated": "Program translated into {language}.",
    "status.translate_failed": "The program could not be translated (see the Build Log).",
    "output.compiled": "Compiled {name}: {size} bytes of bytecode.",
    "panel.canvas": "Canvas",
    "canvas.hint": "Programs that import graphics draw here. Click the picture to give it the keyboard.",
    "canvas.status": "Frame {frame} · {width} × {height} pixels · click the picture, then use the keyboard",
    "log.frames": "  {frames} frame(s) shown on the canvas.",
    "output.input_label": "Input:", "output.send": "Send", "output.end_input": "End input",
    "output.input_hint": "Type a line for input() and press Enter (while the program runs)",
    "output.input_ended": "[end of input]",
    "status.not_running": "No program is running: press Run first.",
    "status.input_closed": "The program does not read input any more.",
    "log.executed": "  executed {steps} instructions, {calls} call(s), max stack depth {stack}, max call depth {depth}",
    "log.results_unreadable": "could not read the VM results: {message}",
    "log.vm_failed": "failed to start the VM: {message}",
    "editor.untitled": "untitled.th", "editor.modified": "(modified)",
    # columns
    "column.index": "#", "column.position": "Line:Col", "column.kind": "Kind", "column.category": "Category",
    "column.lexeme": "Lexeme", "column.type": "Type", "column.slot": "Slot", "column.line": "Line",
    "column.offset": "Offset", "column.bytes": "Bytes", "column.instruction": "Instruction",
    "column.comment": "Comment", "column.phase": "Phase", "column.code": "Code", "column.message": "Message",
    "column.name": "Name", "column.value": "Value",
    # symbols and bytecode tabs
    "symbols.functions": "functions", "symbols.function": "function", "symbols.structs": "structs",
    "symbols.struct": "struct", "symbols.field": "field", "symbols.slots": "{count} slot(s)",
    "symbols.global": "global", "symbols.local": "local", "symbols.parameter": "parameter", "symbols.const": "const",
    "scope.global": "global scope", "scope.function": "function scope", "scope.block": "block scope",
    "scope.for": "for scope", "scope.lines": "(lines {start}-{end})",
    "bytecode.constants": "constant pool ({count})", "bytecode.structs": "structs ({count})",
    "bytecode.globals": "globals ({count})",
    "bytecode.function": "function #{index} {name}  (params {params}, locals {locals}, {size} bytes)",
    "binary.header": "; compiled program: {size} bytes, magic number \"THND\"",
    # debugger
    "vm.first": "|<", "vm.back_line": "<< Line", "vm.back": "< Step", "vm.forward": "Step >",
    "vm.forward_line": "Line >>", "vm.last": ">|",
    "vm.no_trace": "Press Debug to record an execution trace, then step through it here.",
    "vm.step": "Step {index} of {total}", "vm.truncated": " (trace truncated: only the first {total} steps were recorded)",
    "vm.stack": "Operand stack (top first)", "vm.frames": "Call frames", "vm.locals": "Locals of the current frame",
    "vm.globals": "Globals", "vm.output": "Output so far", "vm.next": "Next instruction:",
    "vm.finished": "Program finished.", "vm.error": "Runtime error: {message}",
    "vm.local": "local", "vm.local_slot": "local slot", "vm.operand": "operand",
    "vm.base": "base (stack index)", "vm.owner": "belongs to",
    # pipeline tab
    "overview.title": "The compilation pipeline",
    "overview.not_compiled": "Press Compile to see each phase of the compiler at work.",
    "overview.source": "source text", "overview.earlier_failed": "(an earlier phase failed)",
    "overview.see_problems": "(see the Problems tab)", "overview.timing": "{ms} ms, {detail}",
    "overview.execution": "{steps} instructions, {calls} call(s), max stack depth {depth}",
    "overview.press_run": "press Run or Debug", "overview.what_phases_do": "What each phase does",
    "phase.ok": "OK", "phase.failed": "FAILED", "phase.skipped": "not run",
    "phase.name.lexical": "lexical", "phase.name.syntax": "syntax", "phase.name.semantic": "semantic",
    "phase.name.runtime": "runtime", "severity.error": "error", "severity.warning": "warning",
    "phase.lexical.title": "Lexical analysis", "phase.lexical.produces": "tokens", "phase.lexical.tab": "Tokens tab",
    "phase.lexical.explanation": "The lexer reads the source one character at a time and groups the characters into "
                                 "tokens: keywords, names, numbers, strings, operators and punctuation. Spaces and "
                                 "comments are dropped, but it remembers where each line ends.",
    "phase.syntax.title": "Syntax analysis", "phase.syntax.produces": "abstract syntax tree",
    "phase.syntax.tab": "Syntax Tree tab",
    "phase.syntax.explanation": "The parser checks that the tokens follow the grammar and builds a tree that shows how "
                                "the program is structured. Statements are parsed by recursive descent; expressions "
                                "with Pratt's operator-precedence algorithm, which is why 1 + 2 * 3 groups as "
                                "1 + (2 * 3). A line break ends a statement, so no separator such as ';' is needed.",
    "phase.semantic.title": "Semantic analysis", "phase.semantic.produces": "typed tree + symbol table",
    "phase.semantic.tab": "Symbols tab",
    "phase.semantic.explanation": "The analyser links every name to its declaration, organises declarations into "
                                  "nested scopes, works out the type of every expression (including struct fields) "
                                  "and rejects programs that are grammatical but meaningless, such as adding a number "
                                  "to a bool.",
    "phase.codegen.title": "Code generation", "phase.codegen.produces": "bytecode (.thc file)",
    "phase.codegen.tab": "Bytecode and Binary tabs",
    "phase.codegen.explanation": "The code generator walks the typed tree and emits instructions for a stack machine: "
                                 "operands are pushed onto a stack and each operation pops its inputs and pushes its "
                                 "result. Loops and if statements become jumps.",
    "phase.execution.title": "Execution", "phase.execution.produces": "program output",
    "phase.execution.tab": "VM and Output tabs",
    "phase.execution.explanation": "The virtual machine loads the bytecode, checks it, and executes it one instruction "
                                   "at a time. It reports runtime errors such as division by zero with the source line "
                                   "and the chain of calls.",
    "detail.tokens": "{count} tokens", "detail.nodes": "{count} tree nodes",
    "detail.symbols": "{count} symbols, {functions} function(s), {structs} struct(s)",
    "detail.code": "{count} instructions, {size} bytes",
    "failure.internal": "The compiler itself failed (this is a bug in Thunder): {message}",
    "failure.stopped": "Compilation stopped during {phase}.",
    "failure.skipped": "Because of the errors, these phases did not run: {phases}.",
    # dialogs
    "dialog.yes": "Yes", "dialog.no": "No",
    "dialog.unsaved_title": "Unsaved changes", "dialog.unsaved_message": "Discard the changes to {name}?",
    "dialog.thunder_files": "Thunder source", "dialog.all_files": "All files",
    "dialog.cannot_open": "Cannot open {path}: {message}",
    "dialog.about": "Thunder IDE {version} ({toolkit})\n\nAn educational toolchain for learning how programming "
                    "languages work: lexing, parsing, type checking, code generation and execution on a virtual machine.",
}

_PASHTO = {
    "app.title": "د تندر IDE",
    "menu.file": "فایل", "menu.edit": "سمون", "menu.build": "جوړول", "menu.view": "لید", "menu.help": "مرسته",
    "menu.new": "نوی", "menu.open": "پرانیستل...", "menu.save": "ساتل", "menu.save_as": "په بل نوم ساتل...",
    "menu.exit": "وتل", "menu.translate": "پروګرام ژباړل (انګلیسي / پښتو)",
    "menu.compile": "جوړول", "menu.run": "چلول", "menu.debug": "ګام په ګام (ډیبګ)", "menu.stop": "درول",
    "menu.find": "لټون...", "menu.find_next": "راتلونکی ومومه", "menu.find_previous": "پخوانی ومومه",
    "menu.replace": "لټون او بدلول...", "menu.comment": "کرښې تبصره / بې تبصرې کړه",
    "menu.tidy": "بڼه سمه کړه (ټبونه، د پای تشې)",
    "menu.fold": "بلاک ټول کړه", "menu.unfold": "بلاک پرانیزه",
    "menu.fold_all": "ټول بلاکونه ټول کړه", "menu.unfold_all": "ټول بلاکونه پرانیزه",
    "menu.show_whitespace": "تشې او ټبونه وښایه",
    "menu.package": "د کړنلارې په بڼه بسته کړه...",
    "menu.zoom_in": "لوی لیک", "menu.zoom_out": "کوچنی لیک", "menu.reset_layout": "د پاڼو لومړنی ترتیب",
    "menu.reference": "د ژبې لارښود", "menu.library_reference": "د کتابتون لارښود (دننۍ دندې)",
    "menu.graphics_reference": "د ګرافیک لارښود (ټاټ، حرکت، لوبې)",
    "menu.bytecode_reference": "د بایټ کوډ لارښود",
    "menu.native_reference": "د بهرني اړیکي لارښود (د ++C بلل)",
    "menu.packaging_reference": "د بستې لارښود (خپلواکې کړنلارې)",
    "menu.display_reference": "د ښودونکي لارښود (له IDE بهر کړکۍ)", "menu.about": "په اړه",
    "menu.panels": "پاڼې",
    "toolbar.sample": "بېلګه:", "toolbar.language": "ژبه:", "toolbar.compile": "جوړول", "toolbar.run": "چلول",
    "toolbar.debug": "ډیبګ", "toolbar.stop": "درول", "toolbar.translate": "ژباړه",
    "panel.editor": "لیکونکی", "panel.education": "د پردې شاته", "panel.output": "پایله",
    "tab.overview": "پړاوونه", "tab.tokens": "توکي", "tab.ast": "نحوي ونه", "tab.symbols": "نښې",
    "tab.bytecode": "بایټ کوډ", "tab.binary": "بائنري", "tab.vm": "VM", "tab.output": "پایله",
    "tab.problems": "ستونزې", "tab.log": "د جوړولو یادښت",
    "status.ready": "چمتو.", "status.compiling": "جوړېږي...",
    "find.find": "ولټوه:", "find.replace": "بدل کړه په:", "find.case": "لوی/واړه توري",
    "find.word": "بشپړه کلمه", "find.next": "راتلونکی", "find.previous": "پخوانی",
    "find.replace_one": "بدل کړه", "find.replace_all": "ټول بدل کړه", "find.close": "بند کړه",
    "status.not_found": "«{needle}» ونه موندل شو.", "status.replaced": "{count} ځله بدل شو.",
    "status.folded": "{count} بلاک(ونه) ټول شول.", "status.unfolded": "ټول بلاکونه پرانیستل شول.",
    "status.no_fold": "کرسر په داسې بلاک کې نه دی چې ټولېدای شي.",
    "status.whitespace_on": "تشې او ټبونه ښکاري.",
    "status.whitespace_off": "تشې او ټبونه پټ دي.",
    "status.tidied": "بڼه سمه شوه.", "status.already_tidy": "بڼه دمخه سمه وه.",
    "status.packaging": "بسته کېږي...", "status.packaged": "په {path} کې بسته شو.",
    "status.package_failed": "پروګرام بسته نشو.",
    "status.compiled": "په بریالیتوب جوړ شو ({warnings} خبرداري).",
    "status.compile_failed": "جوړول ناکام شو: {errors} تېروتنې، {warnings} خبرداري.",
    "status.running": "چلېږي...", "status.finished": "پروګرام پای ته ورسېد (د وتلو کوډ {code}).",
    "status.runtime_error": "د اجرا تېروتنه: {message}", "status.stopped": "د کاروونکي له خوا ودرول شو.",
    "status.tracing": "د اجرا ګامونه ثبتېږي...",
    "status.trace_ready": "ګامونه ثبت شول: {steps} ګامونه. د VM پاڼه کې یې ګام په ګام وګورئ.",
    "status.saved": "{path} وساتل شو.", "status.opened": "{path} پرانیستل شو.",
    "status.busy": "یو پروګرام لا چلېږي. لومړی «درول» کېکاږئ.",
    "status.toolchain_missing": "وسایل ونه موندل شول: {message}",
    "status.build_first": "لومړی د C++ وسایل جوړ کړئ (./run.sh یا run.ps1)",
    "status.translated": "پروګرام په {language} وژباړل شو.",
    "status.translate_failed": "پروګرام ونه ژباړل شو (د جوړولو یادښت وګورئ).",
    "output.compiled": "{name} جوړ شو: {size} بایټه بایټ کوډ.",
    "panel.canvas": "ټاټ",
    "canvas.hint": "هغه پروګرامونه چې ګرافیک راوړي دلته انځور کاږي. د کیبورډ لپاره پر انځور کلیک وکړئ.",
    "canvas.status": "چوکاټ {frame} · {width} × {height} پکسله · پر انځور کلیک وکړئ، بیا کیبورډ وکاروئ",
    "log.frames": "  په ټاټ کې {frames} چوکاټونه وښودل شول.",
    "output.input_label": "ننوت:", "output.send": "ولېږه", "output.end_input": "د ننوت پای",
    "output.input_hint": "د ولوله() لپاره یوه کرښه ولیکئ او Enter کېکاږئ (کله چې پروګرام چلېږي)",
    "output.input_ended": "[د ننوت پای]",
    "status.not_running": "هېڅ پروګرام نه چلېږي: لومړی «چلول» کېکاږئ.",
    "status.input_closed": "پروګرام نور ننوت نه لولي.",
    "log.executed": "  {steps} لارښوونې اجرا شوې، {calls} بلنې، د سټک تر ټولو ژوروالی {stack}، د بلنو ژوروالی {depth}",
    "log.results_unreadable": "د VM پایلې ونه لوستل شوې: {message}",
    "log.vm_failed": "VM پیل نشو: {message}",
    "editor.untitled": "untitled.th", "editor.modified": "(بدل شوی)",
    "column.index": "#", "column.position": "کرښه:ستون", "column.kind": "ډول", "column.category": "ډله",
    "column.lexeme": "متن", "column.type": "ډول", "column.slot": "ځای", "column.line": "کرښه",
    "column.offset": "ځای", "column.bytes": "بایټونه", "column.instruction": "لارښوونه",
    "column.comment": "تبصره", "column.phase": "پړاو", "column.code": "کوډ", "column.message": "پیغام",
    "column.name": "نوم", "column.value": "ارزښت",
    "symbols.functions": "دندې", "symbols.function": "دنده", "symbols.structs": "جوړښتونه",
    "symbols.struct": "جوړښت", "symbols.field": "برخه", "symbols.slots": "{count} ځایونه",
    "symbols.global": "عمومي", "symbols.local": "ځایي", "symbols.parameter": "پارامټر", "symbols.const": "ثابت",
    "scope.global": "عمومي ساحه", "scope.function": "د دندې ساحه", "scope.block": "د بلاک ساحه",
    "scope.for": "د لپاره ساحه", "scope.lines": "(کرښې {start}-{end})",
    "bytecode.constants": "ثابتونه ({count})", "bytecode.structs": "جوړښتونه ({count})",
    "bytecode.globals": "عمومي متغیرونه ({count})",
    "bytecode.function": "دنده #{index} {name}  (پارامټرونه {params}، ځایي {locals}، {size} بایټه)",
    "binary.header": "; جوړ شوی پروګرام: {size} بایټه، جادويي شمېره \"THND\"",
    "vm.first": "|<", "vm.back_line": "<< کرښه", "vm.back": "< ګام", "vm.forward": "ګام >",
    "vm.forward_line": "کرښه >>", "vm.last": ">|",
    "vm.no_trace": "«ډیبګ» کېکاږئ چې د اجرا ګامونه ثبت شي، بیا یې دلته ګام په ګام وګورئ.",
    "vm.step": "ګام {index} له {total}", "vm.truncated": " (یوازې لومړي {total} ګامونه ثبت شول)",
    "vm.stack": "د عملیو سټک (پورته لومړی)", "vm.frames": "د بلنو چوکاټونه", "vm.locals": "د اوسني چوکاټ ځایي متغیرونه",
    "vm.globals": "عمومي متغیرونه", "vm.output": "تر اوسه پایله", "vm.next": "راتلونکې لارښوونه:",
    "vm.finished": "پروګرام پای ته ورسېد.", "vm.error": "د اجرا تېروتنه: {message}",
    "vm.local": "ځایي", "vm.local_slot": "ځایي ځای", "vm.operand": "عملیه",
    "vm.base": "بنسټ (د سټک ځای)", "vm.owner": "د چا دی",
    "overview.title": "د جوړولو پړاوونه",
    "overview.not_compiled": "«جوړول» کېکاږئ چې د جوړونکي هر پړاو په کار کې وګورئ.",
    "overview.source": "سرچینه متن", "overview.earlier_failed": "(یو مخکینی پړاو ناکام شو)",
    "overview.see_problems": "(د ستونزو پاڼه وګورئ)", "overview.timing": "{ms} ملي ثانیې، {detail}",
    "overview.execution": "{steps} لارښوونې، {calls} بلنې، د سټک ژوروالی {depth}",
    "overview.press_run": "«چلول» یا «ډیبګ» کېکاږئ", "overview.what_phases_do": "هر پړاو څه کوي",
    "phase.ok": "سم", "phase.failed": "ناکام", "phase.skipped": "ونه چلېد",
    "phase.name.lexical": "لغوي", "phase.name.syntax": "نحوي", "phase.name.semantic": "معنايي",
    "phase.name.runtime": "د اجرا", "severity.error": "تېروتنه", "severity.warning": "خبرداری",
    "phase.lexical.title": "لغوي تحلیل", "phase.lexical.produces": "توکي", "phase.lexical.tab": "د توکو پاڼه",
    "phase.lexical.explanation": "لغوي تحلیل سرچینه توری په توری لولي او توري په توکو کې ډلبندي کوي: کلیدي کلمې، "
                                 "نومونه، عددونه، متنونه، عملګر او نښې. تشې او تبصرې لرې کېږي، خو د کرښو پای یادوي.",
    "phase.syntax.title": "نحوي تحلیل", "phase.syntax.produces": "نحوي ونه", "phase.syntax.tab": "د نحوي ونې پاڼه",
    "phase.syntax.explanation": "نحوي تحلیل ګوري چې توکي د ګرامر سره سم دي او یوه ونه جوړوي چې د پروګرام جوړښت "
                                "ښيي. امرونه په بېرته ورګرځېدونکي ښکته تګ او افادې د Pratt په الګوریتم تحلیلېږي، "
                                "نو ۱ + ۲ × ۳ لکه ۱ + (۲ × ۳) ګروپ کېږي. د کرښې پای امر ختموي، نو '؛' ته اړتیا نشته.",
    "phase.semantic.title": "معنايي تحلیل", "phase.semantic.produces": "ډول لرونکې ونه + د نښو جدول",
    "phase.semantic.tab": "د نښو پاڼه",
    "phase.semantic.explanation": "معنايي تحلیل هر نوم د هغه اعلان سره تړي، اعلانونه په ساحو کې ترتیبوي، د هرې افادې "
                                  "(د جوړښت برخې هم) ډول معلوموي او هغه پروګرامونه ردوي چې ګرامري دي خو مانا نه "
                                  "لري، لکه عدد ته منطقي ارزښت ورزیاتول.",
    "phase.codegen.title": "د کوډ جوړول", "phase.codegen.produces": "بایټ کوډ (.thc فایل)",
    "phase.codegen.tab": "د بایټ کوډ او بائنري پاڼې",
    "phase.codegen.explanation": "کوډ جوړونکی د ډول لرونکې ونې په اوږدو کې ځي او د سټک ماشین لپاره لارښوونې جوړوي: "
                                 "ارزښتونه سټک ته ځي او هره عملیه خپل ارزښتونه اخلي او پایله یې بېرته سټک ته "
                                 "ورکوي. تکرارونه او شرطونه په ټوپونو بدلېږي.",
    "phase.execution.title": "اجرا", "phase.execution.produces": "د پروګرام پایله",
    "phase.execution.tab": "د VM او پایلې پاڼې",
    "phase.execution.explanation": "مجازي ماشین بایټ کوډ لوډوي، ګوري یې، او لارښوونه په لارښوونه یې اجرا کوي. د اجرا "
                                   "تېروتنې، لکه په صفر وېش، د سرچینې د کرښې او د بلنو د لړۍ سره ښيي.",
    "detail.tokens": "{count} توکي", "detail.nodes": "{count} د ونې غوټې",
    "detail.symbols": "{count} نښې، {functions} دندې، {structs} جوړښتونه",
    "detail.code": "{count} لارښوونې، {size} بایټه",
    "failure.internal": "پخپله جوړونکی ناکام شو (دا په تندر کې تېروتنه ده): {message}",
    "failure.stopped": "جوړول د {phase} پر مهال ودرېد.",
    "failure.skipped": "د تېروتنو له امله دا پړاوونه ونه چلېدل: {phases}.",
    "dialog.yes": "هو", "dialog.no": "نه",
    "dialog.unsaved_title": "ناساتل شوي بدلونونه", "dialog.unsaved_message": "د {name} بدلونونه پرېښودل شي؟",
    "dialog.thunder_files": "د تندر سرچینه", "dialog.all_files": "ټول فایلونه",
    "dialog.cannot_open": "{path} نشي پرانیستل کېدای: {message}",
    "dialog.about": "د تندر IDE {version} ({toolkit})\n\nد پروګرام جوړونې ژبو د زده کړې لپاره یوه وسیله: لغوي او "
                    "نحوي تحلیل، د ډولونو کتنه، د کوډ جوړول او په مجازي ماشین کې اجرا.",
}

UI_LANGUAGES: dict[str, dict[str, object]] = {
    "en": {"name": "English", "right_to_left": False, "strings": _ENGLISH},
    "ps": {"name": "پښتو", "right_to_left": True, "strings": _PASHTO},
}

_current = "en"


def set_language(code: str) -> None:
    """Selects the interface language (raises KeyError for unknown codes)."""
    global _current
    if code not in UI_LANGUAGES:
        raise KeyError(code)
    _current = code


def current_language() -> str:
    return _current


def is_right_to_left() -> bool:
    return bool(UI_LANGUAGES[_current]["right_to_left"])


def tr(key: str, **values: object) -> str:
    """Returns the text for ``key`` in the current language, formatted with ``values``.

    Unknown keys fall back to English, then to the key itself, so a missing translation
    never crashes the IDE.
    """
    for code in (_current, "en"):
        strings = UI_LANGUAGES[code]["strings"]
        assert isinstance(strings, dict)
        if key in strings:
            text = str(strings[key])
            return text.format(**values) if values else text
    return key
