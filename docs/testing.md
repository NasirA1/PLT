# The testing reference

`import "testing"` brings a small test framework into a program: write each test as a
function, hand it to `test()` with a name, state what should be true with `expect_equal()`
and its companions, and end with `finish_tests()`. Nothing in the module is native - it is
ordinary Thunder in [`lib/thunder/en/testing.th`](../lib/thunder/en/testing.th), written to be
read. The Pashto module ([`lib/thunder/ps/testing.th`](../lib/thunder/ps/testing.th)) is the
same framework with Pashto names; both are found automatically, next to the toolchain.

**Try it:** [`samples/en/37_testing.th`](../samples/en/37_testing.th) tests two small
functions (Pashto: [`samples/ps/37_testing.th`](../samples/ps/37_testing.th)); it is in the
IDE's sample picker as *37 - Testing*.

## A test file

```thunder
import "testing"

func largest(values: int[]): int
    var best = values[0]
    for value in values
        if value > best
            best = value
    return best

func largest_of_a_list()
    expect_equal(largest([3, 9, 2]), 9)
    expect_equal(largest([-5]), -5)

func an_empty_list_has_no_largest()
    expect_true(true)   # (indexing an empty list is a runtime error: see below)

test("largest of a list", largest_of_a_list)
test("an empty list has no largest", an_empty_list_has_no_largest)
finish_tests()
```

Running it prints one line per test and a summary:

```
✓ largest of a list
✓ an empty list has no largest
2 passed, 0 failed
```

A test whose expectation does not hold prints what went wrong instead, and the summary ends
the program with **exit status 1**, so a script - or an editor - can tell:

```
✗ largest of a list: expected 9, got 3
✓ an empty list has no largest
1 passed, 1 failed
```

A test is any function without parameters; `test()` runs it at once, under the name given,
so the tests run in the order they are written. There are no exceptions in Thunder: a
runtime error inside a test (indexing past the end, dividing by zero) stops the whole program
with the usual report, as it would anywhere else - the tests after it do not run.

## The functions

| Function | What it does |
| --- | --- |
| `test(name: string, body: func())` | Runs the function `body` as the test called `name`: `✓ name` if every expectation in it held, `✗ name: message` for each that did not. |
| `expect(condition: bool, message: string)` | Fails the current test with `message` when `condition` is false. The others are written with it. |
| `expect_true(condition: bool)` | Fails unless `condition` is true. |
| `expect_false(condition: bool)` | Fails unless `condition` is false. |
| `expect_equal<T>(actual: T, expected: T)` | Fails unless the two print the same - so arrays, tuples, maps and structs compare by their contents; the message shows both: `expected [1, 4, 9], got [1, 4, 8]`. |
| `expect_not_equal<T>(actual: T, unexpected: T)` | Fails when the two print the same. |
| `expect_near(actual: float, expected: float, tolerance: float)` | For decimals, which rarely compare exactly: fails unless `actual` is within `tolerance` of `expected`. |
| `fail(message: string)` | Fails the current test with a message of your own. |
| `finish_tests()` | Prints `N passed, M failed` and ends the program with exit status 1 when `M > 0`. Call it last. |

Every failed expectation prints its own `✗` line, so a test with three wrong expectations
reports all three; the test counts as failed once.

## Running some of the tests

The program's arguments choose the tests: `thundervm tests.thc "largest of a list"` runs only
the tests named on the command line (several names, several tests), and the others are not
even started. With no arguments every test runs. This is how VS Code runs a single test
from its Test Explorer (below), and how a script re-runs the one that failed.

## In the IDE and in VS Code

* **Thunder IDE:** Run the file. The ✓ and ✗ lines appear in the Output panel; the status bar
  shows the exit status.
* **VS Code:** a file that imports `"testing"` appears in the **Test Explorer** (the beaker in
  the activity bar) with one entry per `test(...)`, and the editor shows a run button in the
  gutter beside each one. Run one test, a file or everything; failures show their message
  inline and in the Test Results view. The **Run Tests in This File** button on the editor's
  title bar (and in both editors' context menus, the right-to-left editor's included) runs the
  file in front; **Thunder: Run All Tests** runs every test file of the workspace. The *Debug*
  profile runs the chosen tests under the debugger, breakpoints and all.
* **Command line:** `thunderc tests.th && thundervm tests.thc`; the exit status says whether
  everything passed.

## In Pashto

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `test` | `ازموینه` | | `expect` | `تمه` |
| `expect_true` | `رښتیا_تمه` | | `expect_false` | `درواغ_تمه` |
| `expect_equal` | `برابر_تمه` | | `expect_not_equal` | `نابرابر_تمه` |
| `expect_near` | `نږدې_تمه` | | `fail` | `ناکامه_کړه` |
| `finish_tests` | `ازموینې_پای_کړه` | | | |

The Pashto module prints its lines in Pashto (`۲ تېرې شوې، ۰ ناکامې`), with the same ✓ and ✗
marks, so the VS Code Test Explorer reads both alike.
