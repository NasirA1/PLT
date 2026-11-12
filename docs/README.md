# The Thunder SDK reference

Everything about programming in Thunder, in reading order. In the IDE these documents are
under **Help**, in a viewer with a contents list and a keyword search; on GitHub the links
below work directly. Pashto translations live in [`docs/ps/`](ps/README.md), and the Pashto
interface's Help menu opens them automatically.

## Learning the language

| Document | What it covers |
| --- | --- |
| [Language reference](language.md) | The whole language: lexical rules (including `0x`/`0b` number notation), types, operators (arithmetic, comparison, logical and bitwise), statements, functions, structs, tuples, maps, modules and the grammar. |
| [Library reference](library.md) | Everything built in: input/output and `format`, text functions, conversions (`to_int`, `hex`, `bin`, ...), arrays, maps, maths, `map`/`filter`/`reduce`, and the `os` module. |

## Programs that do more

| Document | What it covers |
| --- | --- |
| [Graphics reference](graphics.md) | The canvas, shapes, animation, keys and the mouse; how the IDE shows a running picture. |
| [Sockets reference](sockets.md) | Talking over the network: TCP connections, UDP datagrams, names and addresses (`import "sockets"`). |
| [Native interface reference](native.md) | Calling C or C++ from Thunder and Thunder back from C++: one header, one entry point. |

## Shipping a program

| Document | What it covers |
| --- | --- |
| [Packaging reference](packaging.md) | Turning a program into a single executable that runs on its own. |
| [Display reference](display.md) | The window of a packaged graphics program: the display plug-ins, and how to write one. |

## Under the bonnet

| Document | What it covers |
| --- | --- |
| [Bytecode and VM reference](bytecode.md) | The instruction set (arithmetic, bitwise, jumps, calls), the `.thc` file format and the virtual machine. |

## The pieces on disk

| Directory | Contents |
| --- | --- |
| `samples/en/`, `samples/ps/` | The numbered sample programs (only programs live here). |
| `lib/thunder/` | Reusable Thunder modules shipped with the toolchain (`import "sockets"`). |
| `lib/native/` | Native modules in C++: the demo and the sockets library. |
| `lib/display/` | Display plug-ins in C: text, Win32/GDI and SDL2. |
| `ide/vscode/` | The VS Code extension: highlighting, IntelliSense, tasks and a debugger that steps backwards ([how to install and use it](../ide/vscode/README.md)). |

The development history - every iteration, what changed and why - is in
[`devlog.md`](../devlog.md) at the repository root.
