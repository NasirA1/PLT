# The Thunder Graphics Reference

The built-in module **`graphics`** lets a program draw pictures, animate them and react to the
keyboard and the mouse. That is enough for simulations, visualisations and simple games. In the
IDE the picture appears in the **Canvas** panel; on the command line it can be saved as an SVG
image.

```thunder
import graphics

canvas(400, 300)
clear("navy")
circle(200, 150, 40, "orange")
text(10, 10, "Hello, graphics!", "white")
```

Samples `24_drawing` to `28_snake` (in `samples/en/`, and in Pashto in `samples/ps/`) build up from
a still picture to a game. In the IDE, this document is under
**Help > Graphics reference**.

| Sample | Shows |
| --- | --- |
| `24_drawing` | the canvas, coordinates, colors, `rgb`, every kind of shape, text |
| `25_bouncing_ball` | an animation loop: clear, draw, `frame()`, move |
| `26_fireworks` | a particle simulation: structs, gravity, "fixed point" numbers, `random_int`, `filter`, mouse clicks |
| `27_life` | Conway's Game of Life: a 2D grid, keys (pause, restart) and mouse drawing |
| `28_snake` | a game: arrow keys, an array of pairs as the snake, collisions, score, game over and restart |
| `31_waves` | `sin`, `cos` and `tan` plotted and animated; radians and `pi()` |
| `32_orbits` | circular motion: planets on `cos`/`sin` orbits, and a spirograph from two circles |
| `33_pendulum` | `sqrt`, `exp` and `sin`: the pendulum formula against the equation of motion, step by step |

Outside the IDE a graphics program can have **a window of its own**: `thundervm --display sdl
program.thc` draws it through a display plug-in. The plug-ins are shared libraries with a
small C interface, so another one can be written for any graphics library — see the
[display reference](display.md) and `lib/display/`.

---

## 1. The canvas

* **Coordinates** are whole numbers of pixels. `(0, 0)` is the **top left** corner; x grows to the
  right and **y grows downwards** (as on every screen, unlike in mathematics).
* `canvas(width, height)` sets the size (1 to 2000 pixels each) and clears the canvas to white.
  Without it, the canvas is 400 × 300 pixels.
* **Shapes are drawn in order**, so later shapes cover earlier ones. Shapes may stick out of the
  canvas; the part outside is not shown.
* **The canvas keeps everything** drawn since the last `clear()`, like paint on paper.
  An animation therefore starts every frame with `clear(...)`. At most 100,000 shapes can be
  drawn between two clears (runtime error `R024`), which catches programs that forget to clear.

### Colors

A color is a string:

* a **name**: `black`, `white`, `gray`/`grey`, `silver`, `red`, `maroon`, `orange`, `gold`,
  `yellow`, `olive`, `lime`, `green`, `teal`, `aqua`/`cyan`, `sky`, `blue`, `navy`, `purple`,
  `fuchsia`/`magenta`, `pink`, `brown`. Names are not case-sensitive. Pashto programs may
  use `تور`, `سپین`, `سور`, `زرغون`, `آبي`, `ژېړ`, `نارنجي`, `ګلابي`, `خړ`, `نسواري`, `بنفش`,
  `اسماني`, `طلايي`;
* a **code** `"#rrggbb"` or `"#rgb"` in hexadecimal: `"#ff8800"`, `"#f80"`;
* the result of **`rgb(red, green, blue)`**, each from 0 to 255 (values outside are clamped):
  `rgb(255, 136, 0)` is `"#ff8800"`. This makes colors you can calculate, e.g. a gradient:
  `rgb(40 + band * 6, 110, 230)`.

Anything else is runtime error `R021`.

---

## 2. Drawing

Every coordinate and size is a **decimal** (`float`), so a shape can sit wherever the arithmetic
puts it — `circle(cx + cos(a) * r, cy + sin(a) * r, 2.5, "white")` needs no rounding. Whole
numbers are widened automatically, so `circle(50, 50, 10, "red")` is still correct, and a whole
coordinate is written back as a whole number in SVG files and frame records.

| Function | Draws |
| --- | --- |
| `clear(color)` | fills the whole canvas, removing everything drawn before |
| `pixel(x, y, color)` | one pixel |
| `line(x1, y1, x2, y2, color)` | a line from (x1, y1) to (x2, y2) |
| `rect(x, y, width, height, color)` | a filled rectangle; (x, y) is its top left corner |
| `rect_outline(x, y, width, height, color)` | the outline of a rectangle |
| `circle(x, y, radius, color)` | a filled circle; (x, y) is its centre |
| `circle_outline(x, y, radius, color)` | the outline of a circle |
| `text(x, y, message, color)` | text; (x, y) is its top left corner, for every script |

| Setting | Effect on what is drawn afterwards |
| --- | --- |
| `line_width(width)` | width of lines and outlines in pixels (0.1 to 100, default 1) |
| `text_size(size)` | height of text in pixels (1 to 400, default 16) |

A negative width or height draws the rectangle to the left of or above (x, y). A negative radius,
and settings outside their range, are runtime error `R022`, which says what the allowed range is.

There is no "triangle" or "polygon": `24_drawing` builds a roof from ever shorter lines, which is
also a good loop exercise.

---

## 3. Animation

An animation is a loop that draws one picture, a **frame**, at a time:

```thunder
import graphics

var x = 0
while true
    clear("black")                  // 1. start from an empty canvas
    circle(x, 150, 20, "orange")    // 2. draw everything where it is now
    frame()                         // 3. show the frame and wait for the next one
    x = (x + 4) % 400               // 4. move things a little
```

| Function | Effect |
| --- | --- |
| `frame()` | shows the picture, then **waits** until the next frame is due, so the animation runs at the same speed on every computer |
| `frame_rate(fps)` | how many frames per second `frame()` shows: 1 to 120, default 30 |
| `show()` | shows the picture **now**, without waiting (for a picture that is built up step by step) |

* A picture that has not been shown when the program ends (a still drawing with no `frame()` or
  `show()`) is shown then, also after a runtime error, so you can see how far the drawing got.
* If a frame takes longer to compute than the frame rate allows, the animation simply runs
  slower; it does not try to catch up.
* An animation loop usually never ends: press **Stop** in the IDE. The IDE's safety limit on
  the number of instructions (which stops accidental endless loops) then applies **between two
  frames** rather than to the whole run, so an animation may run for as long as you like.
* For smooth slow movement, keep positions as decimals: `x = x + 0.35` moves a third of a pixel
  per frame. (`26_fireworks` predates decimals and keeps positions in tenths of a pixel instead,
  which is still a useful technique when a program must stay exact.)

---

## 4. Keyboard and mouse

| Function | Result |
| --- | --- |
| `key_pressed(): string` | the **next key press** not yet taken, or `""` if there is none |
| `key_down(key): bool` | `true` while `key` is **held down** |
| `mouse_x(): int`, `mouse_y(): int` | the mouse position, in whole canvas pixels |
| `mouse_down(): bool` | `true` while the mouse button is held down over the canvas |

**Key names** are lower case: `"left"`, `"right"`, `"up"`, `"down"`, `"space"`, `"enter"`,
`"escape"`, `"backspace"`, `"tab"`, and the letter or digit typed (`"a"`, `"7"`; with a Pashto
keyboard, Pashto letters such as `"ب"`). In Pashto programs the special keys have Pashto names:

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `left` | `کیڼ` | | `space` | `تشه` |
| `right` | `ښي` | | `enter` | `ننوتل` |
| `up` | `پورته` | | `escape` | `وتل` |
| `down` | `ښکته` | | `backspace` | `شاته` |
| `tab` | `ټب` | | | |

`key_down` accepts either name.

* `key_pressed()` is a **queue**: every press is kept (up to 64) until the program takes it, so a
  quick double tap is never lost. A game typically takes one key per frame, as `28_snake` does:
  two quick turns then become two steps instead of a reversal into itself.
* `key_down` suits continuous movement, e.g. `if key_down("left")` then `x = x - 3` in every frame.
* For a single click, compare with the previous frame:
  `if mouse_down() and not was_down ...` then `was_down = mouse_down()` (`26_fireworks`).
* In the IDE, **click the Canvas panel** so that it receives the keyboard. The IDE gives it the
  keyboard automatically when the first frame appears.
* On the command line (without the IDE) there are no keys or mouse: `key_pressed()` returns `""`
  and `mouse_down()` returns `false`.

---

## 5. Random numbers

These are available in every program, not only with `import graphics`:

| Function | Result |
| --- | --- |
| `random_int(low, high): int` | a random whole number from `low` to `high`, **both included** (`low > high` is runtime error `R023`) |
| `seed_random(seed: int)` | restarts the sequence: after the same seed, the same numbers follow |

Without a seed, every run is different. `thundervm --seed n` (and the golden tests) start from a
fixed seed. Thunder uses its own generator (SplitMix64), so a seed gives the same numbers on every
operating system and compiler.

---

## 6. In the IDE

![The Canvas panel running 28_snake](images/ide_canvas_snake.png)

* The **Canvas** panel is a tab next to "Under the bonnet". It comes to the front when the
  program shows its first frame. Like the other panels, it can be moved, floated in its own
  window (handy for games) or closed (View > Panels brings it back).
* The picture is scaled to fit the panel, keeping its proportions, so make the panel bigger
  for a bigger picture. Mouse positions are converted back to canvas pixels.
* The line above the picture shows the frame number and the canvas size.
* Text printed with `println` still goes to the Output panel. The Build Log reports how many
  frames were shown.
* The last picture stays visible after the program ends, until you compile or run again.

![A Pashto drawing in the mirrored interface](images/ide_canvas_pashto.png)

---

## 7. On the command line

```
thundervm picture.thc                        # runs; without a display, nothing is shown
thundervm --svg picture.svg picture.thc      # saves the final picture as an SVG image
thundervm --max-frames 100 --svg f.svg anim.thc   # stops an endless animation after 100 frames
thundervm --seed 7 game.thc                  # the same random numbers on every run
thundervm --canvas game.thc                  # the display protocol, as the IDE uses it (below)
```

Without `--canvas`, `frame()` does not wait (nobody is watching), so a headless run finishes as
fast as the computer allows. SVG files open in every web browser.

---

## 8. Under the bonnet

**In the VM.** The canvas is a list of shapes (`include/thunder/graphics.hpp`). Each drawing
function checks its arguments and appends a shape; `clear()` empties the list. `frame()` and
`show()` *present* the list to a **graphics host**, an interface with one method, `present`.
`thundervm --canvas` uses a host that writes to standard output; the tests use one that records
the frames. The built-ins are in `src/core/vm_graphics.cpp`; the shapes, colors, SVG output and the
protocol are in `src/core/graphics.cpp`.

**The display protocol** (`thundervm --canvas`) shares the program's normal input and output:

* Each frame is **one line on standard output**: the ASCII "record separator" character (code
  30), then a JSON object, then a line break:

  ```
  \x1e{"type":"frame","n":12,"width":400,"height":300,"keep":5,"shapes":[["circle",120,80,18,"#ffa500"]]}
  ```

  The IDE already has the first `keep` shapes of the previous frame, so a frame that only adds
  shapes sends just the new ones; after a `clear()`, `keep` is 0. Shapes are arrays:
  `["clear", color]`, `["pixel", x, y, color]`, `["line", x1, y1, x2, y2, color, width]`,
  `["rect", x, y, w, h, color]`, `["rect_outline", x, y, w, h, color, width]`,
  `["circle", x, y, r, color]`, `["circle_outline", x, y, r, color, width]`,
  `["text", x, y, message, color, size]`. Colors are always `"#rrggbb"`.
* **Events are lines on standard input**, starting with the same character:
  `key down left`, `key up left`, `mouse 120 45 1` (x, y, and 1 while the button is held). All
  other lines are what `input()` reads. `thundervm` reads them on a background thread, so a game
  never waits for the keyboard.
* The program's own text cannot start a frame by accident: while `--canvas` is on, a
  record separator in printed text is written as `�`.

**In the IDE.** `ide/thunder_ide/core/canvas.py` separates the records from the text output (a
record may arrive in several pieces) and keeps the current picture. That code runs on the thread
that reads the program's output, and the GUI paints at most one picture per repaint, so a fast
animation cannot flood the interface. `ide/thunder_ide/qt/panels.py` (`QtCanvas`) paints the
shapes with `QPainter` and turns keys and mouse movements into event lines.

**Why this design?** The VM runs in its own process, as it always has, so a stuck program can
never freeze the IDE, and the same `thundervm` works with and without a display. Sending shapes
rather than pixels keeps frames small: a typical frame of `28_snake` is under 300 bytes, compared
with 360 KB for a 400 × 300 bitmap.

---

## 9. Error codes

| Code | Meaning |
| --- | --- |
| `S049` | a graphics function used without `import graphics` |
| `R021` | not a color |
| `R022` | a size or setting outside its range (canvas size, radius, line width, text size, frame rate) |
| `R023` | `random_int(low, high)` with `low > high` |
| `R024` | more than 100,000 shapes since the last `clear()` |

---

## 10. Pashto names

`import graphics` is `راوړه ګرافیک` (the English `graphics` is accepted too). Like the rest of the
Pashto vocabulary, these names are drafts that await review by a native speaker.

| English | Pashto | | English | Pashto |
| --- | --- | --- | --- | --- |
| `canvas` | `ټاټ` | | `clear` | `ټاټ پاک کړه` |
| `pixel` | `ټکی` | | `line` | `لیکه` |
| `rect` | `مستطیل` | | `rect_outline` | `د مستطیل څنډه` |
| `circle` | `دایره` | | `circle_outline` | `د دایرې څنډه` |
| `text` | `لیکنه` | | `text_size` | `د لیکنې اندازه` |
| `line_width` | `د لیکې پلنوالی` | | `rgb` | `رنګ` |
| `frame` | `چوکاټ` | | `show` | `ښکاره کړه` |
| `frame_rate` | `د چوکاټ چټکتیا` | | `key_pressed` | `کښېکښل شوې تڼۍ` |
| `key_down` | `تڼۍ کښېکښل شوې ده` | | `mouse_x` | `د موږک افقي ځای` |
| `mouse_y` | `د موږک عمودي ځای` | | `mouse_down` | `موږک کښېکښل شوی` |
| `random_int` | `تصادفي عدد` | | `seed_random` | `تصادفي بنسټ` |

The Canvas panel is called `ټاټ` in the Pashto interface. The Pashto samples write coordinates as
`س` and `ص` (the Arabic-script letters used for x and y in mathematics).
