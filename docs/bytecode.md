# Thunder Bytecode and Virtual Machine Reference

*Part of the [Thunder SDK reference](README.md).*

The Thunder compiler (`thunderc`) translates source code into **bytecode**: instructions for an
imaginary computer, the Thunder virtual machine (`thundervm`). This document describes that
machine and the `.thc` file format.

The authoritative definition is the opcode table in `include/thunder/bytecode.hpp`. The IDE's
Bytecode tab shows the same descriptions when you select an instruction.

---

## 1. The machine model

Thunder's VM is a **stack machine**. Instead of registers, it has an *operand stack*:
instructions pop their inputs from the top of the stack and push their results back.

```
source:   println(2 + 3 * 4);

bytecode: CONST 0        ; push 2            stack: [2]
          CONST 1        ; push 3            stack: [2, 3]
          CONST 2        ; push 4            stack: [2, 3, 4]
          MUL            ; 3 * 4             stack: [2, 12]
          ADD            ; 2 + 12            stack: [14]
          CALL_BUILTIN 1 1   ; println(14)   stack: []
```

The order of the instructions is the tree **post-order**: operands first, then the operator.

### Values

| Kind | Notes |
| --- | --- |
| `int` | 64-bit signed. Arithmetic is overflow-checked. |
| `bool` | `true` / `false` |
| `string` | Immutable UTF-8 text, shared by reference counting. |
| array | A reference-counted, mutable list of values. |
| struct | A reference-counted, mutable record: a pointer to its layout in the struct table plus one value per field. |
| tuple | An immutable, fixed-size group of values; compared element by element by `EQ`. |
| map | A reference-counted associative array (keys: int, bool or string) that keeps insertion order. |

Reference cycles are impossible because the type system forbids recursive structs, so reference
counting reclaims every value.

### Functions, frames and locals

Each function call creates a **call frame** that records the function, its instruction pointer
(*ip*) and its *base*, the stack index of its first local variable. A function's parameters
and local variables live on the operand stack, starting at that base:

```
            ┌──────────────┐  <- top
            │  operand     │     temporary values of the current frame
            │  local  c    │  slot 2
            │  param  b    │  slot 1
 base(f) -> │  param  a    │  slot 0     frame of f(a, b)
            │  operand     │     caller's temporaries
            │  local  x    │  slot 0     frame of <main>
            └──────────────┘
```

* `CALL f`: the arguments are already on the stack and become slots `0 .. arity-1`. The VM
  reserves the remaining local slots and starts `f` at offset 0.
* `RETURN`: pops the result, discards the callee's whole frame, and pushes the result for the
  caller.

**Globals** (variables declared at the top level) live in a separate table and use
`LOAD_GLOBAL` / `STORE_GLOBAL`.

The top-level statements are compiled into a function called `<main>`, which runs first. If the
program defines `func main()`, `<main>` calls it before `HALT`.

## 2. Instruction set

Each instruction is a 1-byte opcode followed by its operands, which are little-endian.

| Hex | Mnemonic | Operands | Stack (before -> after) | Description |
| --- | --- | --- | --- | --- |
| 00 | `HALT` | | | Stop the program. |
| 01 | `CONST` | u16 constant | -> value | Push a constant from the constant pool. |
| 02 | `TRUE` | | -> true | Push `true`. |
| 03 | `FALSE` | | -> false | Push `false`. |
| 04 | `POP` | | value -> | Discard the top value (e.g. an unused function result). |
| 05 | `LOAD_LOCAL` | u16 slot | -> value | Push a local variable. |
| 06 | `STORE_LOCAL` | u16 slot | value -> | Pop into a local variable. |
| 07 | `LOAD_GLOBAL` | u16 slot | -> value | Push a global variable. |
| 08 | `STORE_GLOBAL` | u16 slot | value -> | Pop into a global variable. |
| 09 | `ADD` | | a b -> a+b | Addition (runtime error on overflow). |
| 0a | `SUB` | | a b -> a-b | Subtraction. |
| 0b | `MUL` | | a b -> a*b | Multiplication. |
| 0c | `DIV` | | a b -> a/b | Division; whole numbers round toward zero (error if b is 0). |
| 0d | `MOD` | | a b -> a%b | Remainder of whole numbers (error if b is 0). |
| 0e | `NEG` | | a -> -a | Negation. |
| 0f | `NOT` | | a -> !a | Boolean NOT. |
| 10 | `CONCAT` | | a b -> ab | Join two strings. |
| 11 | `EQ` | | a b -> a==b | Equality (strings by content). |
| 12 | `NE` | | a b -> a!=b | Inequality. |
| 13 | `LT` | | a b -> a<b | Less than (numbers or strings). |
| 14 | `LE` | | a b -> a<=b | |
| 15 | `GT` | | a b -> a>b | |
| 16 | `GE` | | a b -> a>=b | |
| 17 | `JUMP` | u32 address | | Continue at an absolute offset in the current function. |
| 18 | `JUMP_IF_FALSE` | u32 address | cond -> | Pop a bool; jump if it is false. |
| 19 | `CALL` | u16 function | args -> result? | Call a function from the function table. |
| 1a | `CALL_BUILTIN` | u8 id, u8 argc | args -> result? | Call a built-in (ids listed below the table). |
| 1b | `RETURN` | | value -> | Return a value to the caller. |
| 1c | `RETURN_VOID` | | | Return without a value. |
| 1d | `MAKE_ARRAY` | u16 count | v1..vn -> array | Build an array from the top *n* values. |
| 1e | `INDEX_GET` | | a i -> a[i] | Read an array element, a string character (bounds-checked) or a map value (the key must exist). |
| 1f | `INDEX_SET` | | a i v -> | Write an array element (bounds-checked) or add/replace a map entry. |
| 20 | `DUP` | | v -> v v | Duplicate the top value. |
| 29 | `DUP2` | | a b -> a b a b | Duplicate the top two values (used by `a[i] += v`). |
| 2a | `INT_TO_FLOAT` | | i -> f | Widen a whole number to a decimal. |
| 21 | `NEW_STRUCT` | u16 struct | f1..fn -> s | Pop the field values (in declaration order) and push a new struct. |
| 22 | `GET_FIELD` | u16 field | s -> s.f | Replace a struct by one of its fields. |
| 23 | `SET_FIELD` | u16 field | s v -> | Store a value in a field. |
| 24 | `MAKE_TUPLE` | u16 count | v1..vn -> tuple | Build a tuple from the top *n* values. |
| 25 | `TUPLE_GET` | u16 element | t -> t.i | Replace a tuple by one of its elements. |
| 26 | `MAKE_MAP` | u16 count | k1 v1..kn vn -> map | Build a map from the top *n* key/value pairs (insertion order kept). |
| 27 | `FUNC_REF` | u16 function | -> f | Push a function value (a reference to a function). Operand 65535 is "no function", the default value of a function variable. |
| 28 | `CALL_VALUE` | u16 argc | args f -> result? | Pop a function value and call it with the *argc* arguments below it (runtime error `R020` for "no function"). |
| 2b | `CALL_NATIVE` | u16 index | args -> result? | Call a function of a native library (`import native`). |
| 2c | `BIT_AND` | | a b -> a&b | Bitwise AND of two whole numbers. |
| 2d | `BIT_OR` | | a b -> a\|b | Bitwise OR. |
| 2e | `BIT_XOR` | | a b -> a^b | Bitwise XOR. |
| 2f | `SHL` | | a n -> a<<n | Shift left by n (0..63, else runtime error `R022`); bits shifted off the top are lost. |
| 30 | `SHR` | | a n -> a>>n | Shift right by n (0..63), keeping the sign (arithmetic shift). |
| 31 | `BIT_NOT` | | a -> ~a | Invert every bit (`~a` equals `-a - 1`). |

The arithmetic and comparison instructions work on **whole numbers and decimals alike**: they
look at the kind of the values on the stack, which the type checker has already made agree. That
keeps the instruction set small; the one conversion instruction, `INT_TO_FLOAT`, is emitted
wherever the checker widened an `int` (see `Expr::widen_to_float`). A decimal result that is not
finite is runtime error `R025`, so no infinity or NaN ever reaches a Thunder value.

`CALL_BUILTIN` ids are the positions in the table of built-ins (`src/core/builtins.cpp`): 0
`print`, 1 `println`, 2 `len`, 3 `str`, 4 `to_int`, 5 `array`, 6 `inspect`, 7 `dump`, 8 `copy`,
9 `has`, 10 `remove`, 11 `keys`, 12 `values`, then the library functions in the order of the
[library reference](library.md). The Bytecode tab and `thunderc --emit asm` show each built-in's
name next to its id. `input` takes 0 or 1 arguments; the verifier checks the argument count
against each built-in's range.

**Built-ins that call functions.** `map`, `filter` and `reduce` receive a function value. For
each element, the VM pushes the argument(s) and the function, performs the same steps as
`CALL_VALUE`, and runs the interpreter loop until that call returns; then the built-in continues.
These calls appear in the trace, frames and step count like any other call.

**Graphics.** The `graphics` module needs no new instructions: its functions are built-ins that
change the VM's canvas, and `frame()` hands the canvas to the graphics host (see the
[graphics reference](graphics.md), section 8). Once a program shows frames, `--max-steps` counts the
instructions between two frames, and `--max-frames n` ends a program normally after *n* frames.

## 3. How statements become bytecode

`if c` with block A, `else` with block B:

```
        <c>
        JUMP_IF_FALSE else
        <A>
        JUMP end            ; omitted if A always returns/breaks
else:   <B>
end:
```

`while c` followed by its indented body:

```
start:  <c>
        JUMP_IF_FALSE end
        <body>              ; break -> JUMP end, continue -> JUMP start
        JUMP start
end:
```

`for init, c, step` followed by its indented body:

```
        <init>
start:  <c>
        JUMP_IF_FALSE end
        <body>              ; continue -> JUMP next
next:   <step>
        JUMP start
end:
```

`Point(1, 2)` pushes the field values and builds the struct. `Point(y: 2, x: 1)` builds a struct with
default values first, then sets each named field, so the arguments are evaluated in the order written:

```
        CONST 0  ; 0          default x
        CONST 0  ; 0          default y
        NEW_STRUCT 0  ; Point
        DUP
        CONST 1  ; 2
        SET_FIELD 1           ; y = 2
        DUP
        CONST 2  ; 1
        SET_FIELD 0           ; x = 1
```

`a and b` uses jumps so that `b` is only evaluated when `a` is true (*short-circuit evaluation*):

```
        <a>
        JUMP_IF_FALSE false
        <b>
        JUMP end
false:  FALSE
end:
```

## 4. The `.thc` file format

All integers are little-endian. A *str* is a u32 byte length followed by UTF-8 bytes.

| Section | Contents |
| --- | --- |
| header | magic `"THND"` (4 bytes), u16 format version (4), str source file name, str language code (`en`, `ps`), u16 count + str source file names (empty, or the program followed by the modules it imports) |
| constants | u32 count, then for each: u8 tag (1 = int: i64; 2 = string: str; 3 = float: the IEEE-754 binary64 bits as u64) |
| structs | u16 count, then for each: str name, u16 field count, and for each field: str name, str type (as written in the source language, for `inspect`) |
| globals | u16 count, then a str name for each global slot |
| function table | u16 function count, u16 entry function index |
| each function | str name, u16 arity, u16 local slots, u8 returns-a-value flag, u16 count + str local names, u32 code size, code bytes, u32 count + line table entries (u32 offset, u32 line, u32 column, u16 file) |

The **line table** maps instruction offsets back to source positions. The VM uses it to report
runtime errors and the debugger uses it to highlight the current line. The *file* is an index
into the header's list of source files (0 is the program itself), so code from an imported module
is reported with the module's name. Format version 4 added the source-file list and the file
index; version 5 added decimal constants (and the two instructions above); the VM rejects files
of other versions.

The **language code** decides how the VM shows values and messages: a program compiled from Pashto
source prints `۴۲`, `رښتیا` and `«متن»`, and reports runtime errors in Pashto.

The IDE's Binary tab shows the hex dump with each section in a different colour. From the
command line, use `thunderc --emit hex program.th`.

## 5. Safety: the verifier

`thundervm` never trusts a `.thc` file. Before running, it **verifies** the program:

* the magic number, version and every length field are consistent with the file size;
* every opcode is known and every operand is in range (constants, locals, globals, functions,
  structs and fields, built-ins and their argument counts);
* every jump lands on the start of an instruction;
* every function ends with `HALT`, `RETURN`, `RETURN_VOID` or `JUMP`.

While running, it also checks every operand's type, stack underflow, call depth, stack size and,
optionally, a step limit. A corrupt or hand-crafted file therefore produces an error message
(code `R010`) instead of crashing the VM. The test suite flips every byte of a compiled file and
truncates it at every length to check this.

## 6. Command-line tools

```
thunderc program.th                 # compile to program.thc
thunderc --lang ps program.th       # the program is written in Pashto (or use "# ژبه: ps")
thunderc --translate ps program.th  # print the program translated into Pashto
thunderc --emit all program.th      # also print tokens, AST, symbols, bytecode and a hex dump
thunderc --json program.th          # everything as JSON (what the IDE uses)
thundervm program.thc               # run (input() reads standard input)
thundervm program.thc a b           # run with arguments: args() is ["a", "b"] (import os)
thunderc -I libs program.th         # also look for imported files in libs/
thundervm --svg out.svg --max-frames 50 anim.thc   # graphics: save the picture after 50 frames
thundervm --trace program.thc       # print every instruction and the stack while running
thundervm --stats program.thc       # instructions executed, calls, maximum stack depth
thundervm --trace-json t.json program.thc   # record a step-by-step trace (the IDE debugger)
thundervm --disassemble program.thc # print the listing of a .thc file
```

Exit codes: `0` success, `1` compile or runtime error, `2` usage error, unreadable file or invalid
bytecode, `3` internal compiler error.
