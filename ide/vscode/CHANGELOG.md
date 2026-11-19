# Changelog

## 0.9.4 - 2026-11-19

- A graphics program debugged in VS Code (the Thunder Canvas) runs at nearly its own speed: the
  debugger kept a full snapshot of every variable at every line for Step Back, which slowed a
  Game of Life to a tenth of a frame per second. Large values are now remembered in brief, a
  program that draws gives the history at most a tenth of its time while it runs freely, and
  the debugger's per-instruction check takes a fast path (thundervm).
- The Thunder Canvas page paints at most once per screen refresh, with the newest frame, and
  no longer reallocates its pixels at every frame.
- The right-to-left editor's play button points right, as "play" does in every language.

## 0.9.3 - 2026-11-19

- The right-to-left editor's test buttons are drawn in a circle, as VS Code's are, so they read
  as buttons: a play triangle in a ring, a white tick on a green disc, a white cross on a red
  disc, a turning arc while it runs.
- Thunder Output colours a test file's transcript as the Thunder IDE does: ✓ green, ✗ red, the
  passed count green and a non-zero failed count red.
- A Pashto line in the Test Results view keeps two spaces of margin at its start (its right
  end), so its first letter is no longer hidden at the panel's edge.

## 0.9.2 - 2026-11-19

- The right-to-left editor's context menu has the text editor's test commands: Run Test at
  Cursor, Debug Test at Cursor, Run Tests in This File and Debug Tests in Current File.
- A test's item spans its whole function (a suite's, its whole struct or module), so "at
  cursor" finds the test from anywhere in its body - in the text editor too.
- The Test Results view no longer reverses the letters of Pashto words: the words of a line
  change places, and the terminal draws each word right to left by itself.
- The Pashto `@test` is written `@ازموینه`; `@آزموینه` is still understood.

## 0.9.1 - 2026-11-19

- The right-to-left editor shows a test file as the text editor does: a button beside every
  test and every suite - run, running, passed (✓) or failed (✗) - that runs it; a failed
  test's message at the end of its line; and a panel under the failed test, its arrow pointing
  at it, with everything it reported (× or Escape closes it; the message or a right-click on
  the ✗ opens it again). It follows every run, wherever it was started.
- A suite's own line (its `@test`) carries its button in both editors.
- The Test Results view shows a Pashto transcript right to left, against the right edge, and
  the marks and the summary in green and red in either language; Pashto failure messages read
  right to left.
- The Pashto mock annotation is `@پېښه ګر`.

## 0.9.0 - 2026-11-19

Follows the toolchain's 0.9.0.

- The Test Explorer finds tests by their annotations: a function marked `@test` (Pashto
  `@آزموینه`), and the suites - a struct or a module marked `@test`, whose public methods or
  functions without parameters are its tests, or a single marked method of a struct. A test
  file holds tests only; the old `test.run(...)` lines are gone.
- `@test` and `@mock` are coloured like decorators, in the text editor and the right-to-left
  editor; new snippets for a mixin, a test, a test suite and a mock.
- `mixin` (Pashto `ګډون`) opens a block, is coloured as a declaration and appears in the outline.
- Pashto hovers, completion and signature help show the compiler's Pashto signatures: parameter
  names and types in Pashto, with no English left.
- New Project writes its test file in the new style.

## 0.8.1 - 2026-11-18

- A Pashto program's hovers, completion details and signature help write every type in Pashto:
  the `byte` type (`بایټ`) and the library's enum types (`os.platform(): Platform` is
  `سیسټم.عامل سیسټم(): پلیټفارم`) were still shown in English.
- Hovering a variable, constant, struct or function the program declares writes the keyword and
  the comma of the program's language (`متغیر ش: اعشاري`, `جمع(الف: عدد، ب: اعشاري)`) instead of
  `var`, `const`, `struct` and `,`.

## 0.8.0 - 2026-11-18

Follows the toolchain's 0.8.0: the language gained objects.

- Structs declare methods and a constructor in their body (no `self` parameter; a bare member
  name inside a method means the receiver's member; members starting with `_` are private).
- `interface` (Pashto `برسېر`) declares an interface; values of interface type dispatch to the
  struct's own method at run time. A struct lists interfaces and mixins after `:`.
- Function values can be written in place and capture the variables around them (closures).
- `with` closes a struct resource through its own `close()` method.
- The grammar, snippets and language data were regenerated for the `interface` keyword and the
  new struct members.
- A spell checker no longer underlines the language's own words: the extension contributes
  every built-in, keyword and toolchain name as a dictionary for Thunder files, and a new
  project gets the same list in its `.vscode/settings.json`.
- **New Project…** writes a fuller skeleton: a README, a `.gitignore`, a `tests/` folder with a
  working test, and `.vscode/settings.json` beside the task, launch and schema files.

## 0.7.0 - 2026-11-17

Follows the toolchain's 0.7.0: the language gained modules and `with`.

* **Modules.** `module` (`ټولګی`) is a keyword (highlighted, a block opener for Enter, a
  snippet). After a module's name and a dot - `math.`, `string.`, `ریاضي.`, or one of the
  program's own modules (`geometry.`) - completion offers that module's members: the built-in
  functions with their parameters and enums, or the module's public declarations (`_private`
  ones are left out). The module's members no longer crowd the top-level list, which keeps the
  default module (`println`, `len`, `push`, ...) and the module names. Go to definition, the
  outline and renaming find a member declared inside a module by its short name.
* **Quick fixes** follow the compiler's new messages: S049 (`os.time` without `import os`)
  offers to add the import, S067 (`sqrt` without its module) offers to write `math.sqrt` - in
  both languages.
* **`with`** (`سره`) is highlighted and opens a block for Enter, also after the handle or at the end of a Pashto line
  (`ا سره = ټي_سي_پي.ونښلېږه(ک، پ)`, `ساکټ سره`); a snippet writes it in each language's order.
* **Test suites.** The Test Explorer reads `test.run("name", suite.function)`: a test whose
  function belongs to a module sits under that module's suite (file -> suite -> test). A suite
  runs as a whole (its name is the program's argument) and a single test of a suite as
  `suite/test`; the transcript's suite headings are matched to the suites the file declares.
* The grammar, snippets and bundled language information were regenerated.

## 0.6.2 - 2026-11-17

* **The right-to-left editor's clipboard works from every road.** 0.6.1 fixed the corrupted
  view, but on some VS Code builds the menu's Cut and Paste then did nothing at all: that fix
  leaned on `beforeinput`, which the `execCommand` path does not always deliver, so the browser's
  edit was made and immediately reverted by the resync. The page now handles the **clipboard
  events themselves** (`cut`, `copy`, `paste`) - which both the keyboard shortcuts and the
  context menu dispatch - moving the text between the model and the clipboard and cancelling the
  browser's whole default. The `beforeinput` handlers remain as a fallback, and the
  rebuild-from-the-model resync remains as the last line of defence.
* **IntelliSense lists an enum's members.** Typing `Color.` (or `رنگونه.` - the name is matched
  with the letters folded, so any keyboard's spelling works) now offers the members in position
  order, followed by `values` and `count` - in Pashto `ارزښتونه` and `شمېر`, which the language
  information now carries (`enum_terms`), so nothing is hardcoded. A partly typed member
  (`Color.re`, `r.wi`) keeps the member list instead of falling back to every word.

## 0.6.1 - 2026-11-16

* **Cut and paste from the right-to-left editor's context menu no longer corrupt the view.**
  VS Code runs the webview context menu's Cut and Paste through `execCommand`, whose
  `beforeinput` events are not cancelable: the page's `preventDefault()` was silently ignored,
  so the browser mutated the DOM *as well as* the editor's own model-driven redraw - pasted
  lines appeared with extra indentation, a deleted line took its line number with it. The file
  itself was always right (which is why reopening "fixed" it). The page now rebuilds every line
  from the model whenever an edit slips past it, so the view and the file can no longer part ways.
* **رنگونه is رنګونه.** The Persian gaf (and the Arabic kaf and yeh) now fold to the Pashto
  letters in *identifiers* too, not just keywords and built-ins - in the compiler itself - so
  `رنگونه.سور` names the Color enum and a variable typed on either keyboard is one name. The
  bundled language information follows (a folded variant is no longer listed as a second spelling).
* **The samples are renumbered in teaching order**: the language first (01-25, with decimals,
  for-each, bits and bytes, structs and enums moved up beside their topics), then the SDK
  (26-38: input, os, testing, graphics, native, sockets); the error samples keep their e-names.

## 0.6.0 - 2026-11-16

* **Toolchain 0.6.0: the `byte` type and `enum`s.** The bundled language information, grammar
  and snippets know the new keywords (`byte`, `enum`; Pashto `بایټ`, `ګڼنه`), the bytes
  built-ins (`to_byte`, `string_to_bytes`, `bytes_to_string`, `bytes_to_int`, `int_to_bytes`,
  `read_bytes`, `write_bytes`, `append_bytes`) and the built-in enums `Color` and `Platform`,
  which `--language-info` now lists under `enums`. The compiler's report carries an `enums`
  table beside `structs`.
* **The Pashto `for` keyword is `تکرار`** (it was `لپاره`), for both `for` and `for ... in`; the
  snippets and the Pashto project template follow. The Pashto colour name for blue is `شین`.

## 0.5.12 - 2026-11-16

* **The right-to-left editor's colours stay on their words while you type.** The compiler's
  colours (functions, parameters, fields, types, constants, built-ins) are painted at offsets
  reported for the text as it was analysed; between analyses the page kept painting them at
  those offsets while the text moved under them, so half a keyword took a function's colour
  until the file was closed and reopened. The ranges now follow every edit, and an answer that
  arrives late (the compiler runs while you type, and the document lags the page by the edit in
  flight) is carried over to the text of the moment; an answer overtaken by a newer request is
  dropped.
* **The Thunder Canvas in a window of its own.** A button in the canvas's title bar (and
  **Thunder: Move the Canvas to Its Own Window**) floats the canvas in a desktop window that can
  be resized, maximised and minimised; **Bring the Canvas Back into the Main Window** docks it
  beside the program again with the picture so far, **Thunder: Show the Canvas** reopens a
  closed canvas, and `thunder.canvas.window: "newWindow"` opens every canvas floating from its
  first frame (VS Code 1.85 or later).

## 0.5.11 - 2026-11-15

* **Zoom belongs to the panel that has the focus.** `Ctrl+=` / `Ctrl+-` / `Ctrl+0` zoomed the
  whole window (VS Code's own zoom took the keys); they now go to the focused panel alone - the
  Thunder Output view, or the right-to-left editor - through keybindings scoped to that focus,
  each with its own remembered level, and `Ctrl`+mouse wheel zooms too. Nothing else changes
  size.
* **Pashto tests run like English ones.** The right-to-left editor shows a play button in the
  gutter beside every `ازموینه(...)` / `test(...)` - click it to run that one test - and its
  context menu offers **Run Tests in This File** (the `thunder.hasTests` key now also follows a
  window restored straight into the right-to-left editor). On Windows, a Pashto test name
  passed as a program argument arrived in the ANSI codepage and matched nothing: `thundervm`
  and `thunderc` now read their command line as UTF-16 and convert it to UTF-8, so running a
  single Pashto test (and naming Pashto files on the command line) works there too.

## 0.5.10 - 2026-11-15

* **Keywords are coloured in the right-to-left editor.** They were painted with
  `symbolIcon.keywordForeground`, which VS Code's themes define as the plain text colour (it is
  the Outline's icon colour), so only functions and types looked highlighted; keywords,
  `true`/`false`/`nothing` and constants now use variables that carry a colour in every theme.
* **Tests in the Test Explorer.** The SDK's new `"testing"` module (`test`, `expect_equal` and
  its companions, `finish_tests`; Pashto: `ازموینه`, `برابر_تمه`, ...) gets its own support:
  a file that imports it appears in the Test Explorer with one entry per test, run buttons in
  the gutter, results read from the module's ✓/✗ transcript, a single test run by name through
  the program's arguments, a *Debug* profile, **Run Tests in This File** on both editors' title
  bars and context menus, and **Thunder: Run All Tests**.

## 0.5.9 - 2026-11-15

* **A program can always be stopped.** A **Stop** button now sits in the editor's title bar -
  text editor and right-to-left editor alike - whenever a program is being debugged or runs in
  the terminal, and in both context menus; `Shift+F5` stops a terminal run as it stops a debug
  session. And a program printing in an endless loop no longer freezes the window: the debug
  adapter gathers output into a few events a second instead of one per line, and the Thunder
  Output view keeps a scrollback of 5000 lines rather than growing without end - so Stop can
  be clicked.
* **The right-to-left editor's context menu matches the text editor's.** Right-click offers Go
  to Definition, Go to References, Go to Symbol, Rename Symbol, Format Document, Quick Fix,
  Toggle Line Comment, Find, Replace, Select All, then Compile, Run, Debug, Stop, Translate and
  Open in the Text Editor, and the Command Palette - with VS Code's own Cut, Copy and Paste.
  A right click outside the selection moves the caret there first, as VS Code's editor does.
  (The items other extensions add to the text editor's menu - Copilot, GitLens, spelling - are
  theirs to contribute and cannot be replicated here; Peek, Change All Occurrences and the
  multi-cursor features are VS Code's text editor's own.)
* **Translate from the text editor's title bar too**, not only from the context menu.
* **No more empty lines after a translation.** A Windows source (`\r\n` line breaks) came back
  with an empty line after every line of code: the translator kept the `\r` and the compiler's
  standard output on Windows added another. The translator now writes plain line breaks, and
  the extension gives the translation the document's own line breaks.

## 0.5.8 - 2026-11-15

* **The find bar really hides and closes.** It was always visible and its ✕ did nothing: the
  bar's own `display: flex` outweighed the browser's rule for the `hidden` attribute. It now
  appears only on `Ctrl+F` and closes on ✕ or `Escape` - in the right-to-left editor and the
  Thunder Output view alike.
* **Find got Replace, as in VS Code's editor.** `Ctrl+H` (or the bar's chevron) unfolds a
  replace row in the right-to-left editor: **Replace** swaps the match the view stands on and
  moves to the next, **All** swaps every match in one undoable edit - loose Pashto matching
  included, so replacing `ي` with `ی` can mend every spelling at once. (The Thunder Output view
  shows a program's output, which cannot be edited, so its find bar stays find-only.)
* **Graphics programs draw on the Thunder Canvas.** F5 on a graphics program opens a canvas
  panel beside the editor at its first frame and paints every frame as the Thunder IDE's Canvas
  panel does - scaled to fit, keys and mouse travelling back to the program (`key_pressed()`,
  `mouse_x()` and friends answer as under the IDE). Under `--debug` the virtual machine now
  sends each frame as a `frame` event and takes `canvasEvent` commands (docs/debugging.md);
  `--display` still wins when set.
* **Typing the language directive switches the editor.** Writing `# ژبه: ps` on the first line
  of a new `.th` file makes it a Pashto program there and then: the status bar follows and the
  file moves to the right-to-left editor, as an already-Pashto file does when opened.

## 0.5.7 - 2026-11-15

* **The Thunder Output view is no longer covered on the first F5.** VS Code opens the Debug
  Console at a window's first debug session, over the view the extension had just revealed;
  when the view takes the session, the launch now says `internalConsoleOptions: "neverOpen"`.
* **The Thunder Output view zooms and finds.** With the focus on the view, `Ctrl+=` / `Ctrl+-`
  zoom its text and `Ctrl+0` puts it back (the level is remembered); `Ctrl+F` opens a find bar
  with highlighted matches - VS Code's own find widget exists only for editor webviews, not for
  views in the panel.
* **Loose Pashto letters in find, in the editor and the output view.** In Pashto mode the find
  bars offer a toggle (`ی≈ې`): searching any spelling of a letter finds all of them - ی finds
  ي, ى, ۍ, ې and ئ, ګ finds گ, ک finds ك - and the diacritics and the tatweel are ignored.
  The right-to-left editor's `Ctrl+F` is now this find bar, searching the program's text itself
  (`media/shared/search.js`), in place of VS Code's find widget for webviews, which searches
  the rendered page and knows nothing of Pashto.
* **The bundled Pashto language data is the compiler's again.** `data/language-info.ps.json`
  had hand-translated signatures and no longer matched `thunderc --language-info`, failing the
  generated-assets tests (the signatures are recorded canonically in English by design; the
  extension translates them for display). Regenerated with `ide/vscode/tools/generate_assets.py`.

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
