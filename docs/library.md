# The Thunder Library Reference

*Part of the [Thunder SDK reference](README.md).*

This document describes what was added to Thunder in the fifth iteration:

1. **Modules** – splitting a program into several files with `import` ([section 2](#2-modules)).
2. **Input** – reading what the user types with `input()` ([section 3](#3-input-and-output)).
3. **Text, pair and list functions** – `substring`, `trim`, `to_upper`, `first`, `second`, `map`,
   `filter`, `zip` and more ([sections 4–7](#4-text-functions)), including **functions as values**,
   which `map`, `filter` and `reduce` need ([section 7](#7-functions-as-values-map-filter-and-reduce)).
4. **The `os` module** – files, commands, the clock and the environment ([section 8](#8-the-os-module)).

The **`graphics` module** (drawing, animation, keyboard and mouse) and the random-number
functions `random_int` and `seed_random` have their own document: the
[graphics reference](graphics.md).

The core language is described in the [language reference](language.md). The
[bytecode reference](bytecode.md) explains how all of this runs on the virtual machine. When
the built-ins are not enough, the [native interface reference](native.md) shows how to call
your own **C++** code, and the [packaging reference](packaging.md) how to turn a finished
program into a standalone application. In the IDE, this document is under
**Help > Library reference**.

Samples `19_modules` to `23_os` (in `samples/en/`) show every feature. Samples 19, 21 and 22 are
also in Pashto (`samples/ps/`).

---

## 1. Conventions

* **Positions count characters**, not bytes, and start at 0, exactly as `len(s)` and `s[i]` do.
  `"پښتو"` has 4 characters.
* **Ranges include the start and exclude the end.** `substring("thunder", 1, 4)` is `"hun"`, and
  `range(1, 4)` is `[1, 2, 3]`. A range outside the text or array is runtime error `R012`; Thunder
  never quietly clips it.
* **New value, or change in place?** Functions whose names are verbs that change things (`push`,
  `pop`, `insert`, `remove_at`, and the existing `remove` for maps) change the array or map they are
  given. Every other function returns a new value and leaves its arguments alone. For example,
  `sort(a)` returns a sorted copy.
* **Types are checked when the program is compiled.** `trim(5)` and `push(names, 3)` are compile
  errors (`S010`), never runtime surprises. The generic functions (`contains`, `map`, `zip`, ...)
  work out their result type from their arguments.
* **Every built-in has a Pashto name** (table in [section 10](#10-pashto-names)).
* Signatures below use `T`, `U`, `A`, `K` and `V` for "any type", as in `push(a: T[], value: T)`.

---

## 2. Modules

### Importing a file

```thunder
// lib/geometry.th
struct Rectangle
    width: int
    height: int

func area(r: Rectangle): int
    return r.width * r.height
```

```thunder
// main.th
import "lib/geometry"

var room = Rectangle(4, 3)
println(area(room))          // 12
```

`import "name"` reads the file `name.th` (writing `.th` is optional) and makes **everything it
declares usable by its own name**, as if it were written in the importing file. Thunder has no
`geometry.area` prefix, so a module's names must not clash with the program's own names; a clash is
reported as for two declarations in one file (`S002`, `S003`, `S028`).

### Where files are looked for

1. **Next to the file that contains the `import`.** `main.th` finds `lib/geometry.th` in its own
   directory. If `lib/geometry.th` says `import "numbers"`, it means `lib/numbers.th`.
2. **In the module directories** given to the compiler: `thunderc -I <dir>` (or
   `--module-path <dir>`; may be repeated). The IDE passes the directory the program was opened
   from, so samples and saved files find their modules even though the IDE compiles a copy of the
   editor text.

If the file is not found, error `S045` lists every place that was searched:

```
main.th:1:8: semantic error [S045]: module 'lib/shapes' not found (looked for lib/shapes.th, /home/me/thunder/lib/shapes.th)
```

### What a module may contain

A module **only declares things**: functions, structs, variables, constants and further imports.
Statements such as `println(...)` or `if ...` are rejected (`S047`), so importing a module never
prints anything or has other side effects. A module may not define `main` (`S048`); only the
program that is run can.

A module's variables are ordinary global variables. They are initialised **before** the importing
file's own statements run, wherever the `import` is written. `import` itself may only appear at
the top level of a file (`P016`); by convention it is written at the top.

### Each file is loaded once

However often a file is imported, directly or through other modules, it is loaded once and its
variables are initialised once. Two modules may therefore both import a third (a "diamond"), and
two modules may even import each other.

### Language

A module is compiled in the **same language** as the program that imports it. A module without a
language directive takes the importer's language. A module that declares a different language
(`// lang: ps` in an English program) is rejected with `S046`; translate it with
`thunderc --translate`. File names are written as they are on disk, in any script.

### Errors in modules

Errors are reported in the module's own file, with its own line numbers and source line:

```
lib/geometry.th:7:12: semantic error [S027]: function 'area' must return a value of type 'int' but this value has type 'bool'
    7 |     return true
      |            ^~~~
```

Runtime errors name the module too: `in area [lib/geometry.th] (line 7)`. In the IDE, problems in
a module are listed in the Problems tab with the module's file name, the Bytecode tab shows which
file each instruction came from, and the debugger names the file while it steps through a module.

### Built-in modules

`import` followed by a **name without quotes** imports a module built into the toolchain:
[`os`](#8-the-os-module) (in Pashto also `سیسټم`) and [`graphics`](graphics.md) (`ګرافیک`). An unknown name is
error `S050`, which also reminds you that files are imported with quotes.

### Under the bonnet

Each source file gets a number (0 for the program, then 1, 2, ... for the modules in the order they
were loaded). Every token, syntax-tree node, diagnostic and bytecode line-table entry records its
file number, and the `.thc` file lists the file names (bytecode format version 4). Resolving an
`import` is name resolution, so modules are read, tokenised and parsed at the start of **semantic
analysis**. The program is only analysed further if they all load without errors. The semantic
analyser then sees one combined program: each module's declarations, then the program's own.

---

## 3. Input and output

| Function | Description |
| --- | --- |
| `print(v, ...)` | Writes the values with no separator and no newline. |
| `println(v, ...)` | Like `print`, followed by a newline. |
| `input(): string` | Reads one line and returns it without its line break (`\n` or `\r\n`). Returns `""` at the end of the input. |
| `input(prompt: string): string` | Writes `prompt` first (without a newline), then reads a line. |
| `end_of_input(): bool` | `true` if there is nothing more to read. |
| `format(template, v, ...): string` | The template with each `{}` replaced by the next value; `{x}` writes a whole number in hexadecimal, `{b}` in binary; `{{` and `}}` are literal braces. Formatted printing is `println(format(...))`. Too few or too many values is `R031`; `{x}`/`{b}` on a non-integer is `R032`. |

```thunder
var name = input("What is your name? ")
println(format("Hello, {}! Your name has {} letter(s), or {x} in hex.", name, len(name), len(name)))

var total = 0
while !end_of_input()
    var line = trim(input())
    if is_int(line)
        total = total + to_int(line)
println("total: ", total)
```

**Where the input comes from:**

* **Command line:** standard input. Type the lines (end with Ctrl+D on Linux and macOS, or Ctrl+Z
  then Enter on Windows), or redirect a file: `thundervm sum.thc < numbers.txt`.
* **IDE:** the **Input** line under the Output panel. It is enabled while a program runs (Run or
  Debug). Type a line and press Enter or **Send**; the line is echoed in the output, as a terminal
  would show it. **End input** ends the input, so `end_of_input()` becomes `true`. A prompt appears
  as soon as the program writes it, before the program waits.
* **Golden tests:** if `tests/golden/<language>/<sample>.input` exists, the sample reads that file
  (see `22_input`).

`input()` and `end_of_input()` wait until the user has typed a line (or ended the input). A program
waiting for input uses no instructions, so the IDE's step limit does not stop it; **Stop** does.

---

## 4. Text functions

| Function | Result | Example |
| --- | --- | --- |
| `substring(s, start, end)` | The characters from `start` up to (not including) `end`. | `substring("thunder", 1, 4)` → `"hun"` |
| `slice(s, start, end)` | The same as `substring` (`slice` also works on arrays). | `slice("abc", 0, 2)` → `"ab"` |
| `trim(s)` | `s` without spaces, tabs and line breaks at its start and end. | `trim("  hi \n")` → `"hi"` |
| `to_upper(s)` | `s` with `a`–`z` changed to `A`–`Z`. | `to_upper("Thunder 2")` → `"THUNDER 2"` |
| `to_lower(s)` | `s` with `A`–`Z` changed to `a`–`z`. | `to_lower("ABC")` → `"abc"` |
| `split(s, separator)` | The pieces between the separators. With `""`, the single characters. | `split("a,b,,c", ",")` → `["a", "b", "", "c"]` |
| `join(parts, separator)` | The strings in `parts` with `separator` between them. | `join(["a", "b"], ", ")` → `"a, b"` |
| `replace(s, old, new)` | `s` with every `old` replaced by `new` (from left to right, without overlaps). | `replace("a.b.c", ".", "-")` → `"a-b-c"` |
| `starts_with(s, prefix)` | `true` if `s` begins with `prefix`. | `starts_with("thunder", "thu")` → `true` |
| `ends_with(s, suffix)` | `true` if `s` ends with `suffix`. | `ends_with("thunder", "er")` → `true` |
| `contains(s, part)` | `true` if `part` occurs in `s`. | `contains("thunder", "und")` → `true` |
| `index_of(s, part)` | The position of the first `part` in `s`, or `-1`. | `index_of("thunder", "der")` → `4` |
| `repeat(s, n)` | `s` written `n` times. | `repeat("ab", 3)` → `"ababab"` |
| `reverse(s)` | The characters in reverse order. | `reverse("stressed")` → `"desserts"` |
| `char_code(c)` | The Unicode code point of a one-character string. | `char_code("A")` → `65` |
| `from_char_code(n)` | The one-character string with code point `n`. | `from_char_code(97)` → `"a"` |
| `is_int(s)` | `true` if `to_int(s)` would succeed. Use it to check input before converting it. | `is_int(" 42 ")` → `true`, `is_int("4x")` → `false` |

Notes:

* Only the English letters `a`–`z` and `A`–`Z` change case. Pashto and other Arabic-script
  languages have no upper and lower case.
* `trim` removes ASCII white space (space, tab, line breaks, vertical tab, form feed).
* `split(s, sep)` always returns at least one piece: `split("", ",")` is `[""]`.
  `join(split(s, sep), sep)` gives back `s`.
* `replace(s, "", new)` returns `s` unchanged (there is nothing to look for).
* `is_int` and `to_int` accept surrounding spaces and the digits of every supported script
  (`"۴۲"`).
* Strings themselves stay immutable: these functions return new strings.

**Runtime errors:** a range outside the text (`R012`), `repeat` with a negative count (`R014`) or a
result longer than the VM's size limit (`R007`), `char_code` of a string that is not exactly one
character long (`R015`), and `from_char_code` of a number that is not a code point (`R016`).

---

## 5. Tuples and pairs

A **pair** is a tuple with two elements, `(1, "one")` of type `(int, string)` (see the
[language reference](language.md), section 8).

| Function | Result | Example |
| --- | --- | --- |
| `first(t)` | The first element of a tuple (of any size). | `first((1, "one"))` → `1` |
| `second(t)` | The second element of a tuple. | `second((1, "one"))` → `"one"` |
| `swap(p)` | The pair with its elements exchanged; type `(B, A)` for a `(A, B)`. | `swap((1, "one"))` → `("one", 1)` |
| `zip(a, b)` | Pairs of the elements at the same positions, as many as the shorter array has. Type `(A, B)[]`. | `zip([1, 2, 3], ["a", "b"])` → `[(1, "a"), (2, "b")]` |
| `enumerate(a)` | Every element paired with its position. Type `(int, T)[]`. | `enumerate(["x", "y"])` → `[(0, "x"), (1, "y")]` |
| `items(m)` | The entries of a map as (key, value) pairs, in insertion order. Type `(K, V)[]`. | `items({"a": 1})` → `[("a", 1)]` |

`first(p)` and `second(p)` mean the same as `p.0` and `p.1`. Pairs are easy to unpack:

```thunder
var people = zip(["Ada", "Alan"], [1815, 1912])
for var i = 0, i < len(people), i = i + 1
    var (name, year) = people[i]
    println(name, " was born in ", year)
```

---

## 6. List (array) and map functions

### Changing an array in place

| Function | Effect | Example (with `a = [3, 1, 2]`) |
| --- | --- | --- |
| `push(a, v)` | Adds `v` to the end. | `push(a, 4)` → `a` is `[3, 1, 2, 4]` |
| `pop(a): T` | Removes the last element and returns it. Runtime error `R013` if `a` is empty. | `pop(a)` → `2`, `a` is `[3, 1]` |
| `insert(a, i, v)` | Inserts `v` at position `i` (0 to `len(a)`); later elements move up. | `insert(a, 0, 9)` → `a` is `[9, 3, 1, 2]` |
| `remove_at(a, i): T` | Removes the element at position `i` and returns it; later elements move down. | `remove_at(a, 1)` → `1`, `a` is `[3, 2]` |

An index outside the array is runtime error `R003`, as for `a[i]`. Arrays are reference types, so
every variable that refers to the array sees the change.

### New arrays

| Function | Result | Example |
| --- | --- | --- |
| `slice(a, start, end)` | The elements from `start` up to (not including) `end`. | `slice([1, 2, 3, 4], 1, 3)` → `[2, 3]` |
| `concat(a, b)` | The elements of `a` followed by those of `b`. | `concat([1], [2, 3])` → `[1, 2, 3]` |
| `reverse(a)` | The elements in reverse order. | `reverse([1, 2, 3])` → `[3, 2, 1]` |
| `sort(a)` | The elements in ascending order: numbers by value, strings by character code. Equal elements keep their order. | `sort(["pear", "apple"])` → `["apple", "pear"]` |
| `range(start, end)` | The numbers from `start` up to (not including) `end`; empty if `end <= start`. | `range(1, 5)` → `[1, 2, 3, 4]` |

### Searching and summing

| Function | Result | Example |
| --- | --- | --- |
| `contains(a, v)` | `true` if an element equals `v`. | `contains([1, 2], 2)` → `true` |
| `index_of(a, v)` | The position of the first element equal to `v`, or `-1`. | `index_of(["a", "b"], "b")` → `1` |
| `sum(a)` | The sum of an `int[]` or a `float[]` (`0` if empty; overflow is runtime error `R002`/`R025`). | `sum([1, 2, 3])` → `6` |

`contains` and `index_of` compare with `==`, so they accept the element types `==` accepts: `int`,
`bool`, `string` and tuples. Arrays of structs, arrays and maps would only compare identities,
which is rarely what is meant, so they are a compile error (`S026`). `sort` accepts `int[]`,
`float[]` and `string[]`.

### Maps

| Function | Result | Example (with `m = {"a": 1}`) |
| --- | --- | --- |
| `get(m, key, default)` | The value for `key`, or `default` if `m` does not contain it. Unlike `m[key]`, never a runtime error. | `get(m, "z", 0)` → `0` |
| `items(m)` | The entries as (key, value) pairs ([section 5](#5-tuples-and-pairs)). | `items(m)` → `[("a", 1)]` |

These join the existing map functions `has`, `remove`, `keys` and `values`.

### Numbers and maths

These are always available; they are the companions of the `float` type
([language reference](language.md), section 3). Every one of them also accepts whole numbers,
which are widened, so `sqrt(2)` and `round(2)` are fine.

| Function | Result | Example |
| --- | --- | --- |
| `to_float(x)` | The decimal of a text (`"2.5"`, `"۲٫۵"`) or of a whole number. Runtime error `R026` if the text is not a number. | `to_float("2.5")` → `2.5` |
| `is_number(s)` | `true` if `to_float(s)` would succeed. | `is_number("x")` → `false` |
| `to_int(x)` | Also accepts a decimal, dropping the fractional part (toward zero), and the `0x`/`0b` notations. | `to_int("0x2a")` → `42` |
| `hex(n)`, `bin(n)` | A whole number written in hexadecimal / binary; `to_int` reads both back, so the conversions go both ways. | `hex(42)` → `"0x2a"`, `bin(10)` → `"0b1010"` |
| `abs(x)` | The distance from zero; `int` in, `int` out. | `abs(-3.25)` → `3.25` |
| `min(a, b)`, `max(a, b)` | The smaller / larger of two numbers, or of two texts. | `max(1.5, 0.5)` → `1.5` |
| `round(x)`, `floor(x)`, `ceil(x)` | A decimal as an `int`: nearest (halves away from zero), down, up. | `round(2.5)` → `3` |
| `sqrt(x)` | Square root; runtime error `R027` if `x` is negative. | `sqrt(9.0)` → `3.0` |
| `pow(x, y)` | `x` to the power `y`. | `pow(2.0, 10.0)` → `1024.0` |
| `exp(x)`, `log(x)` | `e`<sup>`x`</sup>, and the natural logarithm (`R027` if `x <= 0`). | `log(1.0)` → `0.0` |
| `sin(x)`, `cos(x)`, `tan(x)` | Trigonometry, with the angle in **radians**. | `sin(pi() / 2.0)` → `1.0` |
| `pi()` | 3.141592653589793. | |
| `random()` | A decimal from 0 (included) to 1 (excluded); `seed_random` makes it repeatable. | |

A result that is too large for a `float` is runtime error `R025`, and dividing by `0.0` is `R001`:
Thunder never produces infinity or "not a number".

---

## 7. Functions as values: map, filter and reduce

### Function types and values

A function's **name, without parentheses, is a value**: a reference to the function. Its type is
written with `func`:

| Type | Meaning |
| --- | --- |
| `func(int): bool` | a function that takes an `int` and returns a `bool` |
| `func(string, int): string` | two parameters |
| `func()` | no parameters and no result |
| `(func(int): int)[]` | an array of functions (the parentheses keep `[]` off the result type) |

```thunder
func double(n: int): int
    return n * 2

func apply(f: func(int): int, x: int): int
    return f(x)                   // call a function value like a function

var f = double                    // f has type func(int): int
println(f(4), " ", apply(double, 5))    // 8 10
var steps: (func(int): int)[] = [double, f]
println(steps[1](3))              // 6: any expression that gives a function can be called
println(f)                        // <func double>
```

* Function values can be stored in variables, arrays, maps, tuples and struct fields, passed as
  arguments and returned from functions.
* A variable or field of function type that has not been given a function holds "no function",
  shown as `<func ?>`. Calling it is runtime error `R020`.
* A local variable that holds a function hides a function with the same name.
* A function name cannot be assigned to (`double = triple` is `S012`); function values cannot be
  compared with `==` (`S006`).
* Only functions you declare can be values. Built-in functions cannot (`map(words, str)` is
  `S025`), because many of them accept several types. Write a small function instead:
  `func text(n: int): string` / `return str(n)`.
* Thunder has no nested functions, so a function value never captures local variables. There are
  no closures.

### Higher-order functions

| Function | Result | Example |
| --- | --- | --- |
| `map(a, f)` | A new array with `f(x)` for every element `x`. `f: func(T): U` gives a `U[]`. | `map([1, 2, 3], double)` → `[2, 4, 6]` |
| `filter(a, keep)` | A new array with the elements for which `keep: func(T): bool` returns `true`. | `filter(range(1, 7), is_even)` → `[2, 4, 6]` |
| `reduce(a, initial, f)` | Combines the elements from left to right with `f: func(A, T): A`: `f(f(initial, a[0]), a[1]) ...`. Returns `initial` for an empty array. | `reduce([1, 2, 3], 0, add)` → `6` |

```thunder
func is_even(n: int): bool
    return n % 2 == 0

func add(total: int, n: int): int
    return total + n

func longer(best: string, word: string): string
    if len(word) > len(best)
        return word
    return best

var numbers = range(1, 11)
println(map(numbers, double))                     // [2, 4, 6, ..., 20]
println(filter(numbers, is_even))                 // [2, 4, 6, 8, 10]
println(reduce(numbers, 0, add))                  // 55
println(reduce(["a", "abc", "ab"], "", longer))   // abc
```

If the function has the wrong type, the message says which type was needed (`?` stands for "any
result type"):

```
semantic error [S051]: argument 2 of 'map' must be a function of type 'func(int): ?' but has type 'func(string): int'
```

`map` and `filter` work on the elements the array had when they started, so the function may
safely change the array. A runtime error inside the function is reported in the function, with the
call from `map` in the call stack. In the IDE's debugger you can step into the function each time
`map` calls it.

---

## 8. The os module

```thunder
import os
```

The operating-system functions are available only after `import os` (in Pashto also
`راوړه سیسټم`). Without the import their names are ordinary identifiers that a program may use
for its own variables and functions, and using one as a function gives a helpful error:

```
semantic error [S049]: 'run' belongs to the 'os' module; add 'import os' at the top of the program
```

### Program arguments, environment and exit

| Function | Result |
| --- | --- |
| `args(): string[]` | The words after the program's name on the command line: `thundervm prog.thc one "two words"` gives `["one", "two words"]`. Options for `thundervm` itself come before the program's name (or before `--`). |
| `get_env(name): string` | The value of an environment variable, or `""` if it is not set. |
| `exit(code: int)` | Ends the program at once. `code` becomes `thundervm`'s exit status (0 means success). |
| `platform(): string` | `"linux"`, `"windows"`, `"macos"` or `"unknown"`. |

### Files and directories

| Function | Effect |
| --- | --- |
| `read_file(path): string` | The whole contents of a text file. Windows line endings are converted to `"\n"`. |
| `write_file(path, text)` | Creates the file, or replaces its contents. |
| `append_file(path, text)` | Adds `text` to the end of the file (creating it if necessary). |
| `file_exists(path): bool` | `true` if a file or directory exists at `path`. |
| `remove_file(path)` | Deletes a file; nothing happens if it does not exist. |
| `list_dir(path): string[]` | The names of the entries in a directory, sorted. |
| `current_dir(): string` | The program's working directory. |

Paths are written with `/` (Windows also accepts `\`). Relative paths start in the working
directory: where `thundervm` was started, or, in the IDE, the directory the program was opened
from. Files are read and written as UTF-8 text. A file that cannot be read is runtime error
`R017`; one that cannot be written or removed is `R018`. The message gives the reason.

### Running commands

| Function | Effect |
| --- | --- |
| `run(command): int` | Runs `command` in the system shell (`sh` on Linux and macOS, `cmd.exe` on Windows). Its output becomes part of the program's output, also in the IDE. Returns the command's exit status. |
| `run_output(command): (int, string)` | Runs `command` and returns its exit status and its output, without printing it. |

```thunder
import os
var status = run("echo Hello from the shell")            // prints: Hello from the shell
var (code, text) = run_output("git --version")
if code == 0
    println("found ", trim(text))
```

The output is collected until the command ends and then passed on. A command that cannot be
started at all is runtime error `R019`. Commands run with the same rights as Thunder itself, so
only run programs you trust.

### Time

| Function | Result |
| --- | --- |
| `time(): int` | Seconds since 1 January 1970, 00:00 UTC (the "Unix time"). |
| `clock_ms(): int` | Milliseconds since the program started. Use differences to time code. |
| `sleep(ms: int)` | Pauses for `ms` milliseconds (nothing happens for 0 or less). |
| `date_time(): string` | The local date and time, `"2026-11-06 14:30:05"`. |

---

## 9. New error codes

| Code | Phase | Meaning |
| --- | --- | --- |
| `P016` | syntax | `import` inside a block |
| `S045` | semantic | imported file not found (lists where it looked) |
| `S046` | semantic | module written in another language |
| `S047` | semantic | a statement in a module (modules only declare things) |
| `S048` | semantic | `main` defined in a module |
| `S049` | semantic | an `os` function used without `import os` (or a `graphics` function without `import graphics`) |
| `S050` | semantic | unknown built-in module (`import geometry` without quotes) |
| `S051` | semantic | the function given to `map`, `filter` or `reduce` has the wrong type |
| `R012` | runtime | range outside a string or array (`substring`, `slice`) |
| `R013` | runtime | `pop` of an empty array |
| `R014` | runtime | negative count (`repeat`) |
| `R015` | runtime | `char_code` of a string that is not one character |
| `R016` | runtime | `from_char_code` of an invalid code point |
| `R017` | runtime | a file or directory cannot be read |
| `R018` | runtime | a file cannot be written or removed |
| `R019` | runtime | a command cannot be started |
| `R020` | runtime | a function variable called before a function was assigned to it |
| `L009` | lexical | a decimal literal that is too large |
| `S052` | semantic | `for ... in` over a value that is not an array, a string or a map |
| `R025` | runtime | a decimal result that is too large (or does not fit in an `int`) |
| `R026` | runtime | `to_float` of a text that is not a number |
| `R027` | runtime | `sqrt` of a negative number, `log` of zero or less, `pow` with no real result |

Changed: `S025` now only concerns built-in functions used as values (user functions are values);
`P006` now only concerns calls of literals such as `1(2)`.

---

## 10. Pashto names

All names are drafts and await review by a native speaker, like the rest of the Pashto
vocabulary (`src/core/language_pashto.cpp`). Multi-word names are written with spaces, as in
ordinary Pashto. The lexer joins the words into one name.

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `import` | `راوړه` | | `os` (module) | `سیسټم` (or `os`) |
| `input` | `ولوله` | | `end_of_input` | `د ننوت پای` |
| `substring` | `د متن برخه` | | `trim` | `پاک کړه` |
| `to_upper` | `لوی توري کړه` | | `to_lower` | `واړه توري کړه` |
| `split` | `وېشه` | | `join` | `یو ځای کړه` |
| `replace` | `بدل کړه` | | `starts_with` | `پیلېږي په` |
| `ends_with` | `پایېږي په` | | `contains` | `شامل دی` |
| `index_of` | `ځای یې` | | `repeat` | `تکرار کړه` |
| `reverse` | `سرچپه کړه` | | `char_code` | `د توري کوډ` |
| `from_char_code` | `د کوډ توری` | | `is_int` | `عدد دی` |
| `first` | `لومړی` | | `second` | `دوهم` |
| `swap` | `ځای بدلول` | | `zip` | `جوړې کړه` |
| `enumerate` | `شمېره ورکړه` | | `items` | `توکي` |
| `push` | `ورزیات کړه` | | `pop` | `وروستی واخله` |
| `insert` | `ننباسه` | | `remove_at` | `له ځایه لرې کړه` |
| `slice` | `ټوټه` | | `concat` | `سره نښلوه` |
| `sort` | `ترتیب کړه` | | `range` | `واټن` |
| `sum` | `مجموعه` | | `map` | `پر هر یو` |
| `to_float` | `اعشاري کړه` | | `is_number` | `شمېره ده` |
| `abs` | `مطلق` | | `min` / `max` | `کمینه` / `اعظمه` |
| `round` | `نږدې کړه` | | `floor` / `ceil` | `ښکته کړه` / `پورته کړه` |
| `sqrt` | `جذر` | | `pow` | `توان کړه` |
| `exp` | `نمايي` | | `log` | `لوګارتم` |
| `sin` / `cos` | `جیب` / `تتمیم جیب` | | `tan` | `ظل` |
| `pi` | `پای` | | `random` | `تصادفي` |
| `filter` | `چاڼ کړه` | | `reduce` | `راټول کړه` |
| `get` | `ترلاسه کړه` | | `args` | `دلیلونه` |
| `get_env` | `د چاپېریال ارزښت` | | `run` | `وچلوه` |
| `run_output` | `د چلولو پایله` | | `read_file` | `فایل ولوله` |
| `write_file` | `فایل ولیکه` | | `append_file` | `فایل ته ورزیات کړه` |
| `file_exists` | `فایل شته` | | `remove_file` | `فایل ړنګ کړه` |
| `list_dir` | `د پوښۍ توکي` | | `current_dir` | `اوسنۍ پوښۍ` |
| `time` | `وخت` | | `clock_ms` | `ساعت` |
| `sleep` | `انتظار` | | `date_time` | `نېټه او وخت` |
| `platform` | `عامل سیسټم` | | `exit` | `پای ته ورسوه` |
| `func(...)` type | `دنده(...)` | | function value | `<دنده name>` |

`thunderc --translate ps` and the IDE's Translate button convert all of these. Modules must be
in the program's language, so translate them too.

---

## 11. Under the bonnet

* **Two new instructions.** `FUNC_REF f` pushes a function value. `CALL_VALUE n` pops a function
  value and calls it with the `n` arguments below it, so `steps[1](3)` compiles to "push 3, push
  `steps[1]`, `CALL_VALUE 1`". The unset function value is `FUNC_REF 65535`.
* **Built-ins that call Thunder functions.** `map`, `filter` and `reduce` call the function from
  inside the VM: the VM pushes the arguments, enters the function and runs instructions until it
  returns, then continues the built-in. The debugger, the step limit and the call-depth limit all
  apply to these calls.
* **Input.** The VM reads from an input stream (standard input for `thundervm`); the IDE connects
  a pipe to it and shows the output as it arrives rather than line by line, so a prompt is visible
  while the program waits.
* **Exit status.** `exit(code)` stops the VM normally, not as an error. The status is in the
  `exit_code` field of `thundervm --result-json` and of the debugger's trace.
* **Bytecode format version 4** adds the list of source files to the header and a file number to
  each line-table entry ([bytecode reference](bytecode.md), section 4).
* **Where the code is.** Built-in names and signatures: `src/core/builtins.cpp`; type checking:
  `check_builtin` in `src/core/sema.cpp`; execution: `src/core/vm_builtins.cpp`; operating-system
  services: `src/core/os.cpp`; module loading: `ModuleLoader` in `src/core/compiler.cpp`.
