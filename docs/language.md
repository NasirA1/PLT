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
{
    x: int
    y: int
}

func square(n: int): int
{
    return n * n
}

for (var i = 1; i <= 3; i = i + 1)
{
    println(i, " squared is ", square(i), " at ", Point(i, square(i)))
}
```

Thunder code is written in **Allman style**: every brace goes on a line of its own, aligned
with the statement that owns it, and the contents are indented by four spaces. The IDE moves a
`{` typed at the end of a line onto its own line, and **Edit > Format** re-formats a whole file.

## 2. Lexical structure

| Element | Examples | Notes |
| --- | --- | --- |
| Comments | `// to end of line`, `/* block */` | Block comments do not nest. |
| Identifiers | `count`, `is_prime`, `_tmp`, `شمیر` | A letter or `_` followed by letters, digits or `_`. Letters from any script are allowed. |
| Integer literals | `0`, `42`, `۴۲` | Decimal only. Digits may be ASCII, Pashto/Persian (۰–۹) or Arabic-Indic (٠–٩). The largest value is 2⁶³−1. |
| String literals | `"text"`, `"tab\there"`, `«متن»` | Must end on the same line. Escapes: `\n` `\t` `\r` `\0` `\"` `\\` (and `\«` `\»`). |
| Boolean literals | `true`, `false` | |
| Keywords | `var const func struct if else while for break continue return` | Reserved. |
| Type names | `int bool string` | Reserved. Struct names are also types. |
| Operators | `+ - * / % == != < <= > >= && \|\| ! = .` | |
| Punctuation | `( ) { } [ ] , ; :` | |

Spaces, tabs and Unicode bidirectional marks (used in right-to-left text) separate tokens but are
otherwise ignored. **Line breaks matter**: they end statements (section 6).

## 3. Types

| Type | Values | Default value |
| --- | --- | --- |
| `int` | 64-bit signed integers | `0` |
| `bool` | `true`, `false` | `false` |
| `string` | immutable sequences of Unicode characters | `""` |
| `T[]` | arrays of `T`, e.g. `int[]`, `string[][]`, `Point[]` | `[]` (empty) |
| a struct name | values of that struct (section 8) | a struct whose fields all have their defaults |

Functions without a return type return nothing (*void*). Their result cannot be used as a value.

There are **no implicit conversions**. `"x = " + 5` is a type error; write `"x = " + str(5)`, or
pass several arguments to `println`.

Arrays and structs are **reference types**: assigning one, or passing it to a function, shares the
same value. Use `copy(value)` for an independent copy. Arrays are created with a literal
(`[1, 2, 3]`) or with `array(n, value)`, and structs by calling the struct's name. There is no
`new` keyword: Thunder manages memory automatically (reference counting; section 8 explains why
there are no cycles).

## 4. Declarations

```thunder
var count: int = 10      // explicit type and initial value
var name = "Thunder"     // type inferred from the value (string)
var total: int           // no initial value: starts at the type's default (0)
const limit = 100        // a constant can never be assigned again
var grid: int[][] = []   // an empty array needs an explicit type
var origin: Point        // a Point with all fields at their defaults
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
| 8 | `f(args)` call, `a[i]` index, `s.field` | | |
| 7 | `-x`, `!x` | `int`, `bool` | same |
| 6 | `*` `/` `%` | `int` | `int` |
| 5 | `+` `-` | `int` (also `string + string`) | `int` / `string` |
| 4 | `<` `<=` `>` `>=` | two `int`s or two `string`s | `bool` |
| 3 | `==` `!=` | two values of the same `int`, `bool` or `string` type | `bool` |
| 2 | `&&` | `bool` | `bool` (short-circuit) |
| 1 | `\|\|` | `bool` | `bool` (short-circuit) |

* Integer division rounds toward zero (`-7 / 2` is `-3`). `%` has the sign of the left operand.
* Integer overflow, division by zero and out-of-range indexes are **runtime errors**. Thunder has
  no undefined behaviour.
* `s[i]` on a string gives a one-character string. Strings cannot be modified.
* Strings compare in Unicode code point order. Arrays and structs cannot be compared with `==`.
* `a && b` evaluates `b` only if `a` is true; `a || b` evaluates `b` only if `a` is false.

## 6. Statements and line breaks

Semicolons are **optional**. A statement ends at the end of its line:

```thunder
var x = 1
var y = 2
x = x + y
```

A line break does **not** end a statement when the line ends inside parentheses or brackets, or
right after an operator or a comma, so long statements can be split over several lines:

```thunder
var ok = width > 0 &&
    height > 0
println("area: ",
        width * height)
```

An operator at the **start** of a line always begins a new statement. Unlike JavaScript, a line
never silently joins the one above it: `x = a` followed by a line `-b` is two statements. Two
statements on one line need a `;` between them (`x = 1; y = 2`). `return` at the end of a line
returns no value.

```thunder
x = x + 1                 // assignment (to a variable, an array element or a struct field)
values[i] = 0
p.x = 3
println(x)                // an expression statement (usually a call)

if (x > 0)                // conditions must be bool; braces are required
{
    println("positive")
}
else if (x < 0)
{
    println("negative")
}
else
{
    println("zero")
}

while (x > 0)
{
    x = x - 1
}

for (var i = 0; i < 10; i = i + 1)   // the ';'s inside a for header are required
{
    if (i == 3)
    {
        continue          // skip to the step (i = i + 1)
    }
    if (i == 7)
    {
        break             // leave the loop
    }
}
```

Assignment is a statement, not an expression, so `if (x = 1)` is a syntax error.

## 7. Functions

```thunder
func gcd(a: int, b: int): int
{
    while (b != 0)
    {
        var t = b
        b = a % b
        a = t
    }
    return a
}

func greet(name: string)   // no return type: returns nothing
{
    println("Hello, ", name)
}
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

## 8. Structs

A struct groups named, typed **fields** into a new type:

```thunder
struct Point
{
    x: int
    y: int
}

struct Rectangle
{
    corner: Point
    width: int
    height: int
    label: string
}
```

Fields are separated by line breaks (a `,` or `;` also works). Structs are declared at the top level
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
{
    corner: Point
    {
        x: int = 1
        y: int = 2
    }
    width: int = 10
    height: int = 0
    label: string = ""
}
```

**No recursive structs.** A struct may not contain itself, directly or through other structs or
arrays (`struct Node { children: Node[] }` is rejected). Every value is therefore finite and has a
default, and reference counting can never leak a cycle.

## 9. Built-in functions

| Function | Description |
| --- | --- |
| `print(v, ...)` | Writes the values with no separator and no newline. |
| `println(v, ...)` | Like `print`, followed by a newline. |
| `len(x)` | Number of characters in a string or elements in an array. |
| `str(v)` | Text form of any value (`str([1, 2])` is `"[1, 2]"`, `str(p)` is `"Point(x: 1, y: 2)"`). |
| `to_int(s)` | Parses a decimal integer such as `"-42"` or `"۴۲"`. A runtime error if `s` is not a number. |
| `array(n, v)` | A new array of `n` copies of `v`. Arrays and structs in `v` are copied, so `array(3, array(3, 0))` is a proper 3×3 grid. |
| `inspect(v)` | A detailed, multi-line description of a value (structs field by field, with types). |
| `dump(v)` | Prints `inspect(v)` followed by a newline. |
| `copy(v)` | An independent (deep) copy of a struct or array. |

Built-in names cannot be used for user functions or structs.

## 10. Errors

Each phase reports the mistakes it can detect. Every message has a stable code:

| Phase | Codes | Examples |
| --- | --- | --- |
| Lexical analysis | `L001`–`L007` | unexpected character `#`, unterminated string, number too large |
| Syntax analysis | `P001`–`P011` | `expected ')'`, two statements on one line (`P010`), missing braces |
| Semantic analysis | `S001`–`S037` | undeclared name, type mismatch, unknown field, recursive struct, missing `return` |
| Warnings | `W001`–`W003` | unused variable, unreachable code, unknown language directive |
| Runtime | `R001`–`R010` | division by zero, integer overflow, index out of bounds, stack overflow, step limit |

Compilation stops at the first phase that reports an error. For example, a program with a syntax
error is never type-checked. The parser recovers from errors, so it can report several syntax
errors at once.

## 11. Writing programs in Pashto (پښتو)

Every word and symbol of the language has a Pashto form, and Pashto programs print their results in
Pashto. Start a Pashto file with the directive `// ژبه: ps` (or `// lang: ps`), or choose پښتو in
the IDE. `thunderc --lang ps` also works.

| English | Pashto | English | Pashto |
| --- | --- | --- | --- |
| `var` | `متغیر` | `int` | `عدد` |
| `const` | `ثابت` | `bool` | `منطقي` |
| `func` | `دنده` | `string` | `متن` |
| `struct` | `جوړښت` | `true` / `false` | `رښتیا` / `دروغ` |
| `if` / `else` | `که` / `کنه` | `print` / `println` | `چاپ` / `چاپ_کرښه` |
| `while` | `ترڅو` | `len` | `اوږدوالی` |
| `for` | `لپاره` | `str` / `to_int` | `متن_کړه` / `عدد_کړه` |
| `break` | `ودرېږه` | `array` | `لړۍ` |
| `continue` | `دوام` | `inspect` / `dump` | `کتنه` / `وښایه` |
| `return` | `راګرځه` | `copy` | `کاپي` |

* **Digits:** `۰۱۲۳۴۵۶۷۸۹` (ASCII digits also work).
* **Punctuation:** the Arabic comma `،` and semicolon `؛`, the percent sign `٪`, and `×` / `÷` for
  multiplication and division are accepted alongside their ASCII forms.
* **Strings** may be written `«like this»` as well as `"like this"`.
* **Output:** a Pashto program prints numbers with Pashto digits, `رښتیا`/`دروغ` for booleans, and
  `«»` quotes and `، ` separators inside arrays and structs.
* **Messages:** compiler and runtime errors are reported in Pashto.

```thunder
// ژبه: ps
جوړښت ټکی
{
    x: عدد
    y: عدد
}

دنده جمع(a: عدد، b: عدد): عدد
{
    راګرځه a + b
}

متغیر ټ = ټکی(y: ۵، x: ۱۰)
چاپ_کرښه(جمع(۲۰، ۲۲)، « »، ټ)      // ۴۲ ټکی(x: ۱۰، y: ۵)
```

**Translating programs.** `thunderc --translate ps program.th` prints the program with every
keyword, built-in, digit, punctuation mark and string quote in Pashto. `--translate en` translates
back, and the IDE's **Translate** button does the same. Comments and your own names are left as they
are, and a name that is reserved in the target language gets a `_` suffix.

## 12. Grammar (EBNF)

```ebnf
program        = { declaration } EOF ;
declaration    = function | struct | statement ;

function       = "func" IDENTIFIER "(" [ parameter { "," parameter } ] ")" [ ":" type ] block ;
parameter      = IDENTIFIER ":" type ;
struct         = "struct" IDENTIFIER "{" { IDENTIFIER ":" type [ "," | ";" ] } "}" ;
type           = ( "int" | "bool" | "string" | IDENTIFIER ) { "[" "]" } ;

statement      = ( var_decl | simple_stmt | "break" | "continue" | "return" [ expression ] ) end
               | if_stmt | while_stmt | for_stmt | block ;
end            = ";" | NEWLINE | (* before "}" or the end of the file *) ;
var_decl       = ( "var" | "const" ) IDENTIFIER [ ":" type ] [ "=" expression ] ;
simple_stmt    = expression [ "=" expression ] ;          (* assignment or expression *)
block          = "{" { declaration } "}" ;
if_stmt        = "if" "(" expression ")" block [ "else" ( if_stmt | block ) ] ;
while_stmt     = "while" "(" expression ")" block ;
for_stmt       = "for" "(" [ var_decl | simple_stmt ] ";" [ expression ] ";" [ simple_stmt ] ")" block ;

expression     = unary { binary_op unary } ;              (* precedence: see section 5 *)
binary_op      = "||" | "&&" | "==" | "!=" | "<" | "<=" | ">" | ">=" | "+" | "-" | "*" | "/" | "%" ;
unary          = ( "-" | "!" ) unary | postfix ;
postfix        = primary { "(" [ arguments ] ")" | "[" expression "]" | "." IDENTIFIER } ;
arguments      = argument { "," argument } ;
argument       = [ IDENTIFIER ":" ] expression ;          (* names are for struct fields *)
primary        = INTEGER | STRING | "true" | "false" | IDENTIFIER
               | "(" expression ")" | "[" [ expression { "," expression } ] "]" ;
```

NEWLINE is a line break outside parentheses and brackets. A binary operator, postfix operator or
argument list may not start a new line (section 6).

**How the parser works.** Statements are parsed by *recursive descent*: each rule above is one
function in `src/core/parser.cpp`. Expressions are parsed with **Pratt's top-down operator
precedence** algorithm. Each binary operator has a *binding power* (its precedence), and the
parser keeps extending the left operand while the next operator binds more tightly than the
operator it is currently inside. A line break outside brackets stops this extension, which is how
statements end without semicolons. This replaced the shunting-yard algorithm of the original
DumbLang prototype. It handles prefix operators, calls, indexing and field access naturally and
produces the tree directly.

## 13. Designed for extension

* **Other natural languages.** The lexer never compares against keyword text. It looks words,
  punctuation, digits and quotes up in a *language pack* (`src/core/language_pack.cpp`, with Pashto
  in `src/core/language_pashto.cpp`). A pack also defines how values are printed and the text of
  every message. Columns in error messages count characters, not bytes, so positions stay correct
  for Arabic-script text.
* **New types and operators.** Types are described by `Type` (`include/thunder/types.hpp`),
  operators by a binding-power table, and instructions by a single table in
  `include/thunder/bytecode.hpp`.
