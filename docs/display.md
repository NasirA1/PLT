# Displays: a window for a graphics program

A Thunder program that imports `graphics` draws into a **canvas**; something else has to put
that canvas on a screen. Inside the IDE that something is the IDE itself. Outside it — in a
terminal, or in a [packaged application](packaging.md) — a **display plug-in** does the job:

```sh
thunderc 28_snake.th -o snake.thc
thundervm --display sdl snake.thc          # a window of its own
```

A plug-in is a shared library that opens a window, draws the shapes of each frame and sends
key and mouse events back. The whole interface is one plain-C header,
[`include/thunder/display.h`](../include/thunder/display.h), and two complete plug-ins are in
[`lib/display/`](../lib/display/): one built on **SDL2**, and a tiny one that writes
the frames as text. In the IDE this document is under **Help > Graphics reference** (section 8
links here).

---

## 1. How a program reaches a screen

```
  a Thunder program            the canvas              where it is shown
  ------------------           ----------             -------------------
  circle(x, y, 10, "red")  ->  a list of shapes  ->   the IDE          (thundervm --canvas)
  frame()                      since the last         a display plug-in (thundervm --display)
                               clear()                an SVG file       (thundervm --svg)
                                                      nowhere           (the default)
```

The program never knows which it is. That is the point: the same `28_snake.th` runs in the
IDE while it is being written, in a window once it is finished, and headless in a test.

---

## 2. Using a display

| Command | What it does |
| --- | --- |
| `thundervm --display sdl program.thc` | loads `libthunder_display_sdl.so` (`thunder_display_sdl.dll` on Windows) |
| `thundervm --display win32 program.thc` | Windows only: loads `thunder_display_win32.dll`, which needs nothing installed |
| `./app --thunder-display sdl` | the same, for a [packaged](packaging.md) program |
| `./app` | a packaged graphics program picks the first plug-in it finds beside itself (`win32` then `sdl` on Windows, `sdl` elsewhere; `text` is never picked automatically), or stops with a message pointing here |
| `thunderc p.th --package app --display sdl` | packages the program **and** puts the plug-in beside it (done automatically for graphics programs when a plug-in is available) |

The library is looked for next to the program, next to `thundervm` itself, then in each
`--native-path` directory and in `THUNDER_NATIVE_PATH`, and finally wherever the operating
system looks — the same places as a [native module](native.md).

**Closing the window ends the program.** The plug-in reports `THUNDER_EVENT_QUIT`, and the
program stops the way it does when the frame limit is reached: normally, with its final
picture intact (so `--svg` still writes it).

### The examples

| Plug-in | What it is for |
| --- | --- |
| `lib/display/sdl` | a real window, built on SDL2. Keyboard, mouse, every shape. |
| `lib/display/win32` | a real window on Windows, drawn with GDI: nothing to install, and text comes out in a real font (Pashto included). Always built on Windows. |
| `lib/display/text` | no window: each frame is written to standard error. The one to read first, and the one the tests use. |

The text plug-in can also stand in for a person, which makes a game testable without a screen:

```sh
THUNDER_DISPLAY_TEXT_KEYS=left,space THUNDER_DISPLAY_TEXT_FRAMES=20 \
    thundervm --display text snake.thc
```

All are built with the toolchain, so they are already beside `thundervm`. The SDL one is
skipped if SDL2 is not installed (`sudo apt install libsdl2-dev`, `brew install sdl2`, or
`vcpkg install sdl2` on Windows); CMake says which it built. On Windows the Win32 one is
there regardless, so a packaged graphics program always has a window to open.

---

## 3. Writing a display

A plug-in includes one header, links against nothing of Thunder's, and exports one function:

```c
#include <thunder/display.h>

static int my_open(ThunderDisplay* d, int32_t width, int32_t height, const char* title,
                   const char** error);
static void my_present(ThunderDisplay* d, const ThunderFrame* frame);
static int  my_poll(ThunderDisplay* d, ThunderEvent* event);
static void my_close(ThunderDisplay* d);

THUNDER_DISPLAY_EXPORT int thunder_display_init(ThunderDisplay* display)
{
    display->abi_version = THUNDER_DISPLAY_ABI;
    display->state = my_state_new();      /* your own data; Thunder never looks at it */
    display->open = my_open;
    display->present = my_present;
    display->poll = my_poll;
    display->close = my_close;
    return 1;
}
```

| Call | When | Must do |
| --- | --- | --- |
| `open` | before the first frame, when the canvas size is known | open the window; return 0 and set `*error` if it cannot |
| `present` | once per `frame()` or `show()` | draw the frame's shapes |
| `poll` | after every frame, until it returns 0 | hand over one event at a time |
| `close` | once, at the end — **even if `open` was never called** | free everything `thunder_display_init` allocated |

Every call comes from the one thread that runs the program, so a plug-in needs no locking.

### A frame

```c
typedef struct ThunderFrame {
    uint64_t number;          /* 1 for the first */
    int32_t width, height;    /* the canvas, in pixels */
    int32_t keep;             /* shapes the display already has */
    int32_t count;            /* how many follow */
    const ThunderShape* shapes;
} ThunderFrame;
```

A frame carries only what is **new**: `keep` says how many shapes of the picture the display
already drew. A program that calls `clear()` at the top of its loop sends `keep = 0` and the
whole picture; one that keeps adding to a drawing sends only the additions, which is why the
SDL plug-in draws into a texture it keeps between frames.

Each shape is a `kind` and up to four numbers; `display.h` lists exactly which number means
what for each kind. Coordinates are **decimals** and may fall outside the canvas — clip them.
Colours are always `"#rrggbb"`.

### Events

`poll` fills in a `ThunderEvent` and returns non-zero, or returns 0 when there is nothing
left. There are four kinds: `KEY_DOWN`, `KEY_UP`, `MOUSE` (position and button together) and
`QUIT`.

Key names are Thunder's English ones — `"left"`, `"right"`, `"up"`, `"down"`, `"space"`,
`"enter"`, `"escape"`, `"backspace"`, `"tab"`, or the character itself for a letter or digit.
The VM translates them into the program's own language, so a Pashto program sees `"کیڼ"`
without the plug-in knowing anything about Pashto.

### Building one

```sh
cc -std=c11 -shared -fPIC -I<thunder>/include -o libthunder_display_mine.so mine.c
```

The name decides the option: `libthunder_display_mine.so` is `--display mine`. With CMake,
copy `lib/display/CMakeLists.txt`. Because the boundary is plain C, the plug-in may be
written in C, C++, Rust, Zig — anything that can export a C function and fill in a struct.

---

## 4. Under the bonnet

* `PluginDisplay` (`include/thunder/display.hpp`, `src/core/display.cpp`) is an ordinary
  `graphics::GraphicsHost`, the same interface the IDE's pipe host implements. It converts the
  canvas shapes into the C structs and feeds the events it gets back into the same
  `graphics::Events` object the IDE's protocol feeds — so `key_pressed()`, `key_down()` and
  the mouse built-ins know nothing about any of this.
* `GraphicsHost::should_stop()` is how a closed window reaches the VM: `Vm::present` checks it
  after every frame and ends the program the way the frame limit does.
* The window is opened by the **first frame**, not at start-up, so a program that draws
  nothing never makes a window appear, and `canvas(w, h)` has already set the size by then.
* `dlopen`/`LoadLibrary` live behind `os::Library`, shared with the
  [native interface](native.md).

**A note on safety**, the same one as for native modules: a display plug-in is native code and
runs with the full rights of the process. The VM does not check it. Load plug-ins you built or
trust.
