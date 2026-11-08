# Display plug-ins: a window for a Thunder graphics program

A Thunder program draws into a canvas; a **display plug-in** puts that canvas on a screen when
there is no IDE to do it. The whole interface is one plain-C header,
[`include/thunder/display.h`](../../include/thunder/display.h) — see the
[display reference](../../docs/display.md).

| Directory | What it is |
| --- | --- |
| `text/` | the smallest possible display: each frame written to standard error. **Read this one first.** |
| `sdl/` | a real window, built on SDL2: every shape, the keyboard and the mouse. |

Both are built with the toolchain and end up beside `thunderc` and `thundervm`, so:

```sh
cd <thunder>
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release && cmake --build build/release

build/release/bin/thunderc samples/en/28_snake.th -o /tmp/snake.thc
build/release/bin/thundervm --display sdl /tmp/snake.thc     # play it in a window
build/release/bin/thundervm --display text /tmp/snake.thc    # watch the frames as text
```

The SDL plug-in needs **SDL2**; CMake skips it (and says so) when SDL2 is not installed:

| System | Install |
| --- | --- |
| Debian / Ubuntu | `sudo apt install libsdl2-dev` |
| Fedora | `sudo dnf install SDL2-devel` |
| macOS | `brew install sdl2` |
| Windows | `vcpkg install sdl2`, then configure with the vcpkg toolchain file |

## Building one on its own, without CMake

```sh
# the text display: no dependencies at all
cc -std=c11 -shared -fPIC -I../../include -o libthunder_display_text.so text/thunder_display_text.c

# the SDL display
cc -std=c11 -shared -fPIC -I../../include $(pkg-config --cflags sdl2) \
   -o libthunder_display_sdl.so sdl/thunder_display_sdl.c $(pkg-config --libs sdl2) -lm
```

The file name decides the option: `libthunder_display_sdl.so` is `--display sdl`.

## Trying a game without a screen

The text display can play the part of the person at the keyboard, which makes a game
runnable in a test or over ssh:

```sh
THUNDER_DISPLAY_TEXT_KEYS=left,space \
THUNDER_DISPLAY_TEXT_MOUSE=120,45,1 \
THUNDER_DISPLAY_TEXT_FRAMES=20 \
THUNDER_DISPLAY_TEXT_QUIET=1 \
    thundervm --display text /tmp/snake.thc
```

| Variable | Effect |
| --- | --- |
| `THUNDER_DISPLAY_TEXT_FRAMES=n` | report "the window was closed" after n frames, so the program ends |
| `THUNDER_DISPLAY_TEXT_KEYS=a,b` | press and release these keys at the first frame |
| `THUNDER_DISPLAY_TEXT_MOUSE=x,y,b` | put the mouse there (`b` = 1 while the button is down) |
| `THUNDER_DISPLAY_TEXT_QUIET=1` | do all of it, but print nothing |

## Shipping a display with an application

`--package` copies the plug-in next to the packaged program when you name it:

```sh
thunderc samples/en/28_snake.th --package snake --display sdl -I build/release/bin
./snake --thunder-display sdl
```

## Writing your own

Copy `text/thunder_display_text.c`: about 100 lines, four functions and one entry point.
[`docs/display.md`](../../docs/display.md) section 3 walks through each of them, and
`sdl/thunder_display_sdl.c` shows what each one looks like with real pixels — including a
small built-in font, so that it needs SDL2 alone. Swap in SDL_ttf for proper typography (and
for Pashto letter shaping, which the built-in font cannot do).
