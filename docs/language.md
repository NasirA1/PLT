# The Thunder Language Reference

Thunder is a small, statically typed, C-family language designed for teaching how
programming languages work. Every program goes through the same pipeline you can watch in
the IDE:

```
source text --lexer--> tokens --parser--> syntax tree --semantic analysis--> typed tree + symbols
            --code generator--> bytecode (.thc) --virtual machine--> output
```

Source files use the extension `.th` and are UTF-8 text. Compiled bytecode files use `.thc`.
Programs can be written in **English** or **Pashto** (section 11).

---

## 1. A first program

```thunder
// Statements at the top level run from top to bottom, one per line.
println("Hello, Thunder!")

struct Point
    x: int
    y: int

func square(n: int): int
    return n * n

for var i = 1, i <= 3, i = i + 1
    println(i, " squared is ", square(i), " at ", Point(i, square(i)))
```

Blocks are marked by **indentation**, as in Python: the body of a function, `if`, `else`, `while`,
`for` or `struct` is the lines that follow it indented one level further (four spaces; a tab counts
as four). There are no braces and no semicolons, and the logical operators are words: `and`,
`or`, `not`. The IDE indents the next line automatically after
a line that opens a block and un-indents after `return`, `break`, `continue` and `pass`.

## 2. Lexical structure

| Element | Examples | Notes |
| --- | --- | --- |
| Comments | `// to end of line`, `/* block */` | Block comments do not nest. |
| Identifiers | `count`, `is_prime`, `_tmp`, `شمیر` | A letter or `_` followed by letters, digits or `_`. Letters from any script are allowed. |
| Integer literals | `0`, `42`, `۴۲` | Decimal only. Digits may be ASCII, Pashto/Persian (۰–۹) or Arabic-Indic (٠–٩). The largest value is 2⁶³−1. |
| Decimal literals | `3.14`, `0.5`, `۳٫۱۴` | A digit, a point, a digit: `1.` and `.5` are not numbers, and `t.0` stays a tuple element. There is no exponent form; write `1000000.0`. Pashto programs may use `٫`. |
| String literals | `"text"`, `"tab\there"`, `«متن»` (Pashto programs only) | Must end on the same line. Escapes: `\n` `\t` `\r` `\0` `\"` `\\` (and `\«` `\»`). |
| Boolean literals | `true`, `false` | |
| Keywords | `var const func struct if else while for in break continue return pass import native` | Reserved. |
| Type names | `int float bool string` | Reserved. Struct names are also types. |
| Operators | `+ - * / % == != < <= > >= = += -= *= /= %= .` and the words `and or not` | The words are reserved. |
| Punctuation | `( ) { } [ ] , :` | Pashto programs write the comma `،`. |

Symbols that other languages use but Thunder does not are reported with a hint: `&&` → `and`,
`||` → `or`, `!x` → `not x` (but `!=` is "not equal"), and `;` → `,` (lexical error `L002`).

Spaces, tabs and Unicode bidirectional marks (used in right-to-left text) separate tokens but are
otherwise ignored. **Line breaks matter**: they end statements (section 6).

## 3. Types

| Type | Values | Default value |
| --- | --- | --- |
| `int` | 64-bit signed integers | `0` |
| `float` | decimals: 64-bit binary floating point (IEEE 754) | `0.0` |
| `bool` | `true`, `false` | `false` |
| `string` | immutable sequences of Unicode characters | `""` |
| `T[]` | arrays of `T`, e.g. `int[]`, `string[][]`, `Point[]` | `[]` (empty) |
| `(T1, T2, ...)` | tuples: a fixed number of values of given types, e.g. `(int, string)` (section 8) | a tuple of the elements' defaults |
| `{K: V}` | maps (associative arrays) from keys of type `K` (`int`, `bool` or `string`) to values of type `V` (section 8) | `{}` (empty) |
| a struct name | values of that struct (section 8) | a struct whose fields all have their defaults |
| `func(T1, ...): R` | function values: references to a function, e.g. `func(int): bool` (section 7) | "no function" (calling it is runtime error `R020`) |

Functions without a return type return nothing (*void*). Their result cannot be used as a value.

There is **one implicit conversion**: an `int` is widened to a `float` wherever a `float` is
wanted — in arithmetic and comparisons (`1 + 0.5`, `n / 2.0`), in an initialiser or assignment
(`var x: float = 2`), in an argument, in a `return`, and among the elements of an array literal
(`[1, 2.5]` is a `float[]`). Nothing else converts by itself: a `float` never becomes an `int` on
its own (use `round`, `floor`, `ceil` or `to_int`), and `"x = " + 5` is a type error; write
`"x = " + str(5)`, or pass several arguments to `println`.

A `float` holds about 15–17 significant decimal digits and stores numbers in base two, so `0.1`
is not exact and `0.1 + 0.2 == 0.3` is `false`. Compare decimals with a tolerance:
`abs(a - b) < 0.000001`. Thunder never produces infinity or "not a number": dividing by `0.0`
is runtime error `R001` and a result that is too large is `R025`.

Arrays, maps and structs are **reference types**: assigning one, or passing it to a function,
shares the same value. Use `copy(value)` for an independent copy. Tuples are immutable, so sharing
makes no difference for them. Arrays are created with a literal (`[1, 2, 3]`) or with
`array(n, value)`, maps with `{key: value, ...}`, tuples with `(a, b)` and structs by calling the
struct's name. There is no `new` keyword: Thunder manages memory automatically (reference
counting; section 8 explains why there are no cycles).

## 4. Declarations

```thunder
var count: int = 10      // explicit type and initial value
var name = "Thunder"     // type inferred from the value (string)
var total: int           // no initial value: starts at the type's default (0)
const limit = 100        // a constant can never be assigned again
var grid: int[][] = []   // an empty array needs an explicit type
var origin: Point        // a Point with all fields at their defaults
var ages: {string: int} = {}   // an empty map needs an explicit type, too
var (low, high) = (1, 9) // unpacking a tuple declares one variable per element
```

A variable needs a type annotation, an initial value, or both. A constant always needs a value.
A constant struct or array cannot be replaced, but its fields or elements can still be changed.

**Scope.** A name is visible from its declaration to the end of the enclosing block. An inner
block may declare a name that *shadows* an outer one. Declaring the same name twice in the same
scope is an error.

**Globals.** Variables declared at the top level of a file are *globals*. Top-level code can only
use a global after its declaration, but functions can use any global. All globals start with their
default value before the program runs.

## 5. Expressions

Operators from highest to lowest precedence. All binary operators are left-associative.

| Precedence | Operators | Operand types | Result |
| --- | --- | --- | --- |
| 9 | `f(args)` call, `a[i]` / `m[key]` index, `s.field`, `t.0` tuple element | | |
| 8 | `-x` | `int`, `float` | the same type |
| 7 | `*` `/` | two `int`s or two `float`s | the same type |
| 7 | `%` | `int` | `int` |
| 6 | `+` `-` | two `int`s or two `float`s (also `string + string`) | the same type / `string` |
| 5 | `<` `<=` `>` `>=` | two numbers or two `string`s | `bool` |
| 4 | `==` `!=` | two values of the same `int`, `float`, `bool`, `string` or tuple type | `bool` |
| 3 | `not x` | `bool` | `bool` |
| 2 | `and` | `bool` | `bool` (short-circuit) |
| 1 | `or` | `bool` | `bool` (short-circuit) |

* Integer division rounds toward zero (`-7 / 2` is `-3`); `7 / 2.0` is `3.5`. `%` has the sign of
  the left operand and is only defined for whole numbers.
* Integer overflow, division by zero (whole or decimal), a decimal result that is too large and
  out-of-range indexes are **runtime errors**. Thunder has no undefined behaviour.
* `s[i]` on a string gives a one-character string. Strings cannot be modified.
* Strings compare in Unicode code point order; tuples compare element by element. Arrays, maps
  and structs cannot be compared with `==`.
* `a and b` evaluates `b` only if `a` is true; `a or b` evaluates `b` only if `a` is false.
* `not` binds less tightly than a comparison, as in Python: `not a == b` means `not (a == b)`, and
  `not a and b` means `(not a) and b`.

## 6. Statements, line breaks and indentation

A statement ends at the end of its line; there is no statement separator:

```thunder
var x = 1
var y = 2
x = x + y
```

A line break does **not** end a statement when the line ends inside parentheses or brackets, or
right after an operator (including `and`, `or`, `not`), so long statements can be split over
several lines:

```thunder
var ok = width > 0 and
    height > 0
println("area: ",
        width * height)
```

An operator at the **start** of a line always begins a new statement. Unlike JavaScript, a line
never silently joins the one above it: `x = a` followed by a line `-b` is two statements. Every
statement has its own line (`x = 1` and `y = 2` on one line is syntax error `P010`). `return` at the
end of a line returns no value.

**Blocks are indented.** A line that is indented more than the line before it opens a block; the
block ends at the next line that is indented less (the lexer turns these changes into `INDENT` and
`DEDENT` tokens, which you can see in the IDE's Tokens tab). Every `if`, `else`, `while`, `for`,
`func` and `struct` must be followed by an indented block; `pass` is the statement that does
nothing, for a block that is intentionally empty. Lines inside brackets and continuation lines
(after an operator) may be indented freely. Un-indenting to a level that no enclosing
block has is an error (`L008`); indenting a line that does not follow a block opener is an error
too (`P013`). Conditions do not need parentheses, but may have them.

```thunder
x = x + 1                 // assignment (to a variable, an array element or a struct field)
x += 1                    // the same, written shorter; also -= *= /= %=
values[i] = 0
values[i] += 10           // values[i] and i are each evaluated only once
p.x = 3
println(x)                // an expression statement (usually a call)

if x > 0                  // conditions must be bool; the body is the indented block
    println("positive")
else if x < 0
    println("negative")
else
    println("zero")

while x > 0
    x = x - 1

for var i = 0, i < 10, i = i + 1   // initialise, condition, step - separated by commas
    if i == 3
        continue          // skip to the step (i = i + 1)
    if i == 7
        break             // leave the loop

for value in values       // one round per element, with no index to get wrong
    println(value)
```

**`for ... in`** walks a collection: the elements of an array, the characters of a string, or the
**keys** of a map (`items(m)` gives the `(key, value)` pairs). The loop variable is declared by
the loop and only exists inside it; `break` and `continue` work as in any other loop. The
collection expression is evaluated once, before the first round, but the position is compared
with the collection's **current** length each round, so adding to or removing from an array
while walking it does change the loop; walk `copy(a)` if you need to change `a`. (Walking a map
is always safe: its keys are taken before the first round.) Several names in parentheses unpack
a tuple:

```thunder
for (position, name) in enumerate(names)
    println(position, ": ", name)
```

**Compound assignment.** `x += e` means `x = x + e`, and `-=` `*=` `/=` `%=` work the same way.
The left-hand side may be a variable, an array or map element, or a struct field, and it is
evaluated exactly once (`a[next()] += 1` calls `next()` once). `+=` also joins strings.

Assignment is a statement, not an expression, so `if x = 1` is a syntax error.

```thunder
while true
    pass                  // an empty block needs "pass"
```

## 7. Functions

```thunder
func gcd(a: int, b: int): int
    while b != 0
        var t = b
        b = a % b
        a = t
    return a

func greet(name: string)   // no return type: returns nothing
    println("Hello, ", name)
```

* Functions are declared at the top level only. They may be called before the line where they
  are declared.
* Parameters behave like local variables. `int`, `bool` and `string` arguments are copied; arrays
  and structs are shared.
* Every path through a function with a return type must end in `return value`.
* Recursion is allowed. The VM limits the call depth (10,000 by default) and reports
  "stack overflow" if the limit is exceeded.
* **`main`**: if a program defines `func main()` (no parameters, no return type), it is called after
  the top-level statements.

### Functions as values

A function's name without parentheses is a **value** of a function type, which can be stored,
passed and returned, and called like the function itself:

```thunder
func double(n: int): int
    return n * 2

var f: func(int): int = double
println(f(21))                    // 42
println(map([1, 2, 3], double))   // [2, 4, 6]
```

Any expression whose value is a function can be called: `handlers[i](x)`, `pick(true)(5)`. There
are no nested functions, so there are no closures. The [library reference](library.md), section 7,
covers the details and `map`, `filter` and `reduce`.

### Modules

`import "lib/geometry"` makes everything the file `lib/geometry.th` declares usable in this file.
`import os` imports the built-in operating-system module. See the
[library reference](library.md), section 2.

### Native functions (C++)

`import native "mathx"` loads a C++ library, and `native func` says what it provides:

```thunder
import native "mathx"
native func fast_hypot(x: float, y: float): float
println(fast_hypot(3, 4))                      // 5.0
```

A native function is checked and called like any other, but its body is machine code rather
than Thunder. Only `int`, `float`, `bool`, `string` and callbacks (`func(...)`) may cross the
boundary, and C++ can call Thunder functions back. The whole interface — one C header, one
entry point — is described in the [native interface reference](native.md), with a working
example in `samples/native/`.

## 8. Structs

A struct groups named, typed **fields** into a new type:

```thunder
struct Point
    x: int
    y: int

struct Rectangle
    corner: Point
    width: int
    height: int
    label: string
```

Fields are separated by line breaks (a `,` also works). Structs are declared at the top level
and can be used before their declaration.

**Creating values.** Call the struct's name, giving the fields either all **in order** or all **by
name**. Fields left out when naming arguments get their default values:

```thunder
var a = Point(1, 2)
var b = Point(y: 2, x: 1)                 // same value
var r = Rectangle(corner: a, width: 10)   // height 0, label ""
```

Named arguments are evaluated from left to right, as written.

**Fields** are read and written with `.`: `r.corner.x = 5`, `shapes[0].width`.

**References.** A struct value is shared, like an array: after `var s = r`, changing `s.width` also
changes `r.width`. `copy(r)` makes an independent copy, including nested structs and arrays.

**Printing.** `str(p)` and `println(p)` show a struct on one line, in the same form used to create
it: `Point(x: 1, y: 2)`. `inspect(value)` returns a detailed, multi-line description with each field's
type, and `dump(value)` prints it:

```
Rectangle
    corner: Point
        x: int = 1
        y: int = 2
    width: int = 10
    height: int = 0
    label: string = ""
```

**No recursive structs.** A struct may not contain itself, directly or through other structs,
arrays, tuples or maps (a `struct Node` with a field `children: Node[]` is rejected). Every value
is therefore finite and has a default, and reference counting can never leak a cycle.

### Tuples

A tuple groups a **fixed number of values of possibly different types**. Its type lists the
element types; a tuple needs at least two elements.

```thunder
var pair = (1, "one")                  // type (int, string)
println(pair.0, " ", pair.1)           // elements are read with .0, .1, ...
var (number, word) = pair              // unpacking into variables

func min_max(values: int[]): (int, int)   // a function returning two results
    return (values[0], values[len(values) - 1])
var (lo, hi) = min_max([1, 5, 9])
```

Tuples are **immutable** (`pair.0 = 2` is an error) and compare element by element with `==`.
`len(t)` is the number of elements, `str(t)` is `(1, "one")`. Tuples may contain any values,
including arrays, maps and other tuples, and may be stored in arrays: `(int, int)[]`.

### Maps (associative arrays)

A map stores values under **keys**. Keys may be `int`, `bool` or `string`; all keys of a map have
one type and all values another, written `{K: V}`.

```thunder
var ages = {"nasir": 40, "sara": 7}    // type {string: int}
ages["ali"] = 12                       // adds a key
ages["sara"] = 8                       // replaces a value
println(ages["nasir"])                 // 40; a missing key is a runtime error (R011)
println(has(ages, "omar"))             // false
remove(ages, "sara")
println(keys(ages), values(ages))      // ["nasir", "ali"] [40, 12]
var counts: {string: int} = {}         // an empty map needs a type annotation
```

Maps keep their entries in **insertion order**; `keys(m)` and `values(m)` return arrays in that
order, `len(m)` is the number of entries. Like arrays, maps are reference types: `copy(m)` makes an
independent copy. `str(m)` and `println(m)` show `{"nasir": 40, "ali": 12}`.

## 9. Built-in functions

| Function | Description |
| --- | --- |
| `print(v, ...)` | Writes the values with no separator and no newline. |
| `println(v, ...)` | Like `print`, followed by a newline. |
| `len(x)` | Number of characters in a string, or of elements in an array, tuple or map. |
| `str(v)` | Text form of any value (`str([1, 2])` is `"[1, 2]"`, `str(p)` is `"Point(x: 1, y: 2)"`). |
| `to_int(x)` | The whole number of a text such as `"-42"` or `"۴۲"`, or of a decimal (`to_int(2.9)` is `2`). A runtime error if the text is not a number. |
| `to_float(x)` | The decimal of a text such as `"2.5"`, or of a whole number. A runtime error if the text is not a number. |
| `array(n, v)` | A new array of `n` copies of `v`. Arrays and structs in `v` are copied, so `array(3, array(3, 0))` is a proper 3×3 grid. |
| `inspect(v)` | A detailed, multi-line description of a value (structs field by field, with types). |
| `dump(v)` | Prints `inspect(v)` followed by a newline. |
| `copy(v)` | An independent (deep) copy of a struct, array, tuple or map. |
| `has(m, k)` | `true` if map `m` contains key `k`. |
| `remove(m, k)` | Removes key `k` and its value from `m` (nothing happens if it is absent). |
| `keys(m)` | The keys of `m` as an array, in insertion order. |
| `values(m)` | The values of `m` as an array, in insertion order. |
| `abs(x)` `min(a, b)` `max(a, b)` | On whole numbers, decimals (and `min`/`max` on texts). |
| `round(x)` `floor(x)` `ceil(x)` | A decimal as a whole number: nearest, down, up. |
| `sqrt` `pow` `exp` `log` `sin` `cos` `tan` `pi()` | Decimal maths; angles are in radians. |
| `random()` | A decimal from 0 (included) to 1 (excluded); `random_int(a, b)` for whole numbers. |

The **library** of further built-ins is described in the [library reference](library.md):
input (`input`, `end_of_input`), text (`substring`, `trim`, `to_upper`, `split`, `join`,
`replace`, ...), tuples and pairs (`first`, `second`, `swap`, `zip`, `enumerate`, `items`), arrays
(`push`, `pop`, `insert`, `remove_at`, `slice`, `sort`, `range`, `sum`, `contains`, `index_of`,
...), higher-order functions (`map`, `filter`, `reduce`), random numbers (`random_int`) and the
`os` module (files, commands, time, program arguments). The `graphics` module (drawing,
animation, keyboard and mouse) is described in the [graphics reference](graphics.md).

Built-in names cannot be used for user functions or structs. The `os` and `graphics` functions
are an exception: their names are reserved only in programs that import their module.

## 10. Errors

Each phase reports the mistakes it can detect. Every message has a stable code:

| Phase | Codes | Examples |
| --- | --- | --- |
| Lexical analysis | `L001`–`L009` | unexpected character `#`, unterminated string, number too large, inconsistent un-indent |
| Syntax analysis | `P001`–`P016` | `expected ')'`, two statements on one line (`P010`), missing indented block (`P012`), braces (`P014`), `import` inside a block (`P016`) |
| Semantic analysis | `S001`–`S055` | undeclared name, type mismatch, unknown field, recursive struct, missing `return`, map key type, module not found (`S045`), `os` function without `import os` (`S049`), `for ... in` over a value that is not a collection (`S052`), a native function using a type that cannot cross to C++ (`S053`) |
| Warnings | `W001`–`W003` | unused variable, unreachable code, unknown language directive |
| Runtime | `R001`–`R030` | division by zero, integer overflow, index out of bounds, stack overflow, step limit, missing map key, range out of bounds (`R012`), file cannot be read (`R017`), decimal result too large (`R025`), `sqrt` of a negative number (`R027`), a native library that cannot be loaded (`R028`) |

The codes added for modules, input and the library are listed in the
[library reference](library.md), section 9.

Compilation stops at the first phase that reports an error. For example, a program with a syntax
error is never type-checked. The parser recovers from errors, so it can report several syntax
errors at once.

## 11. Writing programs in Pashto (پښتو)

Every word of the language has a Pashto form, and Pashto programs print their results in Pashto.
Start a Pashto file with the directive `// ژبه: ps` (or `// lang: ps`), or choose پښتو in the IDE.
`thunderc --lang ps` also works.

| English | Pashto | English | Pashto |
| --- | --- | --- | --- |
| `var` | `متغیر` | `int` | `عدد` |
| `in` (in `for ... in`) | `په` | `float` | `اعشاري` |
| `native` (C++) | `بهرنی` | | |
| `const` | `ثابت` | `bool` | `منطقي` |
| `func` | `دنده` | `string` | `متن` |
| `struct` | `جوړښت` | `true` / `false` | `رښتیا` / `دروغ` |
| `if` / `else` | `که` / `کنه` | `print` / `println` | `چاپ` / `کرښه چاپ` |
| `while` | `ترڅو` | `len` | `اوږدوالی` |
| `for` | `لپاره` | `str` / `to_int` | `متن کړه` / `عدد کړه` |
| `break` | `ودرېږه` | `array` | `لړۍ` |
| `sqrt` / `pow` | `جذر` / `توان کړه` | `to_float` | `اعشاري کړه` |
| `min` / `max` | `کمینه` / `اعظمه` | `abs` / `round` | `مطلق` / `نږدې کړه` |
| `continue` | `دوام` | `inspect` / `dump` | `کتنه` / `وښایه` |
| `return` | `راګرځه` | `copy` | `کاپي` |
| `pass` | `تېر` | `has` / `remove` | `لري` / `لرې کړه` |
| `import` | `راوړه` | `keys` / `values` | `کیلۍ` / `ارزښتونه` |
| `and` / `or` / `not` | `او` / `یا` / `نه` | `main` (the function) | `اصلي` |

* **Multi-word names:** `کرښه چاپ`, `متن کړه`, `عدد کړه` and `لرې کړه` are written with a space between
  their words, as in ordinary Pashto. The lexer joins the words into one token (any number of spaces or
  tabs on the same line may separate them), so `کرښه چاپ("سلام")` calls `println`.
* **Digits:** `۰۱۲۳۴۵۶۷۸۹` (ASCII and Arabic-Indic `٠١٢` digits also work). Decimals are written
  with the Arabic decimal separator `٫` (`۳٫۱۴`); `.` is accepted too, and results are printed
  with `٫`.
* **Punctuation and quotes:** Pashto programs separate with the Arabic comma `،` (also in `for`
  headers: `لپاره متغیر ش = ۰، ش < ۳، ش = ش + ۱`) and use the plain quotes `"..."`. The ASCII comma,
  the percent sign `٪`, `×` / `÷` for multiplication and division, and `«...»` strings are accepted
  as well; the translator writes the preferred forms (`،`, `%`, `"`).
* **Names:** the function that runs after the top-level code may be called `اصلي` (or `main`), and
  `key_pressed()` names keys in Pashto (`"کیڼ"`, `"ښي"`, `"پورته"`, `"ښکته"`, `"تشه"`, ...; see the
  [graphics reference](graphics.md)). Single-letter names follow the Arabic-script convention of
  mathematics: `ن` for n, `س`/`ص` for x/y, `الف`/`ب` for a/b, as in the samples.
* **Output:** a Pashto program prints numbers with Pashto digits, `رښتیا`/`دروغ` for booleans, and
  `«»` quotes and `، ` separators inside arrays and structs.
* **Messages:** compiler and runtime errors are reported in Pashto.
* **Reading direction:** a Pashto program is read right to left, token by token; names written in
  Latin letters keep their own direction. The IDE's editor lays the program out that way (see the
  README, "Right-to-left programs").

```thunder
// ژبه: ps
جوړښت ټکی
    س: عدد
    ص: عدد

دنده جمع(الف: عدد، ب: عدد): عدد
    راګرځه الف + ب

متغیر ټ = ټکی(ص: ۵، س: ۱۰)
که ټ.س > ۰ او نه ټ.ص == ۰
    کرښه چاپ(جمع(۲۰، ۲۲)، " "، ټ)      // ۴۲ ټکی(س: ۱۰، ص: ۵)
```

**Translating programs.** `thunderc --translate ps program.th` prints the program with every
keyword, built-in and digit in Pashto. `--translate en` translates back, and the IDE's
**Translate** button does the same. Comments and your own names are left as they are, and a name
that is reserved in the target language gets a `_` suffix.

## 12. Grammar (EBNF)

```ebnf
program        = { declaration } EOF ;
declaration    = function | native_func | struct | import | statement ;
import         = "import" ( STRING | IDENTIFIER | "native" STRING ) end ;
                 (* a file, a built-in module, or a C++ library *)

function       = "func" IDENTIFIER "(" [ parameter { "," parameter } ] ")" [ ":" type ] block ;
native_func    = "native" "func" IDENTIFIER "(" [ parameter { "," parameter } ] ")" [ ":" type ] end ;
parameter      = IDENTIFIER ":" type ;
struct         = "struct" IDENTIFIER INDENT { IDENTIFIER ":" type [ "," ] } DEDENT ;
type           = ( "int" | "float" | "bool" | "string" | IDENTIFIER
                 | "(" type "," type { "," type } ")"                   (* tuple *)
                 | "{" type ":" type "}"                                (* map *)
                 | function_type | "(" function_type ")" ) { "[" "]" } ;
function_type  = "func" "(" [ type { "," type } ] ")" [ ":" type ] ;

statement      = ( var_decl | simple_stmt | "break" | "continue" | "pass" | "return" [ expression ] ) end
               | if_stmt | while_stmt | for_stmt | for_each_stmt ;
end            = NEWLINE | (* before DEDENT or the end of the file *) ;
var_decl       = ( "var" | "const" ) ( IDENTIFIER [ ":" type ] [ "=" expression ]
                                    | "(" IDENTIFIER "," IDENTIFIER { "," IDENTIFIER } ")" "=" expression ) ;
simple_stmt    = expression [ ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) expression ] ;
block          = INDENT { declaration } DEDENT ;           (* the indented lines that follow *)
if_stmt        = "if" expression block [ "else" ( if_stmt | block ) ] ;
while_stmt     = "while" expression block ;
for_stmt       = "for" ( for_header | "(" for_header ")" ) block ;
for_header     = [ var_decl | simple_stmt ] "," [ expression ] "," [ simple_stmt ] ;
for_each_stmt  = "for" ( IDENTIFIER | "(" IDENTIFIER { "," IDENTIFIER } ")" ) "in" expression block ;

expression     = unary { binary_op unary } ;              (* precedence: see section 5 *)
binary_op      = "or" | "and" | "==" | "!=" | "<" | "<=" | ">" | ">=" | "+" | "-" | "*" | "/" | "%" ;
unary          = "not" unary | "-" unary | postfix ;  (* "not" applies to a whole comparison *)
postfix        = primary { "(" [ arguments ] ")" | "[" expression "]" | "." IDENTIFIER | "." INTEGER } ;
                 (* a call's callee may be any postfix expression except a literal *)
arguments      = argument { "," argument } ;
argument       = [ IDENTIFIER ":" ] expression ;          (* names are for struct fields *)
primary        = INTEGER | DECIMAL | STRING | "true" | "false" | IDENTIFIER
               | "(" expression ")" | "(" expression "," expression { "," expression } ")"   (* tuple *)
               | "[" [ expression { "," expression } ] "]"                                   (* array *)
               | "{" [ expression ":" expression { "," expression ":" expression } ] "}" ;   (* map *)
```

NEWLINE is a line break outside parentheses, brackets and braces. INDENT and DEDENT are produced
by the lexer when a line (outside brackets, and not continuing an expression) is indented more or
less than the line before it. A binary operator, postfix operator or argument list may not start a
new line (section 6).

**How the parser works.** Statements are parsed by *recursive descent*: each rule above is one
function in `src/core/parser.cpp`. Expressions are parsed with **Pratt's top-down operator
precedence** algorithm. Each binary operator has a *binding power* (its precedence), and the
parser keeps extending the left operand while the next operator binds more tightly than the
operator it is currently inside. A line break outside brackets stops this extension, which is how
statements end without separators. Pratt parsing handles prefix operators, calls, indexing and
field access naturally and produces the tree directly.

## 13. Designed for extension

* **Other natural languages.** The lexer never compares against keyword text. It looks words,
  punctuation, digits and quotes up in a *language pack* (`src/core/language_pack.cpp`, with Pashto
  in `src/core/language_pashto.cpp`). A pack also defines how values are printed and the text of
  every message. Columns in error messages count characters, not bytes, so positions stay correct
  for Arabic-script text.
* **New types and operators.** Types are described by `Type` (`include/thunder/types.hpp`),
  operators by a binding-power table, and instructions by a single table in
  `include/thunder/bytecode.hpp`.
