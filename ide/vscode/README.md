# Thunder for VS Code

The whole Thunder toolchain inside VS Code: syntax highlighting, IntelliSense, project set-up,
building, running, packaging and a live debugger that also steps **backwards** — for English and
Pashto programs alike.

It is meant for the work the Qt IDE is not meant for: larger projects with several folders and
many files, source control, side-by-side editors, and everything else VS Code already does well.
The Qt IDE (`./run.sh`) remains the teaching tool that shows the compiler's phases in one window;
this extension brings the same information into an editor people already work in.

```
                          thunderc --json                 thundervm --debug
source  ──────────────▶  diagnostics, tokens, syntax  ──▶  the program under the debugger:
                         tree, symbols, bytecode           breakpoints, steps, a history
```

Nothing about Thunder is re-implemented here: every error message, every keyword, every type and
every instruction comes from the compiler itself, so the editor and the toolchain can never
disagree.

## What it gives you

| | |
| --- | --- |
| **Highlighting** | A TextMate grammar for English *and* Pashto (generated from the language packs), refined by **semantic highlighting** from the compiler's own token stream: functions, parameters, fields, constants and built-ins are coloured for what they are — including built-ins written as several words, such as `کرښه چاپ`. |
| **Diagnostics** | Every lexical, syntax and semantic error, with its stable code (`L002`, `P010`, `S005`, …), as you type. Errors inside an imported module are shown in that module's file. Names the program never reads are greyed out. |
| **IntelliSense** | Completion of keywords, types, built-ins (only those whose module is imported), the names in scope, the program's functions and structs — and, after a `.`, the fields of the struct the expression has. Signature help with the parameter you are on, hovers with the type the compiler inferred, **go to definition** across modules, find all references, rename, the outline, workspace symbols and inlay hints for inferred types. |
| **Bilingual** | A `# lang: ps` directive (or the `thunder.language` setting) switches everything to Pashto. A hover on a keyword or built-in shows its counterpart in the other language, and `Thunder: Translate` converts a whole file. The status bar names the language the file is in. |
| **Right to left** | VS Code's text editor always lays code out left to right, so a Pashto program opens in the extension's own **right-to-left editor** — laid out token by token exactly as the Thunder IDE lays it out, with editing, indentation, undo, completion and the compiler's problems, on the same document (see [Right-to-left programs](#right-to-left-programs)). |
| **Building and running** | Tasks for *build*, *run*, *check* and *package*, with the `$thunderc` problem matcher. Programs run in a terminal, so `input()` reads what you type. |
| **Debugging** | A debug adapter over `thundervm --debug` ([the protocol](../../docs/debugging.md)): breakpoints with conditions and hit counts, step in/over/out, pause, **step back** and **reverse continue** through the lines the machine remembers, the call stack with every frame's locals, the globals, the operand stack, the current instruction, arrays, structs and maps opened element by element, watches and hovers, `input()` answered from the Debug Console - in the text editor and in the right-to-left editor alike. |
| **Under the bonnet** | The token stream, the syntax tree, the scopes and symbols, the bytecode listing and the hex dump of the compiled file, each as a document beside the program — what the Qt IDE shows in its tabs. |
| **Projects** | `thunder.json` says which program is the entry point, which language it is in and where its modules and C++ libraries live. One workspace may hold several. |

## Setting it up

You need **VS Code 1.75 or newer** and the **Thunder toolchain** (`thunderc` and `thundervm`).
You do *not* need Node.js, npm or a package: the extension is plain JavaScript with no
dependencies and no build step.

### 1. Build the toolchain (once)

From the Thunder repository:

```bash
./run.sh                # Linux: builds the toolchain into build/release, then starts the Qt IDE
```
```powershell
.\run.ps1               # Windows (PowerShell)
```

or, without starting the IDE:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --config Release
```

### 2. Install the extension

```bash
cd ide/vscode
./scripts/install.sh
```
```powershell
cd ide\vscode
.\scripts\install.ps1
```
```powershell
# If PowerShell refuses to run the script:
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1
```

The script packages the extension as a `.vsix` (with
[`tools/make_vsix.py`](tools/make_vsix.py) - Python only; no Node.js, no npm, no `vsce`) and
hands it to VS Code:

```
code --install-extension thunder-lang-<version>.vsix --force
```

That is the route VS Code supports: it unpacks the package, records it in its own extensions
index and says so if anything is wrong. Then **Developer: Reload Window** (`Ctrl+Shift+P`).

| Option | What it does |
| --- | --- |
| *(none)* | Package it and let VS Code install it. |
| `--link` / `-Link` | Symlink (Windows: directory junction) VS Code's extensions folder to this one, so what VS Code loads **is** the sources. For working on the extension. |
| `--copy` / `-Copy` | Copy the files in. Used automatically when the `code` command is not on the PATH. |
| `--uninstall` / `-Uninstall` | Remove it again. |
| `--target DIR` / `-Target DIR` | Install into another VS Code (Insiders, OSS, a remote's `~/.vscode-server/extensions`). |

#### Development mode and updates

Two questions worth answering plainly, because the answers are not obvious:

* **Does an installed `.vsix` update itself?** **No.** VS Code only updates extensions that came
  from a marketplace; a package installed from a file is marked `"source": "vsix"` and
  `"pinned": true` in `extensions.json`, and stays exactly as it was installed. After changing
  the sources (or pulling new ones), run the install script again - it rebuilds the package and
  `--force` replaces the installed copy - and reload the window.
* **Is it good enough for developing the extension?** For *using* it, yes. For *developing* it,
  no: every change would mean repackaging. Use one of these instead:
  * **F5 in `ide/vscode`** - VS Code opens an Extension Development Host with the extension
    loaded straight from the sources, with breakpoints in the extension's own code and a
    Thunder sample folder already open (`.vscode/launch.json` in this folder sets that up).
    This is the normal way to work on a VS Code extension.
  * **`--link` / `-Link`** - the installed extension *is* this folder, so an edit here plus
    **Developer: Reload Window** is the whole loop. A junction on Windows needs no
    administrator rights.

### 3. Point it at the toolchain

The extension looks for `thunderc`/`thundervm` in this order:

1. the `thunder.toolchainPath` setting,
2. the `THUNDER_BIN` environment variable,
3. `build/release/bin`, `build/dev/bin`, `build/coverage/bin` under each workspace folder **and
   under the folders above it** (so a project inside the Thunder repository just works),
4. `PATH`.

If it finds nothing it says so once, with the list of what it searched. **Thunder: Select the
Toolchain Directory…** sets the path, and the Thunder view in the side bar shows which one is in
use.

### 4. Try it

* Open the **multi-file sample project** that comes with the toolchain:
  `code samples/project` (and `samples/project/pashto` for a Pashto one). Its
  [README](../../samples/project/README.md) lists what to try, including what to break on
  purpose.
* Or **Thunder: New Project…**, which writes `thunder.json`, a `main.th`, a module in `lib/`,
  and `.vscode/tasks.json` and `.vscode/launch.json` — in English or Pashto.
* Or **Thunder: Open a Sample Program…** and pick one of the numbered lessons.

Then:

| | |
| --- | --- |
| `F7` | Compile (fills the Problems panel and writes the `.thc`) |
| `Ctrl+F5` | Run in a terminal |
| `F5` | Debug — `F9` sets a breakpoint (in the right-to-left editor: click the line number), `F10`/`F11` step, `Shift+F11` steps out, and the debug toolbar also offers **Step Back** and **Reverse Continue** through the lines the machine remembers; the Variables view holds the locals of every frame, the globals, the operand stack and the current instruction; a program that calls `input()` asks in an input box or in the Debug Console |
| `Ctrl+Alt+T` | Translate between English and Pashto |
| — | **Thunder: Open in the Right-to-Left Editor** / **Open in the Text Editor** (a Pashto program opens in the right-to-left editor by itself) |
| — | **Thunder: Run and Save the Picture (SVG)** for a graphics program |
| `Ctrl+Space` | Completion; `Ctrl+Shift+Space` signature help |
| `F12` | Go to definition; `Shift+F12` all references; `F2` rename |

The **Thunder** icon in the activity bar holds three views: *Project* (the entry point, the
language, the module paths and every program), *Under the Bonnet* (each phase with its timing,
and a click to open the tokens, the syntax tree, the symbols, the bytecode or the hex dump) and
*Samples and Reference*.

## Settings

| Setting | Default | What it does |
| --- | --- | --- |
| `thunder.toolchainPath` | `""` | Where `thunderc` and `thundervm` are. |
| `thunder.language` | `auto` | `auto` reads each file's `# lang:` directive; `en`/`ps` force one. |
| `thunder.modulePaths` | `[]` | Extra directories for `import "name"` (`thunderc -I`). |
| `thunder.nativePaths` | `[]` | Extra directories for `import native` libraries. |
| `thunder.display` | `""` | Display plug-in for graphics programs: `sdl`, `win32`, `text`. |
| `thunder.programArguments` | `[]` | Arguments the program gets (`args()` in module `os`). |
| `thunder.maxSteps` | `0` | Stop a program after this many instructions (0: no limit). |
| `thunder.diagnostics.run` | `onType` | `onType`, `onSave` or `off`. |
| `thunder.diagnostics.delay` | `300` | Milliseconds of quiet typing before the compiler is asked. |
| `thunder.diagnostics.unusedNames` | `true` | Grey out names the program never reads. |
| `thunder.completion.moduleBuiltins` | `whenImported` | Whether `os`/`graphics` functions are offered before the import. |
| `thunder.debug.outputView` | `auto` | When **Thunder Output** comes forward while debugging: `auto` (right-to-left programs), `always`, `off`. |
| `thunder.debug.history` | `10000` | Lines the debugger remembers for **Step Back** (a launch configuration's `history` wins; `0` keeps none). |
| `thunder.rightToLeft.editor` | `auto` | `auto` opens Pashto programs in the right-to-left editor; `off` keeps every program in the text editor. |
| `thunder.rightToLeft.pashtoFont` | `Vazirmatn` | The font the right-to-left editor draws Pashto in (Vazirmatn ships with the extension). |
| `thunder.packageOutputDirectory` | `""` | Where **Package as an Application** writes. |

## `thunder.json`

```jsonc
{
  "name": "snake",
  "entry": "src/main.th",     // what Run, Debug and Package work on
  "language": "en",           // or "ps"; a file's own "# lang:" always wins
  "modulePaths": ["lib"],     // besides the importing file's own directory
  "nativePaths": ["build"],   // for "import native"
  "display": "sdl",           // a window for a graphics program
  "arguments": [],            // args() in module os
  "output": "build",          // where the .thc goes
  "package": "dist/snake"     // where the packaged executable goes
}
```

Every key is optional; a folder of `.th` files works without one. VS Code completes and checks
the file against the schema in `schemas/thunder-project.schema.json`.

## How it works

| File | What it does |
| --- | --- |
| `src/toolchain.js` | Finds `thunderc`/`thundervm` and builds every command line (pure functions, so the tests can check exactly what would be run). |
| `src/report.js` | Reads `thunderc --json` into a model: diagnostics, tokens, declarations, scopes, calls, the lines that have code. |
| `src/analysis.js` | Compiles open files (and the modules they import), caches the result per version, and writes unsaved buffers to a scratch copy compiled with `-I <the real directory>` so `import` still resolves. |
| `src/language.js` | `thunderc --language-info` as completion items, signatures and hovers, in any natural language. |
| `src/providers/` | The VS Code language providers; each is a thin layer over the model. |
| `src/debug/adapter.js` | The Debug Adapter Protocol, hand-written — no `@vscode/debugadapter`, so there is nothing to install — translated to the machine's own protocol. |
| `src/debug/debuggee.js` | The connection to `thundervm --debug`: JSON lines over two pipes, as promises and events. |
| `src/debug/launch.js` | Compiling a program and starting it under the debugger. |
| `src/direction.js` | Where the Unicode isolate marks go so that a line reads in the program's direction - a port of the IDE's `core/direction.py`, checked against it line by line. |
| `src/rtleditor.js` | The right-to-left editor's provider: a custom text editor that keeps its webview and the document in step. |
| `media/rtleditor/` | The right-to-left editor itself: `model.js` (editing, indentation, undo, colouring, completion - pure, tested in Node), `editor.js` (the browser side), `editor.css`. |
| `src/views.js`, `src/artefacts.js` | The side bar and the "under the bonnet" documents. |
| `tools/make_vsix.py` | Packages the extension as a `.vsix`, with the Python standard library alone (no `vsce`, no npm). |
| `tools/generate_assets.py` | Generates `syntaxes/`, `snippets/`, `language-configuration.json` and `data/language-info.*.json` **from the compiler**. Run it after changing a language pack. |

## Testing it

The extension has its own suite (about 230 tests) plus Python tests that tie it into the
repository's:

```bash
# Everything, as part of the repository's test run (C++, Python, and this extension)
./run_tests.sh

# Only the extension's own tests
node ide/vscode/test/run.js
# ... or, with no Node.js installed, through the one VS Code ships:
ELECTRON_RUN_AS_NODE=1 /usr/share/code/code ide/vscode/test/run.js
node ide/vscode/test/run.js --filter "debug adapter"

# Only the Python side: the generated assets are up to date, "tidy layout" and the
# right-to-left layout match the IDE's, the templates compile, the .vsix is one VS Code
# installs, the installers work, and no file of the extension is invisible to git
python3 -m unittest tests.python.test_vscode_extension -v
```
```powershell
# Windows
.\run_tests.ps1
$env:ELECTRON_RUN_AS_NODE=1
& "$env:LOCALAPPDATA\Programs\Microsoft VS Code\Code.exe" ide\vscode\test\run.js
```

The tests run the real compiler and the real virtual machine wherever that is what is being
tested (the model, the providers, a debug session), a real `code --install-extension` into a
throwaway extensions directory for the package, and a stand-in for the VS Code API
(`test/fake-vscode.js`) for everything that is about the editor. Tests that need the toolchain -
or VS Code, or Node.js - skip themselves when it is not there.

Two things exist twice and are therefore compared, not trusted: "tidy layout"
(`src/edits.js` against the IDE's `core/edits.py`) and the right-to-left layout
(`src/direction.js` against the IDE's `core/direction.py`, over every line of every sample).

After changing a language pack in `src/core/`, regenerate the assets:

```bash
python3 ide/vscode/tools/generate_assets.py
```

`tests/python/test_vscode_extension.py` runs the generator with `--check`, so a forgotten
regeneration fails the build rather than quietly shipping a stale grammar.

## Right-to-left programs

A Pashto program is read **right to left**, token by token: `متغیر نوم = "ناصر"` puts the
keyword at the right, then the name, then `=`, then the value. The Qt IDE lays its editor out
that way. **VS Code's editor cannot be made to do it**, and that is worth stating precisely,
because it is not a setting anyone has missed:

* VS Code renders every line of the editor left to right. Its line renderer writes
  `dir="rtl"` on a line only when a **model decoration** on that line carries a `textDirection`
  — and when a line contains right-to-left text but no such decoration, it explicitly writes
  `dir="ltr"`, overriding whatever the surrounding page says.
* No extension can set that field. The extension API's `DecorationRenderOptions` is converted
  property by property (`backgroundColor`, `border`, `textDecoration`, …) and `textDirection`
  is not one of them; there is no editor setting and no proposed API for it either.
* So the hook exists in VS Code's core, unused, and an extension cannot reach it. If VS Code
  ever exposes it, the extension can switch the editor itself over in a few lines.

So Pashto programs get **an editor of their own: the right-to-left editor.**

* **It opens by itself.** When a Pashto program comes into view, its text editor gives way to
  the right-to-left editor in the same tab (`thunder.rightToLeft.editor`: `auto`, the default,
  or `off`). The status bar says **پښتو**; a button in the editor's title bar — and
  **Thunder: Open in the Text Editor** — switches back to VS Code's text editor, and the choice
  is kept for the rest of the session. **Thunder: Open in the Right-to-Left Editor** (or
  **Reopen Editor With…**) goes the other way, for any `.th` file.
* **It is the same document.** It is a *custom text editor*: VS Code backs it with the same
  `TextDocument` as the text editor, so **Save**, the dirty marker, **Edit > Undo**, git, and a
  text editor opened beside it all see the same text. Every change becomes the smallest edit of
  the document, so the text editor's own undo history is a real one.
* **It lays lines out the way the Thunder IDE does.** The layout is the IDE's algorithm
  (`src/direction.js`, a port of the IDE's `core/direction.py`, loaded into the editor itself):
  every token is an isolated span, so each name, number, string and comment keeps the direction
  of its own script while the line reads in the program's direction — keyword at the right,
  value at the left, a Latin name inside a Pashto line not reversed, a negative number with its
  sign on the left. No direction marks are added to the text: what is saved is exactly what was
  typed.
* **It edits.** Typing, Enter (keeping the indentation, one level more after `که`, `دنده`,
  `ترڅو` …, one less after `راګرځه`, `ودرېږه` …), Tab and Shift+Tab, Backspace (a whole level in
  the indentation), cut, copy and paste, undo and redo (a burst of typing is one step).
* **It knows the language.** Keywords, types, built-ins (including those written as several
  words, such as `کرښه چاپ`), strings, numbers and comments in their colours; a word typed on an
  Arabic keyboard (`ك` for `ک`) is recognised as the compiler recognises it.
* **It has the text editor's language features, from the same providers.** The page asks the
  extension, which runs VS Code's own `vscode.execute…Provider` commands on the document
  (`src/rtlservices.js`), so the answers are the ones VS Code's editor gets - see the table
  below.
* **It shows the compiler's problems.** A line with an error is marked and its number turns
  red; the problems are listed under the program, and clicking one moves the caret there.
* **Pashto in a Pashto font.** The editor draws Pashto in **Vazirmatn**, which ships with the
  extension (`thunder.rightToLeft.pashtoFont` names another, such as `Noto Naskh Arabic`), and
  everything else in VS Code's editor font. The font is limited to the Arabic script with
  `unicode-range`, so the two never compete for a character.

### What each editor offers

| Feature | Text editor | Right-to-left editor |
| --- | --- | --- |
| Completion (`Ctrl+Space`, a word begun, `.`) | ✓ | ✓ the same items, at the caret, inserted with their brackets |
| Parameter hints (`(`, `,`, `،`, `Ctrl+Shift+Space`) | ✓ | ✓ |
| Hover: type, signature, the other language's name | ✓ | ✓ |
| Hover: a variable's value while debugging | ✓ | ✓ |
| Go to definition (`F12`, `Ctrl+click`), across modules | ✓ | ✓ (a Pashto module opens right to left) |
| Find all references (`Shift+F12`) | ✓ peek view | ✓ as a list to choose from |
| Rename (`F2`) | ✓ | ✓ |
| Format document (`Shift+Alt+F`) | ✓ | ✓ |
| Quick fixes (`Ctrl+.`) | ✓ | ✓ |
| Go to symbol in the file (`Ctrl+Shift+O`) | ✓ | ✓ |
| Occurrences of the name at the caret | ✓ | ✓ |
| Semantic colours (functions, parameters, fields, types, constants) | ✓ | ✓ |
| Problems: squiggles, the Problems panel | ✓ | ✓ squiggles, the list under the program, marked line numbers |
| Toggle line comment (`Ctrl+/`) | ✓ | ✓ |
| Translate between English and Pashto (`Ctrl+Alt+T`) | ✓ | ✓ (title bar button too) |
| Find (`Ctrl+F`) | ✓ | ✓ VS Code's find widget for webviews |
| Breakpoints, the stopped line, the chosen frame | ✓ | ✓ |
| Inlay hints (inferred types) | ✓ | – |
| Outline view and breadcrumbs follow the cursor | ✓ | – (`Ctrl+Shift+O` lists the symbols) |
| Multiple cursors, column selection, folding, minimap | ✓ | – |
| Find and replace (`Ctrl+H`) | ✓ | – (find only) |

The four gaps are features of VS Code's text editor itself rather than of the Thunder
providers: an extension cannot attach the Outline view, folding or multiple cursors to a
webview, and inlay hints would have to be inserted into lines whose layout the direction
algorithm controls. The text editor is one click away on the same document for those.

**Output in the program's direction.** The Debug Console belongs to VS Code: an extension can
write to it but cannot set its alignment, so a Pashto program's output sits on the left there.
The **Thunder Output** view (in the panel, beside the Debug Console) shows the same output the
way the Thunder IDE's Output panel does - every line on the program's side, right for Pashto,
each line reading in the direction of its own text - with an input line for `input()`. It comes
forward by itself for Pashto programs (`thunder.debug.outputView`).

The debugger is at home in both: the right-to-left editor draws the breakpoints (click a line
number to add or remove one) and the line the program is stopped on - VS Code draws nothing of
its own inside a custom editor, so the editor does it from what the debug adapter tells it. For
a file the right-to-left editor shows, the adapter also withholds the frame's `source` from VS
Code, so no text editor opens at each stop (the Call Stack still names the function and line).

How it is tested: its decisions (editing, indentation, undo, colouring, filtering completions,
comments, the smallest edit) in Node, its provider and the language bridge against a stand-in
for VS Code - including the real completion provider behind it, on a Pashto program - and **the page itself in a
real browser engine** (Chromium, through Qt WebEngine): `tests/python/rtleditor_browser.py`
types into it and measures where the tokens of a Pashto line land on the screen. A Python test
also lays every line of every Pashto sample out with `direction.js` and the IDE's
`core/direction.py` and compares them character for character.

## Known limits

* The debugger evaluates **variables, fields, elements and comparisons** (`n`, `point.x`,
  `items[2]`, `n > 3`), not arithmetic or calls: the virtual machine has no expression evaluator
  for running programs.
* **Stepping back** moves through the lines the machine remembers (`thunder.debug.history`,
  10000 by default); their values are text, so an array from an earlier step cannot be opened
  element by element until you step forwards to the live state again.
* **VS Code's text editor is left to right**, whatever the program's language; the
  right-to-left editor above is where a Pashto program is written as it is read. Hovers, go to
  definition and rename live in the text editor, one click away on the same document.
* There is no canvas inside VS Code as there is in the Qt IDE. A graphics program either opens
  its own window through a display plug-in (`thunder.display`: `sdl`, `win32` or `text`), or
  **Thunder: Run and Save the Picture (SVG)** runs it to the end and writes what it drew as an
  SVG, which VS Code then shows.

## Licence

MIT, with the rest of Thunder. See [LICENSE](LICENSE).
