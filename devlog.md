# Thunder development log

*Started 2026-11-04 as `analysis.md`: an analysis of the legacy `dlang` codebase (sections 1-13,
kept unchanged as the historical record) followed by a running log of every development
iteration since (section 14 onwards). Renamed to `devlog.md` on 2026-11-11 to reflect what it
has become; one section is appended per iteration, and the habit is to keep doing so.*

---

## 1. Executive summary

| Area | Finding |
| --- | --- |
| **Existing language** | Integer arithmetic expressions only: `+ - * /`, parentheses, unary minus on literals. There are no variables, statements, control flow, functions, strings or arrays. |
| **Existing compiler** | ~700 lines of C++11 in one file. It has a hand-written lexer, a **shunting-yard** expression parser that builds an AST, and a code generator that emits a 6-opcode stack bytecode. |
| **Existing VM** | ~140 lines. It is a stack machine that only works on `int`. A program's "output" is whatever value is left on top of the stack. |
| **Existing IDE** | C# **WinForms** on .NET Framework 4.5, so Windows-only. It has a source box, a console box and Compile/Run buttons, and it shells out to the compiler `.exe` files using hard-coded `..\..\..\Debug\` paths. |
| **Build system** | Visual Studio 2013/2015 `.sln`/`.vcxproj` only. There is no CMake/Make and nothing builds on Linux. |
| **Tests** | None. `testrig` is a Windows-only launcher that compiles and runs one hard-coded expression. It has no assertions. |
| **Does it build on Linux?** | **No, not as-is.** It fails under GCC 15 because `<algorithm>` is missing and, in C++17, `using byte` collides with `std::byte`. With `-std=c++14 -include algorithm` the compiler and VM do build and run correctly on happy-path input. |
| **Robustness** | Poor. Several inputs crash, overflow buffers or are silently accepted with a wrong result (§3.5). |
| **Environment** | Very good for a **C++20 + Qt 6 + CMake** stack. GCC 15.2, CMake 4.2, Ninja, Qt 6.10.2 (Widgets, Test, Svg) dev packages, gcov, clang-tidy 21 and gdb are installed, plus a live Wayland display. I built and ran a Qt 6 probe app successfully, both headless (`offscreen`) and on screen. |
| **Gaps in environment** | No .NET/Mono, Node, Java or Rust. No `pip`/`ensurepip`, so I can't create a Python venv with pip. No `lcov`/`gcovr`/`cppcheck`/`clang-format`/`valgrind`/`graphviz`. No `sudo`. No Windows toolchain (MinGW/wine), so **Windows builds cannot be verified on this machine**. Internet access works (GitHub and PyPI are reachable). |
| **Bottom line** | The existing code is a good *conceptual* seed: the lexer → parser → AST → bytecode → VM pipeline and the "show the AST, assembly and machine code" idea are exactly right for teaching. It is too small and too Windows-bound to extend directly, though. **I recommend a structured rewrite in portable C++20 with a Qt 6 Widgets IDE that links the compiler/VM library in-process**, keeping the original design ideas and educational spirit. |

---

## 2. Repository inventory

```
PLT/
├── README.md              Generic top-level README; refers to CppLex/ and MyXML/ at the root, which no longer exist there (stale)
├── LICENSE                MIT
├── .gitignore             Python template plus a generic C++/CMake section (*.o, build/, *.d …), but nothing for Visual Studio/MSVC (.vs/, *.suo, Debug/) *(corrected, see §14)*
├── archive/MyXML/         Unrelated toy XML parser (VS2013). Includes headers that aren't in the repo, so it can't build. Out of scope.
└── dlang/
    ├── README.md          "DumbLang – all it currently supports is arithmetic expressions"
    ├── dlang.png          Screenshot of the WinForms IDE evaluating 10+5*((12/2)*5) - -12 = 172
    ├── dlang.sln          VS2013 solution (4 projects); vcxproj files use the v140 (VS2015) toolset, Win32 only
    ├── dlang.v12.suo      Binary VS user-options file. Should not be in version control.
    ├── compiler/main.cpp  (716 LOC) lexer, parser, AST, code generator, disassembler, CLI → dlangc.exe
    ├── interpreter/main.cpp (142 LOC) bytecode VM + CLI → dlang.exe
    ├── testrig/main.cpp   (74 LOC) Win32 CreateProcess launcher (Shlwapi.h). Windows-only.
    └── IDE/               (≈250 LOC hand-written C#) WinForms app "DLangIDE", .NET Framework 4.5
```

Git history: the commits before `ff66a8b` contain a separate project called `CppLex` (a C++ lexer with an MFC UI and
bundled Scintilla 3.7.2). Those commits mention "syntax highlighting with Scintilla" and "async regex", but that work
belonged to CppLex, not DLang, and was removed as unrelated. Nothing in it needs to be recovered.

---

## 3. Existing DLang implementation: detailed review

### 3.1 Pipeline

```
 "10+5*(12/2)"  ──argv──►  dlangc  ──►  lexer ──► parser (shunting-yard) ──► AST ──► codegen ──► out.d (raw bytes)
                                                                                         │
                                       stdout: AST dump, assembly listing, hex dump  ◄───┘
 out.d ──► dlang (VM) ──► "Result: 172"
```

### 3.2 Lexer (`compiler/main.cpp` ≈ L140–232)
* Character-at-a-time scanner with a `consume`/`match` structure, in the style of Terence Parr's *Language
  Implementation Patterns*.
* Tokens: `NUM ( ) + - * / EOF`. A token holds only a type and its text, with **no line/column position**.
* A static `tokenInfo[]` table holds each token's display name, **precedence** and **associativity**. That's a nice
  data-driven touch worth keeping as a teaching device.

### 3.3 Parser (≈ L240–525)
* Uses Dijkstra's **shunting-yard** algorithm, with a good explanatory comment. It builds a binary AST (`EXPR` with
  `unique_ptr` children) directly from the operator/operand stacks.
* There is an unused LL(k) lookahead buffer with `mark`/`release` for backtracking, plus a commented-out
  speculative `stat()` rule. So the author was planning statement-level parsing.
* Unary minus is a special case: `-` followed by a token is glued into a negative literal *string*.

### 3.4 Code generator, bytecode & VM
* ISA: `NOOP=0, ADD, SUB, MUL, DIV, LOADLIT=5`. `LOADLIT` takes a 4-byte little-endian `int32` operand.
* Codegen walks the AST **pre-order, emitting into the buffer backwards**, then calls `std::reverse`. The result is
  a post-order (RPN) sequence that evaluates the right operand first. It works, but it is obscure and not something we
  want students to copy.
* The VM is a `std::stack<int>` with a `for` loop over bytes. It has no PC register, no jumps, no frames and no
  program header/magic number.
* The opcode enum and mnemonic table are **duplicated** in both executables, so there's no shared library.

### 3.5 Defects found (verified on Linux with GCC 15 + ASan/UBSan)

| # | Location | Defect | Observed |
| --- | --- | --- | --- |
| 1 | `compiler/main.cpp:552,665` | `std::reverse` and `std::for_each` are used without `#include <algorithm>`. This only compiled on MSVC because of transitive includes. | Build error on GCC |
| 2 | `compiler:696`, `interpreter:26` | `using byte = unsigned char` plus `using namespace std` is ambiguous with `std::byte` in C++17 and later. | Build error with `-std=c++17` |
| 3 | `interpreter/main.cpp:82` | Division guard is `if (b > 0)`, so **any negative divisor** is reported as division by zero. | `8 / -2` → "Division by zero" |
| 4 | `compiler:521` | Leftover operands are silently discarded and the top of the stack is used as the AST. | `2 3` → result **3**, no error |
| 5 | `compiler:473` | Unary minus glues `"-"` onto the *next token's text*, whatever that token is. | `--3` → 3, no error; `-(3)` → "Mismatched parentheses" |
| 6 | `compiler:521` | `output.top()` on an empty stack. | `1 +` → assertion abort |
| 7 | `compiler:593` | `stoi` throws `std::out_of_range`/`invalid_argument`, but only `std::string` exceptions are caught. | `99999999999` → `terminate` |
| 8 | `interpreter:73` | Signed 32-bit overflow is undefined behaviour. | `2147483647 + 1` → UBSan error, prints −2147483648 |
| 9 | `interpreter:52–56`, `compiler:565–569` | `LOADLIT` reads 4 operand bytes with no bounds check. | Truncated file → **ASan heap-buffer-overflow** |
| 10 | `interpreter:62` | No stack-underflow or invalid-opcode check. A bad opcode pops before it validates. | Assertion abort |
| 11 | `compiler:158,193` | Uses `char c` with `EOF` as a sentinel. Byte 0xFF is mistaken for EOF on signed-char targets, and the loop never ends on unsigned-char targets (ARM). `::isdigit(char)` is UB for negative values. | Latent |
| 12 | `compiler:117` | `isOperator` relies on enum ordinal `> 4`. | Fragile |
| 13 | `compiler:11` | `THROW` throws `std::string` rather than a `std::exception` subtype, and errors carry no source position. | Poor diagnostics |
| 14 | `compiler:700` | Output is always written to `out.d` in the CWD. `.d` is also the D language's source extension. | Usability |
| 15 | `IDE/FrmMain.cs:67,76` | Executables are found via hard-coded `..\..\..\Debug\*.exe`, so the IDE breaks in a Release build or a different layout. | Fragile |
| 16 | `IDE/FrmMain.cs:67` | The **entire source program is passed as a command-line argument**. Embedded `"` characters break it, and Windows limits command lines to ~32 K characters. | Won't scale past expressions |
| 17 | `IDE/FrmMain.cs:27,46` | Sets `CheckForIllegalCrossThreadCalls = false` and appends from thread-pool callbacks. stderr is redirected but `BeginErrorReadLine` is never called, so it can deadlock if stderr fills. The UI thread blocks while the child runs. | Races / hang risk |
| 18 | whole repo | No tests, no CI, Windows-only build files, `.suo` committed, `.gitignore` missing Visual Studio entries. | Hygiene |

### 3.6 What's worth keeping
* The **pipeline architecture** and the **pedagogical outputs** (AST dump → assembly → hex machine code →
  execution). This is the core of the teaching value and the new IDE should expand it.
* The **table-driven token metadata** (name/precedence/associativity).
* The **shunting-yard implementation**, as an *optional* "alternative expression parser" demo next to the main
  parser. Comparing algorithms is useful in a PLT course.
* The **stack-machine VM model** and the little-endian operand encoding concept.
* The "DumbLang" humour/branding (see Decision D8).

### 3.7 What has to change
Every other part needs replacing or a redesign. That includes the source representation (no positions), the AST
(a single binary-node type can't represent statements or declarations), the missing semantic-analysis stage, the
VM (no control flow, frames, heap or types), the IDE (Windows-only and process-based) and the build (VS-only).
Extending the existing files in place would mean rewriting nearly every line anyway, so a clean, modular rewrite
that ports the good ideas is the lower-risk path.

---

## 4. Environment findings

Host: Ubuntu 26.04 LTS (VirtualBox VM), x86_64, 4 cores, 9.9 GiB RAM, 168 GB free disk, Wayland session
(`DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`).

### 4.1 Available ✅

| Tool | Version | Relevance |
| --- | --- | --- |
| GCC / G++ | 15.2.0 | Full C++20 and most of C++23 |
| CMake | 4.2.3 | Cross-platform build. **Note:** CMake 4 removed compatibility with `cmake_minimum_required` < 3.5. |
| Ninja / GNU Make | 1.13.2 / 4.4.1 | Build back-ends |
| **Qt 6** | **6.10.2** | `qt6-base-dev` (Core, Gui, **Widgets**, **Test**, Concurrent), Svg/SvgWidgets, Tools/Designer, Declarative/QML. CMake package configs are present. **Verified:** probe app configured, built and ran headless and on screen. |
| gcov | 15.2.0 | Supports `--json-format --stdout`, so coverage can be summarised without lcov/gcovr |
| clang-tidy | LLVM 21.1.8 | Static analysis / code quality metric (no clang compiler, but clang-tidy works with GCC's `compile_commands.json`) |
| gdb | 17.1 | Debugging |
| GCC sanitizers | ASan/UBSan work | Used for §3.5 |
| Python 3 | 3.14.4 + **tkinter 8.6** | Scripting only. No pip, no `ensurepip`, but `venv --without-pip` works. |
| SDL2 (+image/mixer/ttf) dev | 2.32 | Not needed |
| GTK 3/4 runtime | — | Runtime only, no dev headers |
| git / gh | 2.53 / 2.46 | Remote `origin` = github.com/NasirA1/PLT |
| wget | — | Network access confirmed (github.com, pypi.org). No `curl`. |

### 4.2 Not available ❌
.NET SDK/Mono (so the **existing C# IDE can't even be built here**), Java, Node/npm, Rust, Go, clang/clang++,
lcov, gcovr, cppcheck, clang-format, valgrind, graphviz (`dot`), QScintilla, GoogleTest/Catch2 system packages,
MinGW cross-compiler, wine, Docker, Xvfb. There's no passwordless `sudo`, so nothing can be `apt install`ed.
Anything extra has to be vendored, fetched at configure time (CMake `FetchContent`) or installed in user space.

### 4.3 Implications
1. **C++ + Qt 6 Widgets + CMake** is the only full-featured, cross-platform GUI stack that is *already installed*
   and verified. Everything else needs SDK installs without root.
2. **Testing:** QtTest is installed. A header-only C++ framework (doctest or Catch2 amalgamated, MIT/BSL) can be
   vendored into the repo so tests work offline on both OSes.
3. **Coverage:** gcov works. A summary can come from `gcovr` when present, or otherwise from a small bundled
   summariser that parses `gcov --json-format --stdout` output. Using a CMake `-P` script with `string(JSON)` keeps
   the dependencies to CMake + compiler only, which matters on Windows where Python isn't guaranteed.
4. **GUI tests** can run headless with `QT_QPA_PLATFORM=offscreen`, which is verified. No Xvfb is needed.
5. **Windows can't be verified locally.** We should either add a GitHub Actions matrix (Ubuntu + Windows), which
   needs your approval to push, or provide a Windows VM for verification.

---

## 5. Gap analysis vs. project goals

| Goal | Current state | Gap |
| --- | --- | --- |
| Language with variables, arrays, strings, conditionals, loops, functions | Arithmetic only | Full language design: grammar, type system, semantics |
| Compiler showing lexing, parsing, type-checking, codegen | Lex + parse + codegen; no semantic stage; text dumps | Positions, real AST, symbol tables/scopes, type checker, diagnostics, structured "phase artefacts" for the IDE |
| VM executing compiled code | 6-opcode int stack machine | Typed values, constant pool, jumps, call frames, locals/globals, heap (strings/arrays), runtime errors with source line, step/trace hooks, instruction budget |
| IDE launched with `./run.sh` | WinForms, Windows-only, no script | Cross-platform GUI, `run.sh` (+ Windows equivalent) that builds if needed and launches |
| Sample-program picker | None | `samples/` folder + picker UI |
| Compile/Run buttons | Present (Windows) | Re-implement; add Step/Stop |
| Educational panel (AST, bytecode, …) | Text dump in console | Dedicated tabbed panel with tokens, AST tree, symbols, bytecode, VM state |
| Output panel at bottom with output/errors | Single console | Output + Problems panes, clickable errors |
| `./run_tests.sh`, all passing, coverage + quality summary | No tests | Unit, golden-file, CLI and GUI tests; coverage + lint summary |
| Cross-platform Linux + Windows | Windows-only | CMake, portable code, scripts for both shells, CI |

---

## 6. Recommended technical direction

### 6.1 Technology stack (recommendation: **Option A**)

| Option | Stack | Pros | Cons |
| --- | --- | --- | --- |
| **A (recommended)** | **C++20 core + Qt 6 Widgets IDE + CMake/Ninja** | Everything is installed and verified. One language end-to-end. Continues the existing C++ code. Mature cross-platform GUI (text editor, tree views, tables, docking, syntax highlighter). QtTest + offscreen GUI testing. Students see a "real" compiler language. | Qt must be installed on Windows (official online installer, MSYS2 `mingw-w64-ucrt-x86_64-qt6-base`, or vcpkg). First build ~1–2 min. |
| B | C++ core + Dear ImGui (SDL2) IDE | SDL2 is installed. Lightweight, immediate-mode UI makes custom visualisations easy. | ImGui has to be fetched. Its text editing is weak (no real code editor widget), and accessibility/native look are poor. |
| C | Python (compiler + VM + tkinter IDE) | tkinter is installed. Very readable for students. Fastest to write. | Discards the C++ base. No pip here, so no pytest/coverage without bootstrapping. Python must be installed on Windows. tkinter looks dated. The VM is slow (fine for teaching, though). |
| D | C# .NET 8 + Avalonia | Continues the IDE's C# heritage. Avalonia is cross-platform. | .NET SDK isn't installed (user-space `dotnet-install.sh` is possible). Two languages (C++ core + C# UI) or a full port. NuGet network dependency. |
| E | Web (Electron/Tauri, or browser + local server) | Rich visualisation (D3 AST graphs). | Node/Rust aren't installed. Heavy. Most moving parts. |

### 6.2 Architecture (Option A)

```
                      ┌───────────────────────────── dlang_core (static library, no Qt dependency) ─────────────────────────────┐
 source text ───────► │ SourceFile ► Lexer ► Parser ► AST ► Semantic analysis (scopes, symbols, types) ► CodeGen ► Bytecode   │
                      │                ▲ diagnostics (phase, severity, line:col span, message, hint)                 │           │
                      │                └──────────────────── CompilationResult {tokens, ast, symbols, chunk, diags, timings}       │
                      │ Disassembler · Bytecode (de)serialiser (.dlc file w/ magic+version) · VM (step / run / trace hooks)     │
                      └──────────────▲──────────────────────────────▲──────────────────────────────────────▲──────────────────────┘
                                     │                              │                                      │
                     dlangc  (CLI: compile, --dump-tokens/        dlangvm (CLI: run .dlc,           DLang IDE (Qt 6 Widgets, links
                     --dump-ast/--dump-asm, -o file.dlc)          --trace)                          core in-process; VM in worker thread)
                                     │                              │                                      │
                                     └──────────── tests: unit (core) · golden end-to-end (samples) · CLI · GUI (offscreen) ┘
```

Key points:
* **The IDE links the core library in-process** instead of spawning executables. That removes the path, quoting
  and IPC problems (#15–17). It also lets the IDE show *structured* data (a clickable AST tree, live VM stack) and
  makes step-by-step debugging possible. The CLIs stay as thin front-ends, because a "real toolchain" feel is
  educational and CLIs are easy to test.
* **Every phase produces an inspectable artefact.** The CLI and the IDE render the same `CompilationResult`.
* **The VM runs in a worker thread** with an instruction budget and a Stop button. Loops make non-termination
  possible, and a teaching IDE must never freeze.
* The core is plain C++20 and the standard library only, so it is trivially portable. Qt stays in the IDE layer.

### 6.3 Proposed directory layout

```
dlang/                          (or promote to repo root, see D9)
├── CMakeLists.txt, CMakePresets.json
├── core/        include/dlang/*.hpp, src/*.cpp   (lexer, parser, ast, sema, codegen, bytecode, vm, diagnostics)
├── cli/         dlangc.cpp, dlangvm.cpp
├── ide/         main.cpp, MainWindow, CodeEditor (line numbers, highlighter), panels/*
├── samples/     01_hello.dl … 12_errors_*.dl  (+ .expected output files)
├── tests/       unit/, golden/, cli/, gui/, third_party/doctest.h
├── docs/        language-reference.md (EBNF grammar), isa.md (VM instruction set), architecture.md
├── tools/       coverage_summary.cmake
├── run.sh  run.bat (or run.ps1)
└── run_tests.sh  run_tests.bat
legacy/          the original VS projects, kept for historical comparison (see D10)
```

---

## 7. Language design proposal (draft for discussion)

### 7.1 Design principles for an *educational* language
1. **Familiar C-family syntax** (braces, semicolons). Students coming from C/Java/JS/C# can read it immediately,
   and the grammar stays LL(1)-friendly for a hand-written recursive-descent parser.
2. **Static typing with explicit declarations and simple local inference.** The type checker is a key PLT topic,
   and static types let the IDE show a meaningful "type-checking" phase.
3. **Small but complete.** Every construct in the brief, and nothing that adds complexity without teaching value
   (no classes, generics or closures in v1).
4. **No undefined behaviour.** Integer overflow, out-of-bounds access, division by zero and stack overflow all
   become clear runtime errors with a source line.

### 7.2 Draft feature set (v1)
* **Types:** `int` (64-bit, overflow traps), `bool`, `string` (immutable), arrays `T[]` (fixed length at creation,
  can be nested), `void` (function return only). *Stretch:* `float`, `char`.
* **Declarations:** `var x: int = 5;`, `var y = 10;` (inferred), `const N = 3;`.
* **Expressions:** arithmetic `+ - * / %`, comparison `== != < <= > >=`, logical `&& || !` (short-circuit),
  unary `-`, string concatenation with `+`, indexing `a[i]`, calls `f(x)`, array literals `[1, 2, 3]`,
  `new int[n]`.
* **Statements:** blocks, `if`/`else`, `while`, `for (init; cond; step)`, `break`, `continue`, `return`,
  assignment (including `a[i] = v`), expression statements.
* **Functions:** `func name(p: T, …): R { … }`, recursion, pass-by-value (arrays by reference). `main()` is the
  entry point, or top-level statements are allowed (see D4).
* **Built-ins:** `print(...)`, `println(...)`, `len(x)`, `str(x)`, `int(s)`. *Optional:* `input()`, which needs an
  input box in the IDE.
* **Comments:** `//` and `/* */`.

```dlang
// samples/07_recursion.dl
func fib(n: int): int {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}

func main() {
    var squares: int[] = new int[5];
    for (var i = 0; i < len(squares); i = i + 1) {
        squares[i] = i * i;
    }
    println("fib(10) = " + str(fib(10)));
    println("squares[4] = " + str(squares[4]));
}
```

### 7.3 Compiler front-end
* **Lexer:** hand-written and position-tracking (line, column, byte offset span), with a keyword table. Keep the
  table-driven token metadata from the original.
* **Parser:** **recursive descent for statements + Pratt (precedence-climbing) parsing for expressions.** This is
  the standard teaching approach and maps cleanly onto the EBNF grammar we publish in `docs/`. Error recovery uses
  panic-mode sync on `;`/`}` so students see several errors at once. The original shunting-yard parser can be
  kept as an alternative expression-parsing mode the student can toggle (D7).
* **Semantic analysis:** a name-resolution pass with nested scopes and a symbol table, then type checking.
  Errors look like *"line 7:12: cannot assign 'string' to variable 'x' of type 'int'"*.
* **Optional optimisation pass** (constant folding, dead-code elimination) behind a toggle, so students can
  compare bytecode before and after.

### 7.4 Virtual machine / bytecode
* **Stack machine**, continuing the original design. Values are a tagged union (`int`, `bool`, string ref,
  array ref, function ref). Strings and arrays are reference-counted. Static types prevent reference cycles in
  v1, so a GC isn't required. A mark-and-sweep GC with a heap visualiser is a possible stretch topic.
* **Program file (`.dlc`):** magic `DLNG` + version + constant pool + function table + code + **line table**
  (pc → source line), so runtime errors and the debugger can map back to the source.
* **Draft ISA (~35 ops):** `CONST k`, `LOAD_LOCAL n`, `STORE_LOCAL n`, `LOAD_GLOBAL n`, `STORE_GLOBAL n`, `POP`,
  `DUP`, `ADD SUB MUL DIV MOD NEG`, `EQ NE LT LE GT GE`, `NOT`, `JUMP off`, `JUMP_IF_FALSE off`, `CALL f argc`,
  `RET`, `NEW_ARRAY`, `ARRAY_LIT n`, `INDEX_GET`, `INDEX_SET`, `LEN`, `CONCAT`, `TO_STR`, `PRINT`, `HALT`.
  Operands are little-endian, as in the original.
* **Safety:** every instruction is bounds- and type-checked. There are limits on stack depth and call depth, plus
  an instruction budget. The VM is fuzz-friendly: malformed `.dlc` input gives a clean error, never UB (fixes #9/#10).
* **Hooks:** `step()`, breakpoints by pc or source line, and an observer interface for tracing. The IDE uses
  these for the debugger view.

---

## 8. IDE & educational feature recommendations

### 8.1 Layout (satisfies the brief, extensible)

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ [Sample: 07_recursion ▼]  [▶ Compile] [▶▶ Run] [⏯ Step] [⏹ Stop]  [☐ Optimise]   Phase: ✔✔✔✔ │
├───────────────────────────────────────────┬──────────────────────────────────────────────────┤
│ Code editor                               │ Educational panel (tabs)                         │
│  - line numbers, syntax highlighting      │  Tokens │ AST │ Symbols │ Bytecode │ VM │ Learn  │
│  - error squiggles, current-line marker   │  (clicking a token/AST node/instruction          │
│    during stepping                        │   highlights the matching source span)           │
├───────────────────────────────────────────┴──────────────────────────────────────────────────┤
│ Output │ Problems (clickable line:col) │ Build log (phase timings)                           │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 8.2 Educational panel ideas (prioritised)
**Must-have (MVP)**
1. **Tokens table:** kind, lexeme, line:col. Selecting a row highlights the source.
2. **AST tree view:** node type + key attributes (+ inferred type after checking). Selecting a node highlights its
   source span, and the reverse works too.
3. **Symbols/scopes:** a nested scope tree with name, kind (var/param/func), type and slot index.
4. **Bytecode disassembly:** grouped per function, with the constant pool, offsets and source-line column.
   Hovering an opcode shows a one-line description from the ISA doc.
5. **Errors explained by phase:** *"Lexical error"* vs *"Syntax error"* vs *"Type error"* vs *"Runtime error"*,
   with a short "what this phase does" hint. That teaches *where* in the pipeline each kind of mistake is caught.

**High value**
6. **Step-through VM debugger:** step one instruction or one source line, with a live **operand stack**, **call
   frames**, locals/globals and heap objects (strings/arrays). The current instruction and source line are both
   highlighted. This is the feature that best shows "how it works under the bonnet".
7. **Hex view of the `.dlc` file** with annotated regions (header, constant pool, code). This continues the
   original "machine code" dump.
8. **Execution statistics:** instructions executed, max stack depth, calls, and time per compiler phase.

**Nice-to-have / stretch**
9. **Graphical AST** (QGraphicsView tree layout; no graphviz needed) and export to DOT/SVG/PNG for slides.
10. **Optimisation diff:** bytecode before and after constant folding, side by side.
11. **"Learn" tab:** the language reference / EBNF grammar, linked to the current AST node's production.
12. **Parser comparison mode:** shunting-yard vs Pratt on the same expression, showing their operator/output
    stacks step by step. This reuses the original algorithm.
13. **Exercise mode:** samples with TODOs and expected output, plus an automatic check (useful for labs).

### 8.3 Sample programs (curriculum-ordered; each doubles as a golden test)
`01_hello`, `02_arithmetic` (precedence, the original demo), `03_variables`, `04_if_else`, `05_while`,
`06_for`, `07_functions`, `08_recursion`, `09_arrays`, `10_strings`, `11_algorithms` (bubble sort, primes),
plus *deliberate-error* samples: `e1_lexical_error`, `e2_syntax_error`, `e3_type_error`, `e4_runtime_error`
(div by zero / index out of bounds), `e5_infinite_loop` (shows the Stop button / instruction budget).

---

## 9. Testing, coverage & quality metrics

### 9.1 Test layers
| Layer | What | Tooling |
| --- | --- | --- |
| Unit | Lexer tokens and positions, parser AST shapes (pretty-printed S-expressions), type-checker diagnostics, codegen sequences, VM ops and error paths, bytecode round-trip serialisation | doctest (vendored single header) or Catch2 |
| Golden / end-to-end | For every `samples/*.dl`: compile + run, then compare stdout/diagnostics against `*.expected`. This guarantees every sample in the IDE works. | CTest-driven |
| CLI | `dlangc`/`dlangvm` exit codes, `--dump-*` output, bad-file handling | CTest |
| Robustness | Malformed bytecode, very deep nesting, huge literals, and **regression tests for every defect in §3.5** | Unit + optional libFuzzer-style fuzz target later |
| GUI smoke | Launch the main window offscreen, load a sample, click Compile/Run, assert panel contents and output | QtTest + `QT_QPA_PLATFORM=offscreen` (verified available) |

### 9.2 `./run_tests.sh` proposal
1. Configure a dedicated `coverage` preset (GCC `--coverage -O0`, `-Wall -Wextra -Wpedantic -Werror`,
   ASan + UBSan) into `build/coverage`.
2. Build and run `ctest --output-on-failure`.
3. Print a summary block: tests passed/failed; **line / function / branch coverage per module** (core, cli, ide);
   **clang-tidy finding count** (using a curated `.clang-tidy`); compiler warnings (0 is enforced by `-Werror`);
   and optionally LOC per module and a cyclomatic-complexity hotspot list (lizard, if available).
4. Exit non-zero if any test fails or coverage drops below an agreed threshold (e.g. core ≥ 90 % lines).

On Windows, the equivalent runs under Git Bash/MSYS2 with MinGW-GCC, so gcov still applies. Coverage is skipped
with a notice when the compiler is MSVC.

---

## 10. Cross-platform plan
* **Build:** CMake ≥ 3.21 with `CMakePresets.json` (`dev`, `release`, `coverage`; Linux/GCC, Windows/MSVC and
  Windows/MinGW).
* **Scripts:** `run.sh`/`run_tests.sh` for Linux and Git-Bash/MSYS2, plus `run.bat` (or `run.ps1`) and
  `run_tests.bat` for native Windows `cmd`/PowerShell. They all call the same CMake presets, so logic isn't
  duplicated. `run.sh` builds incrementally (configure on first run only), then launches the IDE.
* **Qt discovery:** honour `Qt6_DIR`/`CMAKE_PREFIX_PATH`. On Windows, the run script runs `windeployqt` (or sets
  `PATH`) so the exe finds the Qt DLLs, and gives a friendly error with install instructions if Qt isn't found.
* **Code:** standard library only in core; `std::filesystem` for paths; UTF-8 everywhere (`/utf-8` on MSVC);
  `int64_t`, never `long`; no `char`/`EOF` sentinels.
* **Repo:** `.gitattributes` (`*.sh eol=lf`, `*.bat eol=crlf`); replace the Python `.gitignore` with a C++/CMake/Qt
  one; remove `dlang.v12.suo`.
* **Verification:** a GitHub Actions workflow (ubuntu-latest + windows-latest, using `jurplel/install-qt-action`)
  would be the practical way to prove Windows support, since this machine can't build for Windows.
  **This involves pushing to GitHub, so I'll only do it with your go-ahead.**

---

## 11. Risks & mitigations
| Risk | Mitigation |
| --- | --- |
| Windows build can't be tested locally | CI matrix (with approval) or a user-provided Windows VM; keep core Qt-free and portable |
| Qt not installed on a student's Windows PC | Clear prerequisites doc; run-script detection with a helpful message; option to ship a `windeployqt` bundle |
| Scope creep (language features × visualisations) | Phased roadmap (§12) with a working, tested vertical slice first |
| Infinite loops / deep recursion freezing the IDE | VM in a worker thread, instruction budget, call-depth limit, Stop button |
| Name confusion with the D language (dlang.org) and the `.d` extension | Use `.dl` for source and `.dlc` for bytecode; consider renaming (D8) |
| CMake 4 policy changes | `cmake_minimum_required(VERSION 3.21)`; presets tested with 4.2 |
| Coverage tooling missing (no gcovr/lcov) | Bundled gcov-JSON summariser; use gcovr when available |

---

## 12. Suggested phased roadmap
1. **Foundation:** CMake skeleton and presets, core library, position-aware lexer, diagnostics, test framework,
   `run_tests.sh` with coverage summary. Port the arithmetic subset; regression tests for §3.5.
2. **Language core:** parser (RD + Pratt) for the full grammar, AST printer, name resolution and type checker.
3. **Back-end:** ISA, codegen, `.dlc` format, disassembler, VM with runtime errors; `dlangc`/`dlangvm` CLIs; golden
   tests for all samples.
4. **IDE MVP:** editor with highlighting, sample picker, Compile/Run, Tokens/AST/Symbols/Bytecode tabs,
   Output/Problems panes, `run.sh`/`run.bat`; offscreen GUI smoke tests.
5. **Under-the-bonnet extras:** step debugger with stack/frames view, hex view, stats, optimisation toggle.
6. **Polish & docs:** language reference, ISA doc, student guide, (CI for Windows).

---

## 13. Decisions needed from you

| # | Decision | My recommendation |
| --- | --- | --- |
| D1 | Technology stack | **Option A: C++20 + Qt 6 Widgets + CMake** |
| D2 | Rewrite vs. incremental extension of the current files | **Clean modular rewrite** that ports the good ideas (§3.6) |
| D3 | Typing discipline | **Static typing** with explicit types + `var` inference |
| D4 | Program entry point: mandatory `main()` or top-level statements | Top-level statements allowed (simpler first samples); `main()` optional |
| D5 | Syntax style | **C-family** (braces/semicolons) |
| D6 | Scope of v1 types: include `float`/`char`? Include `input()`? | v1: `int bool string T[]`; `float` and `input()` as stretch |
| D7 | Keep the shunting-yard parser as a teaching comparison mode? | Yes, as a stretch feature (§8.2 #12) |
| D8 | Name: keep "DLang"? Revive "DumbLang"? Rename to avoid the D-language clash? File extensions? | Keep "DLang" as the product name; source `.dl`, bytecode `.dlc` |
| D9 | Location: keep everything under `dlang/` or promote to the repo root (scripts must be at `./run.sh`)? | Put the new toolchain under `dlang/`, with `run.sh`/`run_tests.sh` at the **repo root** delegating to it (or move everything to the root) |
| D10 | What to do with the legacy VS/WinForms projects and `archive/MyXML` | Move the old DLang to `legacy/` (historical reference, excluded from the build); leave `archive/` untouched |
| D11 | Unit-test framework | **doctest** (vendored, single header, fast compile) for core; **QtTest** for the GUI |
| D12 | Coverage threshold to enforce in `run_tests.sh` | Core ≥ 90 % lines; report-only for IDE |
| D13 | Set up GitHub Actions CI for Windows verification (requires pushing) | Yes, if you're happy for me to push a branch |
| D14 | MVP feature cut for the IDE (§8.2) | Must-have 1–5 plus the step debugger (#6) in the first release |

---

## 14. Decisions taken and implementation status (2026-11-04)

> **Correction to §2:** the original `.gitignore` already had a generic C++/CMake section after
> the Python template. It lacked Visual Studio/MSVC, gcov and toolchain-artefact entries, which
> have now been added.

| # | Decision (from the project owner) | Implemented as |
| --- | --- | --- |
| D1 | Modern C++ (latest standard) for the compiler, VM and CLIs; **Python/Tkinter** for the IDE | C++26 mode (`CMAKE_CXX_STANDARD 26`; MSVC: `/std:c++latest`), standard library only. Tkinter IDE in `ide/thunder_ide/`, standard library only. The IDE drives the CLIs through their JSON output (`thunderc --json`, `thundervm --trace-json/--result-json`), so it contains no compiler logic. |
| D2 | Clean rewrite | New code in the repository root (`src/`, `ide/`, `tests/`, `samples/`, `docs/`, `tools/`). |
| D7 | No shunting-yard; use a better algorithm | Recursive descent for statements + **Pratt (top-down operator precedence)** parsing for expressions, with panic-mode error recovery. |
| D8 | Rename the language | **Thunder** (the request said "Thuder", read as a typo for *Thunder*). Source files `.th`, bytecode `.thc`, magic number `THND`. |
| — | Remove `new` | Arrays are created with literals (`[1, 2]`) or the built-in `array(n, value)` (which copies nested arrays, so grids work as expected). |
| — | Allman style in the IDE and samples | All samples use Allman style (checked by a test). The editor moves a `{` typed after code onto its own line and dedents `}`. **Format** re-indents a whole file. The C++ code also uses Allman (`.clang-format`). |
| — | Small but complete, extensible; Pashto later | Types `int bool string T[]`; `var`/`const` with inference; `if`/`else if`/`else`, `while`, `for`, `break`/`continue`; functions, recursion, optional `main()`. Extensibility: keywords and built-in names come from a **language pack** (`language_pack.cpp`), identifiers may use any script, columns count code points, bidi marks are whitespace, all compiler messages are in one catalog with stable codes (`diagnostics.cpp`), IDE text is in `i18n.py` with a right-to-left flag. |
| D9/D10 | New code at the root; legacy folders left as they are | `dlang/` and `archive/` are untouched. |
| D11 | Any test framework | C++: a small built-in framework (`tests/cpp/minitest.hpp`, no downloads). Python: `unittest`. Coverage: gcov (C++) and a standard-library line tracer (`tools/pycoverage.py`). |
| D12 | 90% coverage threshold | Enforced by `run_tests` for C++ (lines, when measured) and the Python IDE. |
| D13 | No GitHub Actions; PowerShell equivalents | `run.ps1` / `run_tests.ps1` alongside `run.sh` / `run_tests.sh`. All four call the same Python drivers in `tools/`. |

### Notes for Windows (not verifiable on the Linux development machine)

* `run.ps1` lets CMake pick its default generator (the newest Visual Studio), builds the Release
  configuration and starts the IDE with the same Python interpreter.
* The CMake files put executables in `<build directory>/bin` for every generator (no `Debug/` or `Release/` subfolder), so the IDE finds
  them without per-configuration paths.
* The C++ code avoids GCC-only built-ins: overflow checks are portable, console output switches the
  Windows console to UTF-8, and MSVC gets `/W4 /permissive- /utf-8 /Zc:__cplusplus`. It builds
  warning-free on GCC 15 with `-Wconversion -Wsign-conversion`, which catches most of what MSVC's
  C4244/C4267 report. MSVC warnings are counted in the test summary rather than treated as
  errors.
* MSVC has no gcov. With [OpenCppCoverage](https://github.com/OpenCppCoverage/OpenCppCoverage)
  installed, `run_tests.ps1` measures C++ coverage (including the CLI tests, via
  `--cover_children`). Otherwise C++ coverage is reported as "not measured" and only Python
  coverage is enforced.

---

## 15. Second iteration (2026-11-05)

| # | Requirement | Implemented as |
| --- | --- | --- |
| 1 | Highlight syntax while typing | Root cause: Tk's `<<Modified>>` event fires only when the modified flag changes, so only the first edit after loading triggered highlighting. The editor now routes the Text widget's Tcl command through a proxy that reports every insert, delete, undo and redo. The controller re-highlights 40 ms after the last keystroke. A regression test types character by character into the real widget. |
| 2 | Dockable panels | `core/layout.py` (a toolkit-neutral model, saved in the settings file) and `tk/docking.py`. Each panel (Editor, Under the bonnet, Output, Right-to-left view) has a title bar to collapse, float (Tk's `wm manage`/`wm forget`), move to the left, right or bottom area, and hide (the View menu restores it). Resizable sashes; Reset layout. |
| 3 | `include/` + `src/` layout | Public headers in `include/thunder/` (`include/thunder/cli/` for the tools); sources in `src/core/` and `src/cli/`. Includes stay `"thunder/…"`. Test-only headers stay in `tests/cpp/`. |
| 4 | GUI abstracted for a future Qt port | `core/` (no toolkit imports) holds the controller and all logic. `core/ui.py` defines the `Backend`, `Scheduler` and view protocols. `tk/` implements them. `tests/python/fake_ui.py` is a second, display-free backend that the controller tests use, which shows the interfaces are sufficient. |
| 5 | `struct` and a printing function | Structs with typed fields; positional or named construction (`Point(y: 2, x: 1)`, with omitted fields defaulted); field access and assignment; reference semantics like arrays. New built-ins: `inspect(v)` (detailed multi-line string), `dump(v)` (prints it) and `copy(v)` (deep copy). `str`/`println` show `Point(x: 1, y: 2)`. Recursive structs are rejected so reference counting cannot leak cycles. VM: `DUP`, `NEW_STRUCT`, `GET_FIELD`, `SET_FIELD`, a struct table in the bytecode (format version 2). |
| 6 | Full Pashto support, RTL; optional semicolons | A Pashto language pack covers every keyword, type, literal and built-in; Pashto digits (any of the three digit scripts); `،` `؛` `٪` `×` `÷`; «» strings; Pashto runtime output (digits, رښتیا/دروغ, «», `، `); and all compiler and runtime messages. Language is chosen with a `// ژبه: ps` directive or `--lang ps`. `thunderc --translate` converts programs both ways, and the IDE has a Translate button. The IDE interface is fully translated and mirrored for RTL. Semicolons are optional (the rules are in docs/language.md §6). |

### Decisions taken without asking (please review)

* **Statement ending rule:** a line break ends a statement unless the line ends inside brackets or
  after an operator/comma. An operator *starting* a line begins a new statement, so the JavaScript
  pitfall (`a` newline `-b` meaning `a - b`) cannot happen. `;` remains valid and is required between
  two statements on one line and in `for (…; …; …)`.
* **Struct semantics:** reference types (like arrays, and like classes in Java, C# or JavaScript)
  rather than C-style value copies. This keeps the VM simple and consistent with arrays; `copy()` is
  available when a copy is wanted.
* **Struct construction syntax:** `Point(1, 2)` / `Point(x: 1, y: 2)` rather than `Point{…}`, because
  braces after a name would be ambiguous with a following block now that semicolons are optional.
* **Pashto vocabulary** (e.g. `متغیر`, `دنده`, `جوړښت`, `که`, `کنه`, `ترڅو`, `لپاره`, `راګرځه`,
  `چاپ_کرښه`). All Pashto text is in two files for review: `src/core/language_pashto.cpp` (language and
  messages) and `ide/thunder_ide/core/i18n.py` (IDE). **It was drafted without a native speaker and
  should be reviewed by one.**
* **Pashto samples** live in `samples/ps/` with the same file names as their English counterparts,
  so switching language loads the same lesson. 10 lessons are translated; the remaining English
  samples can be converted with the Translate button.

### Right-to-left rendering: what Tk can and cannot do

Verified with screenshots on Ubuntu (Tk 8.6.17, X11/XWayland):

* Tk draws characters strictly left to right and does not join Arabic letters on X11. The IDE
  therefore applies its own implementation of the Unicode Bidirectional Algorithm (UAX #9, the
  rules needed for single lines) and Arabic contextual shaping (`core/bidi.py`) to every read-only
  view: output, tables, labels, menus, and a dedicated **Right-to-left view** of the program.
* Ten Pashto letters (ټ ځ څ ډ ړ ږ ښ ګ ڼ ۍ) have no Unicode presentation forms, so on Linux they
  are shown unjoined (in the correct order). Joining them needs a font shaping engine (HarfBuzz),
  which Tk 8.6 does not use on X11.
* Tk cannot edit bidirectional text, so the editor shows logical order (right-aligned) and the
  Right-to-left view mirrors it live.
* On Windows and macOS the operating system shapes and orders each piece of text that Tk draws.
  The IDE then only orders differently styled pieces. **This path could not be tested here.**
* **Recommendation:** a Qt (PySide6) backend would give full bidirectional editing and complete
  Pashto shaping on every platform. The new `core/ui.py` interfaces make it a self-contained task:
  about the size of `tk/` (~1,300 lines), plus `QDockWidget` for docking.

---

## 16. Third iteration (2026-11-05): Qt IDE, C++23, right-to-left editing

| # | Feedback | Implemented as |
| --- | --- | --- |
| 1 | C++26 does not compile with Visual Studio 2026 / CMake 4.4.4 | `CMAKE_CXX_STANDARD 23`. Nothing in the code needed C++26; GCC 15 builds it warning-free with `-Werror` and all C++ tests pass. (Not verified on Windows from here.) |
| 2 | No extra panel for the right-to-left view | The "Right-to-left view" panel, the `SourceView` interface, `core/bidi.py` and the `collapsed` panel state are gone. The **editor itself** switches between left-to-right and right-to-left layout with the program's language (`EditorView.set_right_to_left`, driven by the `// ژبه: ps` directive or the interface language). It was indeed a workaround for Tk, which cannot edit bidirectional text. |
| 3 | Pashto text direction must match the examples in `issues/turn3_pashto_text_direction_issues.png` | See "Token-level direction" below. The language was also amended to match the examples: `println` is `کرښه چاپ` (two words; `str`/`to_int` are `متن کړه`/`عدد کړه` for consistency), the lexer joins multi-word names into one token, and Pashto source uses the plain `"` quotes and `,` `;` `%` punctuation (the translator now writes them; `«»`, `،`, `؛`, `٪` remain accepted). The Pashto samples were normalised with the translator; their golden outputs are unchanged. |
| — | Switch the GUI to Qt (PySide6) as recommended in §15 | `ide/thunder_ide/qt/` replaces `tk/` (about 1,100 lines: `editor.py`, `panels.py`, `main_window.py`, `backend.py`, `style.py`). Docking uses `QMainWindow`/`QDockWidget` (move, tab, float, close; View > Panels restores; Qt's exact state is saved in `Layout.toolkit_state`). The `core/` code and its interfaces were unchanged apart from the removals above, which confirmed the split made in §15. The Tk backend was removed rather than kept as a fallback: it would render Pashto wrongly and double the UI maintenance. It remains in the git history. |

### Token-level direction (how the expected rendering is produced)

The screenshot's "Current Pashto" box is what the Unicode Bidirectional Algorithm (UBA) produces
when a Pashto program contains English identifiers: `متغیر name = "Nasir"` becomes
`"name = "Nasir متغیر`, because UBA treats `name = "Nasir` as one English phrase. The "Expected"
boxes read the statement right to left **token by token**, as the fully Pashto program is read:
keyword, name, `=`, value; function name, then its arguments. Each Latin name or string literal keeps
its own left-to-right order inside.

The editor achieves this without touching the program text: `core/direction.py` builds a *display*
string in which every token (name, keyword, number, string literal, comment) is wrapped in Unicode
isolate marks (FSI … PDI). For UBA an isolate is an opaque unit, so everything between tokens takes
the paragraph direction and Qt's text engine does the rest: ordering, letter joining (all Pashto
letters, also on Linux) and mirroring of `( ) [ ] { } < >`. The Qt editor keeps `QPlainTextEdit` for
text, undo, cursor and scrolling, but paints each line from the display string with `QTextLayout`
and does its own caret placement and mouse hit-testing (Qt merges an isolate mark with the following
character for cursor purposes, so the caret rule never asks about a token's first character; see
`LineLayout.caret`). The same rule applies in English programs to Pashto names.

Verified against the screenshot (Qt offscreen rendering, also checked pixel by pixel in the tests;
`docs/images/ide_pashto_program.png` shows the IDE with the translated program):

| Expected box | Rendered |
| --- | --- |
| `"Nasir" = name متغیر` | identical |
| `٤٠ = age متغیر` | identical (`۴۰`) |
| `(age, " ", name)کرښه چاپ` | `(age ," " ,name)کرښه چاپ` — the commas follow the token they belong to in reading order |
| `("You are a child")کرښه چاپ` | identical, with the period inside the string staying after "child" |
| `(age < ۱۸) که` | **`(۱۸ > age) که`** — the condition is read right to left like everything else, which matches the "Expected Full Pashto Program" box (`(۱۸ > سن) که`). The mock-up's left-to-right `age < ۱۸` would need an exception for comparisons; please say if you prefer that. |

### Other notes

* **Settings file:** the saved layout now has `toolkit_state` (Qt's serialised arrangement); old
  settings files load and fall back to the default arrangement.
* **Running:** `./run.sh` / `.\run.ps1` check for PySide6 and print the `pip install PySide6`
  command if it is missing. On a Linux Python that refuses `pip install` (PEP 668), create a
  virtual environment first; the scripts use whatever `python3`/`py` is on the PATH.
* **Tests:** `tests/python/test_qt.py` drives the real Qt widgets on Qt's offscreen platform (no
  window appears; `THUNDER_TEST_ON_SCREEN=1` shows them). `test_direction.py` covers the layout
  rule. Coverage threshold and summary are unchanged; the summary's per-part line now reads "qt".
* **Not changed, for review:** runtime output of arrays and structs still uses `«»` and `، `
  (`[«a»، «b»]`); the Pashto vocabulary still awaits a native speaker.

---

## 17. Fourth iteration (2026-11-06): indentation syntax, tuples and maps, self-contained scripts

| # | Feedback | Implemented as |
| --- | --- | --- |
| 1 | Remove the Format button | The Format command (toolbar, menu, shortcut, `core/formatter.py` and its tests) is gone. With indentation syntax the layout *is* the program, so a re-formatter would change its meaning; the editor instead indents as you type (below). |
| 2 | Indentation-based blocks instead of braces | The lexer emits `INDENT`/`DEDENT` tokens for changes of indentation outside brackets (continuation lines after an operator or comma are exempt; a tab counts as four spaces; blank and comment lines are ignored). The parser's `block` rule is `INDENT statements DEDENT`; `if`/`while`/`for` conditions no longer need parentheses (they may still have them); `pass` is the empty statement; `struct` bodies are indented field lists. New diagnostics: `L008` inconsistent un-indent, `P012` missing indented block, `P013` unexpected indentation, `P014` braces used for a block (one clear message, the braced block is skipped), `P015` tuple arity. `inspect`/`dump` output also uses indentation instead of braces. All samples, docs and tests were converted; the C++ test snippets were converted mechanically (a converter in the session, not kept) and the golden transcripts regenerated: only positions moved, plus the redesigned `e2_syntax_error` sample. |
| 3 | Run scripts set up everything, including Python and PySide6 | `tools/pyenv.py`: `run.sh`/`run.ps1`/`run_tests.sh`/`run_tests.ps1` (through `launch.py` / `run_tests.py`) use the running interpreter if it has PySide6, otherwise create `<repo>/.venv`, bootstrap pip if the system Python has no `ensurepip` (the case on this Ubuntu machine), `pip install PySide6` once, and re-execute themselves with that interpreter. `THUNDER_PYTHON` overrides the interpreter; `THUNDER_NO_VENV=1` forbids the automatic set-up. Verified from a clean state on Linux; the Windows path uses the same code with `Scripts\python.exe`. |
| 4 | Tuples and associative arrays | **Tuples** `(1, "one")` of type `(int, string)`: immutable, `t.0`/`t.1` access, `var (a, b) = t` unpacking, `len`, structural `==`, usable as function results and in arrays/maps. **Maps** `{"a": 1}` of type `{string: int}` (keys `int`, `bool` or `string`): `m[k]` (runtime error `R011` if absent), `m[k] = v`, `len`, `has`, `remove`, `keys`, `values` (insertion order), reference semantics like arrays, `copy`. Opcodes `MAKE_TUPLE`, `TUPLE_GET`, `MAKE_MAP`; `INDEX_GET`/`INDEX_SET` also serve maps; bytecode format version 3. Semantic diagnostics `S038`–`S044`. Pashto: `تېر` (pass), `لري`, `لرې کړه`, `کیلۍ`, `ارزښتونه`. Samples `17_tuples` and `18_maps` in both languages. |

### Design notes

* **No colon after block headers.** Python's `:` would collide with Thunder's type annotations
  (`func f(n: int): int:` reads badly), and the indented line that follows is unambiguous on its
  own. The editor therefore recognises block openers by their first word (`if`, `else`, `while`,
  `for`, `func`, `struct`, or the Pashto spellings supplied by the compiler's language description)
  to indent the next line, and un-indents after `return`, `break`, `continue` and `pass`.
* **Bare blocks are gone.** A `{ ... }` block used only for scoping has no indentation equivalent;
  scopes now come from functions, loops and conditionals (sample 14 was adjusted).
* **Maps keep insertion order** and compare by identity (like arrays); tuples compare by value.
  Map keys are limited to `int`, `bool` and `string` so that key equality is always structural.
* **Recursion check** through tuples and maps: a struct may not contain itself via `(int, S)` or
  `{string: S}` either, so values stay finite and reference counting stays cycle-free.
* **Error recovery with indentation:** after a broken statement the parser skips the indented
  block that belongs to it, so one mistake produces one message (tested in `test_parser.cpp`).

---

## 18. Fifth iteration (2026-11-06): modules, input, a library of built-ins, the os module

| # | Requirement | Implemented as |
| --- | --- | --- |
| 1 | Split code into modules or libraries and import them | `import "lib/geometry"` loads `lib/geometry.th`. It is looked for next to the importing file, then in the directories given with `thunderc -I/--module-path` (the IDE passes the directory the program was opened from). Imports are **flat**: a module's names are used directly, and clashes are ordinary redeclaration errors. Each file is loaded once, so diamond and circular imports are harmless. A module may only declare things (`S047`; no `main`, `S048`), its variables are initialised before the importer's statements, and it must be in the importer's language (`S046`). `import os` (an identifier, no quotes) imports a built-in module. Every span carries a file id (`Span::file`). Diagnostics, the JSON report (`file`, `file_name`, `modules`), runtime errors (`in area [lib/geometry.th] (line 7)`) and the bytecode line table (format version 4) say which file code came from. Samples `19_modules` (+ `lib/geometry.th`, which imports `lib/numbers.th`), English and Pashto. |
| 2 | User input | `input()`, `input(prompt)` and `end_of_input()`. The VM reads an `std::istream` (standard input in `thundervm`). The IDE has an **Input** line under the Output panel (enabled while a program runs, with Send and End input), pipes it to the program, echoes typed lines, and now shows output as it arrives rather than line by line, so prompts appear before the program waits. Golden tests feed `tests/golden/<lang>/<sample>.input`. Sample `22_input` (English and Pashto). |
| 3 | String, pair and list functions | 35 new built-ins (`docs/library.md` §4–7): text (`substring`, `trim`, `to_upper`, `to_lower`, `split`, `join`, `replace`, `starts_with`, `ends_with`, `contains`, `index_of`, `repeat`, `reverse`, `char_code`, `from_char_code`, `is_int`); pairs (`first`, `second`, `swap`, `zip`, `enumerate`, `items`); lists (`push`, `pop`, `insert`, `remove_at`, `slice`, `concat`, `sort`, `range`, `sum`); maps (`get`); higher-order (`map`, `filter`, `reduce`). `map`/`filter` needed **function values**: function types `func(int): bool`, function names as values, calls through variables and any expression (`handlers[i](x)`), new opcodes `FUNC_REF` and `CALL_VALUE`. Generic built-ins are type-checked from their arguments, and the function given to `map` is checked against the element type (`S051`). Samples `20_text`, `21_lists`. |
| 4 | A module for system/OS-level functions | `import os` gives 17 functions (`docs/library.md` §8): `args`, `get_env`, `exit`, `platform`; `read_file`, `write_file`, `append_file`, `file_exists`, `remove_file`, `list_dir`, `current_dir`; `run` (output passes through the program's output) and `run_output`; `time`, `clock_ms`, `sleep`, `date_time`. Without the import their names are ordinary identifiers (using one gives `S049`, "add import os"). Everything platform-specific is in `src/core/os.cpp` (POSIX and Windows code paths). `thundervm prog.thc a b` passes arguments, and `exit(n)` becomes the exit status (also in `--result-json`). Sample `23_os`. |
| — | Document the new API in a new MD file | **`docs/library.md`**, also under the IDE's Help > Library reference. `language.md`, `bytecode.md` and the README were updated. |

### Decisions taken without asking (please review)

* **Flat imports, no qualified names.** `area(r)` rather than `geometry.area(r)`. This is the
  simplest model to teach (an import works like pasting the declarations in) and needs no new
  syntax for qualified types and constructors. The cost is that two modules cannot both define a
  name. If you prefer namespaces (`import "lib/geometry" as g` + `g.area`), they can be added on
  top: the module boundaries are already kept.
* **Quoted files, unquoted built-in modules.** `import "lib/geometry"` versus `import os`. The quotes
  allow paths and any script in file names, and the difference shows students which is which.
* **Modules only declare.** A statement in a module is an error rather than code that runs on
  import, so importing has no side effects and the order of imports never matters.
* **Same language for module and program.** Built-in names are resolved in the program's language,
  so a Pashto module in an English program is rejected with a hint to translate it.
* **Modules are loaded during semantic analysis.** Resolving a name in an `import` is name
  resolution, and the IDE's pipeline shows module errors in that phase. The modules' own lexical
  and syntax errors keep their `L`/`P` codes.
* **The library is always available, `os` is not.** The text and list functions are used too
  often to require an import. Making them built-ins does reserve 35 more names: sample 15's own
  `contains` function was renamed `is_inside` (its output is unchanged). The `os` names (`run`,
  `time`, `exit`, ...) are common words, so they are reserved only after `import os`.
* **Naming.** `to_upper`/`to_lower` (the request said "toupper") to match the existing `to_int`;
  `substring(s, start, end)` with an exclusive end (Java/JavaScript style), and `slice` as its
  generic twin for arrays; `first`/`second` work on any tuple (not just pairs).
* **Function values without closures.** Only top-level functions you declare can be values.
  Thunder has no nested functions, so nothing is captured. Built-ins cannot be values, because
  most are generic. A function variable without a value holds "no function" (`<func ?>`); calling
  it is runtime error `R020`. That way struct fields and variables of function type still have a
  default, like every other type.
* **Case conversion is ASCII-only.** Pashto has no case. Full Unicode case mapping would need
  tables the standard library does not provide.
* **`run` captures, then prints.** A command's output is collected and then written through the
  VM's output, so it appears in the IDE and in golden tests. It is not streamed while a long
  command runs.
* **No sandbox.** `os` functions run with the user's rights, like any program the student
  writes. A `--no-os` switch for `thundervm` would be easy to add if programs from untrusted
  sources are ever run.
* **Pashto vocabulary.** All 54 new Pashto built-in names (`docs/library.md` §10), the keyword `راوړه` for
  `import` and `سیسټم` for `os` were drafted without a native speaker and should be reviewed, like
  the existing vocabulary.

### Notes

* **Bytecode format version 4.** `.thc` files from earlier builds are rejected with "unsupported
  bytecode version 3 (expected 4)". Recompile them.
* **`thundervm` arguments.** Everything after the program's file name is passed to the program, so
  `thundervm` options must come first (`thundervm --stats prog.thc a b`). `--` also ends the
  options.
* **Windows (not verifiable here).** Paths are converted from UTF-8 explicitly
  (`os::utf8_path`), so module and file names in Pashto work with MSVC. Commands use
  `_popen`/`cmd.exe`. `localtime_s`, `_dupenv_s` and the IDE's binary pipes with an incremental
  `\r\n` decoder cover the other Windows differences. The `23_os` sample only uses `echo`, which
  works in both shells.

---

## 19. Sixth iteration (2026-11-07): a 2D graphics library, the Canvas panel, and a panel-resizing fix

All tests were run and passed before any change (`./run_tests.sh`: 185 C++ and 154 Python tests,
coverage 97.4% C++ and 95.8% Python). The resizing bug was fixed first.

| # | Request | Implemented as |
| --- | --- | --- |
| 1 | Bug: the bottom Output panel could not be resized as freely as "Under the bonnet" | **Cause:** the debugger's VM tab stacks five tables with a combined minimum height of about 400 pixels, which made the "Under the bonnet" dock at least 454 pixels tall, so the splitter above the Output panel stopped at about a third of the window (323 of 860 pixels). **Fix:** the VM tab scrolls inside a `QScrollArea` and no longer imposes its minimum size on the window. The dock's minimum height fell to 122 pixels, so the Output panel can now take most of the window (and the editor can be made wider, too). A Qt test resizes the docks; without the fix it fails. |
| 2 | Foundations for a simple 2D graphics library | Built-in module **`graphics`** (`import graphics`; 20 functions, `docs/graphics.md`): `canvas`, `clear`, `pixel`, `line`, `rect`, `rect_outline`, `circle`, `circle_outline`, `text`, `text_size`, `line_width`, `rgb`; animation with `frame()` (show, then wait for the next frame), `show()` and `frame_rate`; input with `key_pressed()` (a queue), `key_down(key)`, `mouse_x/y`, `mouse_down`. Also `random_int(low, high)` and `seed_random(seed)` in the always-available library (simulations and games need them). Colors are strings: names, `#rrggbb`/`#rgb`, `rgb(r, g, b)`, and Pashto color names. The canvas is a list of shapes (`include/thunder/graphics.hpp`) that the VM presents to a *graphics host*. New runtime errors `R021`–`R024`. |
| 3 | An intuitive way of displaying output in the IDE | A **Canvas** panel, tabbed next to "Under the bonnet", that comes to the front and takes the keyboard when the program shows its first frame. It scales the picture to fit, reports keys and the mouse to the program, and keeps the last picture after the run. It is a dock like the others (move, float, close; the layout remembers the tab arrangement). `thundervm --canvas` streams frames as JSON record lines on standard output (incremental: only shapes added since the last frame); key and mouse events come back as record lines on standard input, beside ordinary `input()` lines, and are read on a background thread. The IDE applies frames on its output-reader thread and repaints at most once per frame it can show. |
| 4 | Sample programs: simple simulation animations, perhaps a simple game (snake) | `24_drawing` (a still picture), `25_bouncing_ball` (the animation loop), `26_fireworks` (a particle simulation: structs, gravity, fixed-point numbers, `filter`, mouse clicks), `27_life` (Conway's Game of Life: keys and mouse), `28_snake` (the game: arrow keys, collisions, score, game over, restart). `24` and `25` are also in Pashto. |

### Decisions taken without asking (please review)

* **Shapes, not pixels.** The VM sends shape lists rather than bitmaps. A typical Snake frame is
  under 300 bytes instead of 360 KB, the IDE draws with antialiasing at any zoom, and the same
  picture can be saved as SVG (`thundervm --svg`). Without a fill-polygon primitive, triangles are
  built from lines (`24_drawing`). Polygons, images and sounds are possible later extensions.
* **A canvas that keeps its contents.** Everything drawn since the last `clear()` stays,
  like paint on paper (the intuitive model for beginners). Animations clear at the start of each frame.
  A limit of 100,000 shapes between clears (`R024`) catches programs that forget.
* **The VM still runs in its own process.** Frames and events share the program's standard
  streams, marked by the ASCII record separator. This keeps one portable `thundervm` (no extra
  pipes, which are awkward on Windows), and a stuck program still cannot freeze the IDE.
* **Real-time pacing only when someone watches.** With `--canvas` (the IDE), `frame()` waits for
  the next frame. Headless runs do not wait, so tests and `--svg` runs are fast and repeatable.
  `--max-frames n` ends an endless animation normally; `--seed n` fixes the random numbers.
* **Step limit per frame.** The IDE's safety limit on instructions (which stops runaway loops)
  applies *between two frames* once a program shows frames, so animations can run indefinitely
  while a frame that never finishes is still caught.
* **Own random-number generator** (SplitMix64 with rejection sampling) instead of
  `std::uniform_int_distribution`, whose results differ between standard libraries. A seeded
  program then draws the same numbers with GCC and MSVC, which the golden tests rely on.
* **Gated like `os`.** The graphics names (`line`, `text`, `rect`, ...) are common words, so they are
  reserved only in programs that `import graphics`. `random_int`/`seed_random` are always
  available (rare names, useful without graphics).
* **Key names** are English and lower case (`"left"`, `"space"`, `"a"`) in every language, since
  they name physical keys.
* **Pashto vocabulary.** The 22 new names, `ګرافیک` for the module and 13 color names are drafts
  for review by a native speaker (`docs/graphics.md` §10).

### Bugs found and fixed while building this

* **`thundervm` could not exit while its input stayed open.** A thread blocked in
  `std::getline(std::cin)` holds the C library's `stdin` lock, and `exit()` waits for that lock to
  flush the streams, so the process hung. Standard input is now read below the stdio layer
  (`os::read_standard_input_line`, `read`/`_read` on descriptor 0).
* **Pashto text vanished from the canvas** in the mirrored (right-to-left) interface: Qt turns
  `AlignLeft` into right alignment for right-to-left text, which placed it off the canvas. It is
  now drawn with `AlignAbsolute`; a test in the Pashto interface covers it.
* **SVG text placement.** Several SVG viewers ignore `dominant-baseline="hanging"`, so text at the
  top of the picture was cut off; the baseline is now placed explicitly.

### Notes

* **Speed.** The release build runs about 7 million instructions per second; the Game of Life
  (32 × 20 cells) needs about 55,000 per generation. The instrumented test build (AddressSanitizer
  at `-O0`) is roughly 100 times slower, so golden tests show 20 frames of each animation.
* **Windows (not verifiable here).** Standard input is read with `_read`, and painting, keys and
  the mouse go through Qt, which behaves the same on Windows. The graphics code itself uses no
  platform-specific calls.

---

## 20. Seventh iteration (2026-11-07): resizing, Pashto samples, commas and word operators

| # | Feedback | Implemented as |
| --- | --- | --- |
| 1 | The Output panel could be enlarged but not made smaller again | **Cause:** every panel is a dock widget, so the main window had no central widget. Without one, Qt only lets the separator above the bottom dock area move upwards. **Fix:** an empty, zero-width central widget between the left and right areas. Both separators now move both ways (verified by dragging them with the mouse in a Qt test, which fails without the fix). The saved panel proportions are now measured over the space the panels share rather than the whole window. |
| 2 | Add the missing Pashto samples | All 33 English samples now have Pashto counterparts with the same file names, so switching the interface language always loads the same lesson. The former `07_loops` became `06_while_loops` and `07_for_loops`, as in English, and the modules in `samples/ps/lib/` were updated. A test checks that every English sample has a Pashto counterpart. |
| 3 | Replace the semicolon with a comma; `،` in Pashto | `;` is no longer part of the language. The parts of a `for` header are separated by commas (`for var i = 0, i < 10, i = i + 1`), and a statement simply ends at the end of its line. Writing `;` (or the Arabic `؛`) gives lexical error `L002` with the hint "did you mean ','". Pashto programs write `،` (the translator writes it everywhere; `,` is still accepted). A line ending in a comma no longer continues onto the next line, so `for ,,` can open a block. |
| 4 | Pashto programs must use Pashto names only | Every Pashto sample and module now uses Pashto names. Single letters follow the Arabic-script maths convention (`ن` for n, `س`/`ص` for x/y, `الف`/`ب` for a/b). Three built-in pieces of English that Pashto programs could not avoid were given Pashto forms: the entry function `main` → `اصلي` (both accepted; shown as `<اصلي>` in call stacks), the mouse functions `د موږک x/y` → `د موږک افقي ځای` / `د موږک عمودي ځای`, and key names (`"left"` → `"کیڼ"`, `"space"` → `"تشه"`, ...). A test checks that no Pashto sample contains a Latin name or an ASCII comma. |
| 5 | Vocabulary: کېکاږل → کښېکښل, کینوس → ټاټ, تصادفي تخم → تصادفي بنسټ | Applied to the built-in names (`کښېکښل شوې تڼۍ`, `تڼۍ کښېکښل شوې ده`, `موږک کښېکښل شوی`, `ټاټ`, `ټاټ پاک کړه`, `تصادفي بنسټ`), their descriptions and messages, the IDE (the Canvas panel is `ټاټ`) and the documentation. |
| 6 | Operators as words: `and`, `or`, … (Pashto `او`, `یا`, …) | `&&` → `and` / `او`, `\|\|` → `or` / `یا`, `!` → `not` / `نه`. `!=` stays. `not` binds less tightly than comparisons (as in Python: `not a == b` is `not (a == b)`). The old symbols give `L002` with a hint naming the word in the program's language. Messages, the syntax tree and the translator use the words in each language. |
| 7 | Clean up the documentation; remove DLang/DumbLang references | Removed from the README (the prototype screenshot and note, the `dlang/` row), `docs/language.md` and sample `02_arithmetic`. All documents were updated for the syntax changes, and a stale "Python/Tkinter" comment in `CMakeLists.txt` was corrected. **Not changed:** sections 1–13 of this file, which are the original review of that prototype and cannot be rewritten without losing the record. |

### Decisions taken without asking (please review)

* **No one-line statement separator.** The comma took over the semicolon's role in `for` headers
  only. Using it to separate statements on one line (`x = 1, y = 2`) would quietly change the
  meaning of `var p = 1, 2` (a variable and a separate, ignored statement instead of an error). So
  every statement now has its own line, and two statements on one line remain error `P010`.
* **Only the logical operators became words.** Arithmetic and comparison operators (`+`, `<`,
  `==`, `!=`, …) keep their symbols, as in mathematics and in Python.
* **Pashto comma everywhere.** The request named `،` for the semicolon's replacement. Since it is
  now the same comma that separates arguments, Pashto programs write `،` in argument lists too,
  and the translator produces it. This replaces the earlier "plain comma" decision (§16). The
  editor's token-by-token layout treats `،` exactly like `,`.
* **Pashto letters as key names.** With a Pashto keyboard the canvas reports the Pashto letter
  typed. Special keys have Pashto names in Pashto programs, and `key_down` accepts either language.
* **Strings may still contain Latin text** where it is the point of the example: `to_upper`
  only changes the letters a–z, the Caesar cipher shifts English letters, and the `os` sample
  passes `echo 42` to the shell (non-ASCII text through `cmd.exe` is unreliable on Windows).
  These are values, not names.
* **Pashto vocabulary** in the new samples (and the new names `اصلي`, `د موږک افقي/عمودي ځای`,
  the key names) are drafts for review by a native speaker, like the rest.

---

## 21. Eighth iteration (2026-11-07): Pashto rendering and recognition fixes, Pashto documentation

| # | Bug / request | Implemented as |
| --- | --- | --- |
| 1 | Negative numbers must be written sign-first (`-۳`), as in Latin scripts - an exception to the right-to-left rule | The bidirectional algorithm places a lone `-` to the right of Pashto digits (after an Arabic letter the digits become "Arabic numbers", rule W2). Fixed in three places, all display-only (program text and string values never change): the **editor** merges a unary sign into the number token and isolates number tokens with an embedded left-to-right context (FSI + LRM ... LRM + PDI in `core/direction.py` - the combination Qt honours, found by measuring glyph positions; LRI alone does not work in Qt's engine); negative numbers inside **comments and strings** get the same marks; the **Output/log/tree panels** wrap `-۳` the same way (`directional()` in `qt/panels.py`; `get_text()` strips the marks). Tests measure the actual glyph x-positions through QTextLayout, so they fail if the sign ever moves back. |
| 2 | Yes/No dialogs appeared in English in the Pashto interface | `ask_yes_no` no longer uses `QMessageBox.question` (its standard buttons come from Qt's own translations, which are not shipped); the buttons are created with the interface's words - Yes/No, **هو/نه** (`dialog.yes`/`dialog.no` in `i18n.py`). |
| 3 | Typed `كرښه چاپ` was not recognised or highlighted | The user's kaf was U+0643 (Arabic kaf, from an Arabic keyboard layout); the language pack registers U+06A9 (Pashto keheh) - visually near-identical, different code points. Every word lookup now folds ك→ک and گ→ګ: compiler (`fold_letters` in `language_pack.cpp`, applied in keyword/builtin/compound/module/key-name lookups, so the lexer, translator and sema all accept it) and IDE (`fold_letters` in `core/editing.py`, used by the highlighter and the auto-indent block words). ي (U+064A) is NOT folded to ی: it is a distinct Pashto letter. |
| 4 | Help documents appeared in English in Pashto mode | Pashto translations of all four references in **`docs/ps/`** (language, library, graphics, bytecode - faithful, moderately condensed where the English prose is long; each header marks the English original as authoritative and the translation as a draft for native review). The Help menu (`show_reference`) opens `docs/<language>/<name>.md` when the interface is not English and the translation exists; the code examples in the Pashto documents compile. |

### Notes

* The LRM/FSI combination for negative numbers was chosen **empirically**: Qt's text engine does
  not honour LRI or LRE..PDF for this case even though UAX #9 says they should work; an LRM on
  both sides of the number inside the token isolate does (verified by cursorToX measurements and
  8x screenshots). The marks appear only in displayed text; `len()`, string values, saved files
  and the golden outputs are untouched.
* Letter folding is one-way (Arabic -> Pashto) and lookup-only: lexemes keep what the user typed,
  so the translator normalises ك to ک when a program is translated, and user-defined identifiers
  are never rewritten.

---

## 22. Ninth iteration (2026-11-10): editor polish, Pashto spelling, packaged graphics

Three sessions' work, logged late (the log had been neglected - resumed as of this entry).

| # | Request / bug | Implemented as |
| --- | --- | --- |
| 1 | Nothing showed whether a block was folded | The gutter draws a fold mark beside every block header: `-` while open, `+` once folded (`qt/editor.py`; foldable headers from `edits.fold_regions`, cached per edit). Works in LTR and RTL layouts. |
| 2 | Packaged graphics apps showed nothing when run from the OS | Two halves. *Packaging:* `thunderc --package` of a graphics program (detected by scanning the bytecode for graphics built-ins, `bytecode::uses_graphics`) copies a display plug-in beside the executable automatically. *Running:* a packaged graphics program started with no host option loads the first display plug-in found beside itself (`PluginDisplay::discover`), or stops with a message pointing at docs/display.md. |
| 3 | No feedback when zooming the editor font | Larger/Smaller font shows "Font size N pt - zoom P%" in the status bar (100% = 11 pt). |
| 4 | Edit menu lacked the standard commands | Undo/Redo/Cut/Copy/Paste/Select all added. Their shortcuts are shown in the menu but left to the focused widget (`app_shortcut=False` -> Qt `WidgetShortcut`), so Ctrl+C still copies from the Output panel. |
| 5 | Pashto keyboard variants and spellings | ي/ى now fold to ی everywhere (compiler `fold_letters` + IDE), so `متغير`/`رښتيا` are recognised; `دروغ` became `درواغ` (old spelling still accepted); the curly double quotes `“ ” „` delimit strings in any combination; `بلبلي` -> `ببلي` in the bubble-sort sample. Samples, goldens, docs and tests updated. |

## 23. Tenth iteration (2026-11-10): Go to line, Windows failures, sockets

| # | Request / bug | Implemented as |
| --- | --- | --- |
| 1 | "Go to line" and other editing commands | Edit menu gained Go to line... (Ctrl+G, a dialog via the new `ask_go_to_line` view method), Duplicate/Delete/Move up/Move down/Indent/Outdent line(s) - all pure functions in `core/edits.py` with tests, wired through `controller._line_edit`. |
| 2 | 14 C++ test failures on Windows | **Cause:** with the Visual Studio generator, `LIBRARY_OUTPUT_DIRECTORY`/`RUNTIME_OUTPUT_DIRECTORY` of the sample libraries gained a per-configuration subdirectory (`bin/Debug/`), so thundervm and the tests could not find `thunder_demo.dll` or the display plug-ins next to the executables. **Fix:** the `thunder_sample_library()` CMake helper pins both to `<build>/bin$<0:>`. Also: `os::Library::open` on Windows now reports the system's words (FormatMessage) instead of a bare "Windows error 126". |
| 3 | Packaged snake.exe reported a missing DLL | The SDL display plug-in is not built on Windows without SDL2, so there was nothing to pack. A new **Win32 (GDI) display plug-in** (`lib/display/win32/`, plain C, nothing to install, real font so Pashto text shapes correctly) is always built on Windows and preferred there when packaging and when a packaged app picks a display by itself. |
| 4 | Qt test failures on Windows (fonts) | Qt's offscreen platform finds no fonts on Windows (Qt ships none); `tests/python/support.py` points `QT_QPA_FONTDIR` at `C:\Windows\Fonts`. |
| 5 | Sockets for Thunder | A new native module `thunder_sockets` (C++, BSD sockets/Winsock): TCP (listen/accept/connect, send/receive, lines, timeouts, peer/port), UDP (open/send/receive/sender), names (`host_address`, `host_name`), every function under an English and a Pashto name. Thunder wrappers `samples/{en,ps}/lib/sockets.th` (`import "lib/sockets"`), sample 35 (server+client in one program, golden-tested), reference docs/sockets.md + docs/ps/sockets.md, Help menu entry. Network conditions are values (`-1`/`""` + `socket_error()`); program mistakes (a handle that is not open) stop the program via `fail()` (R030). |

## 24. Eleventh iteration (2026-11-11): source control fix, lib/, bitwise operators, Bits tab

| # | Request / bug | Implemented as |
| --- | --- | --- |
| 1 | `samples/display/win32/` missing from GitHub | **Cause:** the `.gitignore` Visual Studio section's `[Ww]in32/` pattern (meant for MSVC's per-platform build folders) ignored the plug-in's *source* directory. **Fix:** the pattern is scoped to build trees (`build/**/[Ww]in32/` etc.); the Python template's blanket `lib/` was scoped to `build/lib/` for the same reason (it would have hidden the new top-level `lib/`). The win32 source is now tracked. |
| 2 | 3 remaining Windows Python test failures | `to_canvas` gained an epsilon so `left + 20*scale` maps back to pixel 20 despite float error (a real bug, visible as off-by-one mouse coordinates). The two editor rendering tests were asserting font-metric equalities that only hold for Linux fonts; they now assert the real invariants (hit-testing may return a caret drawn at the same x; wrapped-line y positions are copied from Qt exactly, total height within half a line). |
| 3 | Move the libraries out of samples/ | `samples/native/` -> `lib/native/` (demo + sockets modules), `samples/display/` -> `lib/display/` (text, win32, sdl plug-ins); `samples/` now holds only Thunder programs. Every path reference updated (CMake, docs, tests, messages, READMEs). |
| 4 | Bitwise operators + visualisation | The language gained `& \| ^ ~ << >>` on `int` (64-bit two's complement): lexer tokens (single `&`/`\|` no longer hint at `and`/`or`; `&&`/`\|\|` still do), Python-style precedence (comparisons looser than bitwise, so `flags & mask != 0` works), six new opcodes appended to the table (old bytecode stays valid), VM semantics (shift count 0..63 else R022 with a translated "shift count" term; `<<` wraps, `>>` arithmetic), sema int-only rules, docs (language/bytecode, en+ps) and lexer/sema/execution/opcode tests. The IDE gained a **Bits** tab (Under the bonnet): type two numbers (decimal/hex/binary, Latin or Pashto digits), pick an operator, and the operands and result appear as aligned, colour-coded rows of bits with the changed bits marked - model in `core/bitwise.py` (pure, tested), widget `QtBitsPanel`. |
| 5 | This log | `analysis.md` renamed to `devlog.md`, the missing iterations written up (sections 22-23), and the habit resumed: one section per iteration from now on. |

---

## 25. Twelfth iteration (2026-11-12): test robustness and speed, Bits regression, hex/binary literals, name highlighting

**Transcript summary.** The request had six parts: (1) Windows still showed test failures - remove fragility (pixel positions, clock time) following the F.I.R.S.T. principles, make the tests faster and give intermediate feedback; (2) the right-hand panel could be widened but not narrowed since the Bits tab arrived; (3) a bitwise-operators sample in English and Pashto; (4) keep this log up to date, run long tests in the foreground and finish with a summary; (5) hexadecimal and binary number notation, with samples and documentation; (6) double-clicking a variable or constant name should highlight all its uses.

| # | Request / bug | Implemented as |
| --- | --- | --- |
| 1a | 3 Windows Python failures (`issues/windows_test_failures.txt`) | *Bits test:* the panel only refreshed on `textEdited` (user typing), so a test that set the text programmatically read a stale result - the panel now listens to `textChanged`, which is also what a program setting a value expects. *Docking tests (`0.39 != 0.5`, editor narrower after dragging right):* both were the resize regression of item 2 showing through the tests; the tests now also assert the cause directly (the "Under the bonnet" dock's minimum width must stay modest) and allow a wider platform tolerance on the size fractions. |
| 1b | Fragility: pixel positions, clock time | `test_marks_are_painted` no longer samples one computed pixel; it collects the colours along the middle of each line and asserts which backgrounds appear on which line (font- and glyph-position-independent). The Qt fixture's `pump()` now blocks *inside Qt* (`processEvents(WaitForMoreEvents)`) rather than spinning with `time.sleep`, and a new `wait_until(condition, timeout)` replaces the poll-and-sleep loops: a wait ends the moment its condition holds and fails with a message rather than hanging. |
| 1c | Speed and feedback | The two sample-transcript tests (36 programs x 2 languages, each a compile and a run in child processes) now run the samples on a thread pool and assert in order - the longest part of the Python suite, several times faster. `thunder_tests` prints one `[ PASS ]` line per test case as it finishes and the Python runner names each test (verbosity 2), so a long run shows where it is. |
| 2 | Right-hand panel could not be narrowed | **Cause:** the Bits tab's hint label (one long line, no word wrap) and its table of up to 64 bits gave the tab a rigid minimum width, which QMainWindow enforces on the whole dock. **Fix:** the hint wraps, the bit table lives in a scroll area, and the tab is wrapped like the debugger tab. A test pins the dock's minimum width. |
| 3 | Bitwise sample | `samples/en/36_bitwise.th` and `samples/ps/36_bitwise.th`: three spellings of twelve, every operator, shifts, flag bits (`BOLD \| UNDERLINE`, `& ~BOLD`), masks on a colour (`>> 16 & 0xFF`) and a bit-counting loop; golden transcripts added. |
| 5 | Hex and binary literals | `0x2A` / `0XfF` and `0b1010` (whole numbers only; digits may be Latin or Pashto, hex letters either case). `0x` alone or `0b2` is `L006 malformed number`; too large is `L005`; `0x10.5` is `0x10` followed by a tuple-style `.5` that the parser rejects, as for any number. The translator leaves these literals untouched (no Pashto digits inside `0x...`). Highlighter updated; docs: language reference (en, ps) lexical table and grammar. Lexer tests. |
| 6 | Highlight all uses of a double-clicked name | `edits.occurrences(text, word)` (pure, tested) finds whole-word uses of a *name* - not numbers or operators, and `x` is not found inside `max`. The editor marks them with a soft green background (`OCCURRENCE_BACKGROUND`) under the normal selection; the marks go when the selection is cleared or the text changes (`selectionChanged`, not only `cursorPositionChanged`: clearing a selection does not move the caret). Qt test drives a real double-click. |

### Notes

* The transcript parallelism is safe because `unittest` assertions still happen on the test
  thread, in sample order; only the subprocess work is spread over threads.
* F.I.R.S.T. where it bit: *Fast* (parallel samples, event-driven waits), *Independent* (each
  Qt test resets layout and program in `setUp`), *Repeatable* (no pixel coordinates, no font
  metrics equalities, timeouts only as a safety net), *Self-validating* (the resize test names
  its cause), *Timely* (every change in this iteration came with its test).

---

## 26. Thirteenth iteration (2026-11-12): SDK structure, a real Help viewer, format/hex/bin, mutation testing and CRAP

**Transcript summary.** Five quality requests: (1) reorganise the documentation into a proper
SDK reference, cover the later features across the board, and give the Help menu a real
markdown viewer with keyword search; (2) clean dead and duplicated code - samples/ must hold
only sample programs, reusable SDK code only lib/; (3) formatted printing, with hex and binary,
plus number<->hex/binary conversion functions; (4) use a mutation tester and CRAP scripts to
find defects and improve code and test quality; (5) keep this log, run the long tests in the
foreground and end with a summary.

| # | Request | Implemented as |
| --- | --- | --- |
| 1a | SDK reference structure | A new index, `docs/README.md` (+ `docs/ps/README.md`): the nine documents in reading order with what each covers, plus the on-disk layout. Every reference document now carries a breadcrumb to the index. Coverage gaps closed as part of items 2-3 (sockets paths, `format`/`hex`/`bin` in both library references, `0x`/`0b` already in both language references). A new Help menu entry "SDK reference (contents)" opens the index. |
| 1b | Markdown Help viewer with search | `qt/help_viewer.py`: a dialog with the whole reference in a contents list, the document rendered by Qt's GitHub-dialect markdown support (tables, code, images via the file's base URL), and a search line - every match highlighted, Enter/F3 and Shift+F3 walk them with an "n of m" counter; links between reference documents switch the list, web links open externally. The catalog is toolkit-neutral (`core/docs.py`, localized by the Help language rules). The old plain-text dialog was removed with its protocol method (`show_document` -> `show_help`). |
| 2 | samples/ vs lib/ | The Thunder-side sockets API moved from `samples/{en,ps}/lib/sockets.th` to **`lib/thunder/{en,ps}/sockets.th`**, and the compiler gained a default SDK module path: `CompileOptions::sdk_module_path`, set by thunderc to `lib/thunder` next to itself (CMake copies the tree to `bin/lib/thunder`), searched last and per language. Programs now write plain `import "sockets"`. `samples/*/lib/` keeps only the lesson modules of sample 19 (they are the lesson). Dead code removed: unused imports (`Path`, `sys`), and `core.commands.command()` - a helper with no callers, found by the CRAP report's 0% coverage line. |
| 3 | Formatted printing and conversions | New built-ins, appended to the id table so old bytecode stays valid: `format(template, values...)` ({} next value, {x} hexadecimal, {b} binary, {{ }} literal braces; other {...} pass through so printed maps are unharmed; count mismatch R031, {x}/{b} on a non-integer R032), `hex(n)` -> "0x2a" and `bin(n)` -> "0b1010" (sign first for negatives), and `to_int` now reads `0x`/`0b` text back, so the conversions go both ways. Pashto names: `بڼه کړه`, `شپاړسیز کړه`, `دوه ییز کړه`, with translated descriptions and R031/R032 messages. Documented in both library references; sample 36 shows them; C++ tests cover round trips, the most negative int, and both error codes. |
| 4 | Mutation testing and CRAP | Two new standard-library-only tools. `tools/mutation.py` mutates the IDE's pure core modules one change at a time (flipped comparisons, +/-, and/or, off-by-one constants, True/False; decorator constants such as frozen=True are skipped as equivalent) and runs the covering unit tests per mutant. `tools/crap.py` computes CRAP(m) = cx^2 (1-cov)^3 + cx per function from the coverage JSON. Findings acted on: the kill rate for bitwise+edits rose from **77.5% to ~96%** and overall to **87.9%** (372 mutants) through ~30 new edge-case assertions - line/selection boundaries, whole-word search with `_` and punctuation, tab-aware comment placement, outdent/move-line edges, two's-complement width edges, shift-by-zero, debugger step/output boundaries, UTF-16 surrogate-pair counting. The ~45 remaining survivors were analysed: platform branches (settings), clamp redundancies and defensively unreachable comparisons - equivalent in practice. CRAP: no function at or over 30; the worst (25) is `bitwise.visualise`, fully covered. |
| 5 | This log and the runs | All suites ran in the foreground to completion: **C++ 254/254**, **Python 242/242** (with coverage 98.2%), plus two full mutation runs and the CRAP report. |

### Notes

* The SDK module path keeps packaging untouched: imports are resolved at compile time and the
  compiled module travels inside the bytecode, so a packaged program needs nothing extra.
* `format` deliberately leaves unknown `{...}` sequences alone: `println(format("{}", m))` on a
  map prints the map's own braces without tripping the placeholder scanner.

---

## 27. Fourteenth iteration (2026-11-12): a VS Code extension for the whole toolchain

**Transcript summary.** One request: a VS Code plugin that supports the full Thunder workflow,
for advanced users and larger projects with several folders and files - syntax highlighting,
IntelliSense, easy project set-up and building, debugging and running inside VS Code, on Linux
and Windows, with steps for installing and testing it, and this log kept up to date.

### What was built

A complete extension in **`ide/vscode/`** (~6,700 lines of JavaScript, ~3,900 lines of its own
tests). It re-implements nothing about Thunder: every keyword, type, error message, symbol and
instruction it shows comes out of `thunderc --json`, `thunderc --language-info` or
`thundervm --trace-json`, so the editor and the toolchain cannot disagree.

| # | Part | Implemented as |
| --- | --- | --- |
| 1 | Syntax highlighting | A TextMate grammar **generated from the language packs** (`tools/generate_assets.py` reads `thunderc --language-info` for every language and writes `syntaxes/thunder.tmLanguage.json`, `snippets/thunder.code-snippets`, `language-configuration.json` and a bundled copy of the language information). On top of it, **semantic tokens** from the compiler's own token stream (`src/providers/highlight.js`): functions, parameters, fields, structs, constants and built-ins are coloured for what they are, in English and Pashto, including built-ins written as several words (`کرښه چاپ`), which no regular expression gets reliably right. |
| 2 | IntelliSense | `src/report.js` turns the JSON report into a model (diagnostics with ranges, the token stream, declarations with their name ranges, the scope tree, the call at a position, the lines that carry instructions). On it: completion (keywords, types, built-ins filtered by the modules the file imports, names in scope, functions, structs, **struct fields after a `.`**, modules and project files after `import`), signature help (the argument index comes from the syntax tree's `arg N` roles, with a bracket-counting fallback while the line is still being typed), hovers (inferred type, symbol kind, the word's counterpart in the other language), go to definition **across modules**, references, rename, document and workspace symbols, inlay hints for inferred types, document highlights, "tidy layout" formatting and quick fixes. |
| 3 | Diagnostics | `src/diagnostics.js`: debounced compiles (`onType`/`onSave`/`off`), published against the file each problem is in - a program that imports a broken module gets the error in the module's editor, with related information naming the program. Names the compiler marked as never read are shown as greyed-out hints. |
| 4 | Projects and building | `thunder.json` (entry point, language, module paths, native paths, display, arguments, output, package) with a JSON schema; several folders per workspace; **Thunder: New Project...** writes the project file, a program, a module and `.vscode/tasks.json` + `launch.json` in English or Pashto. Building, running, checking and packaging are VS Code **tasks** with the `$thunderc` problem matcher; programs run in a terminal, so `input()` reads what the user types. A graphics program either opens its own window through a display plug-in or is run to the end with `--svg`, and VS Code shows the picture. |
| 5 | Debugging | A **Debug Adapter Protocol** adapter written by hand (`src/debug/adapter.js`, no `@vscode/debugadapter` dependency) over a recording made by `thundervm --trace-json`. Breakpoints (moved to the next line that has instructions, verified against the bytecode's line table), step in/over/out, **step back and reverse continue**, the call stack, locals, globals, the operand stack, the current instruction, and a stop at the instruction that caused a runtime error. Stepping semantics live in a pure module (`src/debug/trace.js`). |
| 6 | Under the bonnet | The tokens, syntax tree, symbols, bytecode listing and hex dump as read-only documents beside the program, filled by `thunderc --emit` - the same text the command line prints. A side-bar view shows each phase with its timing and opens them. |
| 7 | Installing | `scripts/install.sh` and `scripts/install.ps1` link or copy the folder into `~/.vscode/extensions`. The extension is plain JavaScript with **no dependencies and no build step**, so there is no npm, no TypeScript compile and no `.vsix` to produce; `ide/vscode/.vscode/launch.json` also starts an Extension Development Host with F5. |
| 8 | Tests | 175 JavaScript tests with a dependency-free runner (`test/run.js`) and a stand-in for the VS Code API (`test/fake-vscode.js`), plus `tests/python/test_vscode_extension.py`, which runs them as part of `./run_tests.sh`. |
| 9 | Compiler change | `thunderc --language-info` gained a **`modules`** array (the built-in modules under their name in each language: `os`/`سیسټم`, `graphics`/`ګرافیک`), so the extension can complete and highlight `import` targets and offer "add the missing import" in the right language. It is the only change outside `ide/vscode/`, apart from the documentation, the test driver and item 10. |
| 10 | `.gitignore` hid the debug adapter | **The same trap as §24.1.** The Visual Studio template's `[Dd]ebug/` (and `[Rr]elease/`, `x64/`, …) are unanchored, so they matched `ide/vscode/src/debug/` - the debug adapter, the trace model and the launcher would have been missing from GitHub, leaving an extension that cannot debug anything. The patterns are now anchored to the top level (`/[Dd]ebug/`) and to build trees (`build/**/[Dd]ebug/`). A test now lists every file of the extension and asserts that `git check-ignore` claims none of them, so this cannot happen a third time. |

### How it is tested

* **The real toolchain where that is the point.** The model, the providers and the recordings are
  tested against `thunderc` and `thundervm`, not against fixtures: a diagnostic's range is checked
  by slicing the source with it, definitions are followed into a real module file, and the
  debugger steps through a real recording of `fact(4)` forwards and backwards.
* **A stand-in for VS Code** (`test/fake-vscode.js`, ~700 lines) for everything that is about the
  editor. The activation test asserts that **every command package.json promises is registered**
  (and none twice), that every contributed view has a provider, and that the task provider builds
  the commands in the right order.
* **The protocol, not the implementation.** The debug adapter is driven with real DAP requests
  (initialize → launch → setBreakpoints → configurationDone → stopped → continue → stepBack …)
  and the responses and events are asserted.
* **No drift.** `generate_assets.py --check` fails the build if the committed grammar, snippets,
  language configuration or bundled language information no longer match the compiler; another
  test compares the extension's `tidy()` with the Qt IDE's `edits.tidy` over **every sample in
  both languages**; another fills in the snippets' placeholders and checks that nothing fails in
  the lexer or the parser; another compiles and runs both project templates; another runs the
  installers into a temporary directory and checks what they put there.
* **Node.js is not a prerequisite.** `find_node()` uses `node` if it is there and otherwise the
  Node that VS Code itself is built on (`ELECTRON_RUN_AS_NODE=1 code`), so a machine that can run
  the extension can run its tests. If neither exists the JavaScript tests skip.

### Decisions taken without asking (please review)

* **Plain JavaScript, no TypeScript and no npm.** The repository's rule so far has been "the C++
  uses only the standard library, the tools only Python's" - a toolchain a student can build with
  what is already installed. An extension written in TypeScript would have brought `npm install`,
  a `node_modules/` tree and a compile step into a repository that has none of that. The cost is
  no static types in the editor's own code; the tests cover the wiring that types would have
  caught (every command, every provider, every task, the whole debug protocol).
* **The debugger replays a recording rather than driving the VM.** `thundervm` has no breakpoint
  support and no stop-and-wait: the IDE's debugger already works from `--trace-json`, and the
  extension does the same. That means the program runs to its end (or to the 200,000-instruction
  trace limit) before the first breakpoint is shown, `input()` comes from the launch
  configuration instead of the keyboard, and expressions are not evaluated - but **stepping
  backwards** comes free, which is the thing a student most wants when a value is wrong. The
  alternative - a debug server inside the VM - would be a far larger change to the C++.
* **Hovers do not invent prose.** The compiler ships a description for every built-in in both
  languages, so those are shown as they are; keywords, which have no stored description, show
  their category, their other accepted spellings and **their counterpart in the other language**
  rather than an explanation written here. Pashto prose in this repository is a draft for a native
  speaker to review, and the extension adds none.
* **A new project file format (`thunder.json`).** Nothing in the toolchain had a notion of a
  project; a folder of `.th` files is still enough, and every key in the file is optional. The
  compiler does not read it - only the extension does - so it cannot drift from the language.
* **No canvas in VS Code.** Graphics programs open their own window through a display plug-in
  (`thunder.display`) or save an SVG. Re-creating the IDE's Canvas panel would mean a webview
  speaking the frame-record protocol; that is a separate piece of work, and the display plug-ins
  already exist.
* **The extension's version follows the toolchain's** (0.2.0), and a test asserts it: the two are
  released together and a mismatch would be a lie about which compiler it was built against.
* **The generated assets are committed.** They could be generated at install time, but then the
  extension would not highlight anything until the toolchain had been built. They are committed
  with a `--check` test instead, the same arrangement as the golden outputs.

### Notes

* The extension finds the toolchain in the same order as the Qt IDE (setting, `THUNDER_BIN`, the
  build directories of each workspace folder **and of the folders above it**, then `PATH`), so a
  project folder inside a clone of this repository works with no configuration at all.
* Unsaved buffers are compiled from a scratch copy with the same file name and
  `-I <the file's real directory>`, which is how the Qt IDE keeps `import "lib/x"` working while
  a file is being edited. Diagnostics are mapped back by file name.
* Columns are converted carefully: `thunderc` counts **code points** (the lexer advances one at a
  time), VS Code counts **UTF-16 units**. They agree for every character a Thunder program
  normally holds, Pashto included, but not for an emoji inside a string - so the conversion is
  measured against the real line rather than assumed.
* While a line is being typed the file usually does not parse, so completion and signature help
  fall back on the last model that compiled: the declarations in it are still the program's
  declarations.

---

## 28. Fifteenth iteration (2026-11-13): Windows fixes for the extension, a right-to-left view, a sample project

**Transcript summary.** Four items after the extension's first drop: (1) a test failure on
Windows (`issues/windows_test_failures.txt`); (2) the install script worked on Linux but not on
Windows - the user's manual `vsce package` + `code --install-extension` worked instead, with the
question "will this work for development mode and autoupdate?"; (3) right-to-left text direction
does not work in VS Code for Pashto sources - make it automatic when a Pashto file is in view;
(4) add a multi-file sample project under `samples/` without exposing it in the Thunder IDE.

| # | Issue | Implemented as |
| --- | --- | --- |
| 1 | 7 JavaScript tests failed on Windows (C++ 254/254 and the Python suite were clean) | **Cause:** the tests built their made-up absolute paths with `path.join(path.sep, "work", "game")`. On Linux that is `/work/game`, and `path.resolve` leaves it alone; on Windows it is `\work\game`, which names *the current drive's* root, so the moment the code under test resolved it the value became `C:\work\game` and the comparisons failed. `path.isAbsolute` says `true` for it, which is why nothing caught it earlier. **Fix:** one helper, `harness.absolute(...parts)` = `path.resolve(path.sep, ...)`, used by every fixture (`project`, `adapter`, `trace`, `toolchain`). A new `harness.test.js` pins the trap itself: `path.win32.resolve("C:\\project", "\\work\\game")` is `C:\work\game`, not `\work\game`. The fix was verified by re-running the pure path logic on Linux under Windows semantics (a throwaway loader that swaps `path` for `path.win32` with a drive-rooted cwd), which reproduced the failures before and passes after. |
| 2 | The installer failed on Windows | The mechanism was the weak part: copying a directory into `~/.vscode/extensions` *does* work (VS Code scans the folder and writes its own `extensions.json` index from it - checked here with `code --extensions-dir`), but it is not the supported route and it fails silently when anything goes wrong. Both installers now **package the extension and let VS Code install it**, the way the user's workaround did, but without needing Node.js or `vsce`: **`ide/vscode/tools/make_vsix.py`** writes the Open Packaging Conventions archive (`extension.vsixmanifest`, `[Content_Types].xml`, `extension/...`) with the Python standard library alone, and the script then runs `code --install-extension <file>.vsix --force` and **checks that `code --list-extensions` really lists it** rather than assuming. `--link`/`-Link` (symlink, or a junction on Windows - no administrator rights needed) is for development; `--copy`/`-Copy` is the fallback when `code` is not on the PATH. `install.ps1` was also made defensive about the things that break PowerShell scripts: `$PSNativeCommandUseErrorActionPreference = $false` so a tool's exit code does not abort it, no `cmd /c rmdir`, junctions removed as links rather than followed, and the execution-policy invocation documented. |
| 2b | "Will this work for development mode and autoupdate?" | **No to both, and the README now says so.** A `.vsix` installed from a file is recorded as `"source": "vsix"`, `"pinned": true` - VS Code only updates extensions that came from a marketplace, so it stays exactly as installed; run the installer again (it repackages and `--force` replaces it) after changing the sources. For *developing* the extension, press **F5 in `ide/vscode`** (an Extension Development Host loads it straight from the sources, with breakpoints in the extension's own code) or install it with `-Link`, where the installed extension *is* the folder and a reload picks up every edit. |
| 3 | Right-to-left Pashto sources | **An extension cannot make VS Code's editor right-to-left, and this is now established rather than assumed.** Reading the installed VS Code (1.144): its line renderer writes `dir="rtl"` only when `getTextDirection(line)` - the sum of the `textDirection` field of the *model decorations* on that line - is 1, and when a line contains right-to-left text without such a decoration it writes an explicit `dir="ltr"`, overriding any inherited direction. The field is settable only on a model decoration, and the extension host's `DecorationRenderOptions.from()` copies a fixed list of properties (`backgroundColor`, `border`, `textDecoration`, ...) that does not include it; there is no editor option for it (the whole option registry was listed) and no API proposal for it either. So the hook is in VS Code's core, unused and unreachable. **What was built instead:** the extension knows which language each file is in and (a) says so in the status bar (**پښتو** with a tooltip, or **English**), and (b) opens a **right-to-left view** beside the editor - automatically when a Pashto program becomes the active editor (`thunder.rightToLeft.autoOpen`, on by default, never taking focus), or on `Thunder: Show the Program Right to Left`. The view is a webview, where the direction is ours to set: it lays the program out with **the Qt IDE's own algorithm**, ported to `src/direction.js`, colours it from the compiler's token stream, follows the file being edited, marks the caret's line and moves the caret when a line is clicked. |
| 4 | A multi-file sample project | **`samples/project/`**: `thunder.json` (entry point, language, module paths, arguments), `main.th`, `lib/geometry.th` (a struct and functions) and `lib/statistics.th` (a module that imports its sibling), plus `.vscode/tasks.json` and `launch.json`, a `README.md` that lists what to try, and **`samples/project/pashto/`** - the same program in Pashto, for the right-to-left view. The IDE's sample list is `samples/<language>/*.th` and the golden tests iterate `samples/en` and `samples/ps`, so the project is invisible to both, as asked. `ide/vscode/.vscode/launch.json` now opens it when the extension is started with F5. |

### How the port of the direction algorithm was made trustworthy

`src/direction.js` is a second implementation of `ide/thunder_ide/core/direction.py`, and two
implementations of one algorithm drift apart unless something compares them:

* A Python test lays **every line of every Pashto and English sample** (3,365 lines, plus a dozen
  awkward hand-written ones) out with both and compares the display string, the logical-to-display
  offset table and the per-token direction flags. It found two real bugs in the port at once:
  JavaScript regular expressions know scripts and categories but not **bidi classes**, and the
  first version treated the Arabic comma `،` (a separator) and `×` (a neutral symbol) as strong
  letters, which reorders a line.
* The fix was to write out the R/AL and AN character ranges exactly, derived from Python's
  `unicodedata`, and to define strong left-to-right as "a letter that is not one of those".
  A second test re-derives the classification for every character in the ASCII, Latin-1, Arabic,
  Hebrew and Arabic-presentation blocks and compares it with `unicodedata`.
* Worth knowing, from that exercise: the Pashto digits `۰-۹` (U+06F0) are **EN**, not AN - a line
  of Pashto digits alone is read left to right - while the Arabic-Indic `٠-٩` (U+0660) are AN;
  and `٫`, the Arabic decimal separator, is AN.

### Decisions taken without asking (please review)

* **The right-to-left view is read-only.** Making the *editor* right-to-left is not possible (see
  item 3), so the view shows the program rather than editing it. Editing continues in the editor
  beside it, where Pashto letters still shape and join correctly and every other feature -
  completion, hovers, diagnostics, debugging - works in Pashto as in English. A decoration-based
  trick was considered and rejected: `textDecoration` is substituted into the generated CSS
  verbatim, so `textDecoration: "none; unicode-bidi: isolate; direction: rtl"` *would* smuggle
  CSS into a token's span - but a span can only reorder its own contents, and the line's own
  direction (the thing that puts the first token at the right) stays with the line element, which
  VS Code pins to `ltr`. Shipping a trick that cannot be verified here and only half works would
  have been worse than shipping nothing. A **custom text editor**
  (`registerCustomEditorProvider`) *would* give an extension full control of the rendering and
  let it write edits back - but it would mean re-implementing a code editor in a webview (caret,
  selection, keyboard, input methods, undo, folding, completion UI), and Monaco itself, the one
  editor that could be embedded there, has exactly the same limitation. The result would be
  worse than today's editor at everything except direction.
* **The view opens by itself** for a Pashto program, because that is what was asked ("RTL is
  automatically applied when a Pashto source file is in view"). It never takes the keyboard focus,
  and `thunder.rightToLeft.autoOpen: false` turns it off.
* **The extension's version is now 0.2.1** while the toolchain stays 0.2.0: an installer fix needs
  no new compiler. The rule the tests enforce became "the same major.minor as the toolchain, and
  the patch may only move forward".
* **`samples/project/` holds two projects** (the English one at the top, the Pashto one in
  `pashto/`) rather than two sibling folders, so that opening `samples/project` in VS Code shows
  both. Each has its own `thunder.json`.
* **`.gitignore` again.** `*.vsix` is ignored (the installers build one on demand), and
  `samples/project/.vscode/` had to be un-ignored like `ide/vscode/.vscode/` - the tasks and
  launch configurations are what the sample project demonstrates.

---

## 29. Sixteenth iteration (2026-11-13): Windows again, a right-to-left editor, revised Pashto names, fonts, Help links

**Transcript summary.** Six items: (1) more Windows test failures; (2) the VS Code extension now
installs on Windows but an error message is printed at the end; (3) the read-only right-to-left
panel is useless for editing - make it useful or remove it; (4) Pashto vocabulary fixes across the
board (لرې → لیرې, ښکته → کښته, mouseX → موږک افقي, mouseY → موږک عمودي, نږدې → نیژدې, واړه → کوچنی,
توان کړه → توان); (5) a Format menu in the Thunder IDE with a choice of fonts, and open-source
Pashto fonts, because the current one is ugly and hard to read; (6) the links at the bottom of the
SDK reference's home page do not work in the IDE's Help viewer.

| # | Item | Implemented as |
| --- | --- | --- |
| 1a | 29 failures of the direction-table test on Windows | **Cause:** Windows runs Python 3.15, whose newer Unicode database gives *unassigned* code points (U+0590, U+05C8-U+05CF ...) a default bidi class of R; the test compared every code point in the blocks, assigned or not. **Fix:** unassigned code points are skipped, and when the running Python's Unicode version differs from the one the tables were derived from (now recorded as `UNICODE_VERSION = "16.0.0"` in `direction.js`), only the characters every version knows - those of Unicode 3.2, Pashto's letters among them - are compared. |
| 1b | The packaged `package.json` did not equal the file on disk | **Cause:** git checks the file out with CRLF on Windows and the test compared decoded text, which hides the line endings on one side only. **Fix:** compared as bytes. The package was right all along. |
| 2 | "code.cmd : (node:5368) [DEP0169] DeprecationWarning ... NativeCommandError" after a successful install (and exit code 1, which also failed the Windows install test) | **Cause:** Windows PowerShell 5.1 turns every line a native command writes to standard error into an error record when the stream is redirected, and with `$ErrorActionPreference = 'Stop'` the first one ends the script - here a harmless Node.js warning printed by VS Code's command line. **Fix:** every tool now runs through `Invoke-Tool`, which relaxes the preference for the call, turns the output back into text and leaves the verdict to the exit code; and `NODE_NO_WARNINGS=1` switches the warning off at the source. |
| 3 | The read-only right-to-left panel | **Replaced by a right-to-left editor.** A VS Code *custom text editor* (`src/rtleditor.js`) backed by the same `TextDocument` as the text editor, so Save, the dirty marker, Edit > Undo, git and a text editor beside it all see one text; every change from the editor becomes the *smallest* `WorkspaceEdit`. The editing surface is a `contenteditable` in a webview (`media/rtleditor/`) that the browser is never allowed to change by itself: every `beforeinput` becomes an edit of the model (`model.js` - editing, indentation, undo, colouring, completion; pure, tested in Node), the changed lines are drawn again and the caret is put back. Lines are laid out by the Qt IDE's own algorithm - `src/direction.js` is loaded into the page - with each token an isolated span (`dir="auto"`, or `ltr` for a number so its sign stays on the left), and no direction marks are ever added to the text. It does Enter with indentation (one level more after `که`/`دنده`/..., one less after `راګرځه`/...), Tab/Shift+Tab, Backspace by levels, cut/copy/paste, undo/redo (a burst of typing is one step), colours (multi-word built-ins included, Arabic-keyboard letters folded), completion (the program's names, then the language's words) and the compiler's problems (marked lines, a clickable list). Pashto is drawn in Vazirmatn, shipped with the extension and limited to the Arabic script by `unicode-range`, so it never competes with the editor font for a character. A Pashto program opens in it by itself (`thunder.rightToLeft.editor`); a title-bar button switches to the text editor and the choice is kept for the session. |
| 3b | How the editor is verified | Besides the Node tests, **the page runs in a real browser engine**: `tests/python/rtleditor_browser.py` loads it in Chromium through Qt WebEngine (headless) and plays 14 scenarios - typing, Enter, Backspace, Tab, undo/redo, completion, a change from another editor, the compiler's problems, and *where the tokens land on the screen*: the keyword of a Pashto line is rightmost, a Latin name inside it is not reversed, a negative number's sign is left of its digits, the line numbers stand at the right. It found a real bug before anyone saw it: `direction: ltr` on the line numbers made `inset-inline-start` resolve to the left. A separate test caught a second one no fake would have: the page loads `src/direction.js`, but the webview's `localResourceRoots` allowed only `media/` - VS Code would have refused the script and the editor would not have started. |
| 4 | Pashto vocabulary | Applied with a script that treats only Arabic-script letters as joining, so `واړه` inside `دواړه` ("both", 11 times in the documentation) was left alone and a sample's variable `ښکته_قطار` was renamed with the word. The user typed the words with Arabic-keyboard letters (`ك`, `ي`, `ى`); they are written with the Pashto letters the rest of the language uses (`ک`, `ی`) - the compiler folds the keyboard forms, so either spelling is understood. **The earlier names stay accepted** (`لرې کړه`, `ښکته کړه`, `نږدې کړه`, `واړه توري کړه`, `توان کړه`, `د موږک افقي ځای`, `د موږک عمودي ځای`, and the key name `ښکته`), as `دروغ` was kept when it became `درواغ`. Changed: the language pack, 15 Pashto samples and module, 4 golden transcripts, the English and Pashto references, the IDE's interface text, and - regenerated from the compiler - the VS Code grammar, snippets and language data. A new C++ test pins every new name, every old one, and the translator writing the new ones. |
| 4b | `توان` collided with a sample | Sample 09 (recursion) defines its own power function, which in Pashto was called `توان`; with `pow` now `توان` it became a redefinition of a built-in (S004). The sample's function is now `د_توان_حساب`. |
| 5 | Format menu and Pashto fonts | **The cause of the ugliness:** the editor's font was DejaVu Sans Mono, which has Arabic glyphs of its own - monospaced Arabic, cramped and hard to read - so Qt never looked for a better font. Five SIL-OFL fonts with every Pashto letter, the Pashto digits and the Arabic punctuation now ship in `ide/thunder_ide/fonts/` (Vazirmatn - the default, Noto Naskh Arabic, Noto Sans Arabic, Scheherazade New, Lateef), each with its licence, plus the code font JetBrains Mono, which has no Arabic. The editor and the code panels draw in a **pair** - code font first, Pashto font second - and **Format > Code font** offers only monospaced fonts *without* Arabic glyphs, so the Pashto font is always the one that draws Pashto. **Format > Pashto font** lists the shipped fonts and any installed font with Arabic; each entry is drawn in its own font. Larger/Smaller font moved from View to Format, with **Default fonts and size**. Choices are saved in the settings and replaced by the defaults on a machine that lacks them. |
| 5b | A Qt trap found on the way | Qt caches the font it falls back to *per primary family*: after `[JetBrains Mono, Vazirmatn]` was used once, `[JetBrains Mono, Scheherazade New]` still drew Pashto in Vazirmatn, so changing the Pashto font would have done nothing. Re-registering the application fonts makes Qt drop that cache; the window does it whenever the Pashto font changes, and a test checks, by asking Qt which font each glyph run was drawn with, that a change of Pashto font really changes the font. |
| 6 | Help viewer links | **Cause:** the viewer matched a link by its *file name*. The index's "how to install and use it" link goes to `../ide/vscode/README.md` - called `README.md`, like the index itself - so the click reopened the page the reader was on; `../devlog.md` matched nothing and also landed on the index. **Fix:** links are resolved against the directory of the document they are in and compared by full path (`core/docs.resolve_link`); a markdown file outside the reference opens in the viewer itself and joins its contents list; a translation's link to an untranslated document falls back to the English original. A test follows every link in every reference document, in both languages; it found nothing broken beyond these two. |

### Decisions taken without asking (please review)

* **The right-to-left editor rather than removal**, because the request behind the panel - write
  Pashto as it is read - is the one that keeps coming back, and a real editor *can* be built and
  tested here. What it does not have, VS Code's text editor still has on the same document:
  hovers, go to definition, rename, the outline's navigation, inlay hints.
* **It replaces the text editor's tab** for a Pashto program instead of opening beside it: two
  tabs for one file on every opening would be noise. One click (or the setting) goes back.
* **Its own undo history**, not VS Code's: the editor redraws lines after every key, which leaves
  the browser's history meaningless, and routing Ctrl+Z to VS Code's Undo from inside a webview is
  not dependable. Edit > Undo in VS Code's menu still undoes the document, and the editor follows.
* **Spelling with Pashto letters** where the request used Arabic-keyboard ones (`ک` for `ك`,
  `ی` for `ي`/`ى`). The words are recognised either way.
* **Pashto font: Vazirmatn** as the default, in the IDE and the extension. It was designed for
  screens; the naskh fonts are there for whoever prefers the look of print. The fonts add 2.2 MB
  to the repository.
* **Larger/Smaller font moved to the Format menu**, next to the fonts they size.
* **The extension's version is 0.2.2.**

## 30. Seventeenth iteration (2026-11-14): '#' comments, a right-aligned lone number, Ctrl+F5, test fragility

**Transcript summary.** Six items: (1) in Pashto mode a number printed on its own (`کرښه چاپ(۴۲)`)
came out left-aligned in the output panel; (2) change the comment token from the C-style `//` to
the Python-like `#`, which reads better with Pashto; (3) keep the codebase stable, make the tests
follow FIRST and remove their fragility, addressing the one failure in the last Windows run
(`issues/windows_test_failures.txt`); (4) re-check four earlier findings (a stale `build/release`,
`: void` in signatures, a webview race, `.vsix` packaging parity); (5) on Linux both F5 and
Ctrl+F5 started a Thunder program under the debugger; (6) `samples/ps/18_maps.th` failed to
compile with P010 at `لیرې کړه`.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | A lone number was left-aligned in the Pashto output panel | `line_direction_mark` (qt/panels.py) started *every* line without a strong letter with a left-to-right mark, so a digits-only line hugged the left edge. The mark exists to keep several numbers on one line in print order; a line that is **one number on its own** has no order to keep, so it now gets no mark and follows the panel - on the right in Pashto, its digits still reading left to right. Lines of several numbers keep the mark and their order. |
| 2 | `//` → `#` | The lexer's line comment is now `#`; `/* */` block comments stay. `//` is reported as L002 with the hint *did you mean '#'?* in both languages. The language directive is `# lang: ps` / `# ژبه: ps` - written by `--translate`, read everywhere; a `//` directive from an old file is still *recognised* (language_pack.cpp, util.js, controller.py), so its errors (and the hint) come in the declared language. Changed together: the IDE's highlighter, comment/uncomment (Ctrl+/), auto-indent, sample titles and the direction algorithm in both implementations (direction.py and direction.js, compared line-for-line by a test); the extension's grammar, language-configuration, snippets and language data (regenerated by generate_assets.py); the rtleditor's colouring; all 98 sample/template/SDK `.th` files; the docs in both languages; and the e1 samples, whose demonstration of an unexpected character was `#` itself (now `$`). Golden transcripts regenerated (only e1's changed - comments never reach the output). Versions: the language changed, so the toolchain is **0.3.0** and the extension follows. |
| 3 | The Windows failure, and FIRST | The one real failure was `rtleditor.test.js` waiting a fixed 20 ms for the webview's `init` message - but answering `loaded` spawns `thunderc --json language-info`, which on a busy machine takes longer than any fixed sleep. The harness now has `waitFor(condition, description)` (poll every 5 ms, fail after 5 s), and every fixed sleep in the extension's tests waits for the thing itself: the init message, the edit landing, the session counting it. The diagnostics "off" test asserts `manager.timers.size === 0` instead of sleeping and hoping - deterministic where a wait cannot prove a negative. The IDE's `test_stop_and_busy` polls for `run.running` instead of `time.sleep(0.2)`. |
| 4 | The four earlier findings | **(a)** `build/release` was *again* built from sources that were never committed (this time a binary that already spoke `#`, left behind by an abandoned session); it is rebuilt from the checked-out sources, and the committed assets are regenerated from it (`generate_assets.py --check` pins this in the tests). The lesson stands: `build/` is untracked, so after pulling, rebuild before trusting it. **(b)** `: void` stays omitted (report.js `signatureOf`). **(c)** The webview race fix holds; the new message-driven waits in the tests would catch a regression rather than hide it. **(d)** `*.vsix` stays ignored and `.vscodeignore` matches the packager. |
| 5 | F5 and Ctrl+F5 both debugged | Nothing honoured `noDebug`, the flag VS Code sets for *Run Without Debugging*. `resolveDebugConfiguration` now starts the **run task** (compile + run in the terminal, where the program can read real input) and cancels the debug session when `noDebug` is set. So F5 debugs - VS Code's own convention, and what the extension's New Project message promises - and **Ctrl+F5 runs without the debugger**. |
| 6 | `18_maps.th` P010 at `لیرې کړه` | Not a bug in the sources: the user's `build/release/bin/thunderc` predated the vocabulary revision (iteration 29, `لرې` → `لیرې`), so the new spelling was not a multi-word built-in token in that binary - the same stale-build trap as 4(a). The sample compiles and runs with a current build (verified). After pulling, run `run_tests.ps1` (or build `build/release`) so the binaries match the sources. |

### Decisions taken without asking (please review)

* **Block comments stay.** The request named the `//` token; `/* */` still works and the
  reference still lists it.
* **`//` is an error with a hint**, not silently two divisions: `a // b` says *did you mean '#'?*
* **A `//` language directive is still recognised** (never written), so pre-change files fail
  with messages in their own language rather than in English.
* **F5 keeps debugging.** The request said F5 should run without debugging, but F5 = debug,
  Ctrl+F5 = run is VS Code's own binding, the extension's documentation and its New Project
  message. What was broken is that Ctrl+F5 *also* debugged; that is fixed. Swapping the keys
  would make Thunder behave unlike every other language in the editor - if that is still wanted,
  it is one `keybindings.json` entry away.
* **Toolchain version 0.3.0** for a language-visible change; the extension matches.
* **Several numbers on one line keep their left-to-right order** (and the mark that enforces
  it); only a number *alone* follows the panel's direction, which is what the report asked.

## 31. Eighteenth iteration (2026-11-14): ''' block comments, the SDK's data structures and algorithms, the task shell on Windows

**Transcript summary.** Three items: (1) change the block comment from the C-style `/* */` to
the Python-style `''' '''`; (2) add "datastructures" and "algorithms" libraries to the SDK,
with clean APIs for the structures and algorithms modern languages ship; (3) on Windows the
VS Code extension could not build Thunder programs - Ctrl+F5 printed ``bash: syntax error near
unexpected token `{'`` - and, as a note, F5 debugging worked for English programs but not
Pashto ones.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | `/* */` → `''' '''` | The lexer's block comment is `''' ... '''` (any quotes shorter than three stay inside it); an unterminated one is still L007, now phrased around `'''`. `/*` is reported as L002 with the hint *did you mean '''?*, like `//` before it. Changed together: the IDE highlighter and both direction implementations, the generated grammar, language-configuration (blockComment, auto-closing pair) and the docs in both languages. No sample ever used a block comment, so no program changed. |
| 2 | The SDK libraries | **`lib/thunder/<en¦ps>/datastructures.th`**: a stack, a queue (front index, amortised O(1), served space reclaimed), a singly linked list and a binary search tree (both in the classic parallel-array "cursor" representation, because Thunder structs cannot contain themselves - documented in the modules as exactly how languages without pointers build them), and a min-heap priority queue. **`lib/thunder/<en¦ps>/algorithms.th`**: bubble, selection, insertion, quick (Hoare partition) and merge sort, linear and binary search, `is_sorted`, `index_of_smallest` and a Fisher-Yates `shuffle`. Nothing is native: both modules are ordinary Thunder written to be read, with a short essay per structure; the elements are `int` and the modules say to copy-and-retype for other element types (Thunder has no generics). The Pashto modules are real translations - Pashto names (`ډېرۍ`, `لیکه`, `ځنځیر`, `ونه`, `لومړیتوب_لیکه`; `چټک_ترتیب`, `نیمایي_لټون`, ...), Pashto comments - not transliterations. Documented in `docs/(ps/)datastructures.md` and `docs/(ps/)algorithms.md`, linked from both reference indexes. **`tests/cpp/test_sdk.cpp`** (10 cases): behaviour of every structure including BST removal's three shapes, every sort over empty/one/duplicates/sorted/reversed, search edges, a seeded shuffle, the same scenarios through the Pashto names, and an en/ps parity check (same number of functions and structs in both files). |
| 3 | `bash: syntax error near unexpected token '{'` | `commandLine` keyed off `process.platform` and assumed *PowerShell on Windows* - but a task runs in the user's default profile, here Git Bash, where `if ($?) { ... }` is a syntax error. The shell is now read, not assumed: the automation profile if set, else `vscode.env.shell`, mapped to posix / powershell / cmd by the executable's name (both path separators handled). POSIX shells and cmd chain with `&&`; Windows PowerShell keeps `if ($?)` (5.1 has no `&&`). Quoting is per shell too, and an argument containing a backslash is now always quoted - unquoted, Git Bash would eat the backslashes of every Windows path. Pinned by new tests (bash.exe on win32 → POSIX line, no `$?`, quoted paths). |
| 3b | F5 debug "doesn't work" for Pashto programs on Windows | No code path differs by language: a new end-to-end test debugs a Pashto program through the real toolchain (directive read, `--lang ps`, stop on entry, step, `۴۲` printed) and passes. The reported machine ran a `build/release` older than the samples' revised vocabulary (the same stale-build trap as iterations 29/30 - it also explains the earlier `لیرې کړه` P010); rebuilding after pulling fixes it. If it persists on a current build, the failing compiler line from the debug console is the thing to send. |

### Decisions taken without asking (please review)

* **`'''` does not nest** (neither did `/* */`), and one or two quotes inside a block comment
  are ordinary text; `'` alone outside one is still an unexpected character.
* **The SDK modules' element type is `int`**, the textbook choice; each file's header says to
  copy the structure and change the type, which is the honest option in a language that has no
  generics - and turns the limitation into a reading exercise.
* **API naming follows the sockets module**: prefixed functions (`stack_push`, `tree_insert`),
  `new_<structure>()` constructors, misuse of an empty structure is a runtime error.
* **cmd also chains with `&&`** rather than getting its own `if` dance; Windows PowerShell
  keeps `if ($?)` because 5.1 has no `&&`.

## 32. Nineteenth iteration (2026-11-14): Pashto escapes, recursive structs and `nothing`, generics, the advanced samples

**Transcript summary.** Four items: (1) Pashto spellings for the control-character escapes
(`\ن` for `\n`, `\ر` for `\r`, ...); (2) recursive structs, until now a hard error (S030);
(3) generics, "simple, clean and elegant", because int-only data structures are not very
useful; (4) samples for the data structures and algorithms in an `advanced` folder, out of the
IDE's picker, referenced from the documentation instead.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Pashto escapes | `\ن` = `\n`, `\ت` = `\t`, `\ر` = `\r`, and a zero of any script = `\0`; both spellings work in any program, like the digits. `--translate` writes each language's own letters (`\n` ↔ `\ن`), round-trip pinned by a test. The Pashto samples that print line breaks now use them. Documented in both language references. |
| 2 | Recursive structs and `nothing` | A struct may now contain itself; the piece that had been missing is an empty reference, the new literal **`nothing`** (Pashto **`هېڅ`**). It takes the struct type of its context (annotation, field, parameter, the other side of `==`) - the same expected-type channel the empty array literal uses; without one it is S056. The default of a field whose type leads back to a struct being built is `nothing` (the code generator carries a visiting set), every other default is unchanged. In the VM it is a null struct reference pushed by `NEW_STRUCT 0xFFFF` (the `no_struct` sentinel, modelled on `no_function`); reading or writing a field of it is the new runtime error R033. Structs now compare with `==`/`!=` by identity (needed for `x == nothing`, and honest in itself). Printing, `inspect` and `copy()` handle `nothing`, and cycles of references too: display marks the repeat with `...`, `copy()` is memoised so shared and cyclic parts keep their shape. **The leak question:** reference counting alone leaks a cycle (`a.next = a`) - and the test suite runs under LeakSanitizer, so this was not theoretical. Every cycle passes through a struct (arrays, tuples and maps cannot contain their own types), so the VM keeps a weak registry of every struct the program makes and clears the survivors' fields on shutdown; a test builds a cycle and the sanitizer confirms the free. |
| 3 | Generics | **Templates with deduction, written only at declarations.** `struct Stack[T]` / `func first[T](values: T[]): T`; a use with concrete types (`Stack[int]`, a call) makes or finds an *instance* - a resolved struct, or a cloned-and-recompiled function named `first[int]` (visible as such in traces and the debugger). Deduction is unification of the written parameter types against the argument types, plus one deliberate extra: the type a result is assigned to closes what the arguments left open (`var s: Stack[int] = new_stack()`), which is also how `Stack(items: [])` works. Nothing is ever written at a call site - there is no turbofish. Implementation: the parser takes `[T, U]` after declaration names and `Name[type, ...]` in types; sema keeps templates out of the normal tables, resolves types under a bindings map, instantiates structs by re-resolving fields (the cache is seeded first, so `next: Stack[T]` may be recursive) and functions by cloning the AST (new `clone_stmt`/`clone_expr` in ast.cpp) and queueing the body on a work-list the analyzer drains (instances may instantiate more; a depth guard stops `Stack[Stack[...]]` runaways). Instance bodies are checked with their concrete types, so a template that compares with `<` works for numbers and strings and reports anything else at the instantiation. New diagnostics: S057 (wrong number of type arguments) and S058 (cannot deduce; silent when an argument already failed). Arguments are checked once - deduction reuses their types, and context-needing literals (`nothing`, `[]`, `{}`) sit out the deduction and are checked against the instance's field. |
| 3b | The SDK rewritten generically | `datastructures` and `algorithms` (iteration 31) are now generic end to end - `Stack[T]`, `Queue[T]`, `Heap[T]`, every sort and search over `T[]` - and the linked list and the binary search tree are built from **real recursive node structs** (`ListNode[T]`, `TreeNode[T]`) ended by `nothing`, replacing the parallel-array workaround the day-old docs had to apologise for. Both languages, docs updated, all `test_sdk` scenarios re-verified over int, string and float elements; the en/ps parity check now counts declarations (templates compile to nothing until used). |
| 4 | Advanced samples | `samples/advanced/en/01_datastructures.th` and `02_algorithms.th` plus full Pashto twins. The IDE's picker lists `samples/<language>/` only, so they stay out of it (pinned by inspection: `list_samples("en")` does not see them); the documentation's two module references link them under "Try it". The golden machinery now covers them: `sample_files()` includes `advanced/en` and `advanced/ps`, transcripts live in `tests/golden/advanced/<language>/`, and every hygiene rule (indentation, no semicolons, Pashto-only Pashto, en↔ps counterparts) applies to them unchanged. |

Versions: the language grew (`nothing`, generics, the escapes), so the toolchain is **0.4.0**
and the extension follows. The grammar, snippets and language data were regenerated (new
keyword `nothing`/`هېڅ`).

### Decisions taken without asking (please review)

* **`nothing` is for structs only.** `var x: int = nothing` is S056: every other type has a
  real default, and an optional-int would be a different (bigger) feature.
* **Struct `==` is identity.** It existed at the value level all along (maps use it); the
  surface language needed it for `x == nothing`, and field-by-field equality would have been a
  lie next to the reference semantics.
* **Cycles are legal, printed with `...`, and freed by the VM's shutdown sweep** rather than
  forbidden, counted, or collected by a tracing GC - the smallest mechanism that keeps the
  sanitizers' guarantee ("no leaks, ever") true.
* **Deduction over annotation.** Type arguments cannot be written at call sites at all; the one
  place they are written is a type (`Stack[int]`). This keeps calls identical to non-generic
  calls and sidesteps the `f[int](x)`-versus-indexing ambiguity entirely.
* **Monomorphisation over erasure**, because instances with concrete types match the teaching
  pipeline: the Bytecode tab shows `first[int]` compiled exactly like a hand-written function.
* **`[T]` brackets, not `<T>`** - `<` is already less-than, and Thunder's types (`int[]`,
  `{string: int}`) have always used brackets and braces.

## 33. Twentieth iteration (2026-11-15): merge-conflict repair, <T> generics, sets

**Transcript summary.** Three items: (1) the push to the remote had left merge conflicts - scan
everything and repair; (2) change the generic notation from `[T]` to the more common `<T>`;
(3) add sets, ordered and unordered - and say what else is missing.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Merge conflicts | `git grep` over the tracked tree found conflict markers in exactly two files: `lib/thunder/ps/datastructures.th` and `lib/thunder/ps/algorithms.th`, each pitting the generic rewrite (HEAD) against the older int-only text. Resolved by keeping HEAD throughout; nothing else in the tree carried markers, and the suites pass again. |
| 2 | `[T]` → `<T>` | Declarations are `struct Stack<T>` / `func first<T>(...)`, types are `Stack<int>`, `Stack<Stack<int>>`. Two lexical seams needed care: **(a)** `Stack<Stack<int>>` ends in one `>>` token and `Box<int>= 1` in one `>=` (maximal munch), so the parser's `expect_closing_angle` carves the first `>` off and leaves the rest as the current token; **(b)** a line ending in an operator continues onto the next line, which would have swallowed the block after `struct Stack<T>` - a trailing `>` or `>>` no longer continues a line (a trailing `<`, `+`, `and`, ... still does). Instance names print as `Stack<int>` everywhere (values, traces, the debugger). Converted across the SDK, the advanced samples, the docs in both languages and the tests; S057's wording says <angle brackets> now. In a type a name is never compared, so `<` after a type name is always type arguments - no ambiguity returns the other way. |
| 2b | A use-after-free the change surfaced | ASan stopped `check_return` reading a freed `Type`: references into `SemanticModel::functions` (a `std::vector`) were held across expression checking, and a generic instantiation *appends* to that vector mid-check - any reallocation left the reference dangling. Latent since generics landed; the regenerated goldens' larger programs finally made the vector grow at the wrong moment. `functions` is now a `std::deque`, whose growth never moves elements, with a comment saying exactly why. |
| 3 | Sets | Two, as asked, both in `datastructures`: **`Set<T>`** - unique elements on top of Thunder's own map (the value is the key), O(1) add/remove/membership, values in first-added order, plus `set_union`, `set_intersection` and `set_difference`; its elements are map keys (int, bool, string). **`OrderedSet<T>`** - a binary search tree underneath, O(log n) operations, values always sorted, with `ordered_set_smallest`/`largest`. Pashto: `ټولګه` and `ترتیب_شوې_ټولګه`, fully translated. Documented in both references (indexes updated), demonstrated at the end of the advanced samples (goldens regenerated), and covered by new `test_sdk` cases in both languages. |

Versions: the written form of the language changed, so the toolchain is **0.5.0** and the
extension follows; the grammar, snippets and language data were regenerated.

### Decisions taken without asking (please review)

* **A trailing `>`/`>>` no longer continues a line.** The cost: `if a >` followed by an
  indented operand now needs parentheses; the win: generic declarations and types can end a
  line, which they always do. Pinned by a test either way.
* **`Set` iterates in insertion order** (that is what the underlying map does) rather than
  pretending to be unordered; the documentation says so, and `OrderedSet` is the sorted one.
* **Set operations only on `Set`** (union/intersection/difference); `OrderedSet` stays lean -
  convert through `ordered_set_values` when needed.
* **What is still missing**, for a future iteration: a deque (double-ended queue), a
  doubly-linked list, an explicit graph with traversals (BFS/DFS/shortest path), a trie, and
  string algorithms (substring search); none blocked today's work.

## 34. Twenty-first iteration (2026-11-15): the extension suite report, and the rest of the shelf

**Transcript summary.** Two items: (1) the VS Code extension test suite was reported failing
(from the `oak` working copy); (2) add the remaining proposed data structures and algorithms,
plus useful string algorithms such as substring search.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | The failing extension suite | Not reproducible in this working copy at the current commit: the JS suite passes 232/232, the Python wrapper (the exact failing test, `JavaScriptSuiteTests`) passes, and every consistency the suite checks holds here - CMakeLists and package.json agree on 0.5.0, `generate_assets.py --check` is clean, the toolchain prints 0.5.0. The report came from the `oak` working copy, which this session cannot read; the two causes that have produced exactly this failure before are a half-resolved merge (the version-agreement test fails when CMakeLists and package.json resolve to different versions, and this repository's last merge left conflict markers in two SDK files) and a stale `build/release`. If it still fails there after a pull and a rebuild, the lines above the Python traceback (the `ok`/`FAIL` list the JS runner prints) name the failing test - that is the piece to send. |
| 2 | The rest of the shelf | **`datastructures` gains four structures.** `Deque<T>` - both ends open, two stacks back to back, O(1) amortised either end. `DoublyLinkedList<T>` - recursive nodes with `previous` and `next`, both ends O(1), walkable backwards. `Graph<T>` - an adjacency map (`{T: T[]}`); directed edges, `graph_connect` for undirected, breadth-first and depth-first searches, `graph_shortest_path` by BFS with a breadcrumb map, `graph_has_path`; the searches use the module's own `Queue` and `Set`. `Trie` - a prefix tree of single-character map keys (a recursive struct through a map), O(word length) lookup and prefix tests, alphabetical `trie_words`; it holds strings, so it is the one structure that is not generic. **`algorithms` gains a text section:** `find_text` (the naive substring slide - what the built-in `index_of` does for strings, spelled out), `find_all_text` (all starts, overlapping included), `is_palindrome`, `longest_common_prefix` and `edit_distance` (Levenshtein, one dynamic-programming row at a time). All of it exists twice - the Pashto modules are full translations (`دوسرې_لیکه`, `دوه_اړخیز_ځنځیر`, `ګراف`, `مختاړې_ونه`; `د_متن_لټون`, `د_بدلون_واټن`, ...) - and is documented in both references, demonstrated at the end of the advanced samples (goldens regenerated), and covered by six new `test_sdk` cases including a Pashto one. |

### Decisions taken without asking (please review)

* **Graph edges are directed** with `graph_connect` as the undirected spelling, rather than an
  undirected-only graph: the direction case is the general one, and the tests pin that
  `graph_add_edge(a, d)` gives `d` no way back.
* **The deque is two stacks**, not a ring buffer: fewer indices to reason about, the same
  amortised O(1), and the refill is three lines a student can step through.
* **`trie_words` returns alphabetical order** (sorted on the way out) rather than insertion
  order - the order a dictionary reader expects.
* **No KMP**: `find_text` stays the naive slide, honestly labelled O(n·m); the failure-function
  machinery would bury the idea the sample teaches. It can join later next to it if wanted.
* **Versions stay at 0.5.0**: the modules are additive library content; nothing about the
  language, the tools or the extension changed shape.

## 35. Twenty-second iteration (2026-11-15): a live debugger, debugging in the right-to-left editor, line attribution

**Transcript summary.** Three complaints about debugging in VS Code: (1) F5 on a Pashto program
shows the yellow arrow for a moment in the text editor, then the right-to-left editor takes over
and nothing moves while F5/F10 still work; (2) in English the pointer starts in the middle of the
program and jumps back up while stepping; (3) if the recording-based debugger is the cause,
replace it with a proper one and make the extension work like other languages' debuggers. Plus a
clean-up pass.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Why Pashto showed nothing | VS Code draws no debug decorations inside a custom (webview) editor, and once the right-to-left editor is the active editor for the document VS Code's "reveal the stack frame" opens nothing else - so the program stepped invisibly. The editor now draws the debugger's view itself: the stopped line (an arrow pointing into the code, left in a right-to-left program), the frame chosen in the Call Stack, a greyed marker for a line from the history, and the breakpoints, with a click on a line number adding or removing one (`vscode.debug.addBreakpoints`). The adapter sends `thunder/position`, `thunder/continued` and `thunder/input` custom events; `extension.js` routes them. F5 from the right-to-left editor also finds its program now (`Services.activeThunderUri` looks at the active tab, since a custom editor is not a `TextEditor`). |
| 2 | Why the pointer jumped | Two compiler faults, visible in any debugger over the trace. The default values of the globals were emitted with each declaration's span, so step 0 sat on the last `var` of the program; they now carry the first statement's position (`CodeGenerator::first_statement_span`). The jump that closes a `while`/`for` body carried the position *after* the body - the next line - so a breakpoint there fired every iteration and stepping visited it; it now carries the loop's own line (the step, or the condition), and the jump that skips an `else` carries the last line of the then branch (`end_of`). The Qt IDE's replay debugger benefits equally. |
| 3 | A live debugger | `thunder::Debugger` (`include/thunder/debugger.hpp`, `src/core/debugger.cpp`), a `VmObserver` that stops the machine and answers a protocol of JSON lines over stdin/stdout: `thundervm --debug [--history n]`. Breakpoints by file index with conditions (`n == 2`, `point.x > 3`, a bare bool) and hit counts (`3`, `>= 3`, `% 3`), continue/next/stepIn/stepOut/pause, every frame's locals, globals, the operand stack, the instruction, values opened by path (`items[1].name`, `scores["ada"]`), `evaluate` for variables/paths/comparisons, `input()` as an `input` event answered by an `input` command, a runtime error reported as an `exception` stop before `exited`. Stepping back is kept: a bounded history of executed lines (call stack + variables as text) that `stepBack`/`reverseContinue` walk, with forward steps rejoining the live state. `StreamChannel` reads stdin on a thread so `pause` arrives mid-run; `ExecutionAborted` lets an observer end the program (`ExecResult::aborted`). A small JSON reader joined `json.hpp`. Documented in `docs/debugging.md` (linked from both reference indexes and the README). |
| 4 | The extension over it | `src/debug/debuggee.js` (pipes → promises and events, holding early lines until the adapter listens), `src/debug/adapter.js` rewritten as a DAP↔machine translator (variablesReference table per stop, restart re-places breakpoints, a replaced machine's late events are ignored, hovers stay quiet), `src/debug/launch.js` compiles and spawns. `trace.js` and its tests are gone. Launch attributes: `history`, `display`, `maxSteps` in; `traceLimit`, `input`, `stopAtRuntimeError` out; setting `thunder.debug.history` replaces `thunder.trace.limit`. Extension 0.5.1, CHANGELOG, README, `.vscodeignore` unchanged. |
| 5 | Tests | C++: `tests/cpp/test_debugger.cpp` (21 cases over a scripted channel: entry, breakpoints and their movement, conditions, hit counts, stepping, history forwards and back, bounded history, pause, terminate, a vanished client, runtime errors, input, malformed requests) and JSON reader cases. JS: `adapter.test.js` over a scripted machine, `launch.test.js` end to end against the real `thundervm --debug` (English, Pashto, a runtime error, a module), right-to-left editor provider tests for breakpoints and the marker, two Chromium scenarios for the gutter click and the markers. Python: `test_cli` drives `--debug` over pipes and checks `--debug --canvas` is refused. |
| 6 | Clean-up | Shared `sameFile`/`readFileOrEmpty` in `util.js` instead of three copies; the never-used `resultJson` run option removed; one exit listener for the test suites' temporary directories (Node's MaxListeners warning is gone); `thundervm`'s runtime-error line is not printed twice in debug mode. |

### Decisions taken without asking (please review)

* **The Qt IDE keeps the replay debugger.** Its step-by-step view is built on the trace and shows
  the stack machine as a teaching tool; `--trace-json` stays, the IDE is untouched, and the two
  share the compiler's corrected line tables. Moving the IDE onto the live protocol is a
  separate piece of work.
* **Expressions are variables, paths and comparisons**, not arithmetic or calls: the virtual
  machine has no evaluator for text at run time, and compiling snippets against a running frame
  would be a project of its own.
* **Stepping back is history, not re-execution**: values of an earlier line are text and cannot
  be expanded until the live state is reached again; `--history 0` turns it off. Output is never
  replayed.
* **`--debug` with `--canvas` is a usage error** (both want standard output); graphics programs
  are debugged with `--display`.
* **The session always begins stopped** at the entry inside the machine; the adapter hides that
  stop when `stopOnEntry` is false, and a breakpoint placed on the first line still fires.


## 36. Twenty-third iteration (2026-11-15): the stepping flicker, the Pashto debugging reference, complexity

**Transcript summary.** Four items after the live debugger landed: (1) stepping through a
Pashto program in the right-to-left editor flashed the text editor on every step; (2) a Pashto
version of the debugging reference, in the Pashto Help menu, and why it was missed; (3) the
pre-existing complexity hotspots - refactor, measure, improve; (4) the clean-up items the
previous report had promised but the committed code did not yet have.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | The flicker | VS Code opens the editor for a stack frame's `source` at every stop and, since the right-to-left editor has `priority: option`, the resolver picks the text editor (the override it sets makes the custom editor's `matches` fail); `followEditor` then swapped it back. The adapter now asks the extension (`services.hidesSource`) whether the right-to-left editor shows the frame's file and, if so, sends the frame **without `source`** - VS Code opens nothing - while a `thunderFile` field keeps the path for the extension's own Call Stack handling. Tests in `adapter.test.js` and `rtleditor.test.js` (`showsFile`). |
| 2 | Pashto reference | `docs/ps/debugging.md`, a full translation in the project's vocabulary (ډیبګر، د درېدو ټکي، ګام په ګام، لارښوونې). `debugging.md` joins `REFERENCE_ORDER`, so the IDE's viewer lists it in both languages; a Help menu entry (`menu.debugging_reference`, English and Pashto) opens it; `test_controller` checks it. The Pashto index links it directly. **Why it was missed:** the previous iteration treated the document as tooling documentation for front-end authors and stopped at an English version with a note in the Pashto index - a judgement call that did not match the project's rule that every reference exists in both languages and in the Help menu. |
| 3 | Complexity | Measured with `tools/quality.py` (decision points per function). Before: C++: 580 functions, average 6.94, max 73 (check_builtin); 25 functions above the summary's threshold of 25. After: C++: 681 functions, average 5.87, max 25 (execute_arithmetic); **none above 25**. Done by splitting dispatchers into groups and moving case bodies into named helpers, never by changing behaviour: `fixed_signature` is a table; `check_builtin`/`check_generic_builtin` dispatch to per-group checkers; `check_expr`, `check_binary` (operands, then an operator table) and `check_call` have one helper per kind; `execute` keeps control flow and delegates data, arithmetic and aggregate opcodes to three group handlers; the library built-ins share `string_arg`/`int_arg`/`array_arg` and one helper per built-in; `graphics_builtin` splits into drawing and events; `deserialize` reads its four parts through four readers; the lexer's symbol table, digit scanner and escape table are their own functions; `describe` has leaf/collection and declaration/control halves; `format_value` has one formatter per reference kind. Python: `print_summary` 36 → three section printers; `bitwise.visualise` 25 → 14. |
| 4 | The promised clean-ups | `sameFile` (case-insensitive on Windows) replaces 12 `path.resolve(a) === path.resolve(b)` copies across the extension; 18 exports nothing imported were dropped (the code stays, used inside its module) and two unused report getters removed; `Function::entry_at` finds line-table entries by binary search; the debugger's `handle` is a table of query handlers plus a set of stepping commands. The shared temporary-directory cleanup was already in place. |

### Decisions taken without asking (please review)

* **Frames without `source` for right-to-left files.** The alternative - a
  `workbench.editorAssociations` entry per Pashto file - would write into the user's settings;
  withholding the source costs only the clickable file link in the Call Stack for those frames,
  and the right-to-left editor follows the chosen frame anyway.
* **The remaining Python functions above 10** (`current_layout` 19, `mutants_of` 18,
  `collect_gcov` 18, ...) were not touched: they were not among the hotspots raised, and each is
  one straight-line configuration or parsing routine.
* **The interpreter loop's dispatch is now two levels** (control flow in `execute`, then three
  group handlers). The cost is one extra switch per non-control instruction; the samples and the
  step-limit tests run as before.


## 37. Twenty-fourth iteration (2026-11-15): a Windows test, output alignment, hovers in the right-to-left editor

**Transcript summary.** (1) A Python test failing on Windows (`issues/windows_test_failures.txt`);
(2) in the Qt IDE's Pashto mode, lines that are only numbers sat on the left of the Output panel
while the rest sat on the right - alignment, not direction; (3) in the VS Code right-to-left
editor no debug information appears while stepping: hovering a variable shows nothing, unlike
the text editor.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | The failing test | `HelpViewerTests.test_markdown_rendering_contents_and_search` counted the reference documents: nine, until the debugging reference joined the IDE's reference order last iteration. The expectation is ten now; the failure was the same on Linux once the full suite ran. |
| 2 | Output alignment | `QtTextPanel` kept `LayoutDirectionAuto` so each paragraph reads in its own direction, but said nothing about alignment, so a paragraph with no strong character (a number on its own) resolved to left-to-right and fell to the left edge. The panel now takes `right_to_left` and sets the default text option's alignment to the interface's side with `AlignAbsolute` (visual, not logical), leaving the direction automatic: Pashto words still read from the right, an English error message from the left, and every line sits against the right edge in a Pashto interface. The main window passes its direction to the output, log, overview and VM-output panels (the hex dump stays left: it is a table). Test `test_text_panel_aligns_every_line_to_the_interface_side`. |
| 3 | Hovers | VS Code evaluates hovers only in its own editor. The webview now watches `mousemove` over `.k-name` spans while an execution position is shown, waits 300 ms, and asks the extension (`hover` message, with `p.x` chains assembled from the neighbouring spans); the provider sends `evaluate` with `context: "hover"` to the active Thunder session through `customRequest` and answers `hoverResult` (`value`, `valueType`); the page shows `name: type = value` in a tooltip by the name, hides it when the pointer leaves or the program runs on, and shows nothing for a word that is not a variable (the adapter already answers hovers quietly). Tests: provider (no session, a Thunder session, a refusal, another debugger's session) and a Chromium scenario. |

### Decisions taken without asking (please review)

* **Hover answers come from the active session's `evaluate`**, not from a cache of the last
  `variables` response, so a field path or a global the Variables view has not expanded yet
  still answers - and the value is always the current one.
* **The hex dump panel keeps left alignment** in Pashto too: offsets and bytes are a table that
  is read left to right.

## 38. Twenty-fifth iteration (2026-11-15): the right-to-left editor gets the text editor's language features

**Transcript summary.** (1) Which features of VS Code's editor the Pashto editor lacks - add them
and report; (2) whether answering debug hovers through `evaluate` rather than a cache is a good
thing, how it compares with the text editor, and what follows from it; (3) IntelliSense in the
Pashto editor offered nine keywords in the corner of the page, instead of the full list at the
caret as in the English editor.

| # | Item | Implemented as |
| --- | --- | --- |
| 3 | Why completion was wrong | The page had a completion list of its own: a static candidate set sent at start-up (the language's keywords and built-ins and the program's names), filtered in the page, capped at 12 - and with no prefix (`Ctrl+Space` on an empty spot) it showed the first entries of that set. It was positioned from the selection's bounding box, which is empty (all zeros) for a collapsed selection at the start of a line or on an empty line, hence the top-left corner. |
| 3 | The fix | The page no longer computes language features. `src/rtlservices.js` (`LanguageBridge`) answers the page's requests by running VS Code's own `vscode.executeCompletionItemProvider` (and the other `vscode.execute…` commands) on the document, so the items are exactly the text editor's: every keyword, built-in and module function, the names in scope, struct fields after `.`, imports - with details, documentation, sort order and snippets (expanded to text, caret at the first tab stop). The page filters them as VS Code does (prefix, then contains, then letters in order; Arabic-keyboard letters folded), opens on `Ctrl+Space`, on a word's first letter and on `.`, and places the list from a measured caret (a zero-width marker when the selection has no box). The provider waits for the page's in-flight edits before asking, so offsets agree. |
| 1 | Feature parity | Through the same bridge: parameter hints, language hovers (when not debugging, or on a word that is not a variable), go to definition (`F12`, `Ctrl+click`; another file opens and is revealed - in its right-to-left editor if it is Pashto), references (`Shift+F12`, a list to choose from), rename (`F2`), format (`Shift+Alt+F`), quick fixes (`Ctrl+.`), go to symbol (`Ctrl+Shift+O`), occurrence highlighting, semantic colours and squiggles for problems - the last three painted with the CSS Custom Highlight API so the text itself (and the direction algorithm's spans) is untouched. In the page: `Ctrl+/` (`model.toggleComment`) and VS Code's find widget (`enableFindWidget`). The old candidate list (`candidatesFor`, the `candidates` message, `model.completions`) is gone. The README has the full table of what each editor offers; the remaining gaps (inlay hints, Outline/breadcrumbs, folding, multiple cursors, find-and-replace) are features of VS Code's text editor itself that a webview cannot be given. |
| 2 | Hovers by `evaluate` | This is exactly what VS Code's text editor does: with `supportsEvaluateForHovers` (which the adapter declares) VS Code sends the adapter a DAP `evaluate` request with `context: "hover"` for the expression under the pointer; it does not read the Variables view. The right-to-left editor sends the same request to the same adapter, so both editors get the same answer. Implications: the value is the current one (or, after Step Back, the remembered one - the adapter answers from the history); a field chain such as `point.x` or a global not yet expanded in the Variables view still answers; a word that is not a variable shows nothing (the adapter answers hovers quietly); arithmetic is not evaluated in either editor (the VM evaluates variables, paths and comparisons only); each hover costs one local request to the VM, which is negligible. |

Tests: `test/rtlservices.test.js` (every feature against a stand-in for the `execute…` commands,
snippet expansion - which caught `Infinity - 1` still being `Infinity` for a `$0` stop - and the
real `CompletionProvider` behind the bridge on a Pashto program: more than 60 items including
`کرښه چاپ`, `که`, `دنده`, `اوږدوالی` and the program's own name), `model.filterCompletions` and
`model.toggleComment` in Node, and Chromium scenarios for the request/insert round trip, the list's
position on an empty line, and `Ctrl+/`.

### Decisions taken without asking (please review)

* **References are a quick-pick list**, not VS Code's peek view: the peek widget lives inside
  the text editor and cannot be shown in a webview.
* **Shortcuts are handled in the page** and do not depend on VS Code keybindings: the
  text-editor commands bound to them (`editor.action.*`) require a focused text editor and do
  nothing while the right-to-left editor has focus, so there is no double action.

## 39. Twenty-sixth iteration (2026-11-15): Pashto types, the output's alignment, a completion leftover, Translate

**Transcript summary.** In the VS Code extension: (1) type names appear in English in the Pashto
editor (`string` instead of `متن`); (2) the Debug Console is aligned left for a Pashto program;
(3) choosing `فایل شته` from the completion list leaves the typed `ف` behind; (4) a Translate
command that works like the Thunder IDE's.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | English type names | The compiler's report was already right (`متن`); the English came from two other places. The language data records each built-in's signature canonically (`file_exists(path: string): bool`), and `LanguageData.signatureOf` only swapped the name: it now also puts the type words and the comma into the language (`localiseTypes`: `فایل شته(path: متن): منطقي`), which reaches completion details, signature help and hovers. And the debugger named value kinds with `value_kind_name`: `Debugger::type_text` now spells them with the program's language pack and describes containers by their contents (`عدد[]`, `(عدد، متن)`, `{متن: عدد}`). Parameter names stay as recorded - there is no translation of them in the language data. C++ test for a Pashto program's types; the English test's expectations follow the container descriptions. |
| 2 | Debug Console alignment | Not something an extension can change: VS Code renders the REPL itself, left to right, and exposes no styling or direction for it. Added the **Thunder Output** view (a webview view in the panel, beside the Debug Console): the inline adapter copies the program's output events to it; it aligns every line to the program's side with physical `text-align` and lets each line take its own direction (`unicode-bidi: plaintext`) - the same rule as the Qt IDE's Output panel - and has an input line that answers `input()` through the session (the input box is used when the view is hidden). It comes forward by itself for right-to-left programs (`thunder.debug.outputView`). Tests: `outputview.test.js`, and a Chromium scenario that measures that a Pashto line, a number on its own and an English error all end at the right edge. |
| 3 | The leftover letter | The page took the start of the word from the bridge, which measured it on the *document*; when the document had not yet received the letter just typed, the start was the caret, so the replacement began after the `ف`. The page now measures the word in its own text, keeps a provider range that starts earlier only when the item's text really covers it, and keeps the list open over a space while an item's name goes on with it (`فایل ش`). Chromium scenario reproducing the lagging answer. |
| 4 | Translate | `thunder.translate` used `activeTextEditor` only, so from the right-to-left editor it reported a missing toolchain. It now takes the program in front (`activeThunderUri`), translates the unsaved text too, replaces the document's text in place like the IDE (undoable; not saved), and moves the file to the right-to-left editor for Pashto or the text editor for English. Title-bar button and `Ctrl+Alt+T` in the right-to-left editor. Tests for both directions and for the no-program message. |

### Decisions taken without asking (please review)

* **Translate replaces the text in place** rather than opening the translation beside the
  original and asking (the extension's previous behaviour): that is what the Thunder IDE does,
  and Undo restores the original.
* **The Debug Console keeps the output too.** Thunder Output is an additional, better-shaped
  copy, so nothing that relied on the console (copying, filtering) is lost.


## 40. Twenty-seventh iteration (2026-11-15): stepping without scrolling, the debugger's remarks in Pashto

**Transcript summary.** In the right-to-left editor, stepping scrolled the page at every stop
(a loop made the scrollbar jump up and down), unlike VS Code's editor; and the Thunder Output
view showed English remarks ("The program waits for a line of input ...", "Program exited with
code 0") for a Pashto program.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Scrolling | `revealExecution` called `scrollIntoView({block: "center"})` unconditionally. It now measures the stopped line's box and scrolls only when the line is outside the viewport - VS Code's rule. Chromium scenario: two stops on visible lines leave `scrollY` at 0, a stop far down brings the line into view. |
| 2 | Remarks | The adapter's own sentences were English literals. `src/debug/messages.js` holds them in English and Pashto (exit codes in Pashto digits, the IDE's wording for "program finished"); the launcher reports the program's language with the prepared session, the adapter picks the table, and the `thunder/input` event carries the input box's title, prompt and placeholder so the extension's input box follows too. The program's own output and runtime errors were already in its language (the VM writes them). Tests: a Pashto session's console text and input request, English unchanged, the fallback for an unknown language. |

### Decisions taken without asking (please review)

* **The view's own title ("Thunder Output", "Program Output") stays as VS Code shows it**: a
  contribution's names follow VS Code's display language (`package.nls`), not the program's,
  and there is no Pashto display language of VS Code to key them to.


## 41. Twenty-eighth iteration (2026-11-15): Windows tests, the first F5, zoom and find in Thunder Output, loose Pashto search

**Transcript summary.** (1) The Windows test run (issues/windows_test_failures.txt) failed two
generated-asset tests; (2) the first F5 on a Thunder file showed the Debug Console instead of the
Thunder Output view, until the file was closed, the view visited and the file reopened; (3)
Ctrl+= / Ctrl+- should zoom the Thunder Output view's text; (4) Ctrl+F should search it when it
has the focus; (5) in Pashto mode, both the editor and the output view should offer a toggle for
loose letter-based search: searching ى must match ى, ي, ۍ, ې and ئ, searching گ every gaf, and
so on.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Windows failures | Not Windows at all: `data/language-info.ps.json` had been edited by hand in the previous iteration (translated signatures, `چاپ(value، ...)`), so it no longer matched `thunderc --language-info` on any platform. The design (iteration 39) records signatures canonically in English and the extension translates them for display (`localiseTypes`), so the file was regenerated from the compiler (`generate_assets.py`), which exactly reverted the hand edit. |
| 2 | The first F5 | VS Code's default `internalConsoleOptions` is `openOnFirstSessionStart`: at the window's first debug session it opens the Debug Console - after `ProgramOutputView.begin` had revealed Thunder Output, covering it; later sessions left the view alone, which is why visiting it once "fixed" it. The launch configuration now resolves with `internalConsoleOptions: "neverOpen"` when the view will take the session (`outputView.takes(program)`: the program's direction and the `thunder.debug.outputView` setting), and only when the user has not set the option themselves. |
| 3 | Zoom | In the view's page (media/output): Ctrl+= / Ctrl++ / numpad + zoom in, Ctrl+- out, Ctrl+0 back, clamped and persisted in the webview's state. The factor multiplies the stylesheet's own font size (`calc(var(--vscode-editor-font-size, 13px) * f)`), not the page root's. |
| 4 | Find in the view | A find bar in the page: Ctrl+F opens it (prefilled from the selection), Enter / Shift+Enter and F3 / Shift+F3 walk the matches, Escape closes, a counter shows "2/6". Matches are painted with the CSS Custom Highlight API. A webview *view* cannot be given VS Code's find widget (`enableFindWidget` exists only for editor webviews), so the bar is the page's own. |
| 5 | Loose search | `media/shared/search.js`, loaded by both pages and tested in Node: letter classes (ی ي ى ۍ ې ئ; ک ك; ګ گ; ا أ إ آ ٱ; ه ۀ ھ ہ ة ە; و ؤ), diacritics and the tatweel ignored, case ignored, with the matches mapped back to the original offsets so highlights span what is really written (وَلي is found by ولي and highlighted whole). The toggle (`ی≈ې`) appears in Pashto mode and is remembered. The right-to-left editor got the same find bar (Ctrl+F, F3, highlights painted with the editor's other decorations and kept across redraws), searching the program's text itself. |

Tests: `test/search.test.js` (the letter classes, offsets across diacritics, case, ordering),
`extension.test.js` (a Pashto launch resolves to `neverOpen`, an English one does not, the
user's own value wins), `outputview.test.js` (`takes()`), and three Chromium scenarios
(`rtleditor_browser.py`): zoom by key, find with the loose toggle in the output view, and the
editor's find bar on every spelling of yeh.

### Decisions taken without asking (please review)

* **The right-to-left editor's Ctrl+F is now its own find bar**, not VS Code's find widget for
  webviews (`enableFindWidget` was turned off): the widget searches the rendered page, cannot
  offer loose letters, and the two would have fought over Ctrl+F. What is lost: nothing visible -
  the bar finds, counts, walks and highlights as the widget did.
* **Loose mode also ignores the Arabic diacritics and the tatweel** (کـور is found by کور),
  beyond the letter classes the request names: a search that minds a fathah would miss decorated
  text for no gain. Exact mode leaves them alone.
* **The loose toggle is only shown in Pashto mode** (the view or editor laid out right to left);
  the search itself still folds nothing unless the toggle is on.
* **The zoom level and the loose toggle are remembered per webview** (VS Code's webview state),
  not as settings: they are view preferences, not project ones.


## 42. Twenty-ninth iteration (2026-11-15): the find bar's hiding, Replace, the Thunder Canvas, the directive switches editors

**Transcript summary.** (1) The Pashto editor's find panel was always visible and its ✕ did
nothing; it should appear on Ctrl+F only, close on ✕ or Escape, and Ctrl+H should offer Replace -
like VS Code's own Find/Replace - in the Thunder Output view too. (2) Graphics programs show
nothing in VS Code; running one should open a canvas window akin to the Thunder IDE's. (3) Typing
the language directive into a new file should switch it to the Pashto editor; creating a new
Pashto file was very hard.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Always visible | A one-line CSS bug: `#find-bar { display: flex }` outweighs the browser's `[hidden] { display: none }` (an id beats the UA rule), so the `hidden` attribute the page toggled had no effect - the bar stood there from the start and ✕/Escape seemed dead. Fixed with `#find-bar[hidden] { display: none }` in both stylesheets; the Chromium scenarios now assert the *computed* display, which is what the eye sees, not the attribute. |
| 1 | Replace | The editor's find bar got VS Code's shape: a chevron unfolds the replace row, `Ctrl+H` opens it unfolded, Enter in the replace box replaces the current match and moves on, **All** replaces every match in one edit (one Undo takes it back). Replacing works with loose letters: find `ي` loosely, replace with `ی`, and every spelling is mended. The Thunder Output view's find stays find-only - its text is the program's output, which cannot be edited. |
| 2 | Graphics | Under `--debug` the VM refused `--canvas` (both own standard output), so a debugged graphics program drew nowhere. The debugger is now itself a `GraphicsHost`: each frame travels as a `"frame"` event carrying the `--canvas` record, and the keys and the mouse come back as `"canvasEvent"` commands (`docs/debugging.md`; `--display` still wins when set). The adapter forwards frames as `thunder/frame`; the extension opens the **Thunder Canvas** (a webview panel beside the editor) at the first frame and paints the shapes with the same meaning as the Qt IDE's QPainter - scaled to fit, centred, text top-left in any script. A canvas the user closes stays closed for that run; the next session opens it afresh. |
| 3 | The directive | Only `onDidChangeActiveTextEditor` looked at the language, so a directive typed into an open file changed nothing until the file was reopened. The extension now watches edits of the first line (the only line that can change the language), and 400ms after the typing quiets it re-reads the language: the status bar follows, and a now-Pashto file moves to the right-to-left editor - unless the user had moved that file back to the text editor (the existing choice is honoured). |

Tests: the ✕/computed-display and Ctrl+H/Replace Chromium scenarios; `test_debugger.cpp` (a
graphics program's frames as events, the canvas answering back, a non-event line ignored);
`adapter.test.js` (frame → `thunder/frame`, `thunder/canvasEvent` → `canvasEvent`, an empty line
not sent); `canvasview.test.js` (the panel opens beside without stealing focus, messages held
until the page is ready, a closed panel not fighting the user); `launch.test.js` (a real graphics
program end to end: frames arrive, the mouse answers it); a Chromium scenario painting a frame
and sampling the pixels; `extension.test.js` (the directive moves a new file to the right-to-left
editor; an edit below the first line is not even looked at).

### Decisions taken without asking (please review)

* **Replace lives in the right-to-left editor only.** The Thunder Output view shows the
  program's output; there is nothing there a replace could edit, so its find bar keeps find
  alone - the visibility fix applies to both.
* **The canvas rides the debug session (F5).** `Ctrl+F5` (Run Without Debugging) stays a
  terminal run, as the tests pin it: the terminal cannot host a canvas, and a window is still
  one setting away (`thunder.display`). F5 is the IDE-like road and now shows everything.
* **Pacing under debug.** The debugger's host says `paced`, so `frame()` keeps the program's
  frame rate while it runs free - as the IDE's canvas does; stepping is unaffected (`frame()`
  only waits when it is called).
* **A closed canvas stays closed for that run** rather than reopening at the next frame: the
  close was the user's answer to this program's pictures. The next F5 opens it again.


## 43. Thirtieth iteration (2026-11-15): stopping a program, the right-to-left editor's menu, Translate's button and line breaks

**Transcript summary.** (1) The Pashto editor offered no way of stopping a program, so an
endless loop hung the IDE. (2) Its context menu had only Cut, Copy and Paste where the English
editor's has a great deal more (a screenshot in issues/). (3) The English editor had no Translate
button, and a translation came back with empty lines between the lines of code.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Stopping | `thunder.stop` existed (it ends the run task and the debug session) but sat in no menu. It is now a title-bar button on both editors while a program runs (`inDebugMode`, or the new `thunder.running` context key set from the run task's process events) and in both context menus; `Shift+F5` stops a terminal run as it stops a debug session. |
| 1 | The hang | A program printing in a tight loop sent one `output` event per line - thousands a second - to the Debug Console and the Thunder Output view; the window froze and Stop could not be clicked. The adapter now gathers output: the first piece after a quiet goes out at once (a prompt must not lag), what follows within 20 ms is joined, and any other event (a stop, an input, the end) flushes first so order is kept. The output view keeps a 5000-line scrollback. The VM itself was never stuck: it polls for a pause between instructions. |
| 2 | The menu | The right-to-left editor is a webview, whose menu VS Code fills from `"webview/context"` contributions. It now has: Go to Definition, Go to References, Go to Symbol, Rename, Format, Quick Fix, Toggle Line Comment, Find, Replace, Select All, Compile, Run, Debug, Stop, Translate, Open in the Text Editor and Command Palette, with VS Code's own Cut/Copy/Paste kept. Each item is a `thunder.rtl.*` command that asks the editor in front to carry it out (`rtleditor.js` `command` → a `command` message → the page runs the same code as its shortcuts). A right click outside the selection moves the caret to the click first, as VS Code's editor does. |
| 3 | Translate | The title-bar button was contributed for the right-to-left editor only (`activeCustomEditorId`); it is now for `resourceLangId == thunder` too. The empty lines: a Windows source has `\r\n`; the translator copied the `\r` with the text between tokens, and `thunderc`'s standard output on Windows turned the `\n` into `\r\n` - `\r\r\n`, which VS Code shows as two line breaks. The translator now drops `\r` from copied text (C++ test with a CRLF source), and the extension gives the translation the document's own line breaks (`withLineEndings`: every raw `\r` goes, then CRLF if the document uses it). |

Tests: `adapter.test.js` (500 lines become one event at once plus a flush, ordered before the
stop); `rtleditor.test.js` (a menu command reaches the page in front; the page declares its menu
context); `extension.test.js` (every menu item is a declared command; the webview menu is
complete and scoped to the editor; Stop and Translate in the title bar); `commands.test.js`
(`withLineEndings`); `test_pashto.cpp` (CRLF source translates to plain line breaks); a Chromium
scenario (Select All, Find and Toggle Line Comment arriving as menu commands).

### What cannot be replicated in the right-to-left editor's menu (please review)

* **Other extensions' items** - Copilot (Add File to Chat, Explain, Review), GitLens (Open
  Changes, File History, Open on Remote), Share, spelling, Git history, Open in GitHub - are
  contributed by those extensions to the *text editor's* menu; a webview's menu only shows what
  its own extension contributes. They remain one click away in the text editor.
* **Peek** (definition/references in an inline widget), **Change All Occurrences** (multi-cursor)
  and the other multi-cursor features are VS Code's text editor's own; Go to References opens
  the list to choose from instead, and Rename covers the "change every occurrence" case.

### Decisions taken without asking

* **Output batching is leading-edge** (first piece at once) rather than purely delayed, so a
  prompt before `input()` appears without lag, and the tests that read output synchronously
  still hold.
* **The scrollback is 5000 lines**, like a terminal's default; the Debug Console keeps its own.


## 44. Thirty-first iteration (2026-11-15): keyword colours in the right-to-left editor, a testing module with Test Explorer support

**Transcript summary.** (1) In Pashto mode only functions were syntax-coloured, not the
keywords - unlike the English editor. (2) Enhancement: a test framework in the SDK, documented
in the IDE's Help with a sample, and VS Code UI to run tests singly and as a suite.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Keyword colours | Not the classification: the host-side word table and `classify` put `k-keyword` on every keyword. The stylesheet painted it with `--vscode-symbolIcon-keywordForeground`, which VS Code's themes define as the plain *foreground* (it is the Outline's icon colour) - so keywords were "coloured" in the text colour, and `--vscode-symbolIcon-booleanForeground` / `constantForeground` did the same to literals and constants. Functions used `symbolIcon.functionForeground`, which is a real colour, hence "only functions". The rules now use variables that carry a colour in every theme (`charts.blue` for keywords, `debugTokenExpression.boolean` for literals, `debugTokenExpression.number` for constants). The Chromium page now defines Dark+'s real variable values, and a scenario checks that keywords, literals and built-ins are not the text colour - which the old stylesheet fails. |
| 2 | The module | `lib/thunder/en/testing.th` and `lib/thunder/ps/testing.th`, ordinary Thunder: `test(name, body)` runs a parameterless function at once and prints `✓ name`, each failed expectation prints `✗ name: message`, `finish_tests()` prints `N passed, M failed` and exits with status 1 on failure. Expectations: `expect`, `expect_true/false`, `expect_equal/not_equal<T>` (by printed form, so containers compare by contents), `expect_near`, `fail`. Program arguments select tests by name (`args()`), which is how a single test is run. No exceptions in Thunder: a runtime error in a test stops the program, documented as such. |
| 2 | Docs and sample | `docs/testing.md` and `docs/ps/testing.md` (API table, transcript, selecting tests, the IDE/VS Code/command line, the names in both languages); README tables in both languages; `REFERENCE_ORDER` lists it in the IDE's Help viewer after the library reference, and the Help menu has a **Testing reference** entry in both interface languages. `samples/en/37_testing.th` / `samples/ps/37_testing.th` with golden transcripts. C++ tests for both modules (every expectation kind, the counts, exit status 1, selection by argument). |
| 2 | VS Code | `src/testing.js`: a `TestController`. A document that imports `"testing"` (any quote style, either language) yields a file item with one child per `test(...)`/`ازموینه(...)`, placed on its line - so the Test Explorer, the gutter buttons and the commands run one test, a file or everything. Running compiles the file (the unsaved text too), runs it with the chosen names as arguments and reads the ✓/✗ transcript (language-neutral marks); a compile error or a runtime error is reported against the tests that got no line; exit status 1 is the module's verdict, not an error. A *Debug* profile starts the debug session with the names as arguments. `thunder.runTests` (title bar on both editors and both context menus, when the file has tests - the `thunder.hasTests` context key) and `thunder.runAllTests`. The fake VS Code gained the Testing API; tests cover discovery in both languages, the transcript, a real run (pass, fail, single test), a compile error and the no-tests message. |

### Decisions taken without asking (please review)

* **`expect_equal` compares the printed forms** (`str`) rather than `==`: Thunder's `==` on
  arrays and structs is what the language defines, but the printed form compares containers by
  their contents and gives the message both sides as the programmer sees them.
* **The module is named `testing`** (`import "testing"`), so that `test` stays free for the
  function that declares a test; the Pashto module keeps the same file name, as the other SDK
  modules do.
* **A runtime error inside a test stops the run** (there is nothing to catch it with); the
  tests after it are reported as "errored" with the VM's report in VS Code, and the reference
  says so.


## 45. Thirty-second iteration (2026-11-15): zoom per panel, Pashto tests run like English ones

**Transcript summary.** (1) The Thunder Output zoom zoomed the whole IDE and seemed to keep
zooming in; wanted: separate zoom for the output panel and the Pashto editor, applying only to
the focused panel, on Ctrl+= / Ctrl+- / Ctrl+0 and Ctrl+mouse wheel. (2) Running tests did not
work on Pashto tests as it does in English: no play button beside each test and no context-menu
commands.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Whole-IDE zoom | The page handled the keys itself, but a webview's keydown also reaches VS Code's keybinding service - and with the focus on the view's container rather than its page, only VS Code saw them - so the *window* zoom fired. The keys are now contributed keybindings, scoped to the focused panel (`focusedView == thunder.programOutput`; for the editor `activeCustomEditorId == thunder.rightToLeftEditor && focusedView == '' && !terminalFocus && !inDebugRepl`), which take precedence over the window zoom and cannot both hold; the commands send a "zoom" message to that panel's page, which sizes its own text (`calc(editor-font-size × 1.1^level)`, level remembered per webview). The pages no longer handle the zoom keys themselves (the keybinding already fired: both acting would double), and Ctrl+wheel - which VS Code does not re-dispatch - is handled in the page, in both panels. Ctrl+= / ctrl+shift+= / numpad and their mac spellings are all bound. |
| 2 | Pashto tests | Three gaps. (a) The gutter play button exists only in VS Code's text editor, so the right-to-left editor got its own: the host finds the file's tests (`discoverTests`) and sends them with the init and after each edit; the page marks those lines and draws a play triangle in the outermost band of the gutter (pointing into the code: left in RTL; an SVG data URI, `img-src data:` added to the page's CSP), and a click there runs exactly that test (`runTestAt`: the test whose range starts on the line) - the rest of the gutter stays the breakpoint's. (b) The context menu's **Run Tests in This File** hid behind `thunder.hasTests`, which was only set when a *text editor* became active: a window restored straight into the right-to-left editor never set it. The editor's init now runs the language status (which sets the key) too. (c) On Windows, `main()`'s argv is in the ANSI codepage: a Pashto test name passed as a program argument arrived mangled, matched no test, and "nothing ran". `cli::arguments()` now reads the wide command line (`CommandLineToArgvW`) and converts to UTF-8; `thundervm` and `thunderc` both use it, which also mends Pashto *file names* on the Windows command line. |

Tests: keybindings scoped and complete (`extension.test.js`); zoom messages from the view and
the editor in front (`outputview.test.js`, `rtleditor.test.js`); the init carries the tests, an
edit updates them, the play button's line runs that one test (`rtleditor.test.js`,
`testing.test.js`); a Pashto file end to end - pass, fail with the Pashto message, a single test
by name (`testing.test.js`); Chromium scenarios for zoom by message and Ctrl+wheel in both pages
(a plain wheel scrolls, never zooms) and for the play button (marked line, SVG painted, the
outer band runs the test, further in stays the breakpoint).

### Decisions taken without asking (please review)

* **The zoom keys moved out of the pages entirely** (keybinding → command → message): with both
  the page and the keybinding acting, one press would zoom twice. The wheel stays in the page -
  VS Code never sees it.
* **The editor's zoom binding yields to the terminal and the debug console**
  (`!terminalFocus && !inDebugRepl`): with the right-to-left editor open but the terminal
  focused, Ctrl+= should keep meaning what VS Code says it means there.
* **The play button sits in the outermost band of the gutter** (before the line number), so the
  number area keeps toggling breakpoints, as it always has.


## 46. Thirty-third iteration (2026-11-16): colours that follow the text, a floating canvas, HTML API documentation, bytes

**Transcript summary.** Four items: (1) syntax highlighting in the Pashto (right-to-left) editor
misbehaves while a file is edited - half a keyword in the wrong colour - and is right again after
closing and reopening the file; (2) can the Thunder Canvas be docked out into a separate window
that can be resized, maximised and minimised - if not, implement it; (3) a professional,
comprehensive HTML5 API documentation for the language and the SDK, JavaDoc-like or better, in
English and Pashto with a link to switch language; (4) does the language have a byte type, what
are the options and limitations, how easy is it to work with raw bytes such as audio files?

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Half-coloured keywords | **Cause:** two layers paint the right-to-left editor. The page's own classifier colours keywords, strings, numbers and built-ins per line as it redraws, and that part was never wrong. On top of it, the *compiler's* semantic colours (functions, parameters, fields, types, constants, multi-word built-ins) are painted with the CSS Custom Highlight API at **absolute offsets** reported for the text as it was when the compiler last ran. Between analyses the user types; every keystroke redraws the lines and repaints those stale offsets, which now land a few characters off - on the neighbouring keyword, which takes a function's or a type's colour for the half the range covers. A late answer made it worse: the host answers `semanticTokens` against the document, which lags the page by the edit in flight, and the compile itself takes time while the user keeps typing. Reopening the file requested fresh colours for the text as it stood, hence "corrected". **Fix:** `model.rebaseRanges(ranges, before, after)` carries ranges over an edit (before it: unchanged; after it: shifted by the edit's size; touched by it: dropped until the next analysis colours the rewritten word). The page runs every text change - typing, undo/redo, an update from another editor - through it (`adoptText`), and `semanticTokens` now answers `{text, ranges}`: the page rebases the answer from the text it was computed for to the text of the moment, and drops an answer a newer request has overtaken. |
| 2 | A canvas window | VS Code has had floating editor windows since 1.85 (`workbench.action.moveEditorToNewWindow`), and a webview panel is an editor, so the canvas could already be dragged out by hand; nothing in the extension made that discoverable or automatic. Now: a **Move the Canvas to Its Own Window** button in the canvas's title bar (and in the Command Palette) brings the canvas forward and asks the workbench to float it - a desktop window the window manager resizes, maximises and minimises, the picture scaling to it; **Bring the Canvas Back into the Main Window** (the same button once it floats, by the `thunder.canvasFloating` context key) closes the floating panel and re-creates one beside the program with the whole picture so far - the host now keeps the shapes exactly as the page does, applying each frame's `keep`; **Thunder: Show the Canvas** reopens a canvas the user closed; and `thunder.canvas.window: "newWindow"` floats every canvas from its first frame. An older VS Code is told why it cannot float and keeps the tab. A re-creation of our own is told apart from the user's close by identity (the disposed panel is no longer the current one), so the run goes on being shown. |
| 3 | API documentation | **`tools/apidoc.py`** (standard library only, like every tool here) writes a static HTML5 site to **`docs/api/`** - committed, like the extension's generated grammar, with `--check` in the test suite so it cannot go stale. Sources, none hand-written twice: the compiler's own language information (`ide/vscode/data/language-info.*.json`, itself checked against `thunderc --language-info`) for the keywords with every spelling and the counterpart in the other language, the types, every built-in with its signature and description and module, and the instruction set; the SDK modules `lib/thunder/{en,ps}/*.th`, parsed for their `struct`s (fields and types), `func`s (type parameters, parameters, return type, native or not) and **their `#` comments as the documentation** - the module's header, each `# ---- Title` banner as a section, the comment above each declaration as its text, JavaDoc-style without inventing a comment syntax; names starting with `_` are internal and left out, with a count; and the reference guides, rendered by a new Markdown renderer (`tools/markdown_html.py`: headings with GitHub-style ids so `language.md#types` links keep working, tables with alignment, fenced code, nested lists, quotes, inline markup, a whitelist of harmless HTML). Every page exists in `en/` and `ps/`, `<html lang dir>` set, with a language switch and an `hreflang` alternate to the same page in the other language; built-ins, structs and functions link to their counterpart in the other language (SDK declarations matched by position, the two modules being translations of each other). Client-side **search** over functions, keywords, types, modules, guides and headings and instructions (loose Pashto letters folded as the compiler folds them), a sidebar, breadcrumbs, "on this page", light and dark themes, responsive, printable. Overview, Language, Built-in functions, SDK modules (one page per module), Guides (all twelve), Instruction set. Links from guides to each other become page links, a translation's link to the English original reaches the English page, links elsewhere in the repository go to GitHub, images stay in `docs/images`. Eleven SDK functions had no comment above them (the `*_is_empty` family, `expect_true/false/not_equal`, two more) and got one in both languages, so that every public declaration is documented - a test keeps it so. `docs/README.md`, `docs/ps/README.md` and the root README point at `docs/api/index.html`. |
| 4 | Bytes | **No.** See the assessment below; nothing was changed in the language for it. |

Tests: `rtleditor.test.js` (`rebaseRanges`: before, after, across, at either edge, same text, a
multi-word Pashto built-in moving as one); `rtlservices.test.js` (`semanticTokens` answers the
text with the ranges); a Chromium scenario (the compiler's colours stay on their letters after
typing at the start of the file, and a late answer computed for the old text lands right too);
`canvasview.test.js` (the host's picture follows `keep`; pop-out reveals with focus and runs the
workbench command once; dock re-creates the panel beside with the whole picture and the run goes
on; `newWindow` floats at the first frame only; Show reopens a closed canvas; an old VS Code is
told); `extension.test.js` (the new commands registered and every menu item declared, as before);
**`tests/python/test_apidoc.py`** (24 tests: the renderer's constructs and every document
rendering; the parsed declarations, comments, native functions and generics in both languages
and the pairing between them; every public declaration documented; the committed site up to date;
both languages have the same pages; HTML5 with the right `lang`/`dir`; **every internal link and
anchor of the 3,500 in the site leads somewhere**; the language switch and the alternate of every
page lead to the same page in the other language; every built-in, keyword, instruction, SDK
function and struct has its anchor; the search index points at real anchors; a translation's
link to the English original; the generator cleans stale files and `--check` fails on a change).

### Item 4: a byte type - the assessment

* **There is no byte type.** The numeric types are `int` (64-bit signed) and `float`; `string`
  is a sequence of Unicode code points, not bytes; arrays are `T[]` of those. Files are read and
  written as **UTF-8 text** (`read_file`, `write_file`, `append_file`), sockets send and receive
  **text** (`socket_receive(sock, max_bytes)` counts bytes but returns a string), and there is no
  `read_bytes` or `write_bytes`.
* **What can be done today.** A byte is an `int` in `0..255`; the bitwise operators (`& | ^ ~ <<
  >>`), `hex`/`bin`/`format("{x}")` and the IDE's Bits tab make bit work comfortable, and an
  `int[]` holds a buffer (8 bytes of memory per byte, with bounds checks). Characters can be
  turned into code points and back (`char_code`, `from_char_code`), so **text** protocols and
  ASCII-range formats can be handled. Binary files cannot: `read_file` keeps the bytes it reads
  (a string is UTF-8 inside) but **strips every carriage return byte (0x0D)** as a line-ending
  convenience, and every string function sees code points - a byte of 0x80 or more that is not
  part of valid UTF-8 reads as U+FFFD through `char_code` and cannot be reached as a number at
  all. So a WAV header or PCM samples can neither be read nor round-tripped (every 0x0D sample
  byte vanishes), even though `write_file` would write bytes faithfully. A native module (the
  C++ interface, `docs/native.md`) can do the byte work and hand Thunder numbers or text; that
  is the escape hatch, as it is for sockets.
* **What it would take.** The smallest honest design: a `bytes` value type (immutable, like
  `string`; or a `byte[]` array with elements checked to `0..255` and stored as bytes) with
  `read_bytes(path): bytes`, `write_bytes(path, data)`, `bytes_to_string`/`string_to_bytes`
  (UTF-8), indexing to `int`, `len`, `slice`, and little-/big-endian helpers (`read_int16_le`,
  …). Every layer is touched - the language pack in both languages, the type checker, the VM's
  value representation and a new instruction or two, the bytecode constants, the serialiser for
  `--trace-json` (the debugger shows values as text), the IDE's inspectors, the extension's
  hovers and the documentation - a medium iteration of its own. Audio in particular also needs
  arithmetic on 16-bit signed samples, which `int` already has, so once bytes can be read and
  written a WAV reader and writer are ordinary Thunder code and would belong in `lib/thunder`.
* **Recommendation.** If byte work is wanted for teaching (file formats, checksums, encodings,
  audio), add the `bytes` type as above rather than piggy-backing on `string`: a string of code
  points is the wrong abstraction and Pashto text makes the mismatch visible immediately. This
  was not started here - it is a language change, which deserves its own decision.

### Decisions taken without asking (please review)

* **The semantic ranges are dropped, not stretched, across the edited region.** A word being
  rewritten is coloured by the page's own classifier until the compiler has seen it; stretching
  the old range over new letters would guess.
* **Dock re-creates the panel** rather than asking the workbench to move it back:
  `workbench.action.restoreEditorsToMainWindow` moves *every* floating editor, and a webview
  panel keeps no position of its own to return to. The host keeps the picture, so nothing is
  lost in the re-creation.
* **The canvas's buttons sit in its own title bar only** (`activeWebviewPanelId ==
  thunder.canvas`) - not on the program's editor - and the Command Palette has all three.
* **`engines.vscode` stays at `^1.75.0`.** Everything else works there; the pop-out tells the
  user it needs 1.85 and otherwise does nothing.
* **The API documentation is committed under `docs/api/`** (1.4 MB) so it opens from a clone and
  GitHub Pages can serve it; it is regenerated by hand and checked by the test suite, like the
  grammar. It needs no compiler to generate: the bundled language information is used, and that
  is already tested against the compiler.
* **Only `#` comments document the SDK** - no new `##`/`/** */` syntax; the modules were written
  to be read, and their comments already are the documentation. The Pashto strings of the site's
  chrome are drafts for a native speaker, as the repository's Pashto prose is.
* **The site's version is the toolchain's** (`project(... VERSION ...)`), not the extension's.


## 47. Thirty-fourth iteration (2026-11-16): `تکرار`, `شین`, the `byte` type, enums, and the testing framework's limits

**Transcript summary.** Five items: (1) replace the Pashto `for` keyword `لپاره` with `تكرار`,
covering `for` and `for ... in`; (2) replace the colour constant `آبي` with `شین` and have `سپین`
(white) and `تور` (black) as well; (3) add the `byte` type proposed last iteration (Pashto
`بايټ`): `byte[]` with `read_bytes`/`write_bytes`, UTF-8 conversions, endian helpers, the file
and sockets APIs extended to binary data, and the bitwise samples showing it; (4) add an `enum`
type (Pashto `گڼنه`) with conversion to and from `int` and `string`, a range, printing, and
replace the language's and SDK's constants with enums, the colour constants first; (5) does the
testing framework support sections or suites, and can it mock files, sockets and the like?

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | `تکرار` | The Pashto pack's `KwFor` spelling is `تکرار` (the user's `تكرار` with the Arabic kaf is accepted through the existing letter folding). `for ... in` is the same keyword followed by `په`, so both forms changed at once. Every Pashto program in the repository followed: the samples (75 statements), the SDK modules (41), the project template, the VS Code snippets, the Chromium scenarios' block words, the IDE's "for scope" label, the documentation and the tests. Only keyword uses were renamed: `لپاره` is also the ordinary Pashto word "for", which stays in printed strings and prose. |
| 2 | `شین` | `graphics.cpp` names blue `شین` (`آبي` is gone); `سپین` and `تور` were already colour names - the documentation listed them, and the request confirmed they belong - and now they are also members of the Color enum (item 4). The `rgb` description, which said "red, green and blue" as "سور، شین او آبي", now says "سور، زرغون او شین". |
| 3 | `byte` | **The type.** `byte` (`بایټ`): one byte, 0..255. It widens to `int` (and on to `float`) wherever an `int` is wanted - the same mechanism as `int` → `float`, a second flag on the expression and one instruction, `BYTE_TO_INT` - so `b + 1`, `b & 0x0F`, `b == 200`, `data[i] * 2` and `hex(b)` all work on the value. Nothing narrows by itself; `to_byte(n)` keeps the low 8 bits (`INT_TO_BYTE`), and one convenience: **a whole-number literal from 0 to 255 written where a byte is wanted is that byte** (`var b: byte = 200`, `data[i] = 0`, `[72, 105]` for a `byte[]`, `push(data, 7)`), so buffers are written naturally while `var b: byte = 256` and `data[0] = n` stay type errors (`S005`). `byte[]` is an ordinary array of bytes: every array built-in, `for ... in`, `array(n, to_byte(0))`, `sort`, `sum` (an `int`), map keys. **Built-ins:** `to_byte`, `string_to_bytes`/`bytes_to_string` (UTF-8; what is not UTF-8 becomes U+FFFD, so the result is a well-formed string), `bytes_to_int(data, start, count, little_endian, signed)` and `int_to_bytes(value, count, little_endian)` for 1-8 byte numbers in either order, signed or not (two's complement) - two functions cover WAV, BMP and network headers and 16-bit audio samples; `read_bytes`, `write_bytes`, `append_bytes` in `os` (`read_bytes` keeps every byte, where `read_file` strips carriage returns). **Sockets:** `socket_send_bytes`, `socket_receive_bytes`, `udp_send_bytes`, `udp_receive_bytes`, in both languages, sharing the text implementations - the bytes travel as they are. **Native interface:** `THUNDER_BYTES` (6) carries a `byte[]` across as a pointer and a length, both ways; `thunder_bytes(data, length)` builds one; the sema maps `byte[]` to it and the VM copies results. **Elsewhere:** `to_int(b)`, `str`, `format("{x}", b)`, the debugger's type names, map-key encoding, the IDE's inspectors (type names come from the compiler). Sample 36 (bitwise) ends with a bytes section in both languages: a packet, masks on bytes, UTF-8, a WAV sample-rate field, a signed 16-bit sample, an XOR checksum. |
| 4 | `enum` | **The type.** `enum Name` followed by indented members, one per line or comma-separated, top level only (`ګڼنه`). A member is `Name.member`, a value of the new type with a position; members print by name (`println(Direction.north)` → `north`) and as `Direction.north` inside arrays, maps and the debugger; `to_int(m)` is the position, `str(m)` the name, `Name(n)` and `Name("name")` go the other way at run time (`R034`, `R035`), `Name.values` is every member in order and `Name.count` how many (Pashto `ارزښتونه`/`شمېر`, the English words accepted too). Members compare with `==`/`!=` and order with `<` etc. by position, `sort` orders them, they are map keys, and a variable without an initialiser holds the first member. Compile-time checks: `S059` not at top level, `S060` empty, `S061` duplicate member, `S062` no such member (listing the members), `S063` made from something other than an `int` or a `string`, `S028` a name already taken. **Runtime:** `Value::Kind::Enum` (layout + ordinal), an `enums` table in the program and the `.thc` file (format version 7), three instructions `ENUM_CONST`, `ENUM_FROM`, `ENUM_VALUES`, all verified. **Built-in enums** replace the string constants: **`Color`** (`import graphics`; `Colour` accepted) with red, green, blue, white, black, yellow, orange, pink, gray/grey, brown, purple, sky, gold - every drawing function takes a `Color` where it takes a colour string (and only there), and the Pashto members are the colour names the strings used (`رنګونه.شین`); **`Platform`** (`import os`): `platform()` now returns `Platform.linux` etc. rather than a string, and sample 23 and the tests follow. Built-in enums come with their module, under the language pack's names (`--translate` converts `Color.red` ↔ `رنګونه.سور`), and `--language-info` lists them under `enums` for the editors and the API documentation, which gained a "Built-in enums" section. Samples 24 (drawing, both languages) use `Color` members; a new sample **38_enums** (both languages) shows declaration, positions, names, `values`/`count`, turning right with wrap-around, sorting, map keys, `filter` and the two built-in enums. **Other candidates considered:** the key names of `key_pressed()`/`key_down()` ("left", "a", "7") are open-ended - a letter is a key - so they stay strings; nothing else in the language or the SDK was a closed set of named constants. |
| 5 | Testing | Assessment below; nothing changed. |

**Also:** the toolchain is **0.6.0** (new types, new bytecode format) and the extension with it;
the VS Code grammar, snippets and bundled language information were regenerated; the API
documentation was regenerated (the `byte` type and `enum` keyword come from the language
information, the bytes functions from the built-in table, the enums from the new `enums` array,
the four socket functions from their comments); the IDE's Symbols tree shows enums and their
members; the golden transcripts of samples 23, 36 and the new 38 were regenerated and checked.

Tests: **`test_bytes.cpp`** (8 cases: widening in arithmetic, comparisons and bit operators,
`to_byte`, literal narrowing and the errors for 256 and for an `int` variable, every array
built-in and map keys, UTF-8 both ways including a malformed byte, numbers in both byte orders
signed and unsigned with the WAV header example, the `R022`/`R012` errors, a file round trip
that keeps the carriage return `read_file` would drop, the native kinds, the Pashto spellings);
**`test_enums.cpp`** (5 cases: printing, conversions, comparison, defaults, loops, sorting, map
keys; every diagnostic; the built-in enums, `Colour`/`grey`, a `Color` accepted only where a
colour is wanted, `platform()`; Pashto enums and colour names; translation both ways; the
bytecode round trip, verifier and listing); the opcode count, the os-module test and the sample
tests updated. C++ 325/325; the extension's 294 JavaScript tests; the Python CLI, API
documentation, docs, IDE and asset tests pass against the fresh build.

### Item 5: the testing framework - sections, suites and mocks

* **Sections or suites: no.** `import "testing"` gives a flat list: `test(name, body)` runs a
  function at once and prints `✓ name` or `✗ name: message`; `finish_tests()` prints the counts
  and sets the exit status. The only grouping is by *file* (VS Code's Test Explorer shows one
  node per file with a child per `test(...)`) and the only selection is by *name* (the program's
  arguments). A convention works today - `test("stack: push then pop", ...)` - and the Explorer
  shows the names in order, but nothing nests, nothing shares set-up, and a failure inside one
  test cannot be isolated from the next: a runtime error stops the program.
* **Mocking: not as a feature, but the language allows the technique.** Thunder has no
  interfaces, no dynamic dispatch and no way to swap a built-in, so `read_file`, `tcp_connect`
  or `input` cannot be replaced from a test. What it has is **function values**: a unit written
  as `func load(path: string, reader: func(string): string)` can be given `read_file` by the
  program and a hand-written fake by the test, and `map`/`filter` show the pattern already. For
  files the practical route is a real temporary file (`write_file`/`write_bytes` then the code
  under test, then `remove_file`); for sockets a real listener on port 0 (`tcp_listen("", 0)`,
  `socket_port`) in the same program - the sockets sample does this. Neither is a mock: they are
  the real thing, made small.
* **What it would take, in order of value.** (a) `suite(name, body)` - a function that prints a
  heading, prefixes the names of the tests run inside it (`suite > test`), and reports per-suite
  counts; the Test Explorer would gain a middle level from the same transcript. Cheap - a
  variable in `testing.th` and a regex in `src/testing.js`. (b) `before_each(f)`/`after_each(f)`
  inside a suite, for shared set-up, equally cheap. (c) An `expect_error(body)` that reports a
  runtime error inside `body` as a failure and goes on needs something Thunder does not have: a
  way to catch a runtime error. A `try(f): bool` built-in that runs a function value and returns
  whether it failed (the VM already unwinds to a frame for `map`'s callbacks) would make it, and
  the error-stops-the-run limitation above, go away; that is a VM change of a day. (d) Mocking
  the built-ins themselves (`read_file`, `input`, sockets) would need either dependency
  injection in the program (possible now, see above) or a VM-level override table - a bigger
  design decision, and arguably against the grain of a teaching language where "what you call is
  what runs". Recommendation: (a) and (b) now, (c) next, (d) by injection only.

### Decisions taken without asking (please review)

* **`لپاره` is gone, not kept as an alternate spelling.** The request said replace; keeping it as
  a keyword would also have kept it from being a name. Old Pashto programs need the one-word
  change (the translator writes the new word).
* **A byte widens; nothing narrows except a literal.** The same rule as `int` → `float`, chosen so
  that `b & 0x0F` and `sum(data)` just work while an accidental `data[i] = n` is still caught.
  The literal exception is what makes `[72, 105, 255]` a `byte[]`; `to_byte` is the explicit road.
* **`byte[]` is an array of byte values**, not a compact buffer type. Every array built-in works
  on it for free and the type system stays small; the cost is memory (one `Value` per byte),
  which is fine for the files a lesson reads. A packed representation can come later behind the
  same interface.
* **Two endian helpers rather than a family.** `bytes_to_int(data, start, count, little_endian,
  signed)` and `int_to_bytes(value, count, little_endian)` read like what they do and cover
  every width from 1 to 8; `read_int16_le` and friends would have been a dozen names.
* **`platform()` returns the `Platform` enum**, which breaks `platform() == "linux"` (`S006`).
  That is the point of replacing constants with enums; the sample, the tests and the
  documentation moved with it, and `str(platform())` is the old string.
* **A `Color` is accepted only as the last argument of a graphics built-in** - every colour
  parameter is last - so `text(x, y, Color.white, "hi")` is still the type error it should be.
* **Enum members print bare at the top level and qualified inside containers**, so
  `println(d)` reads naturally and `[Direction.north]` cannot be mistaken for `["north"]`.
* **Built-in enums arrive with their module** (`Color` with `graphics`, `Platform` with `os`),
  so a program that never imports them can use the names for itself; the Pashto fireworks sample
  had a list called `رنګونه` and was renamed.
* **Bytecode format 7** rejects older `.thc` files, as every format change has.


## 48. Thirty-fifth iteration (2026-11-16): the Windows failures, the editor's cut and paste, gaf folding everywhere, samples in teaching order

**Transcript summary.** (1) The last iteration introduced test failures (a Windows log in
`issues/`); (2) cut and paste in the VS Code Pashto editor corrupt the view - extra indentation,
vanishing line numbers - though the saved file is right; (3) `رنگونه` with the Persian gaf should
mean `رنګونه`, handled across the board like the Arabic kaf; (4) re-order the samples - core
language first, then SDK material, renumbered, docs updated, a for-each example added to the
for-loops sample; the error samples stay as they are.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Windows failures | Two of my own making, both in the new samples. **(a)** `27_os` (then 23) and `20_enums` (then 38) printed `platform()` - whose answer is the machine's - so their golden transcripts held "linux" and failed on Windows. They now print only what is true everywhere (`contains(Platform.values, platform())`, the member count), in both languages. **(b)** The Pashto enums sample had a map literal's continuation line aligned under the brace with 15 spaces, failing the four-spaces rule (the English one had 16 by luck); both now continue with one four-space level. Goldens regenerated. |
| 2 | Cut/paste | **Cause:** the page cancels every `beforeinput` and applies the edit to its model, but VS Code's webview context menu runs Cut and Paste through `execCommand`, whose `beforeinput` events are **not cancelable** - `preventDefault()` is silently ignored. The browser then mutated the DOM *in addition to* the model's redraw: the pasted lines arrived a second time with the caret line's indentation, a cut line's `div` (and with it its line number) vanished, and spans were split - while the model, and therefore the saved file, stayed right, which is why closing and reopening "fixed" it. **Fix:** any `input` event outside composition now means an edit slipped past the page, so it rebuilds every line from the model (`replaceChildren`, full redraw) and puts the caret back where the model says. A Chromium scenario plays a non-cancelable cut of a line (removing its div, as the browser would) and a non-cancelable paste (appending a stray, over-indented div and a loose text node) and asserts the model text, one `.line` div per line, no stray nodes and the caret - the DOM can no longer part ways with the file. |
| 3 | Gaf folding | The lexer now **folds every identifier** (ك→ک, گ→ګ, ي/ى→ی), exactly as it folds keywords, so `رنگونه` is `رنګونه`, `رنگونه.سور` is a Color member, and a variable typed on an Arabic or Persian keyboard is the same name as one typed on a Pashto one - declaration and use, structs, enums, functions, natives, everything. That surfaced three places that compared folded names against raw spellings, now fixed: the language packs key their lookup maps on folded spellings (the authored forms stay for display - `اعشاري کړه` still prints with its ي), `is_main_name` folds both sides (Pashto `اصلي` contains ي), the native registry folds registered names (the sockets library's `یو_ډي_پي_...` functions), and the analyser folds enum names and members. The now-redundant `راگرځه` alternate registration is gone, and the language information no longer lists a foldable variant as a second spelling (`ودرېږه`/`ودریږه` remains one, ې being a letter of its own). Documented in both language references; tests pin `رنگ`/`رنګ` as one variable, a گ-spelt function call, and `رنگونه.سور`. |
| 4 | Sample order | The 38 samples now run **language first, SDK second**: 01 hello, 02 arithmetic, 03 variables, 04 decimals, 05 strings, 06 conditionals, 07 while, 08 for (now ending with a first for-each), 09 for-each, 10 functions, 11 recursion, 12 scope, 13 arrays, 14 bubble sort, 15 sieve, 16 matrices, 17 bits and bytes, 18 structs, 19 struct references, 20 enums, 21 tuples, 22 maps, 23 text, 24 lists, 25 modules - then 26 input, 27 os, 28 testing, 29 drawing, 30 bouncing ball, 31 waves, 32 orbits, 33 pendulum, 34 fireworks, 35 life, 36 snake, 37 native, 38 sockets. The error samples keep e1-e5. Both languages, the golden transcripts (and `26_input.input`) renamed with them; the headers renumbered (zero-padded, Pashto digits in Pashto); every reference followed - the graphics, library, display and testing references (the graphics table re-sorted), the READMEs of the display and sockets libraries, the demo module's comment, the Qt/controller/explain tests (including a positional index), the modules sample's pointers in both languages - and a repo-wide scan asserts that every `NN_name` mentioned anywhere names a sample that exists. The README's teaching-order paragraph describes the new path. |

Tests: C++ 325/325 (with the new folding cases), the extension's 294 JavaScript tests, 33
Chromium scenarios (one new, for the uncancelable cut/paste), and the Python CLI (every sample,
golden-checked, in both languages), extension, API-documentation, docs, IDE controller/Qt/explain
suites all pass against the fresh build. The API documentation, grammar, snippets and bundled
language information were regenerated.

### Decisions taken without asking (please review)

* **The view resyncs rather than suppressing the menu.** Hiding VS Code's own Cut/Paste items
  (or rewriting them as `thunder.rtl.*` commands) would fix the menu but not drag-and-drop or any
  other uncancelable path; rebuilding from the model on every `input` event covers them all, and
  it is cheap - the event only fires when something slipped through.
* **Identifier folding is a language rule now**, not an editor convenience: two spellings of one
  letter are one name everywhere, and the translator writes the folded form. A program that
  deliberately used `رنگ` and `رنګ` as two different variables would break - that program was a
  trap, which is the point of the rule.
* **`platform()` samples print no platform.** Golden transcripts must be identical on every
  machine; anything machine-dependent belongs in assertions (`contains(...)`), not output.
* **Sample numbers 31-33 (waves, orbits, pendulum) kept their numbers** - the ordering happened
  to pass through them - which keeps the diff, and any muscle memory, smaller.


## 49. Thirty-sixth iteration (2026-11-17): the clipboard done right, enum members in IntelliSense

**Transcript summary.** Two VS Code extension issues: (1) after the last fix the Pashto editor's
cut-copy-paste stopped doing anything at all - keyboard and context menu both - with the only
visible effect that the selection collapses (backspace still works); (2) typing `Color.` or
`رنگونه.` does not offer the enum's members in the completion list.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | The clipboard | **Why the last fix overshot:** it assumed every clipboard action announces itself through `beforeinput`, and used an `input`-event resync to repair what the uncancelable menu path did to the DOM. On the user's VS Code the menu's `execCommand` path performs the edit *without* a `beforeinput` the page can act on - so the model was never told, the browser's own edit landed in the DOM, and the resync then faithfully rebuilt the view from the unchanged model: the edit visibly undone, the selection gone. **The fix moves to the right layer:** the page now handles the **clipboard events themselves** - `copy` and `cut` put the model's selection on `event.clipboardData` (and `cut` deletes it from the model), `paste` inserts `clipboardData`'s text - and cancels the default, which cancels the browser's whole action on every path: the keyboard shortcuts and the context menu's `execCommand` both dispatch these events, in every Chromium. An empty selection leaves `copy`/`cut` alone (there is nothing to take), an unreadable clipboard leaves `paste` to the `beforeinput` fallback, and the resync stays as the last line of defence for anything else uncancelable (drops, exotic IMEs) - it can only ever reassert the file's true contents. A new Chromium scenario drives all three events with a real `DataTransfer` and asserts the clipboard text, the model, the untouched DOM shape and that an empty-selection cut is left to the browser; the previous scenario (uncancelable `beforeinput` + stray DOM) still passes. |
| 2 | Enum completion | The report model exposes the symbols' `enums` table (`report.js: enums()`), and the completion provider, after a `.`, now recognises an enum name before the dot - compared with the letters folded, so `رنگونه.` typed with a Persian gaf finds `رنګونه` - and returns its members as `EnumMember` items in position order, followed by `values` (`Enum[]`) and `count` (`int`). The words for those two come from the language information, which now carries them (`enum_terms`: English `values`/`count`, Pashto `ارزښتونه`/`شمېر`, emitted by `thunderc --language-info` from the packs' terms), so the extension hardcodes no Pashto. While a member is being typed (`Color.re`), the member list is kept: the after-the-dot pattern now accepts a partial word, which also mends the same gap for struct fields (`r.wi` previously fell back to the full word list, because the member list is marked incomplete and VS Code re-asks at every letter). The right-to-left editor gets all of this for free - its completion asks the same provider. |

Tests: a Chromium clipboard scenario (34 scenarios, all passing); `providers.test.js` gained the
enum-member completion test (an English program's own enum, the built-in Color while `re` is
half-typed, and a Pashto program asking with `رنگونه.` - members, `ارزښتونه` and `شمېر` all
offered) and a partial-word field-completion test; `test_pashto.cpp` pins `enum_terms` in the
language information. The extension's 296 JavaScript tests, the 16 Pashto C++ cases and the
extension's 33 Python tests pass; the bundled language information was regenerated and the
extension is 0.6.2.

### Decisions taken without asking (please review)

* **Copy and cut with an empty selection are left to the browser.** Cancelling them with nothing
  to put on the clipboard would clear it; the default does the harmless right thing.
* **`paste` falls back rather than failing** when `clipboardData` has no `text/plain`: some
  sources offer only files or HTML, and the `beforeinput` path may still see a plain-text form.
* **The member list keeps only the enum's own vocabulary.** After `Color.` you get members,
  `values` and `count` - not the whole word list - because everything else there is a compile
  error waiting to happen.


## 50. Thirty-seventh iteration (2026-11-17): right-to-left Pashto documents, the Help menu opens the HTML documentation

**Transcript summary.** (1) The Pashto code blocks in the Markdown files render left-aligned
and left-to-right; all Pashto documentation, code blocks included, should be right-aligned and
right-to-left. (2) Remove the Markdown viewer from the IDE's Help and plug in the HTML5 API
documentation instead.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | RTL documents | The generated site already lays Pashto pages out right-to-left (`<html dir="rtl">`, code blocks included - checked with a screenshot), so the complaint is about the Markdown as rendered elsewhere: GitHub, the VS Code preview and the IDE's old viewer. Markdown has no direction of its own; the one portable instrument is an HTML wrapper, which GitHub and every Markdown preview honour. Each of the 13 Pashto documents now opens with `<div dir="rtl" align="right">` and closes with `</div>`, with the Markdown between them - headings, tables, lists and fenced code all inherit the direction, so code reads right-to-left as it does in the editors. The site generator's renderer passes a lone wrapper line through unchanged (the pages already carry the direction) and strips the document's title heading even when the wrapper precedes it, so nothing is doubled. |
| 2 | Help = the HTML docs | **Gone:** the Markdown Help viewer (`QTextBrowser` + Qt's Markdown, contents list, keyword search) and the catalogue behind it (`core/docs.py`: `load_reference`, `resolve_link`, `ReferenceDoc`). **In its place:** `core/docs.py` is a page map - every Help command names a page of `docs/api/` in the interface's language (`help_page(topic, language)`, falling back to English for a page a stale checkout lacks and to the home page for an unknown topic) - and `qt/help_viewer.py` is a small browser around the site: a Qt WebEngine view with Back, Forward, Home (the site's front page in the interface's language) and *Open in the browser*, one window reused for every topic, the page's own search box doing the searching. Navigation stays inside the site; a link that leaves it (GitHub, the repository's other files) opens in the system's browser. WebEngine is set up where Qt wants it - imported, with OpenGL context sharing, before the `QApplication` exists (`qt/backend.py`); headless runs (the tests) get Chromium's `--no-sandbox --disable-gpu`; a machine without WebEngine still gets the pages, in its system browser. The Help menu gained three entries for the API pages proper - keywords and types, built-in functions, SDK modules - beside the guides, in both languages. The UI protocol's `show_help` now takes a page; the test double records it. |

Tests: `test_docs.py` (every Help command resolves to an existing page in both languages, the
menu and the page map agree, Pashto pages are `dir="rtl"`, the fallbacks); `test_controller.py`
(each command opens the right English page; the Pashto interface opens the Pashto pages, each
checked for its heading and its direction); `test_qt.py` (the Help window loads the real site
offscreen, reports its page, is reused for the next topic, goes Home, closes); the site's own
tests (`test_apidoc.py`) with the wrappers in place. The IDE's 135 Qt/controller/explain/docs
tests pass.

### Decisions taken without asking (please review)

* **Markdown gets an HTML wrapper, not a dialect.** `<div dir="rtl">` is the only direction
  control GitHub's and VS Code's renderers share; it costs two lines per file and no tooling.
  If GitHub's own stylesheet ever forces `pre` left-to-right, that is GitHub's choice - the site
  and the IDE are unaffected.
* **One Help window.** A second topic opens in the same window (Back returns to the first)
  rather than stacking dialogs, as the old viewer did.
* **No search box of our own.** The site has one, with the same index on every page; a second
  one in the toolbar would have searched the same thing twice.
* **The system browser is the fallback**, not an error, when Qt WebEngine is missing: PySide6's
  standard wheel ships it, but a trimmed installation should still reach the documentation.


## 51. Thirty-eighth iteration (2026-11-17): a two-entry Help menu, the editor's colours and fonts in the documentation

**Transcript summary.** (1) The IDE's Help menu is cluttered - a dozen entries all pointing into
the same HTML site; condense it to two broad categories, the tutorial-like material and the full
API reference. (2) Add syntax highlighting to the HTML documentation in a theme like the IDE's
code editor, use the IDE's Pashto font for Pashto Thunder, and show program outputs the way the
IDE's output panel shows them.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Help menu | Two entries plus About: **Tutorials and guides** (`F1`; the guides index - the language reference, the library, data structures, algorithms, testing, graphics, sockets, the native interface, packaging, displays, bytecode and debugging, in reading order) and **Full API reference** (the site's front page: keywords and types, built-in functions, SDK modules, instruction set). Every other page is one click away inside the site, which has its own search; `core/docs.py`'s page map shrank to the two doors, and the i18n table lost eleven entries in each language. |
| 2 | Highlighting | The site generator now imports the IDE's own highlighter (`thunder_ide.core.highlight`, pure Python - the same regular expressions and the same word classes the editor uses, fed both languages' words at once so a guide may quote the other language) and wraps every `thunder` fence in `tok-*` spans coloured with the editor's palette (`qt/style.py` `SYNTAX_COLORS`, duplicated in the generator and pinned equal by a test; a brighter set of the same hues serves the dark theme). The signatures and import lines on the API pages are highlighted the same way. |
| 2 | Fonts | The IDE's bundled fonts travel with the site: JetBrains Mono for code and Vazirmatn for Pashto (`docs/api/assets/fonts/`, with their OFL licences; the generator now writes and checks binary files). The code font stack is the IDE's trick in CSS: JetBrains Mono has no Arabic glyphs, so Pashto letters in code fall through to Vazirmatn exactly as in the editor; Pashto pages use Vazirmatn for prose as well. |
| 2 | Program output | An untagged fence is a program's output (the documents use tagged fences - `thunder`, `sh`, `json`, `c`, `ebnf` ... - for everything else), and is now rendered as the IDE's Output panel renders output: a plain panel bearing the panel's title ("Output" / "پایله"), the program font, and **each line in the direction of its own first letter** (`dir="auto"`, `unicode-bidi: plaintext`), so Pashto and English lines of one transcript sit on their own sides. |
| - | A Markdown bug | Bold text containing a code span (`**Next to the file that contains the `import`.**`) rendered as literal asterisks, because code spans were split out before emphasis was parsed. Code spans are now lifted into placeholders and put back after the rest is parsed, so emphasis may span them. |

Tests: `test_apidoc.py` (emphasis across a code span; the fenced-code hook; highlighted
keywords, built-ins, comments and strings on the English and Pashto language guides and on the
API pages' signatures; the generator's palette equals the editor's and the CSS carries it;
output blocks with their label in both languages; the fonts present, byte-identical to the IDE's,
with the `@font-face` rules); `test_docs.py`, `test_controller.py` and `test_qt.py` follow the
two-entry menu. All pass against the regenerated site.

### Decisions taken without asking (please review)

* **F1 opens the guides**, where the language reference lives (its old home), not the API front
  page; the API reference is the second entry.
* **The palette is duplicated, not imported**: `qt/style.py` imports Qt, and the generator must
  run without it (the test suite's plain interpreter, CI without a display). A test keeps the
  two tables equal, which is the same guarantee at no cost.
* **"Untagged fence = output"** is a convention the documents already follow; the two fences that
  are diagrams (the pipeline, the parse tree) now sit in an output panel too, which reads fine.
* **Fonts are copied into the site** rather than referenced across the repository, so the site
  stays self-contained for GitHub Pages and the IDE's Help window alike.


## 52. Thirty-ninth iteration (2026-11-17): modules, the modular library, `with`, test suites

**Transcript summary.** Two requests. (1) A `module` keyword (Pashto `ټولګی`/`ټولګى`) for
organising code into cohesive bundles; then modularise everything in the language and the SDK -
every prefix that stood for a module becomes one (`socket_send` -> `socket.send`), the maths
functions go into `math`, the string functions into `string`, list and higher-order functions
stay in the default module; a comprehensive module -> functions table for review; modules also
organise tests into suites; samples and documentation updated. (2) A `with` keyword giving RAII
for file/socket handles, connections and the like; Pashto `سره`, written after the resource
(`ساكټ سره`, never `سره ساكټ`); a sample and documentation.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | `module` | `module name` + an indented block of declarations (func, struct, enum, var, const, native func), top level only (P017), declarations only (S047). Outside, members are qualified (`geometry.area(r)`, `var p: geometry.Point`, `geometry.Shape.circle`, `geometry.count = 3`); inside, the short names work too unless a local hides them. `_name` members are private (S066). Module names are reserved for top-level names, variables and parameters (S065); one declaration per module, and a built-in module's name cannot be reused (S064); `m.nope` is S070; a member written without its module says where it lives (S067: "'sqrt' belongs to the module 'math': write math.sqrt"; also for the program's own modules). **How:** `src/core/modules.cpp` takes a module apart where it is found (every member renamed to its qualified name, `geometry.area`, and added to the program's items), then a qualifier pass writes every reference out in full before any type is checked - `m.x` becomes the name `m.x`, and inside a module an unqualified own member becomes qualified, with a small scope tracker so locals and parameters still hide members. After that the rest of the analyser, the code generator and the VM see ordinary names that happen to contain a dot (which no identifier can), so modules cost nothing at run time and the bytecode, traces, the debugger and messages all show `geometry.area`. A struct may share its module's name (`ډېرۍ.ډېرۍ` - Pashto has no capitals): a module name before a dot always means the module. Parser: `module` blocks, qualified type names (`stack.Stack<int>`), a type word as a member name (`random.int`), `string.` (the type keyword) as a module name. |
| 2 | The modular library | `BuiltinModule` gained `math`, `string`, `random`, `bytes` and `file` beside `os` and `graphics`. Every built-in's `canonical_name` is now its qualified English name (`math.sqrt`); the language packs register members per module (`module_builtins`), so one member name may live in several modules (`to_int` and `bytes.to_int`, `remove` and `file.remove`). math, string, random and bytes are always there; os, file and graphics reach outside the program and are imported (`import file`, S049 otherwise). The built-in enums are module members too (`graphics.Color.red`, `os.Platform.linux`). Built-in ids and bytecode are unchanged apart from the new `name_of` (appended). The files built-ins were split out of `os` into `file` (`file.read`, `file.write`, `file.exists`, `file.read_bytes` ...); `random()`/`random_int`/`seed_random` became `random.float`/`random.int`/`random.seed`; the byte conversions `bytes.from_string`/`to_string`/`to_int`/`from_int`. Runtime messages name the qualified built-in (`'math.sqrt' is not defined for -1.0`). The table below is the full mapping, generated from the compiler and the SDK files. |
| 3 | The SDK | `sockets.th`: modules `tcp`, `udp`, `socket`, `host`; a socket is now a `socket.Socket` struct (tcp.connect/listen/accept and udp.open return `nothing` on failure) so `with` can close it; the natives are private module members (`socket._send`) and the C++ library registers them under those qualified names in both languages (`ساکټ._ولېږه`). `datastructures.th`: one module per structure (`stack.new()`, `stack.push(s, 1)`, `stack.Stack<T>` ... `trie.words(t)`). `algorithms.th`: `sorting`, `searching`, `text`. `testing.th`: `test` and `expect`. Every Pashto file mirrors its English twin declaration for declaration (the parity test holds). |
| 4 | Test suites | **A module is a suite.** `test.run(name, body)` takes the suite from `name_of(body)` - the new default built-in that returns a function value's declared, qualified name (`name_of(geometry.area)` is "geometry.area"). A suite's name is printed above its tests (indented), `test.finish()` adds per-suite counts when there is more than one suite, and the program's arguments select a test, a whole suite, or `suite/test`. The VS Code Test Explorer shows file -> suite -> test and runs any of them. Sample 28 groups its tests into two suites. |
| 5 | `with` | `with name = resource`, `with name: Type = resource` or `with resource`; Pashto writes the keyword at the end of the line (`ساکټ = ټي_سي_پي.ونښلېږه(...) سره`), and either language's wrong position is P018 (reported, then understood). The resource is a struct; its module's `close` (Pashto `وتړه`; `close` is understood in every language, like `main`) - or a top-level `close` for a struct outside any module - closes it at the end of the block and on every `return`, `break` and `continue` that leaves it, innermost first; a return value is computed before; `nothing` is not closed; the name is a constant. S068 (not a struct), S069 (no close). **How:** sema builds the cleanup as an ordinary checked statement (`if r != nothing: m.close(r)`), and the code generator emits it at the block's end and before every way out (a `with` stack per function, a depth per loop). The translator moves the keyword between the two ends of the logical line. Runtime errors and `os.exit` do not run cleanups (the operating system frees the handles). |
| 6 | Samples | `25_modules` rewritten around `module` (lib/geometry.th and lib/numbers.th declare modules; a module of the program's own with a private helper; the built-in modules); `28_testing` (suites); `38_sockets` (sockets closed by `with`); new `39_with` (end of block, return, break/continue, nesting, nothing, `with value`), all in both languages; every other sample, the advanced samples, the project sample (modules `geometry`, `statistics` / `هندسه`, `احصایه`) and the VS Code project templates moved to the qualified built-ins. Goldens regenerated: only 20 (built-in enum values print qualified inside containers), 25, 28, 38 and the new 39 changed. |
| 7 | Tools | Qt IDE: Enter indents after `module`/`enum`/`with` and after a Pashto line ending in `سره`; the highlighter colours a module's member by its own word. VS Code (0.7.0): completion after `module.` (built-in members with parameters, the program's own public members), quick fixes for S049 and S067, `module`/`with` snippets, indentation rules for the trailing `سره`, test suites in the Test Explorer, module members in the outline and go-to-definition. `--language-info` gained `with_after_resource`, a `member` per built-in and `needs_import` per module. The API site parses module blocks and lists the built-ins by module. |

Versions: the language changed shape, so the toolchain is **0.7.0** and the extension follows.

### The modularisation (generated from the compiler and the SDK files)

### Built-in modules

| Module (Pashto) | Import? | Members (was) |
|---|---|---|
| *(default - no prefix)* |  | `print` (چاپ), `println` (کرښه چاپ), `len` (اوږدوالی), `str` (متن کړه), `to_int` (عدد کړه), `array` (لړۍ), `inspect` (کتنه), `dump` (وښایه), `copy` (کاپي), `has` (لري), `remove` (لیرې کړه), `keys` (کیلۍ), `values` (ارزښتونه), `input` (ولوله), `end_of_input` (د ننوت پای), `contains` (شامل دی), `index_of` (ځای یې), `reverse` (سرچپه کړه), `first` (لومړی), `second` (دوهم), `swap` (ځای بدلول), `zip` (جوړې کړه), `enumerate` (شمېره ورکړه), `items` (توکي), `push` (ورزیات کړه), `pop` (وروستی واخله), `insert` (ننباسه), `remove_at` (له ځایه لیرې کړه), `slice` (ټوټه), `concat` (سره نښلوه), `sort` (ترتیب کړه), `range` (واټن), `sum` (مجموعه), `map` (پر هر یو), `filter` (چاڼ کړه), `reduce` (راټول کړه), `get` (ترلاسه کړه), `to_float` (اعشاري کړه), `hex` (شپاړسیز کړه), `bin` (دوه ییز کړه), `to_byte` (بایټ کړه), `name_of` (د دندې نوم) *new* |
| `math` (ریاضي) | no | `abs` (مطلق) ← `abs`, `min` (کمینه) ← `min`, `max` (اعظمه) ← `max`, `round` (نیژدې کړه) ← `round`, `floor` (کښته کړه) ← `floor`, `ceil` (پورته کړه) ← `ceil`, `sqrt` (جذر) ← `sqrt`, `pow` (توان) ← `pow`, `exp` (نمايي) ← `exp`, `log` (لوګارتم) ← `log`, `sin` (جیب) ← `sin`, `cos` (تتمیم جیب) ← `cos`, `tan` (ظل) ← `tan`, `pi` (پای) ← `pi` |
| `string` (متن) | no | `substring` (برخه) ← `substring`, `trim` (پاک کړه) ← `trim`, `to_upper` (لوی توري کړه) ← `to_upper`, `to_lower` (کوچنی توري کړه) ← `to_lower`, `split` (وېشه) ← `split`, `join` (یو ځای کړه) ← `join`, `replace` (بدل کړه) ← `replace`, `starts_with` (پیلېږي په) ← `starts_with`, `ends_with` (پایېږي په) ← `ends_with`, `repeat` (تکرار کړه) ← `repeat`, `char_code` (د توري کوډ) ← `char_code`, `from_char_code` (د کوډ توری) ← `from_char_code`, `is_int` (عدد دی) ← `is_int`, `is_number` (شمېره ده) ← `is_number`, `format` (بڼه کړه) ← `format` |
| `random` (تصادفي) | no | `int` (عدد) ← `random_int`, `seed` (بنسټ) ← `seed_random`, `float` (اعشاري) ← `random` |
| `bytes` (بایټونه) | no | `from_string` (له متنه) ← `string_to_bytes`, `to_string` (متن کړه) ← `bytes_to_string`, `to_int` (عدد کړه) ← `bytes_to_int`, `from_int` (له عدده) ← `int_to_bytes` |
| `os` (سیسټم) | `import os` | `args` (دلیلونه) ← `args`, `get_env` (د چاپېریال ارزښت) ← `get_env`, `run` (وچلوه) ← `run`, `run_output` (د چلولو پایله) ← `run_output`, `list_dir` (د پوښۍ توکي) ← `list_dir`, `current_dir` (اوسنۍ پوښۍ) ← `current_dir`, `time` (وخت) ← `time`, `clock_ms` (ساعت) ← `clock_ms`, `sleep` (انتظار) ← `sleep`, `date_time` (نېټه او وخت) ← `date_time`, `platform` (عامل سیسټم) ← `platform`, `exit` (پای ته ورسوه) ← `exit`, enum `Platform` (پلیټفارم) ← `Platform` |
| `file` (فایل) | `import file` | `read` (ولوله) ← `read_file`, `write` (ولیکه) ← `write_file`, `append` (ورزیات کړه) ← `append_file`, `exists` (شته) ← `file_exists`, `remove` (ړنګ کړه) ← `remove_file`, `read_bytes` (بایټونه ولوله) ← `read_bytes`, `write_bytes` (بایټونه ولیکه) ← `write_bytes`, `append_bytes` (بایټونه ورزیات کړه) ← `append_bytes` |
| `graphics` (ګرافیک) | `import graphics` | `canvas` (ټاټ) ← `canvas`, `clear` (ټاټ پاک کړه) ← `clear`, `pixel` (ټکی) ← `pixel`, `line` (لیکه) ← `line`, `rect` (مستطیل) ← `rect`, `rect_outline` (د مستطیل څنډه) ← `rect_outline`, `circle` (دایره) ← `circle`, `circle_outline` (د دایرې څنډه) ← `circle_outline`, `text` (لیکنه) ← `text`, `text_size` (د لیکنې اندازه) ← `text_size`, `line_width` (د لیکې پلنوالی) ← `line_width`, `rgb` (رنګ) ← `rgb`, `frame` (چوکاټ) ← `frame`, `show` (ښکاره کړه) ← `show`, `frame_rate` (د چوکاټ چټکتیا) ← `frame_rate`, `key_pressed` (کښېکښل شوې تڼۍ) ← `key_pressed`, `key_down` (تڼۍ کښېکښل شوې ده) ← `key_down`, `mouse_x` (موږک افقي) ← `mouse_x`, `mouse_y` (موږک عمودي) ← `mouse_y`, `mouse_down` (موږک کښېکښل شوی) ← `mouse_down`, enum `Color` (رنګونه) ← `Color` |

### SDK modules (lib/thunder)

| File (import) | Module (Pashto) | Members (Pashto) ← was |
|---|---|---|
| sockets.th (`import "sockets"`) | `socket` (ساکټ) | struct `Socket` (ساکټ) *new*, `send` (ولېږه) ← `socket_send`, `send_line` (کرښه_ولېږه) ← `socket_send_line`, `receive` (واخله) ← `socket_receive`, `receive_line` (کرښه_واخله) ← `socket_receive_line`, `send_bytes` (بایټونه_ولېږه) ← `socket_send_bytes`, `receive_bytes` (بایټونه_واخله) ← `socket_receive_bytes`, `is_open` (پرانیستی_دی) ← `socket_open`, `close` (وتړه) ← `socket_close`, `set_timeout` (وخت_حد) ← `socket_set_timeout`, `port` (پورټ) ← `socket_port`, `peer` (مقابل_لوری) ← `socket_peer`, `error` (تېروتنه) ← `socket_error`, `from_handle` (له_نښې) *new* |
| sockets.th (`import "sockets"`) | `tcp` (ټي_سي_پي) | `listen` (اورېدونکی_جوړ_کړه) ← `tcp_listen`, `accept` (اړیکه_ومنه) ← `tcp_accept`, `connect` (ونښلېږه) ← `tcp_connect`, `request` (غوښتنه) |
| sockets.th (`import "sockets"`) | `udp` (یو_ډي_پي) | `open` (پرانیزه) ← `udp_open`, `send` (ولېږه) ← `udp_send`, `receive` (واخله) ← `udp_receive`, `send_bytes` (بایټونه_ولېږه) ← `udp_send_bytes`, `receive_bytes` (بایټونه_واخله) ← `udp_receive_bytes`, `sender` (لېږونکی) ← `udp_sender` |
| sockets.th (`import "sockets"`) | `host` (کوربه) | `address` (پته) ← `host_address`, `name` (نوم) ← `host_name` |
| testing.th (`import "testing"`) | `test` (ازموینه) | `suite_of` (د_ټولګي_نوم) *new*, `run` (وچلوه) ← `test`, `fail` (ناکامه_کړه), `finish` (پای_ته_ورسوه) ← `finish_tests` |
| testing.th (`import "testing"`) | `expect` (تمه) | `that` (دا_وي) ← `expect`, `is_true` (رښتیا_وي) ← `expect_true`, `is_false` (درواغ_وي) ← `expect_false`, `equal` (برابر) ← `expect_equal`, `not_equal` (نابرابر) ← `expect_not_equal`, `near` (نږدې) ← `expect_near` |
| datastructures.th (`import "datastructures"`) | `stack` (ډېرۍ) | struct `Stack` (ډېرۍ), `new` (نوې) ← `new_stack`, `push` (ورزیات_کړه) ← `stack_push`, `pop` (واخله) ← `stack_pop`, `peek` (وګوره) ← `stack_peek`, `size` (کچه) ← `stack_size`, `is_empty` (تشه_ده) ← `stack_is_empty` |
| datastructures.th (`import "datastructures"`) | `queue` (لیکه) | struct `Queue` (لیکه), `new` (نوې) ← `new_queue`, `add` (ورزیات_کړه) ← `queue_add`, `take` (واخله) ← `queue_take`, `peek` (وګوره) ← `queue_peek`, `size` (کچه) ← `queue_size`, `is_empty` (تشه_ده) ← `queue_is_empty` |
| datastructures.th (`import "datastructures"`) | `linked_list` (ځنځیر) | struct `Node` (د_ځنځیر_غوټۍ) ← `ListNode`, struct `LinkedList` (ځنځیر), `new` (نوی) ← `new_linked_list`, `add_first` (سر_ته_ورزیات_کړه) ← `list_add_first`, `add_last` (پای_ته_ورزیات_کړه) ← `list_add_last`, `remove_first` (له_سره_واخله) ← `list_remove_first`, `remove_value` (لیرې_کړه) ← `list_remove_value`, `has` (لري) ← `list_has`, `size` (کچه) ← `list_size`, `to_array` (لړۍ_کړه) ← `list_to_array` |
| datastructures.th (`import "datastructures"`) | `tree` (ونه) | struct `Node` (د_ونې_غوټۍ) ← `TreeNode`, struct `BinaryTree` (ونه), `new` (نوې) ← `new_tree`, `insert` (ورزیات_کړه) ← `tree_insert`, `has` (لري) ← `tree_has`, `size` (کچه) ← `tree_size`, `height` (لوړوالی) ← `tree_height`, `smallest` (کمینه) ← `tree_smallest`, `largest` (اعظمه) ← `tree_largest`, `sorted` (په_ترتیب) ← `tree_sorted`, `remove` (لیرې_کړه) ← `tree_remove` |
| datastructures.th (`import "datastructures"`) | `heap` (لومړیتوب_لیکه) | struct `Heap` (لومړیتوب_لیکه), `new` (نوې) ← `new_heap`, `add` (ورزیات_کړه) ← `heap_add`, `take` (واخله) ← `heap_take`, `peek` (وګوره) ← `heap_peek`, `size` (کچه) ← `heap_size`, `is_empty` (تشه_ده) ← `heap_is_empty` |
| datastructures.th (`import "datastructures"`) | `set` (ټولګه) | struct `Set` (ټولګه), `new` (نوې) ← `new_set`, `add` (ورزیات_کړه) ← `set_add`, `remove` (لیرې_کړه) ← `set_remove`, `has` (لري) ← `set_has`, `size` (کچه) ← `set_size`, `is_empty` (تشه_ده) ← `set_is_empty`, `values` (ارزښتونه) ← `set_values`, `union` (یوځای) ← `set_union`, `intersection` (ګډ) ← `set_intersection`, `difference` (توپیر) ← `set_difference` |
| datastructures.th (`import "datastructures"`) | `ordered_set` (ترتیب_شوې_ټولګه) | struct `OrderedSet` (ترتیب_شوې_ټولګه), `new` (نوې) ← `new_ordered_set`, `add` (ورزیات_کړه) ← `ordered_set_add`, `remove` (لیرې_کړه) ← `ordered_set_remove`, `has` (لري) ← `ordered_set_has`, `size` (کچه) ← `ordered_set_size`, `is_empty` (تشه_ده) ← `ordered_set_is_empty`, `values` (ارزښتونه) ← `ordered_set_values`, `smallest` (کمینه) ← `ordered_set_smallest`, `largest` (اعظمه) ← `ordered_set_largest` |
| datastructures.th (`import "datastructures"`) | `deque` (دوسرې_لیکه) | struct `Deque` (دوسرې_لیکه), `new` (نوې) ← `new_deque`, `add_first` (سر_ته_ورزیات_کړه) ← `deque_add_first`, `add_last` (پای_ته_ورزیات_کړه) ← `deque_add_last`, `take_first` (له_سره_واخله) ← `deque_take_first`, `take_last` (له_پایه_واخله) ← `deque_take_last`, `peek_first` (سر_وګوره) ← `deque_peek_first`, `peek_last` (پای_وګوره) ← `deque_peek_last`, `size` (کچه) ← `deque_size`, `is_empty` (تشه_ده) ← `deque_is_empty`, `to_array` (لړۍ_کړه) ← `deque_to_array` |
| datastructures.th (`import "datastructures"`) | `doubly_linked_list` (دوه_اړخیز_ځنځیر) | struct `Node` (د_دوه_اړخیز_ځنځیر_غوټۍ) ← `DllNode`, struct `DoublyLinkedList` (دوه_اړخیز_ځنځیر), `new` (نوی) ← `new_dlist`, `add_first` (سر_ته_ورزیات_کړه) ← `dlist_add_first`, `add_last` (پای_ته_ورزیات_کړه) ← `dlist_add_last`, `take_first` (له_سره_واخله) ← `dlist_take_first`, `take_last` (له_پایه_واخله) ← `dlist_take_last`, `remove_value` (لیرې_کړه) ← `dlist_remove_value`, `has` (لري) ← `dlist_has`, `size` (کچه) ← `dlist_size`, `to_array` (لړۍ_کړه) ← `dlist_to_array`, `to_array_reversed` (سرچپه_لړۍ_کړه) ← `dlist_to_array_reversed` |
| datastructures.th (`import "datastructures"`) | `graph` (ګراف) | struct `Graph` (ګراف), `new` (نوی) ← `new_graph`, `add_vertex` (څوکه_ورزیاته_کړه) ← `graph_add_vertex`, `add_edge` (غاړه_ورزیاته_کړه) ← `graph_add_edge`, `connect` (ونښلوه) ← `graph_connect`, `vertices` (څوکې) ← `graph_vertices`, `neighbors` (ګاونډیان) ← `graph_neighbors`, `bfs` (پلن_لټون) ← `graph_bfs`, `dfs` (ژور_لټون) ← `graph_dfs`, `shortest_path` (لنډه_لار) ← `graph_shortest_path`, `has_path` (لار_لري) ← `graph_has_path` |
| datastructures.th (`import "datastructures"`) | `trie` (مختاړې_ونه) | struct `Node` (د_مختاړې_ونې_غوټۍ) ← `TrieNode`, struct `Trie` (مختاړې_ونه), `new` (نوې) ← `new_trie`, `add` (ورزیات_کړه) ← `trie_add`, `has` (لري) ← `trie_has`, `has_prefix` (مختاړی_لري) ← `trie_has_prefix`, `size` (کچه) ← `trie_size`, `words` (وییونه) ← `trie_words` |
| algorithms.th (`import "algorithms"`) | `sorting` (ترتیب) | `bubble` (حبابي) ← `bubble_sort`, `selection` (غوراوی) ← `selection_sort`, `insertion` (ننویستل) ← `insertion_sort`, `quick` (چټک) ← `quick_sort`, `merge` (ګډولو) ← `merge_sort`, `is_sorted` (شوې_ده), `shuffle` (ګډوډه_کړه) |
| algorithms.th (`import "algorithms"`) | `searching` (لټون) | `linear` (پرله_پسې) ← `linear_search`, `binary` (نیمایي) ← `binary_search`, `index_of_smallest` (د_کمینې_ځای) |
| algorithms.th (`import "algorithms"`) | `text` (متنونه) | `find` (ومومه) ← `find_text`, `find_all` (ټول_ومومه) ← `find_all_text`, `is_palindrome` (سرچپه_ورته_ده), `longest_common_prefix` (تر_ټولو_اوږد_ګډ_مختاړی), `edit_distance` (د_بدلون_واټن) |


### Decisions taken without asking (please review)

* **Built-in names stay reserved inside a module.** In `module stack`, plain `push(...)` is still
  the built-in; the module's own `push` is written `stack.push` there. The alternative (members
  shadow built-ins) makes `push(s.items, v)` inside `stack.push` an infinite recursion.
* **Module names are reserved**, for variables and parameters too (S065), rather than shadowable:
  `file.read(file)` with a variable called `file` would otherwise mean something else in every
  function. The cost was renaming a handful of variables (`file` -> `path` in sample 27, the
  Pashto `ټولګی` variable in sample 19, now the keyword).
* **`os`, `file` and `graphics` still need `import`; `math`, `string`, `random`, `bytes` do not.**
  The first three reach outside the program, which an import makes visible; the rest are part of
  computing. `file` was split out of `os` (the prefix rule: `read_file`/`file_exists`).
* **Conversions and generic sequence functions stay in the default module**: `str`, `to_int`,
  `to_float`, `to_byte`, `hex`, `bin`, and `len`/`contains`/`index_of`/`reverse`/`slice`, which
  work on strings and arrays alike. `format`, `is_int` and `is_number` (text in, text-shaped
  answers) went to `string`; `min`/`max`/`abs` to `math`.
* **`random.int` / `random.float`**: a type word may follow a dot. In Pashto the members are then
  the type words themselves (`تصادفي.عدد`, `تصادفي.اعشاري`), which reads naturally.
* **No old names as aliases.** Keeping `sqrt` beside `math.sqrt` would keep every name reserved
  and the library flat; instead S067 tells a 0.6 program where each name went.
* **Sockets became a struct type** (`socket.Socket`) so that `with` can close them; failures are
  `nothing` instead of `-1`. Closing twice is harmless.
* **`with` is RAII for the block, not exceptions:** a runtime error ends the program without
  running cleanups (Thunder has no exceptions; the OS frees what was open).
* **The `with` sample is number 39** (appended), so the 38 existing samples keep their numbers.
* **Pashto vocabulary** new in this iteration (module and member names, `ټولګی`, `سره`, `وتړه`,
  the test and data-structure members) is, as always, a draft for a native speaker to review.

### Found and fixed on the way

* **A module variable read before its module was declared crashed the code generator**: the
  "already reported" rule that keeps `geometry.nope` from giving two errors also hid the plain
  S001 for a member that exists but is not declared yet at that point. It now stays silent only
  when S070 or S049 really was reported (test in `test_with_modules.cpp`).
* **The live debugger could not evaluate `geometry.made`** (it read it as field `made` of a
  variable `geometry`); a path whose first segment names no variable now tries the qualified
  global (`test_debugger.cpp`).
* **Keywords lost to members of the same spelling** in the editors' word tables: Pashto
  `random.int` is `تصادفي.عدد`, and `عدد` was coloured as a built-in instead of the type. Keywords
  now win (VS Code `reservedWords`, the Qt highlighter).
* **A `with` whose body always returns** emitted a dead end-of-block cleanup whose jump pointed
  past the last instruction, which the bytecode verifier rejected; the cleanup is emitted only
  where the block can fall through.

### Documentation

Every reference was updated in both languages: language (new sections Modules, Built-in
functions and modules, Resources: `with`; keywords, grammar, error table), library (reorganised
by module, with an old -> new names table), graphics, sockets (new section on closing sockets
with `with`), testing (new section Test suites), data structures, algorithms, native (natives
inside a module), and the sweep of debugging, bytecode, display and packaging; the root README,
the reference indexes, the project and sockets READMEs; the API site was regenerated. Every code
example in them was compiled (and the runnable ones run) by the writers.

## 53. Fortieth iteration (2026-11-17): the test log, the Pashto place of `سره`, resources without a struct

**Transcript summary.** Three items after the modules/`with` drop: (1) the Windows run showed 1
C++ and 157 Python failures (`issues/test_failures.txt`); (2) the Pashto form of `with` was
wrong: `with handle = Module.open()` must be `لاستی سره = ټولګی.پرانیزه()` - the keyword after
the handle - not `لاستی = ... سره`; (3) why does `with` demand a struct, when the close function
is a module's and cannot be overloaded per type?

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | The failures | All of my own making, in the test suite rather than the toolchain. **C++** (`test_library`): the conversion script had turned a test's `func time()` into `func os.time()`, and the test still expected the 0.6 rule that a user `time()` clashes with os's. **Python**: 152 of the 157 were one test - the grammar check looked for `string.substring` as plain text, but the generated grammar escapes the dot (`string\.substring`); `test_cli` keyed the built-ins by their old bare names; `test_direction` had been edited by the script (a `ریاضي.` prefix inside a layout test); and the two `with` snippets had an empty `$0` body (P012) and, in Pashto, began with a placeholder. The tests were corrected, the snippets' bodies end in `pass`, and the Pashto `with` snippet is written in the order below. |
| 2 | `لاستی سره = ...` | The keyword follows the thing it goes with. With a handle: `لاستی سره = ټولګی.پرانیزه()` (also `لاستی سره: ډول = ...`); without one: `ساکټ سره` at the end of the line, as before. Parser (`at_handle_with`: a name, the keyword, then `=` or `:`), translator (English `with h = e` ↔ Pashto `h سره = e`; `with v` ↔ `v سره`), the Qt editor's and the right-to-left editor's Enter rule (a line whose second word is `سره` opens a block too), the generated indentation rules, the snippet, every Pashto sample, SDK file, document and test that had the old order. P018's Pashto wording now says "after the handle". |
| 3 | Resources without a struct | The struct rule existed because a struct *names its origin*: its module is where `close` is looked for, so `with sock` (an existing variable) works. An int handle names nothing. But the request is right that nothing else needs the struct: the close is the module's. So the rule is now two-fold. A **struct** is closed by its module's `close`, and `nothing` is not closed. **Any other value** (an int handle, a string ...) is closed by the module of the function that made it: `with h = handle.open()` closes with `handle.close(h)`; for such a value the resource must be that call, since a bare `5` or `math.abs(5)` has no module to turn to (S068, reworded to say both ways). |

### Decisions taken without asking (please review)

* **The type still wins when it is a struct** - `with s = tcp.connect(...)` closes with `socket.close`
  (the type's module), not `tcp.close` - so a function may hand out another module's resource.
* **A primitive resource must come from a call** in the `with` itself. `var h = handle.open()`
  followed by `with h` cannot know whose handle `h` is; the message says so.

### Notes

* The documentation writer's compile check caught two things in the first cut of the new word
  order: a bare `راپور سره` (no handle) was read as the handle form and asked for `=`, and the
  Pashto P018 template ("... د کرښې په {1} کې ...") no longer read well with "after the handle"
  in it. The handle form now needs `=` or `:` right after `سره`, and P018 says "په دې ژبه کې 'سره'
  له نوم وروسته لیکل کېږي" (English: "is written at the start of the line").
* Writing the old `handle = resource سره` in Pashto is P018 too, so a program written against
  the first cut is told where the keyword goes.
* Tests added: the handle, bare-value and misplaced forms in both languages (C++), an int-handle
  resource and a variable that cannot be closed, Enter after `module`/`with` in both editors.

## 54. Forty-first iteration (2026-11-17): concurrency - an exploration

**Transcript summary.** Two questions, answered in a report rather than code: (1) does Thunder
offer any concurrency, even primitive (setTimeout/setInterval), and can anything be done without
blocking; (2) what are the options for proper concurrent programming, with a simple API that
still has synchronisation, conditional waiting and barriers.

The report is **`todo/concurrency_report.md`** (first written at the repository root, moved to `todo/` in §56). In short: there is no concurrency
today. One thread of instructions, and every wait blocks the program. Non-blocking programs can
be written by polling: key and mouse queries never block, sockets take a timeout of 0, and
`os.clock_ms()` is a clock. Function values are enough to emulate setTimeout/setInterval in
Thunder; both programs in the report were run. The recommendation is green threads ("tasks")
scheduled by the VM with deterministic, seedable preemption. A minimal kernel (spawn, join,
sleep, yield, mutex, condition, deadlock detection) lives in the VM, and the semaphore, barrier,
channel, wait group and latch are written in Thunder in the SDK. `with` releases locks. A
phased plan and five decisions close the report. Nothing in the toolchain was changed.

## 55. Forty-second iteration (2026-11-17): test decorators - an exploration

**Transcript summary.** The testing SDK makes you write each test twice (the function and its
`test.run` line); the request is a decorator - `@test("description")`, Pashto `@ازموینه(...)` -
on a function or a whole module, run automatically, highlighted in orange in the IDE and in VS
Code, plus other ideas, an effort estimate and the pitfalls. A report, not code.

The report is **`todo/test_decorators_report.md`** (first written at the repository root, moved to `todo/` in §56).
In short: a general annotation syntax (`@name(arguments)` before `func` or `module`), with
`@test`, `@skip`, `@before_each` and `@after_each` first; the compiler generates the `test.run`
calls and `test.finish()` so the harness stays the readable Thunder it is; `@` is free (it is L001
today); a new token kind coloured orange in the Qt IDE, the right-to-left editor, the TextMate
grammar and the API site; the Test Explorer reads the tests from the compiler's report instead of
matching lines; `--no-tests` strips tests when packaging. Estimate: about the enum iteration's
size. Pitfalls: `@` before a right-to-left word in the direction algorithm, a file that is both
program and tests, generic functions, duplicate names, the two Pashto spellings. Nothing in the
toolchain was changed.

## 56. Forty-third iteration (2026-11-17): a GUI SDK - an exploration

**Transcript summary.** Can the language support a simple GUI SDK; which core features are
missing; would OOP (or a subset) help; is the graphics module capable enough; does the display
plug-in interface need extending; what are the options for an efficient cross-platform drawing
layer with a native feel? A report, not code, in a new folder `todo/`, into which the concurrency
and test-decorator reports were moved.

The report is **`todo/gui_sdk_report.md`**. In short: a toy widget toolkit written in today's
Thunder compiles and runs, but hits every gap at once - no polymorphism (one fat struct and `if`
chains), no closures (handler state in globals), no text measurement (sizes guessed from
`len`), no conditional expression, no defaults, no waiting for events. Recommended: an OOP
*subset* - method-call syntax over the module + struct pairing, nominal interfaces with dynamic
dispatch, closures; no inheritance. The graphics module (eight shapes, one-way display ABI 1)
needs an ABI 2 with text measurement, a richer display list (fonts, alpha, clipping, rounded
shapes, images), richer events (text input for input methods, modifiers, wheel, resize) and a
`wait`. GUI path: own-drawn widgets in Thunder first (they run in every existing host and are
readable), native widgets through a retained "UI host" interface second, with Qt Widgets as the
main backend for its Pashto and right-to-left support (PySide6 in the IDE, a webview in VS Code;
wxWidgets or a webview as alternatives). Nothing in the toolchain was changed.

## 57. Forty-fourth iteration (2026-11-18): objects - methods, interfaces, mixins and closures

**Transcript summary.** Implement the OOP subset of `todo/gui_sdk_report.md` - methods,
interfaces and closures - plus single-level mixin inheritance, but **without the `self`
parameter** the report sketched (it makes the syntax noisy and repetitive): methods are declared
inside the struct's body and a bare member name inside a method means the member of the hidden
receiver. The Pashto keyword for `interface` is **`برسېر`**. Rewrite every SDK module that is a
bundle of functions over one data type as an object (the data structures, sockets), make `with`
RAII go through objects (the struct's own `close()` method), add an OOP sample and update the
existing samples and the documentation.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Methods and constructors | Declared inside the struct's body: fields, then `Button(title: string)` (the constructor: a member spelled like the struct) and `func get_title(): string` (methods), each with an indented body. No `self`: a rewriter pass (`SelfRewriter`, sema.cpp) runs on the syntax tree before anything is checked and makes the receiver explicit - `title_` becomes `(self)._title`, `useful()` becomes `(self).useful()` - with a scope tracker so parameters and locals still hide members, and built-ins still winning for bare calls (as inside modules). Each method compiles to an ordinary function `Button.get_title` whose hidden first parameter `(self)` (Pashto `(ځان)`) is the receiver; `b.get_title()` pushes `b`, then the arguments, then `CALL`s it - so methods cost exactly a function call, and traces, the debugger and `name_of` show `Button.get_title`. With a constructor, `Button("OK")` builds the struct with every field at its default and runs the constructor on it (`gen_construct`: default value, `DUP`, arguments, `CALL`); the field-list forms stay for structs without one. Constructor rules: no return type or type parameters (S079); one member per name (S080). Methods of a generic struct are instantiated per instance (`Stack<int>.push`), through the same machinery as generic functions. |
| 2 | Private members | A member whose name **starts with `_`** is the struct's own: fields and methods alike, usable only inside the struct's methods (S076, also across instances of one template). The SDK uses it for every internal (`_items`, `_head`, `_walk`). It is the same mark modules already used for their private members, now read as "mine" inside whichever owner declares it. |
| 3 | Interfaces | `interface Widget` (Pashto `برسېر`) declares method signatures (`func draw()`) and closure-valued members (`on_click(): func()` - satisfied by a field of that function type). `struct Button: Widget, Clickable` promises them; conformance is checked at the struct (S074 missing member, S075 wrong signature). `Type::Base::Interface`, assignability struct -> implemented interface (also inside array literals: `var screen: Widget[] = [button, label]`), `nothing` fits and compares. Calls through an interface value dispatch at run time: a new `CALL_METHOD` instruction (operands: the member's name as a constant, the argument count) looks the method up in the struct's method table - every `StructLayout` in the `.thc` now carries one - or, for a closure member, reads the field and calls the value; `R036` for `nothing`. An interface value's members are only used by calling them (S078; reading a method as a value is S078 too); other member errors are S077. Interfaces may live in modules (`ui.Widget`). |
| 4 | Mixins, one level | A **struct** listed after `:` is a mixin: its fields and methods are copied into the listing struct on the syntax tree (`apply_mixins`), before types resolve - so generic structs take their mixins into every instance. The struct's own method of a name wins (overriding); a mixin's interfaces come along; a mixin may not include a mixin (S072) or be generic (S073); anything else after `:` is S071. No base classes, no `super`, no constructor chains. |
| 5 | Closures | `func(params)[: type]` as an expression: the body is one statement on the same line (one expression when there is a return type, which is returned), or an indented block outside brackets (P020 when missing). A lambda compiles as its own function (`func@line`); a variable of an enclosing function that the body uses is **captured**: the variable moves into a heap box (`NEW_BOX` at function entry; `BOX_GET`/`BOX_SET` at every read/write), `MAKE_CLOSURE fn, n` bundles the boxes with the function value, `CALL_VALUE` hands them to the frame and `LOAD_CAPTURE i` reaches them - so the closure and its maker share the variable, both ways, and nested lambdas pass boxes inward (each level captures from the one above). Captured `const`s stay constant; globals are not captured; a loop variable is one shared box (as in Python; documented). `map`/`filter`/`reduce` accept closures; equality of function values is now identity (index + captures). Capture analysis lives in sema (`resolve_captured`, a `FunctionContext` stack); `Symbol` gained `boxed` and the kind `Capture`; `Value` gained the internal `Box` kind that always displays as its content. |
| 6 | `with` through objects | A struct resource is closed by **its own `close()` method** (`وتړه`; `close` understood in every language): sema builds `if r != nothing: r.close()`. A struct without one is S081 ("give the struct a method 'close()'"); the module-function fallback remains only for non-struct handles (`with h = handle.open()`), S068 reworded. The SDK's `Socket.close()` and the samples' `Log.close()` are methods now. |
| 7 | Bytecode and VM | Opcodes 55 -> 61: `NEW_BOX`, `BOX_GET`, `BOX_SET`, `MAKE_CLOSURE` (u16 function, u16 count), `LOAD_CAPTURE` (u16), `CALL_METHOD` (u16 name constant, u16 argc). `.thc` **format 8**: each struct's method table (name -> function), each function's capture count; the verifier checks them all (method indexes, capture counts against `MAKE_CLOSURE`, `CALL_METHOD`'s name constant). `CallFrame` carries the running closure's boxes. |
| 8 | The SDK as objects | `datastructures.th`: the one-struct modules are gone - `Stack<T>`, `Queue<T>`, `LinkedList<T>`/`ListNode<T>`, `BinaryTree<T>`/`TreeNode<T>`, `Heap<T>`, `Set<T>`, `OrderedSet<T>` (a `BinaryTree` inside: an object built on an object), `Deque<T>`, `DoublyLinkedList<T>`/`DllNode<T>`, `Graph<T>`, `Trie`/`TrieNode` are top-level structs with constructors and methods, internals private (`var s: Stack<int> = Stack()`, `s.push(1)`); node structs stay plain data. `sockets.th`: `socket.Socket` carries every per-socket operation as a method (`connection.send_line("hi")`, `connection.receive_line()`, `listener.port()`, `client.close()`...); `socket.error()`/`from_handle` and the `tcp`/`udp`/`host` factories stay module functions (a datagram names its destination every time, so `udp.send` keeps its shape); the C++ native library needed no change. Pashto twins mirror declaration for declaration (the parity test now counts indented members too). `algorithms.th` and `testing.th` stay functional - no shared data type, nothing gained by objects. |
| 9 | Samples | New **`40_objects.th`** (both languages): the Button/Widget/Clickable/mixin example from the design discussion, closures and counters, an interface-typed array, `nothing` in an interface variable. `38_sockets`, `39_with` (the log is a struct with methods) and `advanced/01_datastructures` rewritten to the object style in both languages; goldens regenerated. |
| 10 | Documentation | language.md (en+ps): closures under Functions; Methods and constructors, Private members, Interfaces and Mixins under Structs; `with` around `close()`; grammar, keyword, type and error tables (P019-P020, S071-S081, R036). datastructures.md and sockets.md rewritten around the object API with a migration table; library.md, debugging.md (method/closure frame names), bytecode.md (new instructions, format 8); the API site generator now parses struct members (constructor and methods rendered and searchable) and the site was regenerated. |
| 11 | Tools | Qt IDE: `interface` opens a block on Enter, keyword fallbacks updated (the live keyword list already comes from the compiler). VS Code (0.8.0): grammar, language data, indentation rules regenerated; new `interface` and "structure with a constructor and a method" snippets in both languages. `--language-info` carries the new keyword automatically. Toolchain version **0.8.0**. |

### Decisions taken without asking (please review)

* **`on_click(): func()` is a field.** The design snippet declares it in the interface and
  repeats it in the struct with "# a closure"; it is implemented as an ordinary field of function
  type whose empty parentheses mark it callable, assignable (`b.on_click = func() ...`) and
  callable through the interface. A method with that signature does not satisfy it: the strict
  reading keeps conformance errors simple.
* **Built-ins win for bare calls inside methods**, exactly as inside modules - so `push(_items, v)`
  inside `Stack.push` is the built-in, and recursion cannot happen by accident. The cost: a
  struct's own member that *shares a built-in's name* cannot be called bare inside its own
  methods (`values()` in `Set.union` would be the built-in); the SDK reads its own map
  (`keys(_entries)`) and says why in a comment.
* **Private is a leading underscore** (`_title`) - the same mark a module uses for its own
  private members, so one rule covers both owners (the design snippet wrote `title_`; the
  trailing form was implemented first and changed in review, see §58). Enforced for fields *and*
  methods, shared among instances of one template.
* **Construction with a constructor is positional only**: named arguments name fields, and a
  constructor's parameters are not fields (S033 as before). Field-list construction remains for
  constructor-less structs, unchanged.
* **Methods are not values**: `var f = b.draw` is an error (S078) - a bound-method value would
  need its own closure form; `func() b.draw()` already expresses it.
* **Mixins may be listed in any number**, each one level deep; the sample and tests use one.
* **A loop variable is captured as one shared box** (Python semantics), not one per iteration;
  documented with the `var mine = i` idiom.
* **Interface values keep no fields**, so there is no dynamic field read; a closure member read
  through an interface must be called, not stored.
* **`with` for structs goes through the method only**: a struct whose *module* has a `close`
  function but that has no method is S081 now (the SDK and samples all moved); plain handles keep
  the module rule from §53.
* **Pashto drafts**: `برسېر` was given; the new diagnostics' wording, `(ځان)` for the hidden
  receiver and `ګډون` for mixin in the messages are drafts for a native speaker, as always.

### Found and fixed on the way

* **Methods were registered twice**: the method declarations are appended to the item list so the
  body-check and codegen passes see them, and `collect_functions` then treated them as top-level
  functions, orphaning the first registration (whose `local_count` stayed 0 - caught by the
  bytecode verifier's "more parameters than local slots"). `collect_functions` now skips methods.
* **An array literal under an interface hint** typed itself from its first element, so
  `[button, label]` was "all elements must be Button"; with an interface element hint the literal
  now checks every element against the hint and takes the interface as its element type.
* **The stale `build/release` toolchain** was silently picked up by the VS Code asset generator
  (search order release, dev); the assets were regenerated against the dev build with
  `THUNDER_BIN`, and the full rebuild regenerates release.

### Tests

New `tests/cpp/test_oop.cpp` (11 cases): methods/constructors (defaults, duplicate members,
S033/S079/S080), privacy, interface dispatch (including the design discussion's Button end to
end), conformance errors, mixins (override, interface propagation, S071-S073), generic structs
with methods and interfaces, closures (sharing, nesting, inline forms, const capture, P020),
a serialize -> verify -> run round trip of a closure-carrying `.thc`, `with` through `close()`,
and the Pashto forms (`برسېر`, a Pashto closure). The with/module tests moved to close methods;
the SDK tests to the object API (plus: reading `q._items` from outside is S076); the sample
goldens were regenerated. The opcode-count and parity tests were updated.

## 58. Forty-fifth iteration (2026-11-18): the private mark moves to the front, and a project that spells

**Transcript summary.** Two corrections after the objects drop. (1) The design snippet wrote a
private member with a *trailing* underscore (`title_`); that was a slip - private members should
be marked the way modules already mark theirs, with a **leading** underscore (`_title`), across
the board. (2) In VS Code a spell checker underlines `println` in every program; the extension
should tell the checker the language's own words - and, while we are there, **New Project**
should write a fuller skeleton.

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Private members start with `_` | One line in the analyser (`report_private_member`: `starts_with('_')`), S076 reworded in both languages, and the convention renamed everywhere: the SDK (`_items`, `_head`, `_count`, `_root`, `_entries`, `_adjacency`, and the private methods `_walk`, `_node_height`, `_refill`, `_dfs_visit`, `_collect`; the Pashto twins likewise), the samples (`_title`, `_path`, `_lines`; the Pashto `_سرلیک`، `_مسیر`، `_کرښې`), the tests, the documentation in both languages and the API site. **Why it is better:** a module's private member has always been `socket._send`, so now one mark means "mine" inside whichever owner declares it - a module or a struct - and nothing in the language reads a trailing underscore any more (it stays what it always was: the escape the translator adds to a name that is reserved in the other language, as in `متن_`). |
| 2 | The spell checker | `ide/vscode/tools/generate_assets.py` now also generates **`data/spelling-words.json`** - every keyword, built-in, module and toolchain name, split the way a checker splits names (`println`, `thunderc`, `sqrt`, ... 170 words) - and writes it into `package.json` as `contributes.configurationDefaults["cSpell.languageSettings"]` for the `thunder` language. A Thunder file is therefore never underlined for the language's own vocabulary, with no setup at all, and the list can never drift from the compiler (the generator's `--check` runs in the test suite). Arabic-script Pashto needs nothing: checkers leave it alone. |
| 3 | New Project | The command already existed (and is on the Thunder view's welcome page); it now writes a whole skeleton: `thunder.json`, `main.th` (Hello World), `lib/greeting.th`, **`tests/greeting_test.th`** (a working test the Testing view picks up at once), **`README.md`** (what each file is, the keys, the command line), **`.gitignore`** (`*.thc`, build output) and `.vscode/` with `tasks.json`, `launch.json`, the project schema and **`settings.json`** - four-space indentation and the same spelling word list, so the project is quiet about `println` even for someone who opens it without the extension. Nothing is overwritten, so it is safe in a folder started by hand. Both languages. |
| 4 | Found on the way | The release build (sanitisers, `-Werror`) rejected four things the development build had let through in §57: a `name`/`index` shadow in `StructLayout::method_function` and `StructInfo::method_index`, a shadowed `name` in `InterfaceInfo::member`, a missing initialiser for `CallFrame::captures`, a missing one for `StructLayout::methods` in two test literals, an `int`→`size_t` conversion in `CALL_METHOD`, and the `Box` value kind missing from the debugger's type switch. All fixed; the debugger now unwraps a captured variable's box in `children`, `expandable` and `type_text`, so a captured variable looks exactly like an ordinary one in the Variables view. |

### Decisions taken without asking (please review)

* **The trailing underscore keeps its old job.** The translator still appends `_` to a name that
  collides with a reserved word in the target language (`متن` -> `متن_`), and the SDK keeps such
  names (the `متن_` parameter of `متنونه.ومومه`). They are not private; nothing reads them.
* **Private fields are now hidden from the API site** (as private methods already were) and
  counted among a module's internal names, so `docs/api` shows a structure's methods, not its
  insides.
* **The spelling list is contributed per language id** (`cSpell.languageSettings` for `thunder`),
  not as global `cSpell.words`: the words are known inside Thunder files and nowhere else. A
  project gets both forms in its own settings, because a project's README mentions them too.
* **Only the Code Spell Checker's settings are contributed.** It is the common one; another
  checker needs its own configuration, which the extension's README says.

## 59. Forty-sixth iteration (2026-11-18): the IDE explains the name under the pointer

**Transcript summary.** Two asks for the Qt IDE. (1) Hovering something that has an API
reference, or a "JavaDoc"-style comment above its declaration - a function call, say - should
show that documentation as a tooltip. (2) It should be configurable through a **new Settings
menu**, on by default, so a teacher can turn it off (a learner who ought to look a function up,
or read the imported file, is not handed the answer by the pointer).

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | What the tooltip says | A new pure-core module, `ide/thunder_ide/core/hoverdocs.py`. For a **built-in** (`println`, `math.sqrt`, the Pashto `کرښه چاپ` from either of its words) the tooltip is the API reference the compiler itself carries for the program's language (`--language-info`): the signature - shown under the name the program writes, `ریاضي.جذر(x: float): float` - and the description. For a name the **program declares** (func, struct, interface, enum, var/const, a struct's method, constructor or field) it is the declaration line and the contiguous `#` comment block right above it - the "JavaDoc" convention the API site already reads - with banners (`# ----`) and the language directive excluded, and a field's trailing comment used when there is no block. A name that is not found in the program is looked for in the files it **imports**, resolved the way the compiler resolves them (next to the program, then the SDK beside thunderc), and the tooltip then names the file ("from datastructures.th"). All of it is plain text work on the editor's contents: no compilation, never stale while typing, and tested without a GUI (21 cases in `test_hoverdocs.py`). |
| 2 | Members after a dot | `s.push(1)` cannot know `s`'s type without the semantic analyser, so a member prefers, in order: the module's built-in when the qualifier *is* a module (`math.sqrt`); a declared method, field or module member of that name (program first, then imports - so with `import "datastructures"` the stack's own `push` is explained, and the file is named); nothing otherwise. A bare name never shows a module's member (the language reserves built-ins, and `sqrt` without `math.` is an error anyway). |
| 3 | The Settings menu | A new top-level menu (`menu.settings`, between View and Help) whose first entry is the checkable **API help on hover** (`hover_docs`), on by default, persisted in the IDE's settings file like every other preference, with status-bar feedback and full Pashto translations (تنظیمات، د موږک لاندې د API مرسته). |
| 4 | The seam | `EditorView` gained one method: `set_hover_provider(callable or None)` - the controller hands `hover_text` over while the setting is on, None when it is off, so the editor stays dumb and the fake UI trivial. The Qt editor answers Qt's own ToolTip event (`viewportEvent`), finds the position with the display layout's existing hit-testing - so hovering works token-by-token in right-to-left programs - and shows the rich text with `QToolTip` (signature verbatim, paragraphs flowed, `dir='auto'` for Pashto). |
| 5 | Tests | `test_hoverdocs.py` (word extraction incl. Pashto and multi-word built-ins, declarations, banners, imports, the real SDK file, escaping); 6 controller cases through the fake UI (built-in, doc comment, SDK method with its file name, a Pashto program answering in Pashto, blank space, and the toggle: setting + menu tick + provider gating + persistence); 5 Qt cases through real ToolTip events offscreen (built-in, doc comment, right-to-left hit-testing, empty space hides, and the Settings menu toggling the feature off and on). |

### Decisions taken without asking (please review)

* **No compile, no semantic analysis.** The tooltip is built from the text and the language
  information alone, so it works from the first keystroke and never shows a stale answer. The
  price: `s.push(1)` picks the first declared `push` in reach rather than resolving `s`'s type -
  the named source file in the tooltip keeps that honest.
* **The signature of a built-in is shown under the program's own spelling** (the reference keeps
  canonical English signatures; the display replaces the name: `کرښه چاپ(value, ...)`).
* **The Settings menu starts with this one entry.** Show spaces and tabs stayed in View, the
  fonts in Format: they are about *looking at* the program, not about how the IDE behaves. New
  behavioural preferences belong in Settings from now on.
* **VS Code untouched:** its hover provider already exists (and its users have the full
  reference a click away); teaching mode there can follow if asked.

## 60. Forty-seventh iteration (2026-11-18): tooltips in both languages, IntelliSense in the IDE

**Transcript summary.** Three asks for the Qt IDE and one question. (1) In Pashto, tooltips
showed parameter types in English ("float"); they must use the Pashto type names, and the VS Code
extension must be checked for the same fault. (2) A tooltip should end with the name in the other
language: the Pashto name under an English tooltip, the English one under a Pashto tooltip.
(3) Add IntelliSense to the IDE, working like the VS Code extension's, toggled in the Settings
menu; **both the tooltip and IntelliSense are off by default**. The question: where in VS Code
is the command that writes a new project skeleton?

| # | Item | Implemented as |
| --- | --- | --- |
| 1 | Pashto types | With the current toolchain the Qt tooltips already put the four type words in Pashto (`ریاضي.توان(x: اعشاري، y: اعشاري): اعشاري`), and a sweep of every Pashto built-in and every Pashto sample found no English type word. The English words still left were the library's **enum types** (`os.platform(): Platform`), now `پلیټفارم`. A stale toolchain gives different output: the `build/release` thunderc of the main checkout dates from 2026-11-10. The sweep is now a test in both suites. Two real Pashto faults turned up on the way and were fixed: a built-in typed with Arabic-keyboard letters (`كرښه چاپ`, the testbench's spelling) was explained as `چاپ` (`print`) because it was looked up unfolded, and the **second word** of a module's member of several words (`کړه` in `متن.پاک کړه`) showed nothing. **VS Code** had a real gap: `localiseTypes` knew `int float bool string func` but not **`byte`** (`بایټونه.عدد کړه(data: byte[]...)`) or the enum types. Its hover of a name the program declares also wrote `var`/`const`/`struct` and `,` in English. All are now written in the program's language; extension **0.8.1**. |
| 2 | The other language's name | `HoverDoc.translations`, rendered under a rule at the bottom of the tooltip as `پښتو: کرښه چاپ` or `English: math.pow`, each part in its own direction. A **built-in** is matched to its twin by its canonical name in the other language's `--language-info`. A declaration of the **SDK** is matched to its twin in the other language's copy of the file (`lib/thunder/en` and `ps` are written declaration for declaration, and a test keeps them in step), by its position among declarations of its kind: `Stack.push` <-> `ډېرۍ.ورزیات_کړه`. A name the program declares itself is the same in every language and gets no line. |
| 3 | A shared declaration scanner | New `core/declarations.py`: every declaration of a text (function, method, constructor, struct, interface, enum and its members, module, variable, constant, field) with its owner, module, parameters, declared type, initialiser, whether it is local, and where a function's or module's body ends. It works line by line on the indentation, with Arabic-keyboard letters folded as the compiler folds them. The hover help now uses it (its own regex loop is gone) and so does completion. |
| 4 | IntelliSense | New `core/completion.py`, the same contexts as the VS Code provider. After a letter: the parameters and locals in scope, the struct's own members inside its methods, the program's and its imports' declarations (privates of imports left out), the built-ins, modules and keywords. After a dot: a built-in module's functions and enums, an enum's members plus `values`/`count` (in the language's words), a module block's public members, or the fields and methods of the struct a variable holds, including its mixins'. The struct is found from the declared type (`var s: Stack<int>`), the initialiser (`var b = Button("OK")`), a parameter's type or a field chain (`a.b.`). After `:` the types come first. After `import` come the modules that need importing and the program's module files (quotes added). Matching is by prefix, folded and case-blind; a name of several words matches from any of its words, and `کرښه چ` completes over both words. **Signature help**: inside a call's brackets the called function's signature, with the current parameter underlined (the last one for variadics), and its description. |
| 5 | The Qt widgets | New `qt/completion_popup.py`. A list with a kind mark per item and a details panel (signature, doc comment, "from datastructures.th"), plus a yellow parameter hint above the caret. Both are child widgets of the editor's viewport, so they never take the focus, need no window manager (Wayland, offscreen tests) and are placed through the display layout's caret position, so they follow the caret in right-to-left programs. They are mirrored there. Keys: letters and `.` open or narrow the list; `Ctrl+Space` asks at any time; `Up`/`Down`/`PgUp`/`PgDn` choose; `Enter`/`Tab` take; `Escape` closes the list, then the hint. A chosen function brings `()` with the caret inside (no second bracket when one follows); a click takes an item. The list closes when the caret leaves the word, the view scrolls, the editor loses focus or a word is complete. |
| 6 | Settings | **Settings > IntelliSense (completion while typing)** (`completion`, Pashto `IntelliSense (د لیکلو پر مهال بشپړونه)`) beside **API help on hover**; both default to **off**, also when absent from an existing settings file, and both are persisted. The seam is one new `EditorView` method, `set_completion_provider(CompletionProvider or None)` (two callables: complete, signature). |

### The question: New Project in VS Code

The command is **Thunder: New Project...** (`thunder.newProject`). It is reachable from:
the Command Palette (`Ctrl+Shift+P`, type "Thunder: New Project"); the **Thunder** view in the
activity bar, either as the "New Thunder Project" button on the Project view's welcome page (shown
while the folder has no project) or as the new-folder icon in the Project view's title bar; and
**File > New File...** (the file-type chooser lists it). It asks for the folder (the open folder
when there is just one), the project name and the language.

### Decisions taken without asking (please review)

* **Parameter names stay English** in Pashto built-in signatures (`x`, `value`, `path`): the
  compiler's reference has no Pashto parameter names, and inventing them in the IDE would mean
  maintaining a second dictionary. The types, the function keyword, the enum types and the
  comma are Pashto.
* **The tooltip names the other language only where a twin really exists**: built-ins and the
  SDK. A program's own `twice` stays `twice` in a translated program, so a line saying so would
  be noise.
* **A finished word closes the list** (typing `println` completely leaves no list open), so
  `Enter` after a complete keyword is a new line, not an accepted completion; `Ctrl+Space` shows
  the list even then.
* **An unknown expression before a dot offers nothing** rather than every member of every
  struct; a wrong list teaches the wrong thing.
* **No Pashto label for "IntelliSense"**: the menu entry reads
  `IntelliSense (د لیکلو پر مهال بشپړونه)`, with the name kept as the recognisable product word.
  Draft for a native speaker, as always.

### Tests

New `tests/python/test_completion.py` (no toolchain or GUI): declarations (every kind, parameters,
locals, function bodies, folded letters, the SDK twins lining up); completion (built-ins,
keywords, explicit vs typed, finished words, strings/comments/numbers, struct members and mixins,
privacy inside and outside the struct, members bare inside a method, parameters and locals,
initialiser types, the real SDK's Stack with its doc comment and file, modules, enums, types after
`:`, imports, Pashto incl. multi-word names and module members of several words, Arabic-keyboard
letters, choosing an item); signature help (finding the call, the marked parameter in both
languages, declared functions, variadics). `test_hoverdocs.py`: the other language's names
(built-ins both ways, the SDK's twins both ways, none for the program's own), a sweep of every
Pashto built-in for English type words, the enum types, Arabic-keyboard letters. Controller (fake
UI): both settings off by default; translations at the bottom; the Pashto sweep through the
controller; completion, module files after `import`, the parameter hint, the toggle and its
persistence. Qt (offscreen, real key events, Pashto letters included): typing opens the list and
Enter takes the chosen item with the caret between the brackets and the hint shown, a dot and
narrowing, Escape, Tab and Ctrl+Space, a right-to-left Pashto program, and the Settings menu
turning it off (Enter is a new line again). VS Code: a sweep of every Pashto signature for English
type words; a Pashto program's own names hovered in Pashto.
