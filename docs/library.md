# The Thunder Library Reference

*Part of the [Thunder SDK reference](README.md).*

This document describes everything that is built into Thunder, module by module:

1. **How the library is organised** – the default module and the built-in modules
   ([section 1](#1-how-the-library-is-organised)).
2. **Modules and files** – splitting a program into several files with `import`
   ([section 2](#2-modules-and-files)).
3. **The default module** – printing, input, conversions, tuples, lists, maps, and functions as
   values with `map`, `filter`, `reduce` and `name_of` ([section 3](#3-the-default-module)).
4. **The always-available modules** – [`math`](#4-the-math-module),
   [`string`](#5-the-string-module), [`random`](#6-the-random-module) and
   [`bytes`](#7-the-bytes-module).
5. **The modules that must be imported** – [`os`](#8-the-os-module) (commands, the clock, the
   environment) and [`file`](#9-the-file-module) (reading and writing files). The **`graphics`
   module** (drawing, animation, keyboard and mouse) has its own document: the
   [graphics reference](graphics.md).
6. **[What changed in 0.7](#10-what-changed-in-07)** – the old names and the new ones.

The core language, including how to declare modules of your own (`module`), is described in the
[language reference](language.md). The [bytecode reference](bytecode.md) explains how all of this
runs on the virtual machine. When the built-ins are not enough, the
[native interface reference](native.md) shows how to call your own **C++** code, and the
[packaging reference](packaging.md) how to turn a finished program into a standalone application.
The libraries written in Thunder itself (sockets, testing, data structures, algorithms) have
their own documents, listed in the [SDK reference](README.md). In the IDE, this document is under
**Help > Library reference**.

Samples `25_modules` to `27_os` (in `samples/en/`, and in Pashto in `samples/ps/`) show every
feature.

---

## 1. How the library is organised

The language's own functions are organised in **modules**, the same kind of named bundle a program
can declare for itself ([language reference](language.md)):

| Module | Pashto | Available | Contains | Section |
| --- | --- | --- | --- | --- |
| *(the default module)* | | always, **no prefix** | printing, input, conversions, tuples, lists, maps, higher-order functions, `name_of` | [3](#3-the-default-module) |
| `math` | `ریاضي` | always | `math.sqrt`, `math.sin`, `math.round`, `math.max`, `math.pi`, ... | [4](#4-the-math-module) |
| `string` | `متن` | always | `string.trim`, `string.split`, `string.format`, `string.is_int`, ... | [5](#5-the-string-module) |
| `random` | `تصادفي` | always | `random.int`, `random.float`, `random.seed` | [6](#6-the-random-module) |
| `bytes` | `بایټونه` | always | `bytes.from_string`, `bytes.to_int`, ... | [7](#7-the-bytes-module) |
| `os` | `سیسټم` | after `import os` | `os.run`, `os.args`, `os.time`, `os.exit`, `os.Platform`, ... | [8](#8-the-os-module) |
| `file` | `فایل` | after `import file` | `file.read`, `file.write`, `file.exists`, `file.read_bytes`, ... | [9](#9-the-file-module) |
| `graphics` | `ګرافیک` | after `import graphics` | `graphics.canvas`, `graphics.circle`, `graphics.frame`, `graphics.Color`, ... | [graphics reference](graphics.md) |

* **The default module needs no prefix.** `println`, `len`, `str`, `push`, `map` and the other
  functions of section 3 are written by their own name, everywhere.
* **Every other built-in is written with its module in front**: `math.sqrt(2.0)`,
  `string.trim(line)`, `random.int(1, 6)`. `math`, `string`, `random` and `bytes` are always
  there. `os`, `file` and `graphics` reach outside the program (the operating system, the disk,
  the screen), so a program says that it uses them: `import os`, `import file`,
  `import graphics`, at the top, without quotes. Using one of their members without the import
  is error `S049`.
* **The built-in enums belong to their modules too**: `graphics.Color.red` and
  `os.Platform.linux`.
* **A name used without its module** is error `S067`, and the message says where the name lives:

  ```
  main.th:3:9: semantic error [S067]: 'sqrt' belongs to the module 'math': write math.sqrt
  ```

  A member that a module does not have is `S070` ("the module 'math' has no member called
  'foo'").
* **Module names are reserved.** No variable, parameter, function or type may be called `math`,
  `os`, `random`, ... (`S065`: "'math' is the name of a module; give this variable another name"),
  and a module of your own cannot be called like a built-in one (`S064`).
* **The default module's names are reserved too**: a function of your own called `len` or `push`
  is `S004` (a struct, `S028`). The names inside the other modules are not: a program may
  declare its own `sqrt` or `trim`, because the built-in ones are `math.sqrt` and `string.trim`.
* Error messages, the debugger, call stacks and the Bytecode tab show the qualified names:
  `runtime error [R027]: 'math.sqrt' is not defined for -1.0`.

```thunder
import file

var words = string.split("pear apple fig", " ")    # string: always there
println(len(words), " ", sort(words))               # default module: no prefix
println(math.sqrt(2.0), " ", random.int(1, 6))     # math and random: always there
file.write("notes.txt", "Hello")                    # file: needs import file
```

### Conventions

* **Positions count characters**, not bytes, and start at 0, exactly as `len(s)` and `s[i]` do.
  `"پښتو"` has 4 characters.
* **Ranges include the start and exclude the end.** `string.substring("thunder", 1, 4)` is
  `"hun"`, and `range(1, 4)` is `[1, 2, 3]`. A range outside the text or array is runtime error
  `R012`; Thunder never quietly clips it.
* **New value, or change in place?** Functions whose names are verbs that change things (`push`,
  `pop`, `insert`, `remove_at`, and `remove` for maps) change the array or map they are given.
  Every other function returns a new value and leaves its arguments alone. For example, `sort(a)`
  returns a sorted copy.
* **Types are checked when the program is compiled.** `string.trim(5)` and `push(names, 3)` are
  compile errors (`S010`), never runtime surprises. The generic functions (`contains`, `map`,
  `zip`, ...) work out their result type from their arguments.
* **Every built-in has a Pashto name** (tables in [section 12](#12-pashto-names)).
* Signatures below are the ones `thunderc --language-info` reports. They use `T`, `U`, `A`, `B`,
  `K` and `V` for "any type", as in `push(a: T[], value: T)`.

---

## 2. Modules and files

### Importing a file

```thunder
# lib/geometry.th
module geometry
    struct Rectangle
        width: int
        height: int

    func area(r: Rectangle): int
        return r.width * r.height
```

```thunder
# main.th
import "lib/geometry"

var room = geometry.Rectangle(4, 3)
println(geometry.area(room))          # 12
```

`import "name"` reads the file `name.th` (writing `.th` is optional) and makes **everything it
declares usable**: the modules it declares, whose members are written with the module's name in
front (`geometry.area`), and any declarations outside a module, which are used by their own name,
as if they were written in the importing file. Those top-level names must therefore not clash with
the program's own names; a clash is reported as for two declarations in one file (`S002`, `S003`,
`S028`). Putting a library's declarations in a `module` avoids clashes, and the
[language reference](language.md) describes modules in full (private `_names`, `S064`-`S070`).

### Where files are looked for

1. **Next to the file that contains the `import`.** `main.th` finds `lib/geometry.th` in its own
   directory. If `lib/geometry.th` says `import "numbers"`, it means `lib/numbers.th`.
2. **In the module directories** given to the compiler: `thunderc -I <dir>` (or
   `--module-path <dir>`; may be repeated). The IDE passes the directory the program was opened
   from, so samples and saved files find their files even though the IDE compiles a copy of the
   editor text.

If the file is not found, error `S045` lists every place that was searched:

```
main.th:1:8: semantic error [S045]: module 'lib/shapes' not found (looked for lib/shapes.th, /home/me/thunder/lib/shapes.th)
```

### What an imported file may contain

An imported file **only declares things**: modules, functions, structs, enums, variables,
constants and further imports. Statements such as `println(...)` or `if ...` are rejected
(`S047`), so importing a file never prints anything or has other side effects. An imported file
may not define `main` (`S048`); only the program that is run can. A file with tests (`@test`) is
run, never imported (`S093`), and only a test file may import `"testing"` or files that declare
`@mock` structs (`S092`; see the [testing reference](testing.md)).

Its variables (and the variables of its modules) are ordinary global variables. They are
initialised **before** the importing file's own statements run, wherever the `import` is written.
`import` itself may only appear at the top level of a file (`P016`); by convention it is written
at the top.

### Each file is loaded once

However often a file is imported, directly or through other files, it is loaded once and its
variables are initialised once. Two files may therefore both import a third (a "diamond"), and two
files may even import each other.

### Language

An imported file is compiled in the **same language** as the program that imports it. A file
without a language directive takes the importer's language. A file that declares a different
language (`# lang: ps` in an English program) is rejected with `S046`; translate it with
`thunderc --translate`. File names are written as they are on disk, in any script.

### Errors in imported files

Errors are reported in the imported file, with its own line numbers and source line:

```
lib/geometry.th:8:16: semantic error [S027]: function 'geometry.area' must return a value of type 'int' but this value has type 'bool'
    8 |         return true
      |                ^~~~
```

Runtime errors name the file too: `in geometry.area [lib/geometry.th] (line 8)`. In the IDE,
problems in an imported file are listed in the Problems tab with its file name, the Bytecode tab
shows which file each instruction came from, and the debugger names the file while it steps
through it.

### Built-in modules

`import` followed by a **name without quotes** imports a module built into the toolchain:
[`os`](#8-the-os-module) (in Pashto `سیسټم`), [`file`](#9-the-file-module) (`فایل`) and
[`graphics`](graphics.md) (`ګرافیک`). An unknown name is error `S050`, which also reminds you that
files are imported with quotes. `math`, `string`, `random` and `bytes` need no import.

### Under the bonnet

Each source file gets a number (0 for the program, then 1, 2, ... for the imported files in the
order they were loaded). Every token, syntax-tree node, diagnostic and bytecode line-table entry
records its file number, and the `.thc` file lists the file names (bytecode format version 4).
Resolving an `import` is name resolution, so the files are read, tokenised and parsed at the start
of **semantic analysis**. The program is only analysed further if they all load without errors.
The semantic analyser then sees one combined program: each file's declarations, then the
program's own. It renames every module member to its qualified name (`geometry.area`) before
checking anything, so modules cost nothing at run time.

---

## 3. The default module

These functions are written without a prefix. Their names are reserved for them: a function of
your own may not be called `len` or `push` (`S004`), nor may a struct or enum (`S028`).

### Printing, input and output

| Function | Description |
| --- | --- |
| `print(value, ...)` | Writes the values with no separator and no newline. |
| `println(value, ...)` | Like `print`, followed by a newline. |
| `input(prompt: string = ""): string` | Writes `prompt` first, if there is one (without a newline), then reads one line and returns it without its line break (`\n` or `\r\n`). Returns `""` at the end of the input. |
| `end_of_input(): bool` | `true` if there is nothing more to read. |

Formatted printing is `println(string.format(...))` ([section 5](#5-the-string-module)).

```thunder
var name = input("What is your name? ")
println(string.format("Hello, {}! Your name has {} letter(s), or {x} in hex.", name, len(name), len(name)))

var total = 0
while not end_of_input()
    var line = string.trim(input())
    if string.is_int(line)
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
  (see `26_input`).

`input()` and `end_of_input()` wait until the user has typed a line (or ended the input). A program
waiting for input uses no instructions, so the IDE's step limit does not stop it; **Stop** does.

### Values and conversions

| Function | Result | Example |
| --- | --- | --- |
| `len(x: string \| T[] \| tuple \| map): int` | The number of characters in a text, or of elements in an array, tuple or map. | `len("پښتو")` → `4` |
| `str(value): string` | Any value as text, as `println` would write it. | `str(3.5)` → `"3.5"` |
| `to_int(s: string): int` | A whole number from a text; also accepts a decimal, dropping the fractional part (toward zero), and the `0x`/`0b` notations. Runtime error if the text is not a number. | `to_int("0x2a")` → `42` |
| `to_float(x: string \| int): float` | The decimal of a text (`"2.5"`, `"۲٫۵"`) or of a whole number. Runtime error `R026` if the text is not a number. | `to_float("2.5")` → `2.5` |
| `hex(n: int): string`, `bin(n: int): string` | A whole number written in hexadecimal / binary; `to_int` reads both back, so the conversions go both ways. | `hex(42)` → `"0x2a"`, `bin(10)` → `"0b1010"` |
| `to_byte(n: int): byte` | The low 8 bits of a whole number ([section 7](#7-the-bytes-module)). | `to_byte(256)` → `0` |
| `array(n: int, value: T): T[]` | An array of `n` copies of `value` (arrays and structs are copied, not shared). | `array(3, 0)` → `[0, 0, 0]` |
| `copy(value: T): T` | An independent copy of a struct, array, tuple or map (nested values are copied too). | |
| `inspect(value): string` | A detailed, multi-line description of a value; structs are shown field by field with their types. | |
| `dump(value)` | Prints `inspect(value)` followed by a newline. | |

`string.is_int(s)` and `string.is_number(s)` check a text before converting it
([section 5](#5-the-string-module)). `to_int` and `to_float` accept surrounding spaces and the
digits of every supported script (`"۴۲"`).

### Tuples and pairs

A **pair** is a tuple with two elements, `(1, "one")` of type `(int, string)` (see the
[language reference](language.md), section 8).

| Function | Result | Example |
| --- | --- | --- |
| `first(t: (A, B, ...)): A` | The first element of a tuple (of any size). | `first((1, "one"))` → `1` |
| `second(t: (A, B, ...)): B` | The second element of a tuple. | `second((1, "one"))` → `"one"` |
| `swap(p: (A, B)): (B, A)` | The pair with its elements exchanged. | `swap((1, "one"))` → `("one", 1)` |
| `zip(a: A[], b: B[]): (A, B)[]` | Pairs of the elements at the same positions, as many as the shorter array has. | `zip([1, 2, 3], ["a", "b"])` → `[(1, "a"), (2, "b")]` |
| `enumerate(a: T[]): (int, T)[]` | Every element paired with its position. | `enumerate(["x", "y"])` → `[(0, "x"), (1, "y")]` |
| `items(m: {K: V}): (K, V)[]` | The entries of a map as (key, value) pairs, in insertion order. | `items({"a": 1})` → `[("a", 1)]` |

`first(p)` and `second(p)` mean the same as `p.0` and `p.1`. Pairs are easy to unpack:

```thunder
var people = zip(["Ada", "Alan"], [1815, 1912])
for var i = 0, i < len(people), i = i + 1
    var (name, year) = people[i]
    println(name, " was born in ", year)
```

### Lists (arrays)

**Changing an array in place:**

| Function | Effect | Example (with `a = [3, 1, 2]`) |
| --- | --- | --- |
| `push(a: T[], value: T)` | Adds `value` to the end. | `push(a, 4)` → `a` is `[3, 1, 2, 4]` |
| `pop(a: T[]): T` | Removes the last element and returns it. Runtime error `R013` if `a` is empty. | `pop(a)` → `2`, `a` is `[3, 1]` |
| `insert(a: T[], index: int, value: T)` | Inserts `value` at position `index` (0 to `len(a)`); later elements move up. | `insert(a, 0, 9)` → `a` is `[9, 3, 1, 2]` |
| `remove_at(a: T[], index: int): T` | Removes the element at position `index` and returns it; later elements move down. | `remove_at(a, 1)` → `1`, `a` is `[3, 2]` |

An index outside the array is runtime error `R003`, as for `a[i]`. Arrays are reference types, so
every variable that refers to the array sees the change.

**New arrays:**

| Function | Result | Example |
| --- | --- | --- |
| `slice(x: string \| T[], start: int, end: int): string \| T[]` | The elements (or characters) from `start` up to (not including) `end`. | `slice([1, 2, 3, 4], 1, 3)` → `[2, 3]`, `slice("abc", 0, 2)` → `"ab"` |
| `concat(a: T[], b: T[]): T[]` | The elements of `a` followed by those of `b`. | `concat([1], [2, 3])` → `[1, 2, 3]` |
| `reverse(x: string \| T[]): string \| T[]` | The elements (or characters) in reverse order. | `reverse([1, 2, 3])` → `[3, 2, 1]`, `reverse("stressed")` → `"desserts"` |
| `sort(a: T[]): T[]` | The elements in ascending order: numbers by value, strings by character code. Equal elements keep their order. | `sort(["pear", "apple"])` → `["apple", "pear"]` |
| `range(start: int, end: int): int[]` | The numbers from `start` up to (not including) `end`; empty if `end <= start`. | `range(1, 5)` → `[1, 2, 3, 4]` |

**Searching and summing:**

| Function | Result | Example |
| --- | --- | --- |
| `contains(x: string \| T[], item): bool` | `true` if an element equals `item`, or if the text contains the part. | `contains([1, 2], 2)` → `true`, `contains("thunder", "und")` → `true` |
| `index_of(x: string \| T[], item): int` | The position of the first element equal to `item` (or of the first occurrence of the part), or `-1`. | `index_of(["a", "b"], "b")` → `1`, `index_of("thunder", "der")` → `4` |
| `sum(a: int[]): int` | The sum of an `int[]` or a `float[]` (`0` if empty; overflow is runtime error `R002`/`R025`). | `sum([1, 2, 3])` → `6` |

`slice`, `reverse`, `contains` and `index_of` work on texts and arrays alike, so they are in the
default module; the string module also has text-only forms of them (`string.slice`, ...,
[section 5](#5-the-string-module)). `contains` and `index_of` compare with `==`, so they
accept the element types `==` accepts: `int`, `bool`, `string` and tuples. Arrays of structs,
arrays and maps would only compare identities, which is rarely what is meant, so they are a
compile error (`S026`). `sort` accepts `int[]`, `float[]` and `string[]`.

### Maps

| Function | Result | Example (with `m = {"a": 1}`) |
| --- | --- | --- |
| `has(m: {K: V}, key: K): bool` | `true` if the map contains the key. | `has(m, "a")` → `true` |
| `get(m: {K: V}, key: K, default: V): V` | The value for `key`, or `default` if `m` does not contain it. Unlike `m[key]`, never a runtime error. | `get(m, "z", 0)` → `0` |
| `remove(m: {K: V}, key: K)` | Removes the key and its value (nothing happens if the key is absent). | |
| `keys(m: {K: V}): K[]` | The keys as an array, in insertion order. | `keys(m)` → `["a"]` |
| `values(m: {K: V}): V[]` | The values as an array, in insertion order. | `values(m)` → `[1]` |
| `items(m: {K: V}): (K, V)[]` | The entries as (key, value) pairs ([tuples and pairs](#tuples-and-pairs)). | `items(m)` → `[("a", 1)]` |

### Functions as values: map, filter, reduce and name_of

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
    return f(x)                   # call a function value like a function

var f = double                    # f has type func(int): int
println(f(4), " ", apply(double, 5))    # 8 10
var steps: (func(int): int)[] = [double, f]
println(steps[1](3))              # 6: any expression that gives a function can be called
println(f)                        # <func double>
```

* Function values can be stored in variables, arrays, maps, tuples and struct fields, passed as
  arguments and returned from functions. A module's function is a value too: `geometry.area`.
* A variable or field of function type that has not been given a function holds "no function",
  shown as `<func ?>`. Calling it is runtime error `R020`.
* A local variable that holds a function hides a function with the same name.
* A function name cannot be assigned to (`double = triple` is `S012`); function values cannot be
  compared with `==` (`S006`).
* Only functions you declare can be values. Built-in functions cannot (`map(words, str)` is
  `S025`), because many of them accept several types. Write a small function instead:
  `func text(n: int): string` / `return str(n)`.
* A function value can also be written in place, without a name: `func(n: int): int n * 2`. Such
  a value may use the variables around it - it is a **closure**, and both sides share the
  captured variable ([language reference](language.md), section 7). `map([1, 2, 3],
  func(n: int): int n * factor)` scales by a variable of the program.
* A closure shows as `<func func@line>`; `name_of` answers `"func@line"` for it.

**Higher-order functions:**

| Function | Result | Example |
| --- | --- | --- |
| `map(a: T[], f: func(T): U): U[]` | A new array with `f(x)` for every element `x`. | `map([1, 2, 3], double)` → `[2, 4, 6]` |
| `filter(a: T[], keep: func(T): bool): T[]` | A new array with the elements for which `keep` returns `true`. | `filter(range(1, 7), is_even)` → `[2, 4, 6]` |
| `reduce(a: T[], initial: A, f: func(A, T): A): A` | Combines the elements from left to right: `f(f(initial, a[0]), a[1]) ...`. Returns `initial` for an empty array. | `reduce([1, 2, 3], 0, add)` → `6` |
| `name_of(f: func): string` | The name the function was declared with, its module in front. | `name_of(geometry.area)` → `"geometry.area"` |

```thunder
func double(n: int): int
    return n * 2

func is_even(n: int): bool
    return n % 2 == 0

func add(total: int, n: int): int
    return total + n

func longer(best: string, word: string): string
    if len(word) > len(best)
        return word
    return best

var numbers = range(1, 11)
println(map(numbers, double))                     # [2, 4, 6, ..., 20]
println(filter(numbers, is_even))                 # [2, 4, 6, 8, 10]
println(reduce(numbers, 0, add))                  # 55
println(reduce(["a", "abc", "ab"], "", longer))   # abc
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

**`name_of`** answers with the name a function value refers to, whatever variable holds it, its
module and all - useful in messages and traces:

```thunder
module shapes
    func square(n: int): int
        return n * n

func cube(n: int): int
    return n * n * n

var f = shapes.square
println(name_of(f), " ", name_of(cube))     # shapes.square cube
println(f)                                   # <func shapes.square>
```

---

## 4. The math module

Always available, no import. These are the companions of the `float` type
([language reference](language.md), section 3). Every one of them also accepts whole numbers,
which are widened, so `math.sqrt(2)` and `math.round(2)` are fine.

| Function | Result | Example |
| --- | --- | --- |
| `math.abs(x: int \| float): int \| float` | The distance from zero; `int` in, `int` out. | `math.abs(-3.25)` → `3.25` |
| `math.min(a, b)`, `math.max(a, b)` | The smaller / larger of two numbers, or the earlier / later of two texts. | `math.max(1.5, 0.5)` → `1.5` |
| `math.round(x: float): int` | The nearest whole number; a half is rounded away from zero. | `math.round(2.5)` → `3` |
| `math.floor(x: float): int`, `math.ceil(x: float): int` | The whole number below / above. | `math.floor(2.5)` → `2`, `math.ceil(2.1)` → `3` |
| `math.sqrt(x: float): float` | Square root; runtime error `R027` if `x` is negative. | `math.sqrt(9.0)` → `3.0` |
| `math.pow(x: float, y: float): float` | `x` to the power `y`. | `math.pow(2.0, 10.0)` → `1024.0` |
| `math.exp(x: float): float`, `math.log(x: float): float` | `e`<sup>`x`</sup>, and the natural logarithm (`R027` if `x <= 0`). | `math.log(1.0)` → `0.0` |
| `math.sin(x: float): float`, `math.cos(x: float): float`, `math.tan(x: float): float` | Trigonometry, with the angle in **radians**. | `math.sin(math.pi() / 2.0)` → `1.0` |
| `math.pi(): float` | 3.141592653589793. | |

A result that is too large for a `float` is runtime error `R025`, and dividing by `0.0` is `R001`:
Thunder never produces infinity or "not a number".

---

## 5. The string module

Always available, no import. Strings themselves stay immutable: these functions return new
strings.

| Function | Result | Example |
| --- | --- | --- |
| `string.substring(s: string, start: int, end: int): string` | The characters from `start` up to (not including) `end`. | `string.substring("thunder", 1, 4)` → `"hun"` |
| `string.trim(s: string): string` | `s` without spaces, tabs and line breaks at its start and end. | `string.trim("  hi \n")` → `"hi"` |
| `string.to_upper(s: string): string` | `s` with `a`–`z` changed to `A`–`Z`. | `string.to_upper("Thunder 2")` → `"THUNDER 2"` |
| `string.to_lower(s: string): string` | `s` with `A`–`Z` changed to `a`–`z`. | `string.to_lower("ABC")` → `"abc"` |
| `string.split(s: string, separator: string): string[]` | The pieces between the separators. With `""`, the single characters. | `string.split("a,b,,c", ",")` → `["a", "b", "", "c"]` |
| `string.join(parts: string[], separator: string): string` | The strings in `parts` with `separator` between them. | `string.join(["a", "b"], ", ")` → `"a, b"` |
| `string.replace(s: string, old: string, new: string): string` | `s` with every `old` replaced by `new` (from left to right, without overlaps). | `string.replace("a.b.c", ".", "-")` → `"a-b-c"` |
| `string.starts_with(s: string, prefix: string): bool` | `true` if `s` begins with `prefix`. | `string.starts_with("thunder", "thu")` → `true` |
| `string.ends_with(s: string, suffix: string): bool` | `true` if `s` ends with `suffix`. | `string.ends_with("thunder", "er")` → `true` |
| `string.repeat(s: string, n: int): string` | `s` written `n` times. | `string.repeat("ab", 3)` → `"ababab"` |
| `string.char_code(c: string): int` | The Unicode code point of a one-character string. | `string.char_code("A")` → `65` |
| `string.from_char_code(code: int): string` | The one-character string with that code point. | `string.from_char_code(97)` → `"a"` |
| `string.is_int(s: string): bool` | `true` if `to_int(s)` would succeed. Use it to check input before converting it. | `string.is_int(" 42 ")` → `true`, `string.is_int("4x")` → `false` |
| `string.is_number(s: string): bool` | `true` if `to_float(s)` would succeed. | `string.is_number("x")` → `false` |
| `string.format(template: string, values...): string` | The template with each `{}` replaced by the next value; `{x}` writes a whole number in hexadecimal, `{b}` in binary; `{{` and `}}` are literal braces. | `string.format("{} is {x}", 10, 10)` → `"10 is 0xa"` |

**Text-only forms of general functions.** The default module's `len`, `contains`, `index_of`,
`reverse`, `slice`, `to_int` and `to_float` work on several kinds of value (texts and arrays, or
texts and numbers). The string module has the same functions for **text only**, under the same
names:

| Function | Result | Example |
| --- | --- | --- |
| `string.len(s: string): int` | The number of characters in the text. | `string.len("پښتو")` → `4` |
| `string.contains(s: string, part: string): bool` | `true` if the text contains `part`. | `string.contains("thunder", "und")` → `true` |
| `string.index_of(s: string, part: string): int` | The position of the first occurrence of `part`, or `-1`. | `string.index_of("thunder", "der")` → `4` |
| `string.reverse(s: string): string` | The characters in reverse order. | `string.reverse("stressed")` → `"desserts"` |
| `string.slice(s: string, start: int, end: int): string` | The characters from `start` up to (not including) `end`, like `string.substring`. | `string.slice("thunder", 1, 4)` → `"hun"` |
| `string.to_int(s: string): int` | The whole number a text holds, in any of the notations `to_int` reads (runtime error `R005` if it holds none). | `string.to_int(" 42 ")` → `42` |
| `string.to_float(s: string): float` | The decimal a text holds (runtime error `R026` if it holds none). | `string.to_float("2.5")` → `2.5` |

Both forms exist so that each reads naturally where it is used. The default module's are the
general ones - one `reverse` for an array of scores and for a word - which is what a beginner
reaches for. The string module's say in the name that the value is text, so code that works on
text can keep to one module (`string.trim`, `string.split`, `string.len`, ...), and the
compiler holds them to it: passing an array, or a number, to a `string.` function is a compile
error (`S010`: "argument 1 of 'string.len' must be of type 'string' but is 'int[]'"). They behave
exactly like the general functions on a text, with the same runtime errors.

Notes:

* Only the English letters `a`–`z` and `A`–`Z` change case. Pashto and other Arabic-script
  languages have no upper and lower case.
* `string.trim` removes ASCII white space (space, tab, line breaks, vertical tab, form feed).
* `string.split(s, sep)` always returns at least one piece: `string.split("", ",")` is `[""]`.
  `string.join(string.split(s, sep), sep)` gives back `s`.
* `string.replace(s, "", new)` returns `s` unchanged (there is nothing to look for).
* `string.is_int` and `to_int` accept surrounding spaces and the digits of every supported script
  (`"۴۲"`).
* Formatted printing is `println(string.format(...))`. Too few or too many values for the
  template is `R031`; `{x}`/`{b}` on a value that is not a whole number is `R032`.

**Runtime errors:** a range outside the text (`R012`), `string.repeat` with a negative count
(`R014`) or a result longer than the VM's size limit (`R007`), `string.char_code` of a string that
is not exactly one character long (`R015`), and `string.from_char_code` of a number that is not a
code point (`R016`).

---

## 6. The random module

Always available, no import.

| Function | Result |
| --- | --- |
| `random.int(low: int, high: int): int` | A random whole number from `low` to `high`, **both included** (`low > high` is runtime error `R023`). |
| `random.float(): float` | A random decimal from 0 (included) to 1 (excluded). |
| `random.seed(seed: int)` | Restarts the sequence: after the same seed, the same numbers follow. |

Without a seed, every run is different. `thundervm --seed n` (and the golden tests) start from a
fixed seed. Thunder uses its own generator (SplitMix64), so a seed gives the same numbers on every
operating system and compiler. Simulations and games use these a lot; see the
[graphics reference](graphics.md).

---

## 7. The bytes module

### Bytes

A `byte` is a whole number from 0 to 255, and a `byte[]` is **raw data**: the contents of a file
as it is on disk, a sound, an image, a network packet. A byte widens to an `int` wherever an
`int` is wanted, so the arithmetic and bit operators work on it; a whole-number literal from 0
to 255 written where a byte is wanted is that byte, and `to_byte` (default module) narrows
everything else (see the [language reference](language.md), section 3).

The `bytes` module is always available, no import. It converts between bytes and texts or whole
numbers:

| Function | Result | Example |
| --- | --- | --- |
| `to_byte(n: int): byte` *(default module)* | The low 8 bits of a whole number. | `to_byte(255)` → `255`, `to_byte(256)` → `0`, `to_byte(-1)` → `255` |
| `bytes.from_string(s: string): byte[]` | The bytes of the text in UTF-8 (one byte per English letter, two or more per Pashto letter). | `bytes.from_string("Az")` → `[65, 122]`, `bytes.from_string("ژ")` → `[218, 152]` |
| `bytes.to_string(data: byte[]): string` | The text the bytes spell in UTF-8; what is not valid UTF-8 becomes U+FFFD. | `bytes.to_string([72, 105])` → `"Hi"` |
| `bytes.to_int(data: byte[], start: int, count: int, little_endian: bool, signed: bool): int` | The whole number stored in `count` bytes (1 to 8) from position `start`, lowest byte first when `little_endian` is `true`, read as two's complement when `signed` is `true`. | `bytes.to_int([52, 18], 0, 2, true, false)` → `4660` (`0x1234`) |
| `bytes.from_int(value: int, count: int, little_endian: bool): byte[]` | The low `count` bytes (1 to 8) of a whole number, lowest first when `little_endian` is `true`. | `bytes.from_int(0x1234, 2, false)` → `[18, 52]`, `bytes.from_int(-2, 2, true)` → `[254, 255]` |

Everything that works on arrays works on a `byte[]`: `len`, indexing, `push`, `slice`,
`concat`, `reverse`, `contains`, `sort`, `sum` (an `int`), `for ... in`, `array(n, to_byte(0))`
for a buffer of zeros. `hex(b)`, `bin(b)` and `string.format("{x}", b)` show a byte's bits;
`str(b)` and `println(b)` show its value. The `file` module reads and writes files as bytes
(`file.read_bytes`, `file.write_bytes`, `file.append_bytes`, [section 9](#9-the-file-module)) and
the sockets library sends and receives them (a socket's `send_bytes` and `receive_bytes`
methods, `udp.send_bytes`, `udp.receive_bytes`: the [sockets reference](sockets.md)). A `byte[]` may also
cross the [native interface](native.md).

Little-endian is the order of WAV and BMP files and of most processors (`44100` is stored as
`[68, 172, 0, 0]`); big-endian is the order of the network and of PNG. Audio samples in a WAV
file are 16-bit signed little-endian numbers: `bytes.to_int(data, 44 + 2 * i, 2, true, true)`
reads sample `i`, and `bytes.from_int(sample, 2, true)` writes one.

**Runtime errors:** a `count` outside 1..8 is `R022`; a range outside the data is `R012`.

---

## 8. The os module

```thunder
import os
```

The operating-system functions are available only after `import os` (in Pashto `راوړه سیسټم`).
Using one without the import gives a helpful error:

```
semantic error [S049]: 'os.run' belongs to the 'os' module; add 'import os' at the top of the program
```

### Program arguments, environment and exit

| Function | Result |
| --- | --- |
| `os.args(): string[]` | The words after the program's name on the command line: `thundervm prog.thc one "two words"` gives `["one", "two words"]`. Options for `thundervm` itself come before the program's name (or before `--`). |
| `os.get_env(name: string): string` | The value of an environment variable, or `""` if it is not set. |
| `os.exit(code: int)` | Ends the program at once. `code` becomes `thundervm`'s exit status (0 means success). |
| `os.platform(): Platform` | The operating system as a member of the **`os.Platform` enum**: `os.Platform.linux`, `os.Platform.windows`, `os.Platform.macos` or `os.Platform.unknown` (`str(os.platform())` is `"linux"`, ...). |

### Directories

| Function | Result |
| --- | --- |
| `os.list_dir(path: string): string[]` | The names of the entries in a directory, sorted. |
| `os.current_dir(): string` | The program's working directory. |

Reading and writing files is the [`file` module](#9-the-file-module).

### Running commands

| Function | Effect |
| --- | --- |
| `os.run(command: string): int` | Runs `command` in the system shell (`sh` on Linux and macOS, `cmd.exe` on Windows). Its output becomes part of the program's output, also in the IDE. Returns the command's exit status. |
| `os.run_output(command: string): (int, string)` | Runs `command` and returns its exit status and its output, without printing it. |

```thunder
import os
var status = os.run("echo Hello from the shell")            # prints: Hello from the shell
var (code, text) = os.run_output("git --version")
if code == 0
    println("found ", string.trim(text))
```

The output is collected until the command ends and then passed on. A command that cannot be
started at all is runtime error `R019`. Commands run with the same rights as Thunder itself, so
only run programs you trust.

### Time

| Function | Result |
| --- | --- |
| `os.time(): int` | Seconds since 1 January 1970, 00:00 UTC (the "Unix time"). |
| `os.clock_ms(): int` | Milliseconds since the program started. Use differences to time code. |
| `os.sleep(ms: int)` | Pauses for `ms` milliseconds (nothing happens for 0 or less). |
| `os.date_time(): string` | The local date and time, `"2026-11-06 14:30:05"`. |

A directory that cannot be read is runtime error `R017`.

---

## 9. The file module

```thunder
import file
```

The file functions are available only after `import file` (in Pashto `راوړه فایل`); without it,
their use is `S049`, as for `os`.

| Function | Effect |
| --- | --- |
| `file.read(path: string): string` | The whole contents of a text file. Windows line endings are converted to `"\n"`. |
| `file.write(path: string, text: string)` | Creates the file, or replaces its contents. |
| `file.append(path: string, text: string)` | Adds `text` to the end of the file (creating it if necessary). |
| `file.exists(path: string): bool` | `true` if a file or directory exists at `path`. |
| `file.remove(path: string)` | Deletes a file; nothing happens if it does not exist. |
| `file.read_bytes(path: string): byte[]` | The whole contents of a file, byte for byte: nothing is converted (for images, sounds and other binary files; see [the bytes module](#7-the-bytes-module)). |
| `file.write_bytes(path: string, data: byte[])` | Creates the file, or replaces its contents, with exactly these bytes. |
| `file.append_bytes(path: string, data: byte[])` | Adds the bytes to the end of the file (creating it if necessary). |

```thunder
import file

file.write("notes.txt", "first line\n")
file.append("notes.txt", "second line\n")
if file.exists("notes.txt")
    var lines = string.split(string.trim(file.read("notes.txt")), "\n")
    println(len(lines), " lines, the last: ", lines[len(lines) - 1])
file.write_bytes("data.bin", bytes.from_int(44100, 4, true))
println(file.read_bytes("data.bin"))     # [68, 172, 0, 0]
file.remove("notes.txt")
file.remove("data.bin")
```

Paths are written with `/` (Windows also accepts `\`). Relative paths start in the working
directory: where `thundervm` was started, or, in the IDE, the directory the program was opened
from. `file.read`, `file.write` and `file.append` treat a file as UTF-8 text; the `_bytes`
functions treat it as what it is. A file that cannot be read is runtime error `R017`; one that
cannot be written or removed is `R018`. The message gives the reason.

---

## 10. What changed in 0.7

Up to Thunder 0.6 every built-in was written by its own name, and `import os` brought in the file
functions too. In 0.7 the built-ins are organised in the modules above. **The translator
(`thunderc --translate`, the IDE's Translate) does not convert old programs**; update them by
hand. For a name that kept its member name, the compiler's `S067` message says where it lives now
(`'sqrt' belongs to the module 'math': write math.sqrt`). The names below were renamed as well; the
old name is simply "not declared" (`S001`):

| Old (0.6) | New (0.7) | | Old (0.6) | New (0.7) |
| --- | --- | --- | --- | --- |
| `random_int(a, b)` | `random.int(a, b)` | | `seed_random(n)` | `random.seed(n)` |
| `random()` | `random.float()` | | `string_to_bytes(s)` | `bytes.from_string(s)` |
| `bytes_to_string(d)` | `bytes.to_string(d)` | | `bytes_to_int(...)` | `bytes.to_int(...)` |
| `int_to_bytes(...)` | `bytes.from_int(...)` | | `read_file(p)` | `file.read(p)` |
| `write_file(p, t)` | `file.write(p, t)` | | `append_file(p, t)` | `file.append(p, t)` |
| `file_exists(p)` | `file.exists(p)` | | `remove_file(p)` | `file.remove(p)` |
| `Color.red` | `graphics.Color.red` | | `Platform.linux` | `os.Platform.linux` |

Everything else only gained its module's name:

| Module | Old names, now written `module.name` |
| --- | --- |
| `math` | `abs`, `min`, `max`, `round`, `floor`, `ceil`, `sqrt`, `pow`, `exp`, `log`, `sin`, `cos`, `tan`, `pi` |
| `string` | `substring`, `trim`, `to_upper`, `to_lower`, `split`, `join`, `replace`, `starts_with`, `ends_with`, `repeat`, `char_code`, `from_char_code`, `is_int`, `is_number`, `format` |
| `os` (`import os`) | `args`, `get_env`, `exit`, `platform`, `run`, `run_output`, `list_dir`, `current_dir`, `time`, `clock_ms`, `sleep`, `date_time` |
| `file` (`import file`) | `read_bytes`, `write_bytes`, `append_bytes` (they were in `os`) |
| `graphics` (`import graphics`) | every graphics function: `canvas`, `circle`, `frame`, `rgb`, `key_pressed`, ... ([graphics reference](graphics.md)) |

A program that used files needs `import file` in addition to (or instead of) `import os`. The
default module is unchanged, apart from the new `name_of`: `len`, `str`, `to_int`, `to_float`,
`hex`, `bin`, `to_byte`, `push`, `map`, `contains`, `index_of`, `reverse`, `slice`, ... are still
written without a prefix. The libraries in `lib/thunder` (sockets, testing, data structures,
algorithms) were reorganised into modules in the same way; see their own documents.

---

## 11. Error codes

| Code | Phase | Meaning |
| --- | --- | --- |
| `P016` | syntax | `import` inside a block |
| `S004` | semantic | a function with the name of a default-module built-in (`len`, `push`, ...) |
| `S045` | semantic | imported file not found (lists where it looked) |
| `S046` | semantic | imported file written in another language |
| `S047` | semantic | a statement in an imported file or a module (they only declare things) |
| `S048` | semantic | `main` defined in an imported file |
| `S049` | semantic | a member of `os`, `file` or `graphics` used without `import os` / `import file` / `import graphics` |
| `S050` | semantic | unknown built-in module (`import geometry` without quotes) |
| `S051` | semantic | the function given to `map`, `filter` or `reduce` has the wrong type |
| `S064` | semantic | a module declared twice, or with the name of a built-in module |
| `S065` | semantic | a variable, parameter, function or type named like a module |
| `S067` | semantic | a module's member used without its module (the message names the module: "write math.sqrt") |
| `S070` | semantic | a module has no member of that name |
| `S010` | semantic | an argument of the wrong type - among others a value that is not text given to a `string.` function (`string.len([1, 2])`) |
| `R012` | runtime | range outside a string or array (`string.substring`, `slice`) |
| `R013` | runtime | `pop` of an empty array |
| `R014` | runtime | negative count (`string.repeat`) |
| `R015` | runtime | `string.char_code` of a string that is not one character |
| `R016` | runtime | `string.from_char_code` of an invalid code point |
| `R017` | runtime | a file or directory cannot be read |
| `R018` | runtime | a file cannot be written or removed |
| `R019` | runtime | a command cannot be started |
| `R020` | runtime | a function variable called before a function was assigned to it |
| `R022` | runtime | a byte count outside 1..8 (`bytes.to_int`, `bytes.from_int`), or a graphics setting outside its range |
| `R023` | runtime | `random.int(low, high)` with `low > high` |
| `L009` | lexical | a decimal literal that is too large |
| `S052` | semantic | `for ... in` over a value that is not an array, a string or a map |
| `R025` | runtime | a decimal result that is too large (or does not fit in an `int`) |
| `R026` | runtime | `to_float` of a text that is not a number |
| `R027` | runtime | `math.sqrt` of a negative number, `math.log` of zero or less, `math.pow` with no real result |
| `R031`, `R032` | runtime | `string.format`: too few or too many values; `{x}`/`{b}` on a value that is not a whole number |
| `S059`-`S063` | semantic | enums: declared inside a function, empty, a member listed twice, a member that does not exist, made from something other than an `int` or a `string` |
| `R034` | runtime | `Enum(n)` with a position outside the enum |
| `R035` | runtime | `Enum("name")` with a name that is not a member |

`S025` concerns built-in functions used as values (user functions are values); `P006` concerns
calls of literals such as `1(2)`. The [language reference](language.md) lists the other module
codes (`S066` private member, `P017` a module that is not at the top level).

---

## 12. Pashto names

All names are drafts and await review by a native speaker, like the rest of the Pashto
vocabulary (`src/core/language_pashto.cpp`). Multi-word names are written with spaces, as in
ordinary Pashto. The lexer joins the words into one name. `thunderc --language-info --lang ps`
lists them all.

**Modules and keywords.** In a Pashto program the English module names are accepted too
(`math.جذر`, `راوړه graphics`); the members are always written in Pashto.

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `import` | `راوړه` | | `module` | `ټولګی` |
| `math` | `ریاضي` | | `string` | `متن` |
| `random` | `تصادفي` | | `bytes` | `بایټونه` |
| `os` | `سیسټم` | | `file` | `فایل` |
| `graphics` | `ګرافیک` | | `func(...)` type | `دنده(...)` |
| function value | `<دنده name>` | | | |

**The default module:**

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `print` | `چاپ` | | `println` | `کرښه چاپ` |
| `len` | `اوږدوالی` | | `str` | `متن کړه` |
| `to_int` | `عدد کړه` | | `to_float` | `اعشاري کړه` |
| `hex` | `شپاړسیز کړه` | | `bin` | `دوه ییز کړه` |
| `to_byte` | `بایټ کړه` | | `array` | `لړۍ` |
| `inspect` | `کتنه` | | `dump` | `وښایه` |
| `copy` | `کاپي` | | `input` | `ولوله` |
| `end_of_input` | `د ننوت پای` | | `name_of` | `د دندې نوم` |
| `has` | `لري` | | `remove` | `لیرې کړه` |
| `keys` | `کیلۍ` | | `values` | `ارزښتونه` |
| `get` | `ترلاسه کړه` | | `items` | `توکي` |
| `first` | `لومړی` | | `second` | `دوهم` |
| `swap` | `ځای بدلول` | | `zip` | `جوړې کړه` |
| `enumerate` | `شمېره ورکړه` | | `push` | `ورزیات کړه` |
| `pop` | `وروستی واخله` | | `insert` | `ننباسه` |
| `remove_at` | `له ځایه لیرې کړه` | | `slice` | `ټوټه` |
| `concat` | `سره نښلوه` | | `reverse` | `سرچپه کړه` |
| `sort` | `ترتیب کړه` | | `range` | `واټن` |
| `sum` | `مجموعه` | | `contains` | `شامل دی` |
| `index_of` | `ځای یې` | | `map` | `پر هر یو` |
| `filter` | `چاڼ کړه` | | `reduce` | `راټول کړه` |

**math, string, random and bytes:**

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `math.abs` | `ریاضي.مطلق` | | `math.min` | `ریاضي.کمینه` |
| `math.max` | `ریاضي.اعظمه` | | `math.round` | `ریاضي.نیژدې کړه` |
| `math.floor` | `ریاضي.کښته کړه` | | `math.ceil` | `ریاضي.پورته کړه` |
| `math.sqrt` | `ریاضي.جذر` | | `math.pow` | `ریاضي.توان` |
| `math.exp` | `ریاضي.نمايي` | | `math.log` | `ریاضي.لوګارتم` |
| `math.sin` | `ریاضي.جیب` | | `math.cos` | `ریاضي.تتمیم جیب` |
| `math.tan` | `ریاضي.ظل` | | `math.pi` | `ریاضي.پای` |
| `string.substring` | `متن.برخه` | | `string.trim` | `متن.پاک کړه` |
| `string.to_upper` | `متن.لوی توري کړه` | | `string.to_lower` | `متن.کوچنی توري کړه` |
| `string.split` | `متن.وېشه` | | `string.join` | `متن.یو ځای کړه` |
| `string.replace` | `متن.بدل کړه` | | `string.starts_with` | `متن.پیلېږي په` |
| `string.ends_with` | `متن.پایېږي په` | | `string.repeat` | `متن.تکرار کړه` |
| `string.char_code` | `متن.د توري کوډ` | | `string.from_char_code` | `متن.د کوډ توری` |
| `string.is_int` | `متن.عدد دی` | | `string.is_number` | `متن.شمېره ده` |
| `string.format` | `متن.بڼه کړه` | | `string.len` | `متن.اوږدوالی` |
| `string.contains` | `متن.شامل دی` | | `string.index_of` | `متن.ځای یې` |
| `string.reverse` | `متن.سرچپه کړه` | | `string.slice` | `متن.ټوټه` |
| `string.to_int` | `متن.عدد کړه` | | `string.to_float` | `متن.اعشاري کړه` |
| `random.int` | `تصادفي.عدد` | | `random.float` | `تصادفي.اعشاري` |
| `random.seed` | `تصادفي.بنسټ` | | | |
| `bytes.from_string` | `بایټونه.له متنه` | | `bytes.to_string` | `بایټونه.متن کړه` |
| `bytes.to_int` | `بایټونه.عدد کړه` | | `bytes.from_int` | `بایټونه.له عدده` |

**os and file:**

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `os.args` | `سیسټم.دلیلونه` | | `os.get_env` | `سیسټم.د چاپېریال ارزښت` |
| `os.exit` | `سیسټم.پای ته ورسوه` | | `os.platform` | `سیسټم.عامل سیسټم` |
| `os.Platform` (enum) | `سیسټم.پلیټفارم` | | `os.run` | `سیسټم.وچلوه` |
| `os.run_output` | `سیسټم.د چلولو پایله` | | `os.list_dir` | `سیسټم.د پوښۍ توکي` |
| `os.current_dir` | `سیسټم.اوسنۍ پوښۍ` | | `os.time` | `سیسټم.وخت` |
| `os.clock_ms` | `سیسټم.ساعت` | | `os.sleep` | `سیسټم.انتظار` |
| `os.date_time` | `سیسټم.نېټه او وخت` | | | |
| `file.read` | `فایل.ولوله` | | `file.write` | `فایل.ولیکه` |
| `file.append` | `فایل.ورزیات کړه` | | `file.exists` | `فایل.شته` |
| `file.remove` | `فایل.ړنګ کړه` | | `file.read_bytes` | `فایل.بایټونه ولوله` |
| `file.write_bytes` | `فایل.بایټونه ولیکه` | | `file.append_bytes` | `فایل.بایټونه ورزیات کړه` |

The `graphics` names (and `graphics.Color`, `ګرافیک.رنګونه`) are listed in the
[graphics reference](graphics.md), section 10. `thunderc --translate ps` and the IDE's Translate
button convert all of these between the languages. Imported files must be in the program's
language, so translate them too.

---

## 13. Under the bonnet

* **Built-in modules cost nothing.** A qualified name such as `math.sqrt` is resolved by the
  semantic analyser; the bytecode calls the built-in by its number (`CALL_BUILTIN`), exactly as
  before. The Bytecode tab and `thunderc --emit asm` show the qualified name next to the number.
* **Two instructions for function values.** `FUNC_REF f` pushes a function value. `CALL_VALUE n`
  pops a function value and calls it with the `n` arguments below it, so `steps[1](3)` compiles to
  "push 3, push `steps[1]`, `CALL_VALUE 1`". The unset function value is `FUNC_REF 65535`.
* **Built-ins that call Thunder functions.** `map`, `filter` and `reduce` call the function from
  inside the VM: the VM pushes the arguments, enters the function and runs instructions until it
  returns, then continues the built-in. The debugger, the step limit and the call-depth limit all
  apply to these calls.
* **Input.** The VM reads from an input stream (standard input for `thundervm`); the IDE connects
  a pipe to it and shows the output as it arrives rather than line by line, so a prompt is visible
  while the program waits.
* **Exit status.** `os.exit(code)` stops the VM normally, not as an error. The status is in the
  `exit_code` field of `thundervm --result-json` and of the debugger's trace.
* **Bytecode format version 4** adds the list of source files to the header and a file number to
  each line-table entry ([bytecode reference](bytecode.md), section 4).
* **Where the code is.** Built-in names, modules and signatures: `src/core/builtins.cpp`; type
  checking: `check_builtin` in `src/core/sema.cpp`; execution: `src/core/vm_builtins.cpp`;
  operating-system services: `src/core/os.cpp`; file loading: `ModuleLoader` in
  `src/core/compiler.cpp`.
