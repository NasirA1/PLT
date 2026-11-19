# The Thunder Language Reference

*Part of the [Thunder SDK reference](README.md).*

Thunder is a small, statically typed, C-family language designed for teaching how
programming languages work. Every program goes through the same pipeline you can watch in
the IDE:

```
source text --lexer--> tokens --parser--> syntax tree --semantic analysis--> typed tree + symbols
            --code generator--> bytecode (.thc) --virtual machine--> output
```

Source files use the extension `.th` and are UTF-8 text. Compiled bytecode files use `.thc`.
Programs can be written in **English** or **Pashto** (section 14).

---

## 1. A first program

```thunder
# Statements at the top level run from top to bottom, one per line.
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
`for`, `struct`, `mixin`, `interface`, `module` or `with` is the lines that follow it indented one
level further (four spaces; a tab counts as four). There are no braces and no semicolons, and the logical operators
are words: `and`, `or`, `not`. The IDE indents the next line automatically after
a line that opens a block and un-indents after `return`, `break`, `continue` and `pass`.

## 2. Lexical structure

| Element | Examples | Notes |
| --- | --- | --- |
| Comments | `# to end of line`, `''' block '''` | Block comments do not nest. |
| Identifiers | `count`, `is_prime`, `_tmp`, `شمیر` | A letter or `_` followed by letters, digits or `_`. Letters from any script are allowed. |
| Integer literals | `0`, `42`, `۴۲`, `0x2A`, `0b101010` | Decimal digits may be ASCII, Pashto/Persian (۰–۹) or Arabic-Indic (٠–٩). `0x` starts a hexadecimal number (digits `0-9 a-f A-F`), `0b` a binary one (`0` and `1`): the same value, written so that its bits can be seen (see the bitwise operators, section 5, and the IDE's Bits tab). The largest value is 2⁶³−1. |
| Decimal literals | `3.14`, `0.5`, `۳٫۱۴` | A digit, a point, a digit: `1.` and `.5` are not numbers, and `t.0` stays a tuple element. There is no exponent form; write `1000000.0`. Pashto programs may use `٫`. |
| String literals | `"text"`, `"tab\there"`, `«متن»` (Pashto programs only) | Must end on the same line. Escapes: `\n` `\t` `\r` `\0` `\"` `\\` (and `\«` `\»`). Each control character also has a Pashto letter - `\ن` is `\n`, `\ت` is `\t`, `\ر` is `\r`, and a zero in any script is `\0`; both spellings work in any program, and `thunderc --translate` converts between them. |
| Boolean literals | `true`, `false` | |
| Nothing | `nothing` | The empty struct reference (section 8). |
| Keywords | `var const func struct mixin interface enum module if else while for in with break continue return pass import native nothing` | Reserved. |
| Type names | `int float bool string byte` | Reserved. Struct and enum names are also types. (`string` is also the name of the built-in text module: `string.trim(s)`, section 10.) |
| Operators | `+ - * / % == != < <= > >= & \| ^ ~ << >> = += -= *= /= %= .` and the words `and or not` | The words are reserved. |
| Punctuation | `( ) { } [ ] , :` | Pashto programs write the comma `،`. |
| Annotations | `@test`, `@test("adds up")`, `@mock` | `@`, a name and optionally a text in brackets, on a line of their own before a declaration (section 9, "Test files and annotations"). |

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
| `byte` | one byte: a whole number from 0 to 255. `byte[]` is raw data - the contents of a file, a sound, a network packet (section 10 and the library reference, "Bytes") | `0` |
| an enum name | one of the members listed in the enum's declaration, e.g. `Direction.north` or `graphics.Color.red` (section 8) | the first member |
| `T[]` | arrays of `T`, e.g. `int[]`, `string[][]`, `Point[]` | `[]` (empty) |
| `(T1, T2, ...)` | tuples: a fixed number of values of given types, e.g. `(int, string)` (section 8) | a tuple of the elements' defaults |
| `{K: V}` | maps (associative arrays) from keys of type `K` (`int`, `bool` or `string`) to values of type `V` (section 8) | `{}` (empty) |
| a struct name | values of that struct (section 8); a struct of a module is written with the module's name, `geometry.Point` (section 9) | a struct whose fields all have their defaults (the constructor is not run) |
| an interface name | any struct value whose struct implements the interface (section 8), or `nothing` | `nothing` |
| `func(T1, ...): R` | function values: references to a function, e.g. `func(int): bool` (section 7) | "no function" (calling it is runtime error `R020`) |

Functions without a return type return nothing (*void*). Their result cannot be used as a value.

There are **two implicit conversions**, both widenings that never lose information: an `int` is
widened to a `float` wherever a `float` is wanted — in arithmetic and comparisons (`1 + 0.5`,
`n / 2.0`), in an initialiser or assignment (`var x: float = 2`), in an argument, in a `return`,
and among the elements of an array literal (`[1, 2.5]` is a `float[]`) — and a `byte` is widened
to an `int` (or on to a `float`) in the same places, so `b + 1`, `b & 0x0F`, `b == 200` and
`data[i] * 2` all work on the byte's value as an `int`. Nothing narrows by itself: a `float` never
becomes an `int` on its own (use `math.round`, `math.floor`, `math.ceil` or `to_int`), an `int`
never becomes a `byte` (use `to_byte(n)`, which keeps the low 8 bits) - except that a
**whole-number literal from 0 to 255 written where a byte is wanted is that byte**
(`var b: byte = 200`, `data[i] = 0`, `var data: byte[] = [72, 105]`) - and `"x = " + 5` is a
type error; write `"x = " + str(5)`, or pass several arguments to `println`.

A `float` holds about 15–17 significant decimal digits and stores numbers in base two, so `0.1`
is not exact and `0.1 + 0.2 == 0.3` is `false`. Compare decimals with a tolerance:
`math.abs(a - b) < 0.000001`. Thunder never produces infinity or "not a number": dividing by `0.0`
is runtime error `R001` and a result that is too large is `R025`.

Arrays, maps and structs are **reference types**: assigning one, or passing it to a function,
shares the same value. Use `copy(value)` for an independent copy. Tuples are immutable, so sharing
makes no difference for them. Arrays are created with a literal (`[1, 2, 3]`) or with
`array(n, value)`, maps with `{key: value, ...}`, tuples with `(a, b)` and structs by calling the
struct's name. There is no `new` keyword: Thunder manages memory automatically (reference
counting, with a shutdown sweep for cycles of references).

## 4. Declarations

```thunder
var count: int = 10      # explicit type and initial value
var name = "Thunder"     # type inferred from the value (string)
var total: int           # no initial value: starts at the type's default (0)
const limit = 100        # a constant can never be assigned again
var grid: int[][] = []   # an empty array needs an explicit type
var origin: Point        # a Point with all fields at their defaults
var ages: {string: int} = {}   # an empty map needs an explicit type, too
var (low, high) = (1, 9) # unpacking a tuple declares one variable per element
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
| 13 | `f(args)` call, `a[i]` / `m[key]` index, `s.field`, `t.0` tuple element | | |
| 12 | `-x`, `~x` bitwise NOT | `int`, `float` (`~` only `int`) | the same type |
| 11 | `*` `/` | two `int`s or two `float`s | the same type |
| 11 | `%` | `int` | `int` |
| 10 | `+` `-` | two `int`s or two `float`s (also `string + string`) | the same type / `string` |
| 9 | `<<` `>>` bit shifts | `int` (the count 0..63) | `int` |
| 8 | `&` bitwise AND | `int` | `int` |
| 7 | `^` bitwise XOR | `int` | `int` |
| 6 | `\|` bitwise OR | `int` | `int` |
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
* **Bits.** The bitwise operators work on the 64 bits of an `int` (two's complement). They bind
  *tighter* than comparisons, as in Python, so `flags & mask != 0` means `(flags & mask) != 0` -
  not C's trap. `~a` equals `-a - 1`. A shift count outside 0..63 is a runtime error; `<<` drops
  the bits shifted off the top, and `>>` keeps the sign (an arithmetic shift: `-16 >> 2` is `-4`).
  The IDE's **Bits** tab (Under the bonnet) shows any of these operations bit by bit.
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
`func`, `struct`, `enum`, `module` and `with` must be followed by an indented block; `pass` is the
statement that does nothing, for a block that is intentionally empty. Lines inside brackets and
continuation lines (after an operator) may be indented freely. Un-indenting to a level that no
enclosing block has is an error (`L008`); indenting a line that does not follow a block opener is an error
too (`P013`). Conditions do not need parentheses, but may have them.

```thunder
x = x + 1                 # assignment (to a variable, an array element or a struct field)
x += 1                    # the same, written shorter; also -= *= /= %=
values[i] = 0
values[i] += 10           # values[i] and i are each evaluated only once
p.x = 3
println(x)                # an expression statement (usually a call)

if x > 0                  # conditions must be bool; the body is the indented block
    println("positive")
else if x < 0
    println("negative")
else
    println("zero")

while x > 0
    x = x - 1

for var i = 0, i < 10, i = i + 1   # initialise, condition, step - separated by commas
    if i == 3
        continue          # skip to the step (i = i + 1)
    if i == 7
        break             # leave the loop

for value in values       # one round per element, with no index to get wrong
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
    pass                  # an empty block needs "pass"
```

**`with`** is the statement for resources - an open file, a network connection - that must be
closed when the program is done with them: its block closes the resource however it ends. It is
described in section 11, after the modules it relies on.

## 7. Functions

```thunder
func gcd(a: int, b: int): int
    while b != 0
        var t = b
        b = a % b
        a = t
    return a

func greet(name: string)   # no return type: returns nothing
    println("Hello, ", name)
```

* Functions are declared at the top level of a file, or as members of a module (section 9).
  They may be called before the line where they are declared.
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
println(f(21))                    # 42
println(map([1, 2, 3], double))   # [2, 4, 6]
```

Any expression whose value is a function can be called: `handlers[i](x)`, `pick(true)(5)`. The
[library reference](library.md), section 7, covers the details and `map`, `filter` and `reduce`.

### Closures: function values written in place

A function value can also be **written where it is needed**, with `func` and no name. The body is
either a single statement on the same line — a single expression when there is a return type,
whose value the function returns — or, outside parentheses, an indented block:

```thunder
var shout = func(line: string) println(line + "!")
shout("go")                                     # go!

println(map([1, 2, 3], func(n: int): int n * n)) # [1, 4, 9] - one expression, returned

var step = func(n: int): int                    # a block body (outside parentheses)
    if n % 2 == 0
        return n / 2
    return 3 * n + 1
println(step(7))                                # 22
```

Such a function may use the **variables around it** — it is a *closure*. A captured variable
moves into a shared box: the closure and the function that made it both read and write the same
variable, even after that function has returned.

```thunder
func make_counter(): func(): int
    var count = 0
    var bump = func(): int
        count += 1
        return count
    return bump

var a = make_counter()
var b = make_counter()
println(a(), a(), b())    # 1 2 1 - each counter captured its own count
```

Parameter types are always written; the return type is written when there is one, exactly as in
a declaration. Globals are not captured (the body simply uses them), and a captured `const`
stays constant. A loop variable is **one variable**: every closure made in the loop shares it and
sees its final value, as in Python — capture a copy (`var mine = i`) inside the body for one
value per round. In a trace or the debugger a closure appears as a function named `func@line`.
Closures carry the state of event handlers and callbacks — see `on_click` in section 8.

Functions that belong together can be bundled in a **module** and called with its name in front,
`geometry.area(r)`; the language's own functions are organised that way too (`math.sqrt(x)`,
`string.trim(s)`). Modules are section 9, the built-in modules section 10.

### Native functions (C++)

`import native "mathx"` loads a C++ library, and `native func` says what it provides:

```thunder
import native "mathx"
native func fast_hypot(x: float, y: float): float
println(fast_hypot(3, 4))                      # 5.0
```

A native function is checked and called like any other, but its body is machine code rather
than Thunder. Only `int`, `float`, `bool`, `string` and callbacks (`func(...)`) may cross the
boundary, and C++ can call Thunder functions back. The whole interface — one C header, one
entry point — is described in the [native interface reference](native.md), with a working
example in `lib/native/`.

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
or in a module (section 9: `geometry.Point`), and can be used before their declaration.

**Creating values.** Call the struct's name, giving the fields either all **in order** or all **by
name**, or giving **no arguments** at all. Fields left out when naming arguments get their
default values, and with no arguments every field has its default; positional arguments must
give every field (`S009`):

```thunder
var a = Point(1, 2)
var b = Point(y: 2, x: 1)                 # same value
var r = Rectangle(corner: a, width: 10)   # height 0, label ""
var o = Point()                           # Point(x: 0, y: 0)
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

**Recursive structs and `nothing`.** A struct may contain itself - the shape of a linked list
or a tree node. `nothing` is the empty struct reference: it ends a chain, it is what a recursive
field defaults to, and it compares with `==` and `!=` (a struct equals `nothing` only when no
struct was ever assigned there). Reading or writing a field of `nothing` stops the program with
runtime error `R033`. `nothing` has no type of its own: it takes the struct type of its context
(an annotated declaration, a field, a parameter, or the other side of a comparison).

```thunder
struct Node
    value: int
    next: Node                                # a struct may contain itself

var tail = Node(value: 2, next: nothing)      # nothing ends the chain
var head = Node(value: 1, next: tail)
while head != nothing
    println(head.value)
    head = head.next
```

A cycle of references (`a.next = a`) is legal; printing one shows `...` where the value repeats,
and `copy()` keeps the shape instead of recursing for ever.

### Methods and constructors

A struct can carry **behaviour** as well as data. Inside the struct's body, after the fields,
come the **constructor** — a member spelled like the struct — and the **methods**:

```thunder
struct Button
    _title: string                  # a private field (see below)
    on_click(): func()              # a member holding a function value (a closure)

    Button(title: string)           # the constructor: Button("OK") runs it
        _title = title

    func get_title(): string        # a method: b.get_title()
        return _title

    func draw()
        println("[", _title, "]")

var b = Button("OK")                # construct...
b.draw()                            # ...and call methods with '.'
b.on_click = func() println("!")    # wire the closure member up
b.on_click()                        # and call it the same way
```

There is **no `self` parameter to write**: inside a method, a bare member name means the member
of the value the method was called on — `_title` above reads the receiver's field, and one
method calls another the same way (`draw()` inside a method means "draw this value"). A
parameter or local of the same name hides the member, and **built-in names stay reserved**, as
everywhere: inside a method of a struct with its own `push` method, `push(_items, v)` is still
the built-in (the struct's own is called on a value: `other.push(v)`).

With a constructor, `Button("OK")` builds the struct with every field at its **default value**
and then runs the constructor's body on it; the field-list forms (`Point(1, 2)`,
`Point(y: 2, x: 1)`) belong to structs **without** a constructor. A constructor takes no return
type and no type parameters (`S079`), and each member name is used once (`S080`).

A member written `on_click(): func()` is an ordinary **field of function type** — the
parentheses mark it as callable. It is assigned a function value (usually a closure) and called
like a method. An interface may declare one too (below), and then every struct implementing the
interface has it without writing it.

### Private members: a leading underscore

A member whose name **starts with `_`** is private: only the struct's own methods may read, write
or call it (`S076`). `_title` and helper methods like `_walk(node)` keep a structure's insides its
own business; everything else is public. It is the same mark a module uses for its private
members (`socket._send`, section 9): inside its owner, a leading underscore means "mine".

### Interfaces

An **interface** names what a value can do, not what it is:

```thunder
interface Widget
    func draw()
    func width(): int

interface Clickable
    on_click(): func()              # a callable member: a field every Clickable struct has
```

A struct lists the interfaces it implements after `:`; the compiler checks that every promised
method is there with the right signature (`S074`, `S075`):

```thunder
struct Button: Widget, Clickable
    ...                             # must have draw() and width(); on_click comes with Clickable

struct Label: Widget
    text: string
    func draw()
        println(text)
    func width(): int
        return len(text)
```

A variable (or field, parameter, array element...) of an interface type accepts **any struct
that implements it**, and calling a member through it finds the struct's own method while the
program runs — *dynamic dispatch*:

```thunder
var screen: Widget[] = [Button("OK"), Label("hello")]
for widget in screen
    widget.draw()                   # each value answers with its own draw
```

An interface value is a reference like a struct: it may be `nothing` (comparing with `==` works;
calling a member of `nothing` is runtime error `R036`), and assigning a struct to it neither
copies nor converts anything. Interfaces are **nominal**: a struct implements only what it
declares after `:`, so the error for a missing member points at the struct, not at a distant
call. An interface value has no fields of its own — its members are used by calling them
(`S078`); to reach the struct's fields, keep a variable of the struct type.

**Closure members are written once.** A callable member that an interface declares
(`on_click(): func()`) is a *field*, and every struct that implements the interface - directly,
or through a mixin that implements it (below) - **has that field automatically**; the struct
does not repeat it. (Writing it anyway is allowed, and then it must have the interface's type,
`S075`.) The field starts as "no function yet", like any field of function type: calling it
before a function is assigned to it is runtime error `R020`.

```thunder
interface Clickable
    on_click(): func()

struct Key: Clickable                # Key has two fields: label, then on_click
    label: string

var presses = 0
var k = Key(label: "A")              # on_click: no function yet
k.on_click = func() presses += 1     # whoever wires the key up assigns it
var control: Clickable = k
control.on_click()                   # presses is 1
```

The interface's fields come after the struct's own, so positional construction gives them too
(`Key("A", func() presses += 1)`); naming the arguments, or giving none, leaves them unset.

### Mixins: fields and methods included in structs

A **mixin** is declared like a struct - fields and methods - with the keyword **`mixin`**
(Pashto `ګډون`) instead of `struct`. It is **not a type**: it is a bundle that a struct includes
by listing it after `:`, and its fields and methods are copied into the struct as if written
there. It is composition by copying - there is no base-class hierarchy and no `super`.

```thunder
interface Describable
    func describe(): string

mixin Named: Describable             # a mixin: fields and methods worth sharing
    name: string
    func describe(): string
        return "I am " + name

struct Robot: Named                  # Robot now has name and describe(), and is Describable
    power: int
    Robot(power_value: int)
        name = "robot"               # the mixin's field, like our own
        power = power_value

struct Dog: Named
    func describe(): string          # our own method wins over the mixin's
        return "Woof, I am " + name

var things: Describable[] = [Robot(9), Dog(name: "Rex")]
for thing in things
    println(thing.describe())        # I am robot, then Woof, I am Rex
```

* **Only a mixin may be included.** A name after `:` is an interface (implemented) or a mixin
  (included); a plain struct there is `S082` ("'Named' is a struct, not a mixin: only a mixin
  (declared with 'mixin Named') can be included after ':'"), and anything else is `S071`.
* **A mixin is not a type.** It cannot be the type of a variable, field or parameter, and
  cannot be constructed (`Named("x")`) - `S083`. It has **no constructor** (`S084`): the struct
  that includes it builds the value, and sets the mixin's fields like its own.
* **One level deep.** A mixin may implement interfaces - the including struct then implements
  them too, and gets their closure members - but may not include a mixin of its own (`S072`,
  reported at the mixin). A mixin cannot be generic (`S073`, "a mixin cannot be generic").
* **The struct wins.** A struct's own method of the same name **overrides** the mixin's; a field
  declared both in the struct and in the mixin is reported (`S029`). The copied fields come after
  the struct's own, which is their order for positional arguments and printing.

**A struct without a body.** A struct that lists bases after `:` may have no body at all, when
everything it needs comes from them - `struct Button: Greets, Clickable` is a struct with the
methods of the mixin `Greets` and the field `on_click` of the interface `Clickable`. The test
framework's mocks are often written so ([testing reference](testing.md)). A struct without
bases still needs its indented block.

### Generic structs and functions

A struct or function may take **type parameters**, written in `<angle brackets>` after its name. It is
a template: each use with concrete types makes an instance, and the types are **deduced** - from
the arguments, or from the type the result is assigned to - never written at a call:

```thunder
struct Stack<T>
    items: T[]

func stack_push<T>(stack: Stack<T>, value: T)
    push(stack.items, value)

func new_stack<T>(): Stack<T>
    var empty: T[] = []
    return Stack(items: empty)

var words = Stack(items: ["a"])          # Stack<string>, deduced from the argument
var numbers: Stack<int> = new_stack()    # Stack<int>, deduced from the declaration
stack_push(words, "b")                   # stack_push<string>
```

Written-out instance types (`Stack<int>`, `Pair<string, int>`) appear anywhere a type may:
annotations, fields, parameters, results, even nested (`Stack<Stack<int>>`). An instance's body
is checked with its concrete types, so a template that compares its elements with `<` works for
numbers and strings and is reported when instantiated with anything else. When a type parameter
cannot be deduced, the compiler says so (`S058`); give the declaration a type. The data
structures and algorithms of the SDK ([datastructures](datastructures.md),
[algorithms](algorithms.md)) are written this way.

### Tuples

A tuple groups a **fixed number of values of possibly different types**. Its type lists the
element types; a tuple needs at least two elements.

```thunder
var pair = (1, "one")                  # type (int, string)
println(pair.0, " ", pair.1)           # elements are read with .0, .1, ...
var (number, word) = pair              # unpacking into variables

func min_max(values: int[]): (int, int)   # a function returning two results
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
var ages = {"nasir": 40, "sara": 7}    # type {string: int}
ages["ali"] = 12                       # adds a key
ages["sara"] = 8                       # replaces a value
println(ages["nasir"])                 # 40; a missing key is a runtime error (R011)
println(has(ages, "omar"))             # false
remove(ages, "sara")
println(keys(ages), values(ages))      # ["nasir", "ali"] [40, 12]
var counts: {string: int} = {}         # an empty map needs a type annotation
```

Maps keep their entries in **insertion order**; `keys(m)` and `values(m)` return arrays in that
order, `len(m)` is the number of entries. Like arrays, maps are reference types: `copy(m)` makes an
independent copy. `str(m)` and `println(m)` show `{"nasir": 40, "ali": 12}`.

### Enums

An **enum** is a type with a fixed set of named values, its *members*. It is declared like a
struct, one member per line (or several on a line, separated by commas), and may only appear at
the top level of a file or in a module (where its members are written `shapes.Shape.circle`):

```thunder
enum Direction
    north
    east
    south
    west

var heading = Direction.north          # a member is written Type.member
println(heading)                       # north - members print by name
println(to_int(Direction.south))       # 2 - each member has a position: 0, 1, 2, ...
println(Direction(1), Direction("west")) # east west - the member at a position / with a name
println(Direction.count, Direction.values) # 4 [Direction.north, Direction.east, Direction.south, Direction.west]
for d in Direction.values
    print(d, " ")
println(Direction.north < Direction.west, heading == Direction.north) # true true
```

* **Members compare by position**: `==`, `!=`, `<`, `<=`, `>` and `>=` work between members of
  the same enum, `sort` orders an array of members by position, and a member may be a map key.
* **`to_int(m)`** gives a member's position and **`str(m)`** its name; `Direction(n)` and
  `Direction("name")` go the other way when the program runs (runtime errors `R034` for a
  position that is out of range and `R035` for an unknown name). Inside arrays, maps and the
  debugger a member is shown as `Direction.north`, so it is never mistaken for a string.
* **`Direction.values`** is a new array of every member in order, and **`Direction.count`** how
  many there are. A variable of an enum type without an initialiser holds the first member.
* Enums are **values, not references**: there is nothing to copy or share. A member has no
  fields; arithmetic on members is a type error (`Direction.north + 1`). Use the position:
  `Direction((to_int(d) + 1) % Direction.count)` is the next member, wrapping round.
* The toolchain declares two enums of its own, members of their built-in modules (section 10):
  **`graphics.Color`** (`import graphics`: `graphics.Color.red`, and likewise `green`, `blue`,
  `white`, `black`, `yellow`, `orange`, `pink`, `gray`, `brown`, `purple`, `sky` and `gold`;
  `graphics.Colour` and `grey` are accepted too) - every drawing function takes a `graphics.Color`
  where it takes a colour string - and **`os.Platform`** (`import os`: `os.Platform.linux`,
  `os.Platform.windows`, `os.Platform.macos`, `os.Platform.unknown`), which `os.platform()`
  answers with. In Pashto they are `ګرافیک.رنګونه` and `سیسټم.پلیټفارم`, with Pashto member
  names (`ګرافیک.رنګونه.شین`).
* Mistakes are caught at compile time: a member that does not exist (`S062`), making a member
  from anything but an `int` or a `string` (`S063`), a duplicate member (`S061`), an empty enum
  (`S060`), an enum declared inside a function (`S059`) and a name already used by a struct or
  function (`S028`).

## 9. Modules

A **module** is a named bundle of declarations that belong together: functions, structs, enums,
variables, constants and native functions. It is declared at the top level of a file, and its
members are the indented block that follows the keyword `module` and the module's name:

```thunder
module geometry
    struct Point
        x: int
        y: int

    const ORIGIN_X = 0
    var made = 0                              # a variable that belongs to the module

    func point(x: int, y: int): Point         # inside the module, the short names work
        made += 1
        return Point(x, y)

    func distance(a: Point, b: Point): float
        return math.sqrt(to_float(_square(a.x - b.x) + _square(a.y - b.y)))

    func _square(v: int): int                 # private: only geometry may use it
        return v * v

var p: geometry.Point = geometry.point(3, 4)  # outside, the module's name comes first
var origin = geometry.point(geometry.ORIGIN_X, 0)
println(geometry.distance(p, origin))         # 5.0
println(geometry.made, " ", p)                # 2 geometry.Point(x: 3, y: 4)
geometry.made = 0                             # a module's variable can be assigned from outside
```

* **Outside the module**, every member is written with the module's name and a dot in front - a
  *qualified name*: `geometry.point(1, 2)` for a function, `geometry.made` for a variable (also on
  the left of `=`), `geometry.ORIGIN_X` for a constant, `geometry.Point` for a struct, in calls and
  in types alike (`var p: geometry.Point`, `geometry.Point[]`, `func(int, int): geometry.Point`),
  and `geometry.Shape.circle` for a member of an enum the module declares.
* **Inside the module** the short names work too (`Point(x, y)`, `made += 1`), unless a local
  variable or parameter of the same name hides the member. The qualified name always works, inside
  as well as out.
* **Private members.** A member whose name starts with `_` is private to its module: only the
  module's own members may use it, and `geometry._square(2)` anywhere else is `S066`.
* **A module only declares.** A statement inside a module is `S047` - it would run whenever the
  file is imported. Modules do not nest: a module inside another module or inside a function is
  syntax error `P017`. A module's variables are globals with a qualified name: they start with
  their default value before the program runs, and get their initial value when the program
  reaches the module's declaration, so top-level code uses them after the declaration (functions,
  as with any global, at any time).
* **Module names are reserved.** No variable, parameter, function, struct or enum may be named
  like a module that is visible (`var geometry = 1` is `S065`). That includes the built-in modules
  (`var math = 0`), and `os`, `file` and `graphics` once they are imported. Each module is declared
  once, in one place (`S064`): two files cannot both add to `geometry`, and the name of a built-in
  module (`math`, `string`, ...) cannot be used for a module of one's own.
* **Members do not clash with other names.** A member always has its module in front, so
  `geometry.area`, `shapes.area` and a top-level `area` are three different functions, and a
  module may have a member called `len` or `push` (`stack.push`) without disturbing the built-in.
* **A built-in keeps its meaning inside a module.** In `module stack`, a plain `push(...)` is still
  the built-in `push` for arrays; the module's own `push` is written `stack.push(...)` there:

```thunder
module stack
    struct Stack
        items: int[]

    func push(s: Stack, value: int)
        push(s.items, value)                  # the built-in push, for arrays

    func push_twice(s: Stack, value: int)
        stack.push(s, value)                  # this module's own push
        stack.push(s, value)

var numbers = stack.Stack([])
stack.push_twice(numbers, 7)
println(numbers)                              # stack.Stack(items: [7, 7])
```

* **`name_of(f)`** gives the name a function value was declared with, its module and all:
  `name_of(geometry.point)` is `"geometry.point"`.

**Modules and files.** A *file* is what is loaded; a *module* is a namespace inside it. `import
"lib/geometry"` reads the file `lib/geometry.th` (looked for next to the importing file, then in
the module paths given with `-I`, then in the SDK's `lib/thunder`) and makes everything it declares
usable here - its modules, and any declarations it makes outside a module. An imported file is a
**library file**: it only declares, so importing it never prints anything, and each file is loaded
only once however often it is imported. A library file usually holds one module (the samples'
`lib/geometry.th` declares `module geometry`), but may hold several - `import "sockets"` brings the
four modules `tcp`, `udp`, `socket` and `host` - and a program may declare modules of its own next
to its statements. The [library reference](library.md) ("Modules") has the details of finding,
loading and translating files.

```thunder
import "lib/geometry"      # a file: lib/geometry.th, which declares the module geometry

var room = geometry.Rectangle(4, 3)
println(geometry.area(room))
```

**Test suites.** In a test file, a module marked `@test` is a **test suite**: each of its public
functions without parameters is a test, the suite's name is printed above them, its counts are
kept apart, and the suite can be run on its own by its name. A struct marked `@test` is a suite
too. The next subsection and the [testing reference](testing.md) describe it.

**Under the bonnet.** Before it checks anything, the analyser renames every member to its
qualified name (`point` inside `geometry` becomes `geometry.point`), so the Symbols tab, the
bytecode, the debugger, call stacks and error messages all show the qualified names. Modules cost
nothing when the program runs: a call to `geometry.point` is an ordinary call.

### Test files and annotations

An **annotation** marks a declaration for a tool - here, for the test framework. It is `@` and a
name, optionally followed by one text in brackets, on a line of its own (a comment may follow)
**before** the declaration it marks; several annotations may stand on consecutive lines. There
are two:

| Annotation | Pashto | Marks |
| --- | --- | --- |
| `@test`, `@test("description")` | `@آزموینه` (`@ازموینه` is accepted too) | a test: a function without parameters and without a result; or a **suite** - a struct (each public method without parameters is a test, run on a new instance made by the constructor, and followed by `close()` when there is one) or a module (each public function without parameters is a test); or one method or function of a suite that is not marked itself |
| `@mock` | `@ساختګی` | a struct that stands in for the interfaces it lists, recording every call to their methods |

The English names are accepted in every language, and `thunderc --translate` converts them. An
unknown name is syntax error `P021` ("unknown annotation '@tset' (the annotations are @test,
@mock)"), and an annotation that no declaration follows is `P022`. An annotation on a
declaration it cannot mark - `@test` on a mixin, an interface or an enum, `@mock` on a
function - is `S085`; an argument it does not take is `S086`.

```thunder
import "lib/shop"                       # the code under test comes from an import

@test                                   # the test "largest of a list"
func largest_of_a_list()
    expect.equal(shop.largest([3, 9, 2]), 9)

@test("palindromes")                    # a description names the test
func reads_both_ways()
    expect.is_true(shop.is_palindrome("level"))
```

**Test files and program files.** A file with any `@test` is a **test file**. The compiler
imports the module `"testing"` into it automatically and appends a call
`test.run(suite, name, body)` for each test, in declaration order, followed by `test.finish()`;
so a test file is run like any program, and its arguments choose the tests (a test's name, a
suite's name, or `suite/name`). The two kinds of file are kept apart:

* a test file holds declarations only - **no top-level statements and no `main()`** (`S091`);
* a **production program** (whose main file has no `@test`) may not `import "testing"` or
  declare a `@mock`, in any of its files (`S092`);
* a file with tests **cannot be imported** (`S093`, at the `import`): the code under test lives
  in library files that the program and the test file both import. A helper file with mocks but
  no tests may be imported by test files.

The rules for tests themselves - no parameters and no result (`S087`), not generic (`S088`),
names unique in a suite (`S089`), a suite's constructor without parameters (`S090`), a mock
that lists an interface (`S094`) - and the whole framework are in the
[testing reference](testing.md).

## 10. Built-in functions and modules

The language's own functions are organised in modules too. The **default module** - printing,
conversions, arrays, maps, tuples and the higher-order functions - needs no prefix: `println`,
`len`, `push`, `map`. Every other built-in is written with its module in front, like a member of a
module of one's own: `math.sqrt(2.0)`, `string.trim(s)`, `random.int(1, 6)`.

| Function (default module) | Description |
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
| `name_of(f)` | The declared name of a function value, module and all (`"geometry.point"`). |

The default module also has input (`input`, `end_of_input`), tuples and pairs (`first`, `second`,
`swap`, `zip`, `enumerate`, `items`), arrays (`push`, `pop`, `insert`, `remove_at`, `slice`,
`concat`, `sort`, `range`, `sum`, `contains`, `index_of`, `reverse`, ...), the higher-order
functions (`map`, `filter`, `reduce`) and the number conversions `hex`, `bin` and `to_byte`.

The **built-in modules**:

| Module | Pashto | Needs | What it holds |
| --- | --- | --- | --- |
| `math` | `ریاضي` | - | Numbers and maths: `math.abs`, `math.min`, `math.max`, `math.round`, `math.floor`, `math.ceil`, `math.sqrt`, `math.pow`, `math.exp`, `math.log`, `math.sin`, `math.cos`, `math.tan`, `math.pi()`; angles are in radians. |
| `string` | `متن` | - | Text: `string.substring`, `string.trim`, `string.to_upper`, `string.split`, `string.join`, `string.replace`, `string.format`, ..., and text-only forms of default-module functions: `string.len`, `string.contains`, `string.index_of`, `string.reverse`, `string.slice`, `string.to_int`, `string.to_float` (the module is spelled like the type). |
| `random` | `تصادفي` | - | Random numbers: `random.int(a, b)`, `random.float()` (from 0 up to 1, not included), `random.seed(n)`. |
| `bytes` | `بایټونه` | - | Raw data: `bytes.from_string`, `bytes.to_string`, `bytes.to_int`, `bytes.from_int`. |
| `os` | `سیسټم` | `import os` | The operating system: program arguments, the environment, commands, directories, time, `os.platform()` and the enum `os.Platform`, `os.exit(code)`. |
| `file` | `فایل` | `import file` | Files: `file.read`, `file.write`, `file.append`, `file.exists`, `file.remove`, and `file.read_bytes`, `file.write_bytes`, `file.append_bytes`. |
| `graphics` | `ګرافیک` | `import graphics` | Drawing, animation, keyboard and mouse, and the enum `graphics.Color` ([graphics reference](graphics.md)). |

Every member, with its parameters and its Pashto name, is in the [library reference](library.md);
the HTML [API documentation](api/index.html) lists them module by module.

```thunder
import file

var words = string.split(string.trim("  to be or not to be  "), " ")
println(len(words), " words; the first in capitals: ", string.to_upper(words[0]))
println(math.sqrt(2.0), " ", math.max(3, 7), " ", math.round(2.5))
var die = random.int(1, 6)                     # 1, 2, 3, 4, 5 or 6
if file.exists("notes.txt")
    println(file.read("notes.txt"))
```

* `math`, `string`, `random` and `bytes` are always there. `os`, `file` and `graphics` reach
  outside the program, so a program says that it uses them: `import os` - the module's name, without
  quotes. A member used without the import is `S049`, with the import to add; until a program
  imports them, their names are free for its own use.
* A built-in written without its module is reported with the fix: `sqrt(2.0)` is `S067`, "'sqrt'
  belongs to the module 'math': write math.sqrt". A member that does not exist (`math.cube`) is
  `S070`.
* The default module's names cannot be used for functions or structs of one's own (`S004`); a
  module's names can, because they are always written with the module: a top-level `sqrt` does not
  clash with `math.sqrt`.
* Runtime errors name the qualified built-in: `math.sqrt(-1.0)` stops the program with "'math.sqrt'
  is not defined for -1.0" (`R027`).
* In Pashto the modules have Pashto names (table above) and their members too: `ریاضي.جذر(۲)`,
  `متن.پاک کړه(س)`. The English module names are accepted in Pashto programs as well, with the
  Pashto member names (`math.جذر`).

## 11. Resources: `with`

Some values stand for something outside the program that must be given back when the program is
done with it: an open file, a network connection. Forgetting to close one is an easy mistake - all
the more when a function returns early or a loop breaks out. **`with`** closes it when its block
ends, however the block ends:

```thunder
import file

struct Log
    _path: string
    _lines: string[]

    Log(path: string)
        _path = path
        _lines = []

    func write(line: string)
        push(_lines, line)

    func close()                              # with calls this when its block ends
        file.write(_path, string.join(_lines, "\n") + "\n")

with notes = Log("notes.txt")
    notes.write("first")
    notes.write("second")
# notes is closed here, and the file has been written
println(file.read("notes.txt"))
```

| Form | Meaning |
| --- | --- |
| `with name = resource` | Declares `name` for the block, holding the resource. |
| `with name: Type = resource` | The same with the type written out (for instance when the value is `nothing`). |
| `with resource` | Closes a value that already exists, usually a variable holding it, at the end of the block. |

* **When it is closed.** At the end of the block, or when `return`, `break` or `continue` leaves
  it - from however deep inside. Nested `with` blocks close the innermost first. A `return`'s value
  is computed *before* the resources are closed, so `return socket.receive_line(connection)` still
  reads from an open connection. This is RAII (*Resource Acquisition Is Initialisation*): the
  resource's lifetime is the block's.
* **What makes a resource, and what closes it.** Two rules:
  * A **struct** is an object: it is closed by its own **`close()` method** (section 8), so any
    struct of one's own becomes a resource the moment it has one. In Pashto the method is called
    `وتړه`, and the English name `close` is understood in every language. A struct without a
    `close()` method is not a resource (`S081`: "a 'Log' cannot be closed: give the struct a
    method 'close()'").
  * **Any other value** - an `int` handle, a string - is closed by the `close` of the module
    whose function produced it: `with h = handle.open()` closes with `handle.close(h)`. The
    resource expression must therefore be a call of a module's function; `with 5` or
    `with math.abs(5)` is `S068` ("a value of type 'int' cannot be closed by 'with': a resource
    is a struct with a close() method, or the result of a module's function, such as
    handle.open()").
* **`nothing`.** A struct resource that is `nothing` is not closed, so a function that can fail
  may answer `nothing` and the block checks: `if connection != nothing`.
* **A constant.** The name a `with` declares is a constant inside the block: assigning it is `S012`.
* **What is not covered.** When the program stops with a runtime error, or with `os.exit`, the
  blocks are not finished and nothing is closed; the operating system then frees the program's
  files and sockets itself.
* The sockets of the SDK are resources: `tcp.connect`, `tcp.listen`, `tcp.accept` and `udp.open`
  return a `socket.Socket`, or `nothing` when they fail ([sockets reference](sockets.md)).

The second rule in the smallest form - a module whose `open` hands out a plain `int` handle and
whose `close` takes it back:

```thunder
module handle
    func open(): int
        return 7                          # whatever the system gave out

    func close(h: int)
        println("closed ", h)

with h = handle.open()                    # closed by handle.close(h)
    println("using ", h)
```

**The place of the keyword.** English says "with the socket" and writes `with` at the start of the
line. Pashto says «ساکټ سره» - the socket, then "with" - and writes **`سره` after the thing it goes
with**: after the handle (`اړیکه سره = ...`, then `=` and the expression), or, when there is no
handle, after the value, where it ends the line (`ساکټ سره`); never «سره ساکټ» at the start:

```thunder
import "sockets"

with connection = tcp.connect("localhost", 7000)
    if connection != nothing
        connection.send_line("hello")
```

```thunder
# ژبه: ps
راوړه "sockets"

اړیکه سره = ټي_سي_پي.ونښلېږه("localhost"، ۷۰۰۰)
    که اړیکه != هېڅ
        اړیکه.کرښه_ولېږه("سلام")
```

The three forms in Pashto are `نوم سره = سرچینه`, `نوم سره: ډول = سرچینه` and `سرچینه سره`.
Writing the keyword in the other language's place is syntax error `P018` ("in this language 'with'
is written at the start of the line"; in Pashto, "'سره' is written after the handle"); the parser
still understands the line and carries on, so any further syntax errors are reported too. The
translator (`thunderc --translate`, the IDE's **Translate**) moves the keyword to its place in
the other language. The sample `39_with.th` shows every case.

## 12. Grammar (EBNF)

```ebnf
program        = { declaration } EOF ;
declaration    = { annotation } ( function | native_func | struct | mixin | interface | enum | module_decl )
               | import | statement ;
annotation     = "@" IDENTIFIER [ "(" STRING ")" ] NEWLINE ;   (* @test, @test("adds up"), @mock;
                                                                   which declaration each may mark is checked later (S085) *)
import         = "import" ( STRING | IDENTIFIER | "native" STRING ) end ;
                 (* a file, a built-in module, or a C++ library *)
module_decl    = "module" IDENTIFIER INDENT { member } DEDENT ;   (* only at the top level *)
member         = { annotation } ( function | native_func | struct | mixin | interface | enum ) | var_decl end ;
qualified_name = IDENTIFIER { "." IDENTIFIER } ;   (* geometry.Point, math.sqrt, graphics.Color.red *)

function       = "func" IDENTIFIER "(" [ parameter { "," parameter } ] ")" [ ":" type ] block ;
native_func    = "native" "func" IDENTIFIER "(" [ parameter { "," parameter } ] ")" [ ":" type ] end ;
parameter      = IDENTIFIER ":" type ;
struct         = "struct" IDENTIFIER [ "<" IDENTIFIER { "," IDENTIFIER } ">" ]
                 ( ":" qualified_name { "," qualified_name }      (* interfaces and mixins ... *)
                   ( INDENT { struct_member } DEDENT | end )      (* ... after which the body may be left out *)
                 | INDENT { struct_member } DEDENT ) ;
mixin          = "mixin" IDENTIFIER [ ":" qualified_name { "," qualified_name } ]   (* interfaces only *)
                 INDENT { struct_member } DEDENT ;               (* no constructor *)
struct_member  = IDENTIFIER ":" type [ "," ]                       (* a field *)
               | IDENTIFIER "(" ")" ":" function_type [ "," ]      (* a callable field (a closure) *)
               | { annotation } "func" IDENTIFIER "(" [ parameter { "," parameter } ] ")" [ ":" type ] block  (* a method *)
               | IDENTIFIER "(" [ parameter { "," parameter } ] ")" block ;  (* the constructor: spelled like the struct *)
interface      = "interface" IDENTIFIER INDENT { interface_member } DEDENT ;
interface_member = "func" IDENTIFIER "(" [ parameter { "," parameter } ] ")" [ ":" type ] end
               | IDENTIFIER "(" ")" ":" function_type [ "," ] ;    (* a callable member *)
enum           = "enum" IDENTIFIER INDENT { IDENTIFIER [ "," ] } DEDENT ;
type           = ( "int" | "float" | "bool" | "string" | "byte"
                 | qualified_name [ "<" type { "," type } ">" ]          (* a struct or enum *)
                 | "(" type "," type { "," type } ")"                   (* tuple *)
                 | "{" type ":" type "}"                                (* map *)
                 | function_type | "(" function_type ")" ) { "[" "]" } ;
function_type  = "func" "(" [ type { "," type } ] ")" [ ":" type ] ;

statement      = ( var_decl | simple_stmt | "break" | "continue" | "pass" | "return" [ expression ] ) end
               | if_stmt | while_stmt | for_stmt | for_each_stmt | with_stmt ;
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
with_stmt      = "with" with_head block                  (* English: the keyword first *)
               | IDENTIFIER "with" [ ":" type ] "=" expression block   (* Pashto: سره after the handle *)
               | expression "with" block ;               (* Pashto: no handle, سره ends the line *)
with_head      = [ IDENTIFIER [ ":" type ] "=" ] expression ;

expression     = unary { binary_op unary } ;              (* precedence: see section 5 *)
binary_op      = "or" | "and" | "==" | "!=" | "<" | "<=" | ">" | ">=" | "|" | "^" | "&" | "<<" | ">>"
               | "+" | "-" | "*" | "/" | "%" ;
unary          = "not" unary | "-" unary | "~" unary | postfix ;  (* "not" applies to a whole comparison *)
postfix        = primary { "(" [ arguments ] ")" | "[" expression "]" | "." IDENTIFIER | "." INTEGER } ;
                 (* a call's callee may be any postfix expression except a literal *)
arguments      = argument { "," argument } ;
argument       = [ IDENTIFIER ":" ] expression ;          (* names are for struct fields *)
primary        = INTEGER | DECIMAL | STRING | "true" | "false" | "nothing" | IDENTIFIER
               | "string"                  (* only before ".": the text module, string.trim *)
               | lambda
               | "(" expression ")" | "(" expression "," expression { "," expression } ")"   (* tuple *)
               | "[" [ expression { "," expression } ] "]"                                   (* array *)
               | "{" [ expression ":" expression { "," expression ":" expression } ] "}" ;   (* map *)
lambda         = "func" "(" [ parameter { "," parameter } ] ")" [ ":" type ]
                 ( simple_stmt | expression (* with a return type *) | block (* outside brackets *) ) ;
```

A qualified name in an expression (`geometry.point(1, 2)`, `math.sqrt(x)`,
`graphics.Color.red`) is a name followed by `"." IDENTIFIER` postfixes; the semantic analysis
decides whether a dot reaches into a module, a struct's field or an enum's member. In a type, the
dots are part of the type's name (`geometry.Point[]`). Each language uses one of the two forms of
`with_stmt` (English the first, Pashto the other two); the other language's form is reported
(`P018`) and then understood anyway.

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

## 13. Errors

Each phase reports the mistakes it can detect. Every message has a stable code:

| Phase | Codes | Examples |
| --- | --- | --- |
| Lexical analysis | `L001`–`L009` | unexpected character `#`, unterminated string, number too large, inconsistent un-indent |
| Syntax analysis | `P001`–`P022` | `expected ')'`, two statements on one line (`P010`), missing indented block (`P012`), braces (`P014`), `import` inside a block (`P016`), a `module` that is not at the top level (`P017`), `with` / `سره` written in the other language's place (`P018`), an `interface` that is not at the top level (`P019`), a function value without its body (`P020`), an unknown annotation (`P021`), an annotation that no declaration follows (`P022`) |
| Semantic analysis | `S001`–`S094` | undeclared name, type mismatch, unknown field, recursive struct, missing `return`, map key type, file not found (`S045`), a statement inside a module (`S047`), a member of `os`, `file` or `graphics` without its `import` (`S049`), `for ... in` over a value that is not a collection (`S052`), a native function using a type that cannot cross to C++ (`S053`), a module declared twice (`S064`), a name that is a module's (`S065`), a private member used outside its module (`S066`), a built-in written without its module (`S067`: "'sqrt' belongs to the module 'math': write math.sqrt"), `with` on a value that is neither a struct nor the result of a module's function (`S068`) or whose module has no `close` (`S069`), a member the module does not have (`S070`), a name after a struct's `:` that is neither an interface nor a mixin (`S071`), a mixin that includes a mixin of its own (`S072`), a generic mixin (`S073`: "a mixin cannot be generic"), a promised interface member that is missing (`S074`) or has the wrong signature (`S075`), a private member (`name_`) used outside its struct's methods (`S076`), a method or field the type does not have (`S077`), a method read instead of called (`S078`), a constructor with a return type (`S079`), a member name used twice (`S080`), a struct resource without a `close()` method (`S081`), a plain struct after `:` where only a mixin can be included (`S082`), a mixin used as a type or constructed (`S083`), a mixin with a constructor (`S084`); and for tests (the [testing reference](testing.md)): an annotation on the wrong kind of declaration (`S085`) or with an argument it does not take (`S086`), a test with parameters or a result (`S087`), a generic test (`S088`), a test name used twice in a suite (`S089`), a suite constructor with parameters (`S090`), a statement or `main()` in a test file (`S091`), test code in a production program (`S092`), an import of a file with tests (`S093`), a mock that lists no interface (`S094`) |
| Warnings | `W001`–`W003` | unused variable, unreachable code, unknown language directive |
| Runtime | `R001`–`R036` | division by zero, integer overflow, index out of bounds, stack overflow, step limit, missing map key, range out of bounds (`R012`), file cannot be read (`R017`), decimal result too large (`R025`), `math.sqrt` of a negative number (`R027`), a native library that cannot be loaded (`R028`), a field of `nothing` (`R033`), a method call on `nothing` (`R036`) |

The codes for modules, input and the library are also listed in the
[library reference](library.md).

Compilation stops at the first phase that reports an error. For example, a program with a syntax
error is never type-checked. The parser recovers from errors, so it can report several syntax
errors at once.

## 14. Writing programs in Pashto (پښتو)

Every word of the language has a Pashto form, and Pashto programs print their results in Pashto.
Start a Pashto file with the directive `# ژبه: ps` (or `# lang: ps`), or choose پښتو in the IDE.
`thunderc --lang ps` also works.

| English | Pashto | English | Pashto |
| --- | --- | --- | --- |
| `var` | `متغیر` | `int` | `عدد` |
| `in` (in `for ... in`) | `په` | `float` | `اعشاري` |
| `native` (C++) | `بهرنی` | `name_of` | `د دندې نوم` |
| `module` | `ټولګی` | `math` / `string` | `ریاضي` / `متن` |
| `with` (written after the handle, or after the value when there is no handle) | `سره` | `random` / `bytes` | `تصادفي` / `بایټونه` |
| `os` / `file` | `سیسټم` / `فایل` | `graphics` | `ګرافیک` |
| `const` | `ثابت` | `bool` | `منطقي` |
| `func` | `دنده` | `string` | `متن` |
| `struct` | `جوړښت` | `true` / `false` | `رښتیا` / `درواغ` |
| `interface` | `برسېر` | `mixin` | `ګډون` |
| `enum` | `ګڼنه` | `byte` | `بایټ` |
| `graphics.Color` / `os.Platform` (enums) | `ګرافیک.رنګونه` / `سیسټم.پلیټفارم` | `to_byte` | `بایټ کړه` |
| `if` / `else` | `که` / `کنه` | `print` / `println` | `چاپ` / `کرښه چاپ` |
| `while` | `ترڅو` | `len` | `اوږدوالی` |
| `for` | `تکرار` | `str` / `to_int` | `متن کړه` / `عدد کړه` |
| `break` | `ودرېږه` | `array` | `لړۍ` |
| `math.sqrt` / `math.pow` | `ریاضي.جذر` / `ریاضي.توان` | `to_float` | `اعشاري کړه` |
| `math.min` / `math.max` | `ریاضي.کمینه` / `ریاضي.اعظمه` | `math.abs` / `math.round` | `ریاضي.مطلق` / `ریاضي.نیژدې کړه` |
| `continue` | `دوام` | `inspect` / `dump` | `کتنه` / `وښایه` |
| `return` | `راګرځه` | `copy` | `کاپي` |
| `pass` | `تېر` | `has` / `remove` | `لري` / `لیرې کړه` |
| `import` | `راوړه` | `keys` / `values` | `کیلۍ` / `ارزښتونه` |
| `and` / `or` / `not` | `او` / `یا` / `نه` | `main` (the function) | `اصلي` |
| `@test` (annotation) | `@آزموینه` | `@mock` (annotation) | `@ساختګی` |

* **Keyboard letter forms:** Arabic and Persian keyboards produce ك (U+0643), گ (U+06AF) and
  ي (U+064A); Thunder folds them to the Pashto ک (U+06A9), ګ (U+06AB) and ی (U+06CC) - for
  keywords, built-ins **and the program's own names alike** - so `تكرار` is the keyword
  `تکرار`, `ټولگى` and `ټولګى` are `ټولګی`, `گډون` is the keyword `ګډون` (`mixin`),
  `ګرافیک.رنگونه.سور` is `ګرافیک.رنګونه.سور`, and a
  variable written `رنگ` is the same name as `رنګ`.
* **Multi-word names:** `کرښه چاپ`, `متن کړه`, `عدد کړه` and `لیرې کړه` are written with a space between
  their words, as in ordinary Pashto. The lexer joins the words into one token (any number of spaces or
  tabs on the same line may separate them), so `کرښه چاپ("سلام")` calls `println`, and
  `متن.پاک کړه(س)` calls `string.trim`.
* **Digits:** `۰۱۲۳۴۵۶۷۸۹` (ASCII and Arabic-Indic `٠١٢` digits also work). Decimals are written
  with the Arabic decimal separator `٫` (`۳٫۱۴`); `.` is accepted too, and results are printed
  with `٫`.
* **Punctuation and quotes:** Pashto programs separate with the Arabic comma `،` (also in `for`
  headers: `تکرار متغیر ش = ۰، ش < ۳، ش = ش + ۱`) and use the plain quotes `"..."`. The ASCII comma,
  the percent sign `٪`, `×` / `÷` for multiplication and division, and `«...»` strings are accepted
  as well; the translator writes the preferred forms (`،`, `%`, `"`).
* **Names:** the function that runs after the top-level code may be called `اصلي` (or `main`), and
  `graphics.key_pressed()` names keys in Pashto (`"کیڼ"`, `"ښي"`, `"پورته"`, `"کښته"`, `"تشه"`,
  ...; see the [graphics reference](graphics.md)). Single-letter names follow the Arabic-script convention of
  mathematics: `ن` for n, `س`/`ص` for x/y, `الف`/`ب` for a/b, as in the samples.
* **Output:** a Pashto program prints numbers with Pashto digits, `رښتیا`/`درواغ` for booleans, and
  `«»` quotes and `، ` separators inside arrays and structs.
* **Messages:** compiler and runtime errors are reported in Pashto.
* **Reading direction:** a Pashto program is read right to left, token by token; names written in
  Latin letters keep their own direction. The IDE's editor lays the program out that way (see the
  README, "Right-to-left programs").
* **Word order:** the keyword `سره` (`with`) follows the thing it goes with, as Pashto says
  «ساکټ سره» (section 11): after the handle, `نوم سره = سرچینه`, or after the value when there is
  no handle, `سرچینه سره`.
* **Modules:** the built-in modules have Pashto names (`ریاضي.جذر`, `متن.پاک کړه`, section 10);
  the English module names are accepted too, with the Pashto member names (`math.جذر`).

```thunder
# ژبه: ps
جوړښت ټکی
    س: عدد
    ص: عدد

دنده جمع(الف: عدد، ب: عدد): عدد
    راګرځه الف + ب

متغیر ټ = ټکی(ص: ۵، س: ۱۰)
که ټ.س > ۰ او نه ټ.ص == ۰
    کرښه چاپ(جمع(۲۰، ۲۲)، " "، ټ)      # ۴۲ ټکی(س: ۱۰، ص: ۵)
```

**Translating programs.** `thunderc --translate ps program.th` prints the program with every
keyword, built-in, built-in module and digit in Pashto, and moves `with` / `سره` to its place in
that language. `--translate en` translates back, and the IDE's
**Translate** button does the same. Comments and your own names are left as they are, and a name
that is reserved in the target language gets a `_` suffix.

## 15. Designed for extension

* **Other natural languages.** The lexer never compares against keyword text. It looks words,
  punctuation, digits and quotes up in a *language pack* (`src/core/language_pack.cpp`, with Pashto
  in `src/core/language_pashto.cpp`). A pack also defines how values are printed and the text of
  every message. Columns in error messages count characters, not bytes, so positions stay correct
  for Arabic-script text.
* **New types and operators.** Types are described by `Type` (`include/thunder/types.hpp`),
  operators by a binding-power table, and instructions by a single table in
  `include/thunder/bytecode.hpp`.
