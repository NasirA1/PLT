# DLang Educational Toolchain: Codebase & Environment Analysis

*Date: 2026-11-04. Analysed at commit `0f895e6`, branch `hfi/…/elm`.*

This document covers the existing `dlang` codebase and the build environment, and gives recommendations
for turning DLang into a cross-platform teaching toolchain (IDE + compiler + VM) for
Programming Language Technology (PLT) students. No code has been written. Section 13 lists the
decisions we need to make before implementation starts.

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
