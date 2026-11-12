# Thunder for VS Code

The whole Thunder toolchain inside VS Code: syntax highlighting, IntelliSense, project set-up,
building, running, packaging and a debugger that steps **forwards and backwards** — for English
and Pashto programs alike.

It is meant for the work the Qt IDE is not meant for: larger projects with several folders and
many files, source control, side-by-side editors, and everything else VS Code already does well.
The Qt IDE (`./run.sh`) remains the teaching tool that shows the compiler's phases in one window;
this extension brings the same information into an editor people already work in.

```
                          thunderc --json                 thundervm --trace-json
source  ──────────────▶  diagnostics, tokens, syntax  ──▶  a recorded execution the
                         tree, symbols, bytecode           debugger replays, both ways
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
| **Bilingual** | A `// lang: ps` directive (or the `thunder.language` setting) switches everything to Pashto. A hover on a keyword or built-in shows its counterpart in the other language, and `Thunder: Translate` converts a whole file. |
| **Building and running** | Tasks for *build*, *run*, *check* and *package*, with the `$thunderc` problem matcher. Programs run in a terminal, so `input()` reads what you type. |
| **Debugging** | A debug adapter over `thundervm --trace-json`: breakpoints, step in/over/out, **step back** and **reverse continue**, the call stack, locals, globals, the operand stack and the instruction at every step. |
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
./scripts/install.sh            # links it into ~/.vscode/extensions
```
```powershell
cd ide\vscode
.\scripts\install.ps1           # copies it into %USERPROFILE%\.vscode\extensions
.\scripts\install.ps1 -Link     # or link it, so edits here take effect at once
```

Then **Developer: Reload Window** (`Ctrl+Shift+P`).

`--uninstall` / `-Uninstall` removes it again; `--target DIR` / `-Target DIR` installs into
another VS Code (Insiders, OSS, a remote's `~/.vscode-server/extensions`).

> Prefer to develop on it? Open `ide/vscode` as a folder and press **F5** — VS Code starts an
> Extension Development Host with the extension loaded from the sources. (That needs nothing
> installed either; the `.vscode/launch.json` in this folder is already set up.)

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

* **Thunder: New Project…** writes `thunder.json`, a `main.th`, a module in `lib/`, and
  `.vscode/tasks.json` and `.vscode/launch.json` — in English or Pashto.
* Or **Thunder: Open a Sample Program…** and pick one of the samples that come with the toolchain.

Then:

| | |
| --- | --- |
| `F7` | Compile (fills the Problems panel and writes the `.thc`) |
| `Ctrl+F5` | Run in a terminal |
| `F5` | Debug — `F10`/`F11` step forwards, and because the execution is a recording the debug toolbar also offers **Step Back** and **Reverse Continue**; the Variables view holds the locals, the globals, the operand stack and the current instruction |
| `Ctrl+Alt+T` | Translate between English and Pashto |
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
| `thunder.language` | `auto` | `auto` reads each file's `// lang:` directive; `en`/`ps` force one. |
| `thunder.modulePaths` | `[]` | Extra directories for `import "name"` (`thunderc -I`). |
| `thunder.nativePaths` | `[]` | Extra directories for `import native` libraries. |
| `thunder.display` | `""` | Display plug-in for graphics programs: `sdl`, `win32`, `text`. |
| `thunder.programArguments` | `[]` | Arguments the program gets (`args()` in module `os`). |
| `thunder.maxSteps` | `0` | Stop a program after this many instructions (0: no limit). |
| `thunder.diagnostics.run` | `onType` | `onType`, `onSave` or `off`. |
| `thunder.diagnostics.delay` | `300` | Milliseconds of quiet typing before the compiler is asked. |
| `thunder.diagnostics.unusedNames` | `true` | Grey out names the program never reads. |
| `thunder.completion.moduleBuiltins` | `whenImported` | Whether `os`/`graphics` functions are offered before the import. |
| `thunder.trace.limit` | `200000` | Instructions the debugger records (a launch configuration's `traceLimit` wins). |
| `thunder.packageOutputDirectory` | `""` | Where **Package as an Application** writes. |

## `thunder.json`

```jsonc
{
  "name": "snake",
  "entry": "src/main.th",     // what Run, Debug and Package work on
  "language": "en",           // or "ps"; a file's own "// lang:" always wins
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
| `src/debug/trace.js` | What stepping means over a recording (pure; this is where "step back" lives). |
| `src/debug/adapter.js` | The Debug Adapter Protocol, hand-written — no `@vscode/debugadapter`, so there is nothing to install. |
| `src/views.js`, `src/artefacts.js` | The side bar and the "under the bonnet" documents. |
| `tools/generate_assets.py` | Generates `syntaxes/`, `snippets/`, `language-configuration.json` and `data/language-info.*.json` **from the compiler**. Run it after changing a language pack. |

## Testing it

The extension has its own suite (175 tests) plus a Python test that ties it into the
repository's:

```bash
# Everything, as part of the repository's test run (C++, Python, and this extension)
./run_tests.sh

# Only the extension's own tests
node ide/vscode/test/run.js
# ... or, with no Node.js installed, through the one VS Code ships:
ELECTRON_RUN_AS_NODE=1 /usr/share/code/code ide/vscode/test/run.js
node ide/vscode/test/run.js --filter "debug adapter"

# Only the Python side (assets up to date, tidy matches the IDE's, templates compile, installers)
python3 -m unittest tests.python.test_vscode_extension -v
```
```powershell
# Windows
.\run_tests.ps1
$env:ELECTRON_RUN_AS_NODE=1
& "$env:LOCALAPPDATA\Programs\Microsoft VS Code\Code.exe" ide\vscode\test\run.js
```

The tests run the real compiler and the real virtual machine wherever that is what is being
tested (the model, the providers, the recordings), and a stand-in for the VS Code API
(`test/fake-vscode.js`) for everything that is about the editor. Tests that need the toolchain
skip themselves when it has not been built.

After changing a language pack in `src/core/`, regenerate the assets:

```bash
python3 ide/vscode/tools/generate_assets.py
```

`tests/python/test_vscode_extension.py` runs the generator with `--check`, so a forgotten
regeneration fails the build rather than quietly shipping a stale grammar.

## Known limits

* The debugger **replays a recording**: the program runs to its end (or to
  `thunder.trace.limit` instructions) before the first breakpoint is shown, and expressions are
  not evaluated — you can read variables, not call functions. In exchange, you can step
  backwards. Programs that read `input()` take their input from the launch configuration's
  `input` field rather than the keyboard.
* Only the **running** frame's locals are recorded by the virtual machine, so a frame further
  down the call stack shows its function and position but not its variables. Stepping back into
  it shows them again.
* There is no canvas inside VS Code as there is in the Qt IDE. A graphics program either opens
  its own window through a display plug-in (`thunder.display`: `sdl`, `win32` or `text`), or
  **Thunder: Run and Save the Picture (SVG)** runs it to the end and writes what it drew as an
  SVG, which VS Code then shows.

## Licence

MIT, with the rest of Thunder. See [LICENSE](LICENSE).
