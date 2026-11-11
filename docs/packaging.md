# Packaging a program as an application

A finished Thunder program can be turned into **one executable file** that the operating
system runs directly. The person running it needs neither the IDE, nor `thundervm`, nor a
`.thc` file — just the one file you give them.

```sh
thunderc hello.th --package hello      # writes ./hello (hello.exe on Windows)
./hello one two                        # runs on its own; args() is ["one", "two"]
```

In the IDE the same thing is **Build > Package as an application...**.

---

## 1. What a package is

```
  [ the thundervm executable, byte for byte ] [ your program.thc ] [ 24-byte trailer ]
```

That is all. Both ELF (Linux, macOS) and PE (Windows) executables ignore bytes after the end
of the image, so the copy still starts the way the original does; at start-up it looks at its
own file, finds the trailer, and runs the program it is carrying instead of expecting one on
the command line. The trailer is the payload's offset and size as 64-bit little-endian
numbers, followed by the eight characters `THNDPACK`.

Nothing is compiled to machine code: the virtual machine inside the package interprets the
same bytecode the IDE runs, so the program behaves **exactly** as it did while you were
writing it — including the error messages, which are still in the program's own language.

The price is the size: the package is the virtual machine plus your program, so it is about
1.5 MB whatever the program does (a toolchain built in debug mode is far larger, because
`thundervm` then carries its debugging symbols — build with `-DCMAKE_BUILD_TYPE=Release`).

---

## 2. Using it

```
thunderc <source.th> --package <file> [--runtime <thundervm>] [-I <dir>]
```

| Option | Effect |
| --- | --- |
| `--package <file>` | Write the executable. On Windows `.exe` is added if you leave it out. |
| `--runtime <file>` | The virtual machine to copy. By default the `thundervm` next to `thunderc`. |
| `--display <name>` | Also copy that [display plug-in](display.md), so the program can open a window. A graphics program packaged without this option takes the first plug-in found (`sdl` preferred); if none is found, a note says the window will not open on its own. |
| `-I <dir>` | Also look there for imported modules and for the libraries to copy. |

`--package` on its own does not also leave a `.thc` behind; add `-o program.thc` if you want
both. A program that does not compile is never packaged.

**Modules.** `import "lib/geometry"` is resolved while compiling, so imported Thunder files
are already inside the package. Nothing needs to travel with it.

**Native libraries.** A program that uses [`import native`](native.md) needs its `.so` / `.dll`
/ `.dylib` at run time; `--package` copies each one next to the executable and says so:

```
Packaged demo.th -> demoapp (4215088 bytes).
  with the native library libthunder_demo.so
```

Those files are part of the application: ship the directory, not only the executable. If a
library cannot be found, packaging fails rather than producing something that would break on
someone else's machine.

---

## 3. Running a packaged program

Everything on the command line goes to the program — `args()` (module `os`) returns it — and
`input()` reads standard input, as always. `exit(code)` becomes the process's exit status.

A few of the virtual machine's own options are still reachable, each with a `--thunder-`
prefix so that it can never be confused with one of the program's own arguments:

| Option | Effect |
| --- | --- |
| `--thunder-help` | What this executable is, and these options |
| `--thunder-version` | The version of the virtual machine inside it |
| `--thunder-svg <file>` | Save the final picture of a graphics program as an SVG image |
| `--thunder-canvas` | Write frames as records on standard output (for a host such as the IDE) |
| `--thunder-display <name>` | Show the program in a window of its own, through a [display plug-in](display.md) |
| `--thunder-stats` | Print execution statistics |

**Graphics programs.** Thunder draws into a canvas that a *host* puts on a screen, and in the
IDE the IDE is that host. A packaged program gets its own window from a **display plug-in**
([display reference](display.md)). Packaging a graphics program copies one automatically:
`--display <name>` names it, and without the option the first plug-in found next to the
toolchain is taken (`win32` then `sdl` on Windows, `sdl` elsewhere). When the application is then run straight from the
operating system, it looks for a display plug-in beside itself, opens its window through the
first one it finds, and if there is none it stops with a message that points here and to the
[display reference](display.md):

```sh
thunderc snake.th --package snake       # also copies libthunder_display_sdl.so, and says so
./snake                                 # opens its own window through the plug-in
```

`--thunder-display <name>` still chooses a particular plug-in, `--thunder-svg` saves the
final picture instead, and `--thunder-canvas` writes the frame records a host could draw.
A console program needs none of this.

---

## 4. Checking and troubleshooting

| Problem | What to do |
| --- | --- |
| `cannot find thundervm next to thunderc` | Name it: `--runtime path/to/thundervm` |
| `cannot find the native library 'libx.so'` | Put it where the program is, or add `-I <dir>` |
| "Permission denied" on Linux/macOS | `thunderc` sets the execute bit; if the file was copied through a filesystem that drops it, `chmod +x <file>` |
| The package is very large | Build the toolchain in release mode: a debug `thundervm` carries its symbols with it |

A packaged program is an ordinary file: it can be copied, renamed and put on `PATH`. Rename it
freely — the trailer is found by reading the file, not by its name.

---

## 5. Under the bonnet

* The format and the two functions that make and find a payload are in
  `include/thunder/package.hpp` and `src/core/package.cpp`.
* `thundervm` calls `package::self_payload()` as its very first act. It reads only the last 24
  bytes and then the payload itself, never the whole executable, so an ordinary run costs one
  extra seek.
* The trailer is never trusted: an offset or a size that does not fit inside the file is
  treated as "this executable carries nothing", and the program falls back to its usual
  command line.
