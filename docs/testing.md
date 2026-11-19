# The testing reference

Thunder keeps tests in **test files** of their own. Mark each test with the annotation
**`@test`** - a function, or a whole struct or module, which is then a **test suite** - state
what should be true with `expect.equal()` and its companions, and run the file: the compiler
finds the tests, brings in the test framework and runs them one after another, then prints a
summary. A struct marked **`@mock`** stands in for an interface and records how it was used. The
framework is two modules: **`test`** runs the tests and **`expect`** says what should be true.
Nothing in it is native - it is ordinary Thunder in
[`lib/thunder/en/testing.th`](../lib/thunder/en/testing.th), written to be read. The Pashto file
([`lib/thunder/ps/testing.th`](../lib/thunder/ps/testing.th)) is the same framework with Pashto
names; both are found automatically, next to the toolchain.

**Try it:** [`samples/en/28_testing.th`](../samples/en/28_testing.th) tests the code of
[`samples/en/lib/scores.th`](../samples/en/lib/scores.th) with single tests, a module suite, a
struct suite and two mocks (Pashto: [`samples/ps/28_testing.th`](../samples/ps/28_testing.th));
it is in the IDE's sample picker as *28 - Testing*.

## A test file

The code under test lives in a library file, which a program and a test file can both import:

```thunder
# lib/shop.th - the code under test
module shop
    func largest(values: int[]): int
        var best = values[0]
        for value in values
            if value > best
                best = value
        return best

    func is_palindrome(word: string): bool
        return string.reverse(word) == word

    # Where prices come from: a price list in the program, a stand-in in a test.
    interface Prices
        func price_of(item: string): int

    struct Basket
        items: string[]

        func add(item: string)
            push(items, item)

        func total(prices: Prices): int
            var sum = 0
            for item in items
                sum += prices.price_of(item)
            return sum
```

The test file imports it and declares the tests - nothing else:

```thunder
import "lib/shop"                       # the code under test

@test                                   # one test, called "largest of a list"
func largest_of_a_list()
    expect.equal(shop.largest([3, 9, 2]), 9)
    expect.equal(shop.largest([-5]), -5)

@test("decimals are compared with a tolerance")
func decimals()
    expect.near(1.0 / 3.0, 0.3333, 0.001)

@test                                   # a module is a suite: its public functions are the tests
module words
    func palindromes()
        expect.is_true(shop.is_palindrome("level"))
        expect.is_false(shop.is_palindrome("thunder"))

    func arrays_compare_by_contents()
        expect.equal(string.split("a,b", ","), ["a", "b"])

@test("a basket")                       # a struct is a suite: every test gets a new BasketTests
struct BasketTests
    _basket: shop.Basket

    BasketTests()                       # the fixture: runs before each test
        _basket = shop.Basket()
        _basket.add("tea")

    func starts_with_one_item()
        expect.equal(len(_basket.items), 1)

    func adds_an_item()
        _basket.add("milk")
        expect.equal(_basket.items, ["tea", "milk"])

    func close()                        # the teardown: runs after each test
        _basket.items = []

@mock                                   # a stand-in for the interface shop.Prices
struct FixedPrices: shop.Prices
    func price_of(item: string): int
        return 3

@test("a basket adds up its prices")
func totals()
    var basket = shop.Basket(["tea", "milk"])
    var prices = FixedPrices()
    expect.equal(basket.total(prices), 6)
    prices.expect_called("price_of", 2)
    expect.equal(prices.calls(), ["price_of(tea)", "price_of(milk)"])
```

There is no `import "testing"` and no call that runs the tests: the compiler adds both. Running
it (`thunderc shop_test.th && thundervm shop_test.thc`) prints one line per test - a suite's
name above its tests, which are indented - and a summary: each suite's counts, then the totals:

```
✓ largest of a list
✓ decimals are compared with a tolerance
words
  ✓ palindromes
  ✓ arrays compare by contents
a basket
  ✓ starts with one item
  ✓ adds an item
✓ a basket adds up its prices
words: 2 passed, 0 failed
a basket: 2 passed, 0 failed
7 passed, 0 failed
```

A test whose expectation does not hold prints what went wrong instead, and the summary ends
the program with **exit status 1**, so a script - or an editor - can tell. With a mistake in
`largest` (`<` where `>` belongs):

```
✗ largest of a list: expected 9, got 2
✓ decimals are compared with a tolerance
words
  ✓ palindromes
  ✓ arrays compare by contents
a basket
  ✓ starts with one item
  ✓ adds an item
✓ a basket adds up its prices
words: 2 passed, 0 failed
a basket: 2 passed, 0 failed
6 passed, 1 failed
```

The tests run in the order they are declared. Every failed expectation prints its own `✗`
line, so a test with three wrong expectations reports all three; the test counts as failed once.

## Marking tests: `@test`

An **annotation** is `@` and a name, optionally with a text in brackets, on the line (or lines)
before a declaration: `@test`, `@test("adds up")`. A comment may follow it on its line. The
language reference describes annotations in general (section 9, "Test files and annotations").
`@test` may mark four things:

| `@test` on | Makes | Named |
| --- | --- | --- |
| a function without parameters and without a result | one test | by the description, or by the function's name with each `_` turned into a space: `largest_of_a_list` is "largest of a list" |
| a struct | a **suite**: every public method (one not starting with `_`) without parameters and without a result, except `close`, is a test | the suite by the description or the struct's name; each test by its method's name, as above |
| a module | a **suite**: every public function without parameters and without a result is a test | the suite by the description or the module's name |
| one method of a struct, or one function of a module, that is not marked itself | just that one: a test of that suite | by its own description or name |

The description is the name the transcript shows, so it may be a whole phrase. Inside a marked
struct or module, a member may still have an `@test("...")` of its own, to give it a
description (`@test("adds twice") func doubles()` in the sample). Methods and functions that
take parameters, return a result or start with `_` are helpers: in a suite they are simply not
tests. Marked explicitly, a function that cannot run as a test is an error (`S087`), as is a
generic one (`S088`) and two tests of the same name in one suite (`S089`).

A test function outside any module or marked struct belongs to no suite: it prints `✓ name`
with no heading above it and no indentation.

### Suites made of structs: fixtures and teardown

Each test of a struct suite runs on a **new instance** of the struct, made by its
constructor. The constructor is therefore the **fixture** - the set-up every test starts from -
and no test sees what another one did: in the example `adds_an_item` adds to a basket of its
own, and `starts_with_one_item` still finds one item, whichever runs first. When the struct
has a method `close()` (Pashto `وتړه()`), it runs after each test, to tidy up - delete a file
the test wrote, close a connection. `close` is never a test itself. For each test the
compiler writes, in effect:

```
var suite = BasketTests()
suite.adds_an_item()
suite.close()
```

The suite is made without arguments, so its constructor may not take any (`S090`). Its fields
are the test's state; a private method (`_add_twice(amount: int)`) is a helper the tests share.

### Suites made of modules

A module marked `@test` is a suite of its public functions without parameters. Unlike a
struct's tests, they share the module's variables, which keep their values from one test to
the next, so a module suite suits tests that need no state of their own. A test in a module
may use the module's private helpers (`_name`), as any member may.

## Test files and program files

Tests and production code are kept apart, and the compiler checks it:

* **A file with any `@test` is a test file.** It holds declarations only - functions, structs,
  mixins, interfaces, enums, modules, variables, constants and imports - and **no top-level
  statements and no `main()`** (`S091`): the compiler writes the statements that run it.
* **The testing module comes by itself.** In a test file the compiler imports `"testing"`
  automatically (writing the import yourself is allowed, and changes nothing).
* **A production program may not use test code.** A program whose main file has no `@test` may
  not `import "testing"`, nor declare a `@mock` - in its own file or in any file it imports
  (`S092`).
* **A file with tests cannot be imported** (`S093`, reported at the `import`): tests are run,
  not used. The code under test therefore lives in library files, which the program and the
  test file both import.
* **Shared mocks** go into a helper file without tests - `lib/fakes.th` with a few `@mock`
  structs - which several test files may import (a program may not, by the rule above).

`test` and `expect` are module names, so in a test file they cannot be the names of variables
or functions (`S065`).

## Mocks: stand-ins for interfaces

Code that talks to the outside world - a clock, a price list, a file, a socket - is easiest to
test when it receives that world as an **interface** (`prices: Prices` above) rather than
reaching for it itself. A test then hands it a **mock**: a struct marked `@mock` that lists the
interfaces it stands in for.

```thunder
@mock
struct FixedPrices: shop.Prices          # answers 3 for every item
    func price_of(item: string): int
        return 3

@mock
struct NoPrices: shop.Prices             # no body: every method answers with a default
```

For a `@mock` struct the compiler includes the mixin **`test.Mock`** and makes every method of
the listed interfaces **record its call** - the method's name and its arguments as text, as
`str` shows them - and then answer: with the struct's own method of that name when it has one,
otherwise by doing nothing and returning the default value of its result type (`0`, `""`,
`false`, `nothing`, `[]`). A mock that writes no methods needs no body at all. A mock that lists
no interface stands in for nothing (`S094`).

A test asks the mock what happened:

| Method | Answers |
| --- | --- |
| `called(method: string): bool` | `true` if `method` was called at least once. |
| `call_count(method: string): int` | How many times `method` was called. |
| `arguments_of(method: string): string[][]` | The arguments of every call to `method`, in order, each as text: `[["tea"], ["milk"]]`. |
| `calls(): string[]` | Every call, in order, written like a call: `["load(k)", "save(k, 42)"]`. |
| `forget_calls()` | Forgets the calls recorded so far (to check only what happens next). |
| `expect_called(method: string, times: int)` | An expectation: fails the current test unless `method` was called exactly `times` times - `expected price_of to be called 2 time(s), but it was called 0 time(s)`. |

```thunder
@test
func a_mock_answers_with_defaults()
    var basket = shop.Basket(["tea"])
    var prices = NoPrices()
    expect.equal(basket.total(prices), 0)                     # price_of answered 0
    expect.equal(prices.arguments_of("price_of"), [["tea"]])
    prices.forget_calls()
    expect.is_false(prices.called("price_of"))
```

A mock may have fields of its own (`time: int`, set by `FakeClock(time: 9)`) for the test to
choose its answers. `@mock` belongs on a struct only (`S085`) and takes no description
(`S086`).

## The functions

The module `expect`:

| Function | What it does |
| --- | --- |
| `expect.that(condition: bool, message: string)` | Fails the current test with `message` when `condition` is false. The others are written with it. |
| `expect.is_true(condition: bool)` | Fails unless `condition` is true. |
| `expect.is_false(condition: bool)` | Fails unless `condition` is false. |
| `expect.equal<T>(actual: T, expected: T)` | Fails unless the two print the same - so arrays, tuples, maps and structs compare by their contents; the message shows both: `expected [1, 4, 9], got [1, 4, 8]`. |
| `expect.not_equal<T>(actual: T, unexpected: T)` | Fails when the two print the same. |
| `expect.near(actual: float, expected: float, tolerance: float)` | For decimals, which rarely compare exactly: fails unless `actual` is within `tolerance` of `expected`. |

The module `test`:

| Member | What it is |
| --- | --- |
| `test.fail(message: string)` | Fails the current test with a message of your own. |
| `test.run(suite: string, name: string, body: func())` | Runs the function `body` as the test `name` of `suite` (`""` for none): `✓ name` if every expectation in it held, `✗ name: message` for each that did not. The compiler writes one call per test. |
| `test.finish()` | Prints each suite's `suite: N passed, M failed` (when there is more than one suite), then `N passed, M failed`, and ends the program with exit status 1 when any test failed. The compiler writes the call, after the last test. |
| `test.runner` | The one `test.Runner` that `run`, `fail` and `finish` use. |
| `test.Runner`, `test.Result` | The harness itself (next section). |
| `test.Mock` | The mixin every `@mock` struct includes (previous section). |

## How it works

The harness is a small object model, which a curious reader can follow in
`lib/thunder/en/testing.th`:

* **`struct Result`** - what happened to one test: `suite`, `name` and `failures` (the messages
  of its failed expectations), and `passed()`, true when there are none.
* **`struct Runner`** - runs the tests and keeps their `results`. `wants(suite, name)` says
  whether the command line chose a test; `run(suite, name, body)` runs it, prints the suite's
  heading when the suite changes, and records a `Result`; `fail(message)` adds a failure to the
  test that is running and prints its `✗` line; `counts(suite)` answers how many of a suite's
  tests (`""`: of all) passed and failed; `report()` prints the summary and answers the number of
  failures.
* **`var runner = Runner(os.args())`** - the module's one runner, made with the program's
  arguments. `test.run`, `test.fail` and `test.finish` hand their work to it, and
  `test.finish()` ends the program with exit status 1 when `runner.report()` counted failures.

When it compiles a test file, the compiler imports `"testing"`, appends one
`test.run(suite, name, body)` per test in declaration order - with `body` the test function, or,
for a struct suite, a function that makes the instance, calls the method and calls `close()` -
and then `test.finish()`. `thunderc --emit ast` shows these statements.
`thunderc --json` reports the tests it found, which is how editors list them:

```
"tests": [{"suite": "", "name": "largest of a list", "function": "largest_of_a_list", "line": 3}, ...]
```

`function` is the qualified name of the test's function or method (`BasketTests.adds_an_item`)
and `line` the line of its `@test` (or, for a test implied by its suite, of its name).

## Running some of the tests

The program's arguments choose the tests, and the others are not even started. With no
arguments every test runs. An argument may be:

| Argument | Runs |
| --- | --- |
| a test's name: `"largest of a list"` | every test of that name, in whichever suite |
| a suite's name: `words` | every test of that suite |
| `suite/test`: `"a basket/adds an item"` | that one test of that one suite |

Several arguments, several choices:

```
$ thundervm shop_test.thc words
words
  ✓ palindromes
  ✓ arrays compare by contents
2 passed, 0 failed
$ thundervm shop_test.thc "a basket/adds an item" "largest of a list"
✓ largest of a list
a basket
  ✓ adds an item
2 passed, 0 failed
```

The tests still run in declaration order, whatever the order of the arguments. This is how
VS Code runs a single test or a single suite from its Test Explorer (below), and how a script
re-runs the one that failed.

## What it cannot do

The framework is a couple of hundred lines of Thunder, and it stays small on purpose.

* **Suites, yes; nesting, no.** A suite is one struct or one module, and neither nests in a
  suite, so there are no suites inside suites.
* **No catching errors.** There are no exceptions in Thunder: a runtime error inside a test
  (indexing past the end, dividing by zero) stops the whole program with the usual report, as
  it would anywhere else - the tests after it do not run. A test therefore cannot check that
  something *fails*; test the condition that would make it fail instead (`expect.is_true(len(values) > 0)`).
* **Mocks stand in for interfaces only.** A function, a built-in such as `file.read` or a module
  cannot be replaced for the length of a test. Give the code under test what it needs as an
  interface (or a function value, or the text instead of the file), so that a test can hand it
  a mock.

## Errors

| Code | Meaning |
| --- | --- |
| `P021` | an unknown annotation: `@tset` ("the annotations are @test, @mock") |
| `P022` | an annotation that no declaration follows (a statement, or the end of the file) |
| `S085` | an annotation on something it cannot mark: `@test` on a mixin, an interface, an enum or a `native func`, `@mock` on a function, a method or a module, `@test` and `@mock` on one struct |
| `S086` | an annotation with an argument it does not take (`@mock` takes none) |
| `S087` | a test that takes parameters or returns a result |
| `S088` | a generic function or struct marked as a test |
| `S089` | two tests of the same name in one suite |
| `S090` | a struct suite whose constructor takes parameters |
| `S091` | a statement, or a `main()`, in a test file |
| `S092` | test code in a production program: `import "testing"` or a `@mock` |
| `S093` | an import of a file that has tests |
| `S094` | a `@mock` struct that lists no interface |

## In the IDE and in VS Code

* **Thunder IDE:** Run the test file. The suites and the ✓ and ✗ lines appear in the Output
  panel - ✓ in green and ✗ in red, and in the summary the passed count green and a failed count
  red - and the status bar shows the exit status. Compiling happens in the background: a busy
  bar shows in the status bar, and Compile, Run, Debug, Translate, Package, New, Open, the
  sample list and the language list wait until it is done (editing goes on).
* **VS Code:** every file with `@test` (or `@آزموینه`) appears in the **Test Explorer** (the
  beaker in the activity bar) as a tree: **file → suite → test**, found as the compiler finds
  them - a test outside any suite hangs straight from its file. The editor shows a run button in
  the gutter beside each test. Run one test, a suite, a file or everything; the extension
  compiles the file and runs it with the chosen tests as arguments (`suite/name`, or the
  suite's name), and failures show their message inline and in the Test Results view. The
  **Run Tests in This File** button on the editor's title bar (and in both editors' context
  menus, the right-to-left editor's included) runs the file in front; **Thunder: Run All
  Tests** runs every test file of the workspace. The *Debug* profile runs the chosen tests under
  the debugger, breakpoints and all.
* **The right-to-left editor** (Pashto files) does the same as the text editor: a button in
  the gutter beside every test and every suite (on its `@آزموینه` line) - ▶ to run, a spinning
  circle while it runs, ✓ when it passed, ✗ when it failed - that runs it when clicked; a
  failed test's first message at the end of its line; and, when a test fails, a panel under
  its line with everything it reported, its arrow pointing at the test (close it with × or
  Escape; a click on the message at the end of the line, or a right-click on the ✗, opens it
  again). It shows the results of every run, wherever it was started.
* **The Test Results view** shows a Pashto transcript right to left and against the right
  edge, like the Thunder IDE, with the marks in colour. (The view is a terminal, which has no
  right-to-left text of its own: the lines are written in the order they are seen in. A
  terminal draws each letter on its own, so Pashto letters there are not joined.)
* **Command line:** `thunderc shop_test.th && thundervm shop_test.thc`; the exit status says
  whether everything passed.

## In Pashto

The annotations are `@آزموینه` (`@test`; `@ازموینه` is accepted too, and the keyboard forms of
`ی` are folded as everywhere) and `@پېښه ګر` (`@mock`). The English names are accepted in every
language, and `thunderc --translate` converts them. A struct suite's teardown method is
`وتړه()` (`close()` works too).

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `@test` | `@آزموینه` | | `@mock` | `@پېښه ګر` |
| module `test` | `ازموینه` | | module `expect` | `تمه` |
| `test.run` | `ازموینه.وچلوه` | | `expect.that` | `تمه.دا_وي` |
| `test.fail` | `ازموینه.ناکامه_کړه` | | `expect.is_true` | `تمه.رښتیا_وي` |
| `test.finish` | `ازموینه.پای_ته_ورسوه` | | `expect.is_false` | `تمه.درواغ_وي` |
| `test.runner` | `ازموینه.د_ازموینو_چلوونکی` | | `expect.equal` | `تمه.برابر` |
| `test.Runner` | `ازموینه.چلوونکی` | | `expect.not_equal` | `تمه.نابرابر` |
| `test.Result` | `ازموینه.پایله` | | `expect.near` | `تمه.نږدې` |
| `test.Mock` (mixin) | `ازموینه.پېښه_ګر` | | `close` (teardown) | `وتړه` |
| `called` | `وبلل_شو` | | `call_count` | `د_بلنو_شمېر` |
| `arguments_of` | `دلیلونه_یې` | | `calls` | `بلنې` |
| `forget_calls` | `بلنې_هېرې_کړه` | | `expect_called` | `تمه_د_بللو` |
| `Result`: `suite`, `name`, `failures`, `passed()` | `ډله`، `نوم`، `ناکامۍ`، `تېره_شوه()` | | `Runner`: `wants`, `counts`, `report` | `غواړي`، `شمېرې`، `راپور` |

The Pashto file prints its lines in Pashto (`۲ تېرې شوې، ۰ ناکامې`), with the same ✓ and ✗
marks and the suite's name above its tests, so the VS Code Test Explorer reads both alike.

## Before annotations

Tests used to be a program: it imported `"testing"`, called `test.run("name", body)` for each
test and `test.finish()` at the end, and put tests into suites by declaring them in modules
(`test.suite_of` found the module). That style no longer compiles - a program may not import
`"testing"` (`S092`), `test.run` now takes the suite as well, and `test.suite_of` is gone. To
convert such a file, move the code under test into a library file, delete the `test.run` and
`test.finish` lines, and mark the module (or each function) `@test`, giving `@test("...")` the
name that `test.run` was given.

**Before 0.7** these were plain functions: `test` (now the test's annotation), `expect`
(`expect.that`), `expect_true`, `expect_false`, `expect_equal`, `expect_not_equal`,
`expect_near` (`expect.is_true` ... `expect.near`), `fail` (`test.fail`) and `finish_tests`
(`test.finish`).
