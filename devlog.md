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
