# The testing reference

`import "testing"` brings a small test framework into a program, as two modules: **`test`**
runs the tests and **`expect`** says what should be true. Write each test as a function, hand
it to `test.run()` with a name, state what should be true with `expect.equal()` and its
companions, and end with `test.finish()`. Tests that belong together go into a module of their
own, which makes them a **test suite**. Nothing in the file is native - it is ordinary Thunder
in [`lib/thunder/en/testing.th`](../lib/thunder/en/testing.th), written to be read. The Pashto
file ([`lib/thunder/ps/testing.th`](../lib/thunder/ps/testing.th)) is the same framework with
Pashto names; both are found automatically, next to the toolchain.

**Try it:** [`samples/en/28_testing.th`](../samples/en/28_testing.th) tests two small
functions in two suites (Pashto: [`samples/ps/28_testing.th`](../samples/ps/28_testing.th));
it is in the IDE's sample picker as *28 - Testing*.

## A test file

```thunder
import "testing"

# The code under test.
func largest(values: int[]): int
    var best = values[0]
    for value in values
        if value > best
            best = value
    return best

func is_palindrome(word: string): bool
    var reversed = ""
    for var i = len(word) - 1, i >= 0, i = i - 1
        reversed = reversed + word[i]
    return reversed == word

# The tests. Each is a plain function with no parameters; each module is one suite.
module numbers
    func largest_of_a_list()
        expect.equal(largest([3, 9, 2]), 9)
        expect.equal(largest([-5]), -5)

    func decimals_are_compared_with_a_tolerance()
        expect.near(1.0 / 3.0, 0.3333, 0.001)
        expect.that(len("abc") == 3, "a message of your own")

module text
    func palindromes()
        expect.is_true(is_palindrome("level"))
        expect.is_false(is_palindrome("thunder"))

    func arrays_compare_by_contents()
        var squares: int[] = []
        for n in [1, 2, 3]
            push(squares, n * n)
        expect.equal(squares, [1, 4, 9])
        expect.not_equal(squares, [1, 2, 3])

test.run("largest of a list", numbers.largest_of_a_list)
test.run("decimals are compared with a tolerance", numbers.decimals_are_compared_with_a_tolerance)
test.run("palindromes", text.palindromes)
test.run("arrays compare by contents", text.arrays_compare_by_contents)
test.finish()
```

Running it prints each suite's name, one line per test under it, and a summary - each suite's
counts, then the totals:

```
numbers
  ✓ largest of a list
  ✓ decimals are compared with a tolerance
text
  ✓ palindromes
  ✓ arrays compare by contents
numbers: 2 passed, 0 failed
text: 2 passed, 0 failed
4 passed, 0 failed
```

A test whose expectation does not hold prints what went wrong instead, and the summary ends
the program with **exit status 1**, so a script - or an editor - can tell. With a mistake in
`largest` (`<` where `>` belongs):

```
numbers
  ✗ largest of a list: expected 9, got 2
  ✓ decimals are compared with a tolerance
text
  ✓ palindromes
  ✓ arrays compare by contents
numbers: 1 passed, 1 failed
text: 2 passed, 0 failed
3 passed, 1 failed
```

A test is any function without parameters; `test.run()` runs it at once, under the name given,
so the tests run in the order they are written. A test function does not have to be in a
module: `test.run("adds up", adds_up)` with a top-level `func adds_up()` prints `✓ adds up`
with no suite above it and no indentation.

## The functions

The module `test`:

| Function | What it does |
| --- | --- |
| `test.run(name: string, body: func())` | Runs the function `body` as the test called `name`: `✓ name` if every expectation in it held, `✗ name: message` for each that did not. When `body` belongs to a module, the test belongs to that suite. |
| `test.fail(message: string)` | Fails the current test with a message of your own. |
| `test.finish()` | Prints each suite's `suite: N passed, M failed` (when there is more than one suite), then `N passed, M failed`, and ends the program with exit status 1 when any test failed. Call it last. |
| `test.suite_of(body: func()): string` | The suite a test function belongs to: the name of the module it is declared in, `""` for a function outside any module. |

The module `expect`:

| Function | What it does |
| --- | --- |
| `expect.that(condition: bool, message: string)` | Fails the current test with `message` when `condition` is false. The others are written with it. |
| `expect.is_true(condition: bool)` | Fails unless `condition` is true. |
| `expect.is_false(condition: bool)` | Fails unless `condition` is false. |
| `expect.equal<T>(actual: T, expected: T)` | Fails unless the two print the same - so arrays, tuples, maps and structs compare by their contents; the message shows both: `expected [1, 4, 9], got [1, 4, 8]`. |
| `expect.not_equal<T>(actual: T, unexpected: T)` | Fails when the two print the same. |
| `expect.near(actual: float, expected: float, tolerance: float)` | For decimals, which rarely compare exactly: fails unless `actual` is within `tolerance` of `expected`. |

Every failed expectation prints its own `✗` line, so a test with three wrong expectations
reports all three; the test counts as failed once.

`test` and `expect` are module names, so in a program that imports `"testing"` they cannot be
the names of variables or functions (error `S065`).

## Test suites

A suite is a **module of tests**. Put the tests that belong together in a module, and hand
them to `test.run` with the module's name in front - `test.run("palindromes",
text.palindromes)`. The framework asks the function which module it was declared in (it uses
`name_of`, which gives `"text.palindromes"`), and that module is the test's suite:

* **its name is printed above its tests**, which are indented by two spaces; run a suite's
  tests one after the other, or its name is printed again when it comes back;
* **its counts are kept apart**: when the program has more than one suite, `test.finish()`
  prints `suite: N passed, M failed` for each, in the order they first ran, before the totals;
* **it can be run on its own**, by naming it on the command line (next section).

The test's own name stays the one given to `test.run`; the suite only groups it. Tests in a
module may use the module's private helpers (`_name`) and its variables, as any member may.

```thunder
import "testing"

module arithmetic
    func adds_up()
        expect.equal(2 + 2, 4)

    func divides()
        if 7 / 2 != 3
            test.fail("whole numbers should round toward zero")

test.run("adds up", arithmetic.adds_up)     # prints "arithmetic" above "  ✓ adds up"
test.run("divides", arithmetic.divides)
test.finish()
```

## Running some of the tests

The program's arguments choose the tests, and the others are not even started. With no
arguments every test runs. An argument may be:

| Argument | Runs |
| --- | --- |
| a test's name: `"largest of a list"` | every test of that name, in whichever suite |
| a suite's name: `text` | every test of that suite |
| `suite/test`: `"numbers/largest of a list"` | that one test of that one suite |

Several arguments, several choices:

```
$ thundervm 28_testing.thc text
text
  ✓ palindromes
  ✓ arrays compare by contents
2 passed, 0 failed
$ thundervm 28_testing.thc "numbers/largest of a list" palindromes
numbers
  ✓ largest of a list
text
  ✓ palindromes
numbers: 1 passed, 0 failed
text: 1 passed, 0 failed
2 passed, 0 failed
```

This is how VS Code runs a single test or a single suite from its Test Explorer (below), and
how a script re-runs the one that failed.

## What it cannot do

The framework is about a hundred lines of Thunder, and it stays small on purpose.

* **Suites, yes; nesting, no.** A suite is one module, and modules do not nest, so there are
  no suites inside suites. There is no set-up or tear-down step either: call a function of
  your own at the start of each test.
* **No catching errors.** There are no exceptions in Thunder: a runtime error inside a test
  (indexing past the end, dividing by zero) stops the whole program with the usual report, as
  it would anywhere else - the tests after it do not run. A test therefore cannot check that
  something *fails*; test the condition that would make it fail instead (`expect.is_true(len(values) > 0)`).
* **No mocks.** There is no way to replace a function, a file or a socket for the length of a
  test. Pass the function under test what it needs - a function value, the text instead of
  the file - so that a test can hand it something simple.

## In the IDE and in VS Code

* **Thunder IDE:** Run the file. The suites and the ✓ and ✗ lines appear in the Output panel;
  the status bar shows the exit status.
* **VS Code:** a file that imports `"testing"` appears in the **Test Explorer** (the beaker in
  the activity bar) as a tree: **file → suite → test**, with one entry per `test.run(...)`
  (a test outside any module hangs straight from its file). The editor shows a run button in
  the gutter beside each `test.run`. Run one test, a suite, a file or everything; failures
  show their message inline and in the Test Results view. The **Run Tests in This File**
  button on the editor's title bar (and in both editors' context menus, the right-to-left
  editor's included) runs the file in front; **Thunder: Run All Tests** runs every test file of
  the workspace. The *Debug* profile runs the chosen tests under the debugger, breakpoints and
  all.
* **Command line:** `thunderc tests.th && thundervm tests.thc`; the exit status says whether
  everything passed.

## In Pashto

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| module `test` | `ازموینه` | | module `expect` | `تمه` |
| `test.run` | `ازموینه.وچلوه` | | `expect.that` | `تمه.دا_وي` |
| `test.fail` | `ازموینه.ناکامه_کړه` | | `expect.is_true` | `تمه.رښتیا_وي` |
| `test.finish` | `ازموینه.پای_ته_ورسوه` | | `expect.is_false` | `تمه.درواغ_وي` |
| `test.suite_of` | `ازموینه.د_ټولګي_نوم` | | `expect.equal` | `تمه.برابر` |
| | | | `expect.not_equal` | `تمه.نابرابر` |
| | | | `expect.near` | `تمه.نږدې` |

The Pashto file prints its lines in Pashto (`۲ تېرې شوې، ۰ ناکامې`), with the same ✓ and ✗
marks and the suite's name above its tests, so the VS Code Test Explorer reads both alike.

**Before 0.7** these were plain functions: `test` (now `test.run`), `expect` (`expect.that`),
`expect_true`, `expect_false`, `expect_equal`, `expect_not_equal`, `expect_near`
(`expect.is_true` ... `expect.near`), `fail` (`test.fail`) and `finish_tests` (`test.finish`).
