# Test decorators: making the testing SDK friendly

*Exploration report, 2026-11-17. A design and an estimate; no code was written. Companion to
`concurrency_report.md`.*

## Summary

Today a test is written twice: the function, then a `test.run("name", module.function)` line
to register it, then `test.finish()`. The proposal is an **annotation** written on the line
above a declaration:

```thunder
import "testing"

@test("adds up")
func adds_up()
    expect.equal(2 + 2, 4)

@test                       # a whole module: every function in it is a test, the module is the suite
module text
    func joins()            # named "joins" in the report
        expect.equal("a" + "b", "ab")
```

Running the file runs the tests; nothing else needs to be written. In Pashto the same line reads
`@ازموینه("جمع سمه ده")`. The recommended way to build it is **a general annotation syntax in the
compiler** (`@name(arguments)` before a declaration), with `@test`, `@skip` and `@before_each` /
`@after_each` as the first annotations, and the test harness itself staying the readable Thunder
of `lib/thunder/<lang>/testing.th`: the compiler only generates the `test.run(...)` calls the
programmer no longer writes. Annotations get a token kind of their own, coloured orange in the Qt
IDE, the right-to-left editor and VS Code, and the Test Explorer reads them from the compiler's
report instead of matching lines by pattern.

Estimate: **one iteration of the size of the enum one** - roughly two thirds of it in the
compiler and the SDK, one third in the three editors and the documentation. The main pitfalls
are listed in §6: the right-to-left rendering of `@` before a Pashto word, what to do with a file
that has both tests and a program, and that generic functions cannot be tests.

---

## 1. What is wrong today

```thunder
import "testing"

module numbers
    func largest_of_a_list()
        expect.equal(largest([3, 9, 2]), 9)

test.run("largest of a list", numbers.largest_of_a_list)   # once per test, by hand
test.finish()                                              # and this, last
```

* Every test is named twice: in its `func` and again as the string in `test.run`.
* Forgetting a `test.run` line silently drops a test; forgetting `test.finish()` silently drops
  the summary and the exit status.
* The registration lines live far from the tests, so the Test Explorer has to match
  `test.run(` lines by regular expression to find out where a test *is*.
* A test is told apart from a helper only by whether someone registered it.

The one good thing to keep: the harness is ordinary Thunder (`testing.th`), readable by a
student, and `test.run` / `expect.*` are plain functions. The decorator should generate what the
programmer writes today, not replace the harness with C++.

## 2. The options

| | Shape | Pros | Cons |
| --- | --- | --- | --- |
| **A** | **Annotations** `@test("name")` on a function or a module (recommended) | Reads like Python, Java, C#; no new statement form; extends to `@skip`, `@before_each`; the compiler knows exactly which declarations are tests, so the editors do too | A new lexical token and a new slot in the grammar; the compiler must generate the registration |
| **B** | A `test` keyword: `test "adds up"` followed by a block (Zig, Nim, Rust's `#[test]` is really A) | Shortest; no function needed | A new keyword that cannot then be a module name (`test.run` today); a test is no longer a function one can also call; nothing generalises to other annotations |
| **C** | Naming convention: functions called `test_*` (or in a module called `*_tests`) run under `thundervm --test` (Go, pytest) | No syntax at all | Invisible rule; Pashto needs its own prefix; names cannot carry a sentence as description; a `--test` mode is a second way to run a program |
| **D** | Reflection: `test.run_all(module)` with a built-in that lists a module's functions | Smallest compiler change | A new kind of value ("a module") or a magic built-in; still one line to write; nothing for the editors to see |

**A** is the one that reads naturally in both languages, keeps the harness in Thunder, and gives
the toolchain real knowledge of where the tests are. B and D are mentioned because they are the
usual alternatives; C is what the request explicitly wants to avoid.

## 3. The design (option A)

### 3.1 Syntax

```
annotation   = "@" name [ "(" arguments ")" ] NEWLINE
declaration  = { annotation } ( func_decl | module_decl )
```

* `@test("description")` or `@test` (the description is then the function's name with the
  underscores turned into spaces: `largest_of_a_list` -> "largest of a list").
* On a **function**: the function is a test. It must take no parameters and return nothing
  (S0xx otherwise), and may not be generic.
* On a **module**: every function of the module that takes no parameters and returns nothing is
  a test; the module is the suite (as now); functions with parameters are helpers and are left
  alone; `_private` functions are helpers too. A function inside such a module may carry its own
  `@test("better description")` or `@skip`.
* **Pashto**: `@ازموینه("جمع سمه ده")`. The request spells it آزموینه (with alef madda); the SDK
  module is ازموینه. Letter folding does not unify آ and ا, so both spellings should be
  registered, as the pack does for other words with two accepted spellings.
* `@` is free: today it is lexical error L001 ("unexpected character '@'"), so no program uses it.

### 3.2 Meaning: what the compiler generates

An annotation is **declarative sugar for the existing harness**. For a file with tests the
analyser appends, after the program's own top-level statements and in declaration order:

```thunder
test.run("adds up", adds_up)
test.run("joins", text.joins)
test.finish()
```

- `import "testing"` is added for the program when any `@test` is present (a test file should not
  have to say it; writing it anyway is harmless).
- `@skip("reason")` on a test prints `- name (skipped: reason)` and counts it as skipped
  (`test.finish()` gains a skipped count).
- `@before_each` / `@after_each` on a parameterless function of a module: the harness calls it
  before / after each test of that module (`test.run` gains two optional function arguments,
  passed by the generated code; the harness stays Thunder).
- Hand-written `test.run(...)` lines keep working, so existing test files need no change, and the
  two styles mix.

Because the compiler knows the tests, two things come free:
* **`thunderc --no-tests`** strips annotated functions and the generated calls, so a program can
  carry its tests in the same file and still be packaged without them.
* The **JSON report** lists the tests (`"tests": [{name, suite, function, line, skip}]`): the
  editors stop guessing from regular expressions.

### 3.3 A file that is both a program and a test file

Three reasonable rules; the first is recommended:
1. **Tests run after the program's top-level statements** (and after `main()`), and the exit
   status is the tests'. Simple, predictable, and a lesson file can show the code and its tests
   together.
2. `thundervm --test` runs only the tests; a plain run ignores them. Two ways to run a file.
3. A file with tests may have no other statements (S0xx). Strict, but it forbids the
   "lesson plus its tests" file.

### 3.4 Highlighting

A new token kind `Annotation` (the `@` and the name, one token) with a category of its own, so
every editor can colour it without knowing the names:

| Where | Change |
| --- | --- |
| Qt IDE (`core/highlight.py`, `qt/style.py`) | tag `annotation`, colour `#c75000` (orange) in `SYNTAX_COLORS`; the palette is duplicated in `tools/apidoc.py` and a test pins the two equal |
| Right-to-left editor (`media/rtleditor/model.js`, `.css`) | class `k-annotation`, orange via `--vscode-charts-orange` |
| VS Code grammar (`generate_assets.py`) | scope `entity.name.function.decorator.thunder` (themes colour it like Python decorators); semantic token type `decorator` from the compiler's token stream |
| API site | the same palette, so documented examples match the IDE |

### 3.5 Test Explorer and the IDEs

* Discovery from the report's `tests` array (and, while a file does not parse, from the last
  good report) - file -> suite -> test as now, with `@skip` shown as skipped.
* The gutter play button and "Run Tests in This File" work unchanged; a single test still runs
  as `suite/name` through the program's arguments.
* The Qt IDE needs nothing new to *run* tests (a test file is a program), but its Symbols tab can
  mark test functions, and a "Run tests" command could filter the Output panel to the ✓/✗ lines.

### 3.6 Translation

`@test` <-> `@ازموینه` is a token the language pack owns, like a keyword: `--translate` maps it,
and the description string is translated as any string is (not at all). The hovers in VS Code
show the counterpart in the other language, as they do for keywords.

## 4. Other ideas worth considering

* **`@test` on a struct's module** is not needed - tests are functions - but a **`@suite("Numbers")`**
  on a module would let the suite's display name differ from the module's identifier.
* **Parameterised tests** (`@test("squares", [1, 2, 3])`, the function taking the value) are
  attractive but need the harness to call `f(value)` for each value, which without generics over
  function types means one `test.run_each<T>` per arity. Possible; leave for later.
* **`@expect_error("R001")`** - a test that passes when the body fails with a runtime error -
  needs the VM to *catch* an error inside a call, which Thunder cannot do today (the concurrency
  report's `try(f): bool` idea would unlock it). Not for the first version.
* **`@timeout(ms)`** would need a per-call step budget in the VM; cheap to add later.
* **General annotations** beyond tests, once the syntax exists: `@deprecated("use x")` (a
  warning at each use), `@main` instead of the magic name, `@native("socket._send")` to bind a
  native under a different name. None is urgent; the syntax should merely not rule them out.

## 5. Effort

| Part | Work | Size |
| --- | --- | --- |
| Lexer, token kind, language packs, `--language-info` | `@name` token, `Annotation` category, `test`/`ازموینه` (+ آزموینه) and `skip`, `before_each`, `after_each` spellings | small |
| Parser and AST | annotations collected before `func` and `module`; an orphan annotation (nothing follows) is a syntax error | small |
| Sema | known annotations only (S0xx unknown), argument checks, "a test takes no parameters and returns nothing", not generic, not on a native; module-level expansion; generating the `test.run` calls and the auto-import; `--no-tests` | medium |
| SDK `testing.th` (both languages) | skipped count, before/after hooks, unchanged `test.run` | small |
| Highlighting | Qt IDE, right-to-left page, TextMate + semantic tokens, API site palette, direction algorithm (see pitfalls) | medium |
| VS Code Test Explorer | discovery from the report; `@skip` state; tests for discovery and runs | small-medium |
| Documentation and samples | `docs/testing.md` + Pashto, language reference (annotations section, grammar, keyword tables), sample 28 rewritten with `@test` (goldens regenerated), the API site | medium |
| Tests | lexer/parser/sema/translation/golden in C++, Python for the Qt highlighter and the report, JS for the explorer and grammar | medium |

Altogether about the size of the enum iteration (§47) and smaller than the modules one: the
compiler part is contained (one token, one AST slot, one generated statement list), and the
rest is the usual fan-out across two languages and three editors.

## 6. Limitations and pitfalls

1. **`@` before a right-to-left word.** The token-level direction algorithm (`core/direction.py`
   and its JavaScript twin, compared line for line by a test) isolates each token. `@ازموینه` as
   one token keeps the `@` attached on the correct side; splitting `@` from the name would let
   the bidirectional algorithm float it to the wrong end of the line. Both implementations need
   the new token class, and the comparison test will catch a divergence.
2. **Where tests run** (§3.3). Rule 1 is recommended; whichever is chosen must be documented, and
   `os.exit` in the program's own statements would end the run before the tests.
3. **Generic functions cannot be tests** (a template has no code until it is instantiated);
   S0xx says so. `@test` on a `native func` is an error too.
4. **Names.** A module-level `@test` derives names from function names; two tests cannot share a
   name within a suite (the harness selects by name), so a duplicate is a compile error.
5. **Desugaring order.** The generated calls go after the program's statements and after `main()`
   is called, so globals and module variables are initialised; a test that depends on another
   test's side effects is the student's problem, as in every framework - the docs should say
   tests run in declaration order.
6. **Error recovery.** An annotation followed by a `var` or a statement, or at the end of the
   file, must give one clear syntax error and skip nothing else.
7. **The old API stays.** Existing test files (`test.run`, sample 28's current form) must keep
   passing; the decorator is sugar for them, and the report's `tests` array should list hand-
   registered tests too (the explorer can still fall back on the `test.run(` pattern for those).
8. **Packaging**: `thunderc --package` of a test file should warn, or `--no-tests` should be the
   default for packaging, so a shipped program does not run its tests on start.
9. **Bytecode**: nothing changes in the format - the generated calls are ordinary calls - unless
   the report's `tests` list is also wanted in the `.thc` file; it is not needed for anything
   yet.
10. **Pashto spelling**: the request's `آزموینه` versus the SDK's `ازموینه`; registering both
    avoids a trap, but the preferred one (written by the translator) should be chosen by a native
    speaker.

## 7. Decisions

1. Option A (annotations), as recommended?
2. Rule for a file that is both program and tests: tests after the program (recommended), a
   `--test` mode, or forbidden?
3. First set of annotations: `@test`, `@skip`, `@before_each`, `@after_each` - or `@test` only?
4. Auto-import of `"testing"` when `@test` is present (recommended), or require the import?
5. Pashto spelling to write: `ازموینه` (the SDK's) or `آزموینه` (the request's)?
