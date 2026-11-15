# Changelog

## 0.5.6 - 2026-11-15

* **No more jumping while stepping in the right-to-left editor.** The page scrolled the stopped
  line to the middle at every stop; it now scrolls only when the line is out of view, as VS
  Code's editor does.
* **The debugger speaks the program's language.** Its remarks in the Debug Console and the
  Thunder Output view - the exit code, "the program waits for input", a stop by the debugger -
  and the input box's title and prompt are in Pashto for a Pashto program (`src/debug/messages.js`).

## 0.5.5 - 2026-11-15

* **Types in the program's language.** The compiler records the built-ins' signatures in English
  (`file_exists(path: string): bool`); completion details, signature help and hovers now show
  them with the language's type words and comma (`فایل شته(path: متن): منطقي`). The debugger
  names a value's type in the program's language too - in the Variables view, watches and
  hovers - and describes arrays, tuples and maps by what they hold (`عدد[]`, `(عدد، متن)`).
* **Completion no longer leaves the typed letter behind.** The page measures the word being
  completed in its own text (the document may not have received the last keystroke yet), and a
  multi-word Pashto name such as `فایل شته` can be typed on past its space with the list open.
* **Thunder: Translate** works from the right-to-left editor as well as the text editor (it used
  to look only at the text editor and report a missing toolchain), translates the program in
  place as the Thunder IDE's Translate button does - one Undo takes it back, nothing is saved -
  and moves the file to the editor that suits its new language. It is on the right-to-left
  editor's title bar and on `Ctrl+Alt+T` there too.
* **Thunder Output**, a view in the panel beside the Debug Console, shows a debugged program's
  output aligned to the program's side - right for Pashto, which the Debug Console cannot do -
  with an input line for `input()`. It comes forward by itself for Pashto programs
  (`thunder.debug.outputView`: `auto`, `always` or `off`).

## 0.5.4 - 2026-11-15

* **The right-to-left editor has the text editor's language features.** Instead of a small
  completion list of its own, the editor now asks the extension, which runs VS Code's
  `vscode.execute…Provider` commands on the document - the same providers the text editor uses.
  So `Ctrl+Space` offers the whole SDK and the program's names, with signatures, inserted with
  their brackets, and the list opens at the caret (it used to open in the corner of the page on
  an empty line, where a collapsed selection has no box). Also new: parameter hints, language
  hovers, go to definition (`F12`, `Ctrl+click`), references (`Shift+F12`), rename (`F2`),
  format (`Shift+Alt+F`), quick fixes (`Ctrl+.`), go to symbol (`Ctrl+Shift+O`), occurrence
  highlighting, semantic colours, problem squiggles, `Ctrl+/` comments and the find widget.
  The README lists what each editor offers.

## 0.5.3 - 2026-11-15

* **Hovers while debugging in the right-to-left editor.** Resting the pointer on a name (or a
  `point.x` chain) while the program is stopped asks the debugger for its value and shows it by
  the name, as VS Code's text editor does; a word that is not a variable shows nothing. The
  tooltip disappears when the program runs on.

## 0.5.2 - 2026-11-15

* **No more flicker while stepping through a Pashto program.** At every stop VS Code opened the
  text editor for the stack frame's file, and the extension swapped it back for the right-to-left
  editor at once. For a file the right-to-left editor shows, the adapter now gives VS Code no
  `source` for the frame (the file travels in a `thunderFile` field instead), so nothing opens;
  the right-to-left editor draws the position itself, as before.
* **One path comparison** (`sameFile` in `util.js`) replaces a dozen hand-written
  `path.resolve(a) === path.resolve(b)` checks. It ignores case on Windows, where VS Code may
  spell a drive letter `c:` while the toolchain says `C:`.
* Removed exports nothing imported, an unused report property, and the unused `resultJson`
  run option; `thundervm` looks line-table entries up by binary search.

## 0.5.1 - 2026-11-15

* **A live debugger.** F5 now runs the program under `thundervm --debug` instead of replaying a
  recording: breakpoints stop the program as it reaches them (with conditions such as `n == 2`
  and hit counts), **Pause** works, every frame of the call stack shows its locals, arrays,
  structs and maps open element by element, watches and hovers evaluate variables and
  comparisons, `input()` asks in an input box or the Debug Console, and a runtime error stops the
  program in the state it failed in. **Step Back** and **Reverse Continue** remain: the machine
  remembers the lines it ran (`thunder.debug.history`). The protocol is documented in
  `docs/debugging.md`. The `traceLimit` and `input` launch attributes and the
  `thunder.trace.limit` setting are gone; `history`, `display` and `maxSteps` are new.
* **Debugging in the right-to-left editor.** VS Code draws nothing inside a custom editor, so the
  yellow arrow never reached a Pashto program: the editor now draws the stopped line, the frame
  chosen in the Call Stack and the breakpoints itself, and a click on a line number adds or
  removes a breakpoint. F5 also finds the program when the right-to-left editor is the active one.
* **The debugger no longer starts in the middle of the program.** The default values of the
  globals were attributed to each declaration's line, so a session began on the last `var` and
  jumped back; the jump that closes a loop was attributed to the line after the loop. Both are
  compiler fixes (`thunderc`), so the Qt IDE's step-by-step debugger benefits as well.

## 0.2.2 - 2026-11-13

* **A right-to-left editor** replaces the read-only right-to-left view. A Pashto program opens
  in it by itself (`thunder.rightToLeft.editor`); it lays lines out as the Thunder IDE does and
  *edits* them - typing, Enter with indentation, Tab, Backspace, undo and redo, completion, the
  compiler's problems - on the same document as VS Code's text editor, which is one click away.
  Pashto is drawn in Vazirmatn, which ships with the extension (`thunder.rightToLeft.pashtoFont`).
* **The Windows installer no longer reports an error after a successful install.** Windows
  PowerShell 5.1 turned a Node.js deprecation warning printed by `code.cmd` into a
  `NativeCommandError`, and made the script exit with code 1. Tools now run through a helper
  that never lets standard error stop the script, and Node's warnings are switched off.
* Revised Pashto names from the compiler (`لیرې کړه`, `کښته کړه`, `نیژدې کړه`, `کوچنی توري کړه`,
  `توان`, `موږک افقي`, `موږک عمودي`) in the grammar, snippets and completion.

## 0.2.1 - 2026-11-13

* **Installing on Windows.** The installers now package the extension as a `.vsix` with
  `tools/make_vsix.py` (the Python standard library alone - no Node.js, no npm, no `vsce`) and
  hand it to `code --install-extension`, which is the route VS Code supports. `--link`/`-Link`
  still links the sources in for development, and `--copy`/`-Copy` copies them. The README says
  plainly what an installed package does about updates (nothing: it is a snapshot) and how to
  develop against the sources instead.
* **Right-to-left programs.** A Pashto program now opens a **right-to-left view** beside the
  editor, laid out token by token by the Thunder IDE's own algorithm (`src/direction.js`), with
  the compiler's colours, following the caret; the status bar names the language of the file.
  VS Code's editor itself cannot be laid out right to left by an extension - the README explains
  exactly why.
* **A multi-file sample project** to try all of this on: `samples/project/` (and
  `samples/project/pashto/`).
* Seven tests failed on Windows because their made-up absolute paths (`\work\game`) had no
  drive letter; they now build paths the platform calls absolute.

## 0.2.0 - 2026-11-12

The first release, shipped with Thunder 0.2.0.

* **Highlighting.** A TextMate grammar generated from the compiler's language packs (English and
  Pashto, including built-ins written as several words), refined by semantic tokens from
  `thunderc --json`.
* **Diagnostics** from the compiler itself, as you type or on save, with the phase and the error
  code; problems inside an imported module appear in that module's file; names the program never
  reads are greyed out.
* **IntelliSense:** completion (keywords, types, built-ins by imported module, names in scope,
  functions, structs, struct fields after `.`, modules after `import`), signature help, hovers
  with inferred types and the word's counterpart in the other language, go to definition across
  modules, references, rename, document and workspace symbols, inlay hints for inferred types.
* **Projects:** `thunder.json` with a JSON schema, a project view, several folders per workspace,
  and **Thunder: New Project…** with English and Pashto templates.
* **Building and running:** `thunder` tasks (build, run, check, package) with the `$thunderc`
  problem matcher; programs run in a terminal so `input()` works.
* **Debugging:** a debug adapter over `thundervm --trace-json` with breakpoints, stepping in both
  directions, reverse continue, the call stack, locals, globals, the operand stack and the
  current instruction.
* **Under the bonnet:** the token stream, syntax tree, symbols, bytecode listing and hex dump as
  documents beside the program.
* **Translate** a file between English and Pashto (`thunderc --translate`), and
  **Run and Save the Picture (SVG)** for a graphics program (`thundervm --svg`).
