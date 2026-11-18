# A GUI SDK for Thunder: the language, the drawing layer, and native look and feel

*Exploration report, 2026-11-17. Analysis only; no code was written. The small widget toolkit in
§2.2 was compiled and run against the toolchain as it then was (0.7.0). Companions in this folder:
`concurrency_report.md` (an event loop and timers, which a GUI needs) and
`test_decorators_report.md`.*

> **Status (0.8.0, 2026-11-18):** the OOP subset of §3 is implemented - methods, constructors,
> interfaces (`interface`, Pashto `برسېر`) with dynamic dispatch, closures, and (beyond this
> report) single-level mixins - with one change against the sketch: methods are declared inside
> the struct's body with **no `self` parameter** (a bare member name inside a method means the
> receiver's member), and a member written `on_click(): func()` is a closure-valued field. Rows
> 1-3 of the table in §2.3 are done; rows 4-7 (defaults and named arguments, a conditional
> expression, event-driven waiting, sum types) and everything in §4-§5 (graphics ABI 2, the `ui`
> API) remain open. See devlog §57.

## Summary

* **Can the language support a simple GUI SDK today?** Only just. A widget tree with click
  handlers *can* be written, and §2.2 shows one that works: recursive structs, function values in
  struct fields, enums and modules. It hits every one of the language's gaps at once, though.
  Every widget kind must live in one "fat" struct, with hand-written `if` chains instead of
  dispatch. Handlers cannot carry state (no closures). A button cannot be sized to its text (no
  text measurement). The program has to spin on `graphics.frame()` because nothing can wait for
  an event.
* **Missing from the core language**, in order of importance: **closures**; **polymorphism**
  (interfaces with dynamic dispatch); **method-call syntax** (`button.set_text("OK")`); **default
  values** for struct fields and function parameters, and named arguments for functions; a
  **conditional expression**; and **event-driven waiting** (see `concurrency_report.md`).
* **OOP: yes, a subset** - methods, interfaces and closures; no class inheritance. Methods fit
  the module design almost for free: a struct and the functions of its module *are* a class.
  Interfaces give the dispatch a widget tree needs. Inheritance adds complexity that composition
  plus interfaces already covers (as in Go and Rust).
* **The graphics module is not enough for a GUI.** It is an immediate drawing list of eight
  shapes. It has no text measurement, fonts, transparency, clipping, rounded rectangles, paths or
  images. Its input has no text entry (so no Pashto typing through an input method), no
  modifier keys, no mouse wheel, no right button and no window resize. The quality also differs
  per plug-in: the SDL one has a 5×7 ASCII font and draws Pashto as boxes.
* **The display plug-in interface is the right *kind* of seam, but it must grow.** Version 1 is
  one-way (`present` and `poll`). A GUI needs **queries back** (how wide is this text?), a
  **richer drawing vocabulary**, **richer events**, and **waiting** for an event instead of a
  frame. That means ABI version 2.
* **Recommendation: one Thunder `ui` API with two kinds of backend.**
  1. **Own-drawn widgets** on an extended display ABI. These work everywhere the toolchain
     already draws (Qt IDE canvas, VS Code canvas, SVG, golden tests), and students can read how
     a button works. This comes first.
  2. **Native widgets** through a new retained "UI host" interface, with **Qt Widgets** as the
     main backend. Qt gives native-like styles on every OS, full Pashto shaping, right-to-left
     layout, input methods and accessibility. The Qt IDE can implement the same interface in
     PySide6, and VS Code in a webview.

---

## 1. What a GUI SDK needs

| Need | Example |
| --- | --- |
| A tree of widgets of *different kinds*, treated alike | a window holds panels, which hold buttons, labels and text fields |
| Behaviour per kind | each kind draws itself, measures itself and handles its events |
| Event handlers that remember things | "when this button is clicked, add 1 to *this* counter and show it in *that* label" |
| Convenient construction | `button("OK", width: 80)` without listing ten fields |
| Layout | rows, columns, grids; sizes from content (text width!) |
| Text input | typing, editing, selecting; Pashto through the OS input method; right-to-left caret |
| Waiting for events without burning the CPU, plus timers | an idle GUI uses 0% CPU |
| A drawing layer (own-drawn widgets) or native controls | |
| Look and feel | native, or at least clean and consistent |

## 2. The language today

### 2.1 What is already there

Structs with reference semantics, recursive structs and `nothing`, generics, enums, modules
with private members, function values (references to declared functions), maps and arrays,
`with` for resources, named struct construction (`Point(y: 2, x: 1)`) with omitted fields set to
their type's default. That is a solid base: a widget tree is just recursive structs.

### 2.2 A widget toolkit in today's Thunder (compiles and runs)

```thunder
import graphics

module ui
    enum Kind
        panel, label, button

    struct Widget                    # one struct for every kind of widget
        kind: Kind
        x: int
        y: int
        width: int
        height: int
        text: string
        children: Widget[]
        on_click: func(Widget)       # no closures: the handler gets the widget itself
        pressed: bool

    func button(text: string, on_click: func(Widget)): Widget
        # There is no way to ask how wide the text is: guess from the character count.
        return Widget(kind: Kind.button, x: 0, y: 0, width: len(text) * 9 + 16, height: 28, text: text,
            children: [], on_click: on_click, pressed: false)

    func draw(w: Widget)             # "dynamic dispatch" by hand, edited for every new kind
        if w.kind == Kind.panel
            graphics.rect(w.x, w.y, w.width, w.height, "#f0f0f0")
        else if w.kind == Kind.button
            graphics.rect(w.x, w.y, w.width, w.height, "#cde")
        if w.text != ""
            graphics.text(w.x + 8, w.y + 6, w.text, graphics.Color.black)
        for c in w.children
            draw(c)
    # ... panel(), label(), add() (a column layout) and click() (hit testing) as above

var clicks = 0                       # a handler's state has to be global
var status = ui.label("clicked 0 times")

func counted(b: ui.Widget)
    clicks += 1
    status.text = "clicked " + str(clicks) + " times"

var window = ui.panel(0, 0, 300, 200)
ui.add(window, ui.button("Click me", counted))
ui.add(window, status)
```

It works: a simulated click at (20, 20) updates the label, and the picture renders to SVG. Each
comment marks a gap:

| Gap | What it costs here |
| --- | --- |
| **No polymorphism** | Every kind shares one struct with the union of all fields; every operation (draw, measure, click, key) is an `if` chain that must be edited for each new widget kind; a user cannot add a widget kind without editing the SDK |
| **No closures** | `counted` can only reach `clicks` and `status` because they are globals; two windows with their own counters need a lookup table keyed by widget |
| **No text measurement** | Sizes are guessed from `len(text)`, which is wrong for proportional fonts and meaningless for Pashto (joined letters of varying width) |
| **No conditional expression** | `w.pressed ? "#9ab" : "#cde"` is lexical error L001 today; a three-line `if` instead (found while writing the prototype) |
| **No default values** | `pressed: false` and `children: []` are written in every constructor; `height: 28` cannot be a declared default |
| **Function call style** | `ui.add(window, status)`, `ui.draw(window)` - fine, but `window.add(status)` reads like every GUI toolkit students will meet later |
| **No waiting** | A real event loop is `while true ... graphics.frame()`: it polls 30 times a second and cannot sleep until the next click |

### 2.3 Missing language features, ranked

| # | Feature | Why a GUI needs it | VM/compiler impact | Size |
| --- | --- | --- | --- | --- |
| 1 | **Closures** (lambdas capturing variables) | Event handlers with their own state: `ui.button("+", func() count += 1)` | Function values carry an environment; captured variables live in heap "boxes" so the closure and its creator share them; the trace and debugger show captures. Reference cycles (a button whose handler captures its window) always pass through a struct, so the existing shutdown sweep still frees them | medium-large |
| 2 | **Interfaces + dynamic dispatch** | A heterogeneous widget tree; user-defined widgets; one renderer interface with two backends (own-drawn, native) | `interface Widget` with method signatures; `struct Button implements Widget`; a value of interface type is a struct reference whose methods are found through a table the compiler builds per struct (every struct is known at compile time, generic instances included); a new `CALL_INTERFACE` instruction | medium |
| 3 | **Method-call syntax** | `button.set_text("OK")`, `window.add(child)` | Sugar only: `x.f(args)` means `M.f(x, args)` where `M` is the module that declares the type of `x`. It fits the module design: struct + its module's functions = a class | small |
| 4 | **Default values and named arguments** | `ui.button("OK", width: 80)`; field defaults `height: int = 28` | Parser and checker; the defaults become ordinary expressions at the call site | small-medium |
| 5 | **Conditional expression** | Compact styling code everywhere | `a if c else b` (Python order reads well in Pashto too) or `if c then a else b`; one AST node, two jumps | small |
| 6 | **Event-driven waiting, timers** | Idle at 0% CPU; blinking caret, animations, tooltips | See `concurrency_report.md`: tasks and timers, plus a display that can *wait* (§4) | medium-large |
| 7 | **Sum types (enums with data) + `match`** | Events (`Click(x, y)`, `Key(name)`, `Text(s)`) and messages | An alternative or complement to interfaces for *closed* sets; exhaustive `match` is a strong teaching tool | medium |

Not needed for a GUI SDK: class inheritance, constructors (module `new` functions already serve),
operator overloading, properties, access modifiers beyond `_private`. Exceptions would be nice for
error reporting from deep inside a handler, but no GUI design depends on them.

## 3. Object-oriented programming: useful, as a subset

**Yes, but not "classes with inheritance".** The parts that pay off for GUIs, and for teaching
in general:

1. **Methods** - method-call syntax over the existing module + struct pairing. No new concept to
   teach beyond "the first parameter is the object". Pashto reads `تڼۍ.متن_ټاکه("سمه")` with no
   new word.
2. **Interfaces** - the essence of OOP polymorphism: "anything that can draw itself and handle a
   click". Nominal (`implements`) rather than structural, so a student gets "Button does not
   implement measure()" at the struct rather than a mysterious mismatch at a distant call.
3. **Closures** - not OOP strictly, but in GUI code they do the job inner classes or delegates do
   in Java or C#.

What to leave out, and why:

* **Inheritance** (`class Button extends Widget`): fragile base classes, `super` calls,
  overriding rules and constructor chains are the hardest part of OOP to teach and the least
  needed when interfaces and composition exist. Go and Rust build complete GUI toolkits without
  it. The common "base fields" case (x, y, width, height) is served by composition: a `Box`
  struct inside each widget, with a method that returns it.
* **Visibility keywords**: `_private` already exists at module level.
* **Static members, abstract classes, overloading**: not needed with modules and interfaces.

How it would read (a sketch, not a final syntax):

```thunder
module ui
    interface Widget
        func draw(self, canvas: Canvas)
        func measure(self, canvas: Canvas): Size
        func handle(self, event: Event): bool

    struct Button implements Widget
        text: string
        on_click: func()             # a closure
        width: int = 0               # a default value: 0 = "fit the text"

    func draw(self: Button, canvas: Canvas)
        canvas.rounded_rect(self.bounds(), 4, theme.button_face)
        canvas.text(self.bounds().inset(8), self.text, theme.text, align: Align.center)

var count = 0
var label = ui.Label(text: "0")
var plus = ui.Button(text: "+", on_click: func() count += 1; label.text = str(count))
window.add(plus)                     # window: a ui.Column, also a ui.Widget
```

New Pashto vocabulary would be needed for `interface`, `implements`, `self` and the lambda form
(drafts for a native speaker, as always). The translator handles them like any keyword.

## 4. The graphics module and the display interface

### 4.1 Is the graphics module capable enough? No.

| Area | Today | A GUI needs |
| --- | --- | --- |
| Shapes | `pixel`, `line`, `rect`, `rect_outline`, `circle`, `circle_outline`, `text`, `clear` | rounded rectangles, polygons/paths, arcs, images (icons), gradients (optional) |
| Colour | `#rrggbb` | transparency (`#rrggbbaa`) for hover and disabled states, shadows |
| Text | one size (`text_size`), no font choice, **no measurement** | font family, weight, size; measuring width and height; alignment; clipping; ellipsis |
| Clipping | none | clip to a widget's rectangle (scrolling, lists) |
| Input | key names (no modifiers, no case), mouse position, one button | **text input events (UTF-8 from the OS input method: the only way to type Pashto properly)**, modifiers, right/middle button, wheel, double click, focus |
| Window | fixed size (at most 2000×2000), one window, a title | resize events, DPI scale, cursor shapes, clipboard, several windows (dialogs) |
| Loop | `frame()` paced at a frame rate; events polled once per frame | **wait until an event or a timer**; redraw only when something changed |
| Volume | up to 100,000 shapes between clears; frames carry only new shapes | fine for widgets - a window is a few hundred shapes |

There is also **uneven quality between plug-ins**. The Qt IDE canvas antialiases and shapes Pashto.
The Win32 plug-in uses a real GDI font (Pashto shaped) but no antialiasing for shapes. The SDL
plug-in draws text with a built-in 5×7 ASCII font and draws non-ASCII characters as boxes. A GUI
SDK has to be held to the best of these, not the worst.

### 4.2 The display interface (`include/thunder/display.h`, ABI 1)

What is good and should be kept:
* a **plain C ABI** with no dependency on Thunder, so a plug-in can be built against any library
  in any language;
* **one thread** (no locking in plug-ins);
* a **version number** (the VM refuses mismatches);
* **retained, incremental frames** (`keep`: only new shapes travel).

What a GUI needs it to gain - **ABI 2**:

1. **Queries back to the display.** `measure_text(font, size, text) -> width, ascent, descent` and
   `image_size`. This is the first two-way call: the program cannot lay out text without the
   display's own font engine, and Pashto shaping lives there.
2. **A richer drawing vocabulary.** Rounded rectangles, polygons/paths, images (decoded from
   bytes the program supplies), fonts (family, weight), alpha, and clip push/pop. Keeping the
   "shape list" design (a display list) keeps the IDE, SVG and golden tests working.
3. **Richer events.** Text input (UTF-8 committed text, plus composition for input methods),
   modifiers, all buttons, wheel, double click, resize, focus, and timestamps.
4. **Waiting.** `wait(display, timeout_ms)` returns when an event arrives or the timeout ends,
   replacing "poll once per frame". This is the hook the concurrency report's scheduler needs to
   sleep efficiently.
5. **Window operations.** Title, resize, cursor shape, clipboard, a scale factor for high-DPI
   screens, and a second window for dialogs.
6. **Capabilities.** A plug-in says what it supports (antialiasing, images, input methods), so
   the SDK can degrade gracefully, for example on the text plug-in in tests.

The host protocol to the IDEs (`thundervm --canvas`, JSON records on stdout and stdin) grows the
same way. Measurement then needs a request and reply on that pipe. That is synchronous, so the
host must answer promptly, and each text measured costs a round trip unless measurements are
cached by (font, size, text) in the VM.

## 5. Cross-platform drawing with a native feel: the options

There are two architectures.

**A - Own-drawn widgets.** The Thunder SDK draws every widget with the display list. One look
everywhere, full control, and the toolkit is readable Thunder: students see how a button,
a scroll bar or a text field works. Text editing (caret, selection, bidirectional Pashto) must
then be written by us. That is hard, but this project has done it twice (the Qt editor and the
VS Code right-to-left editor), and the hard part, shaping, stays in the display's font engine.

**B - Native widgets.** The host creates real controls (button, text box, list) by handle; the
program sets properties and receives events (`clicked 17`, `text_changed 18 "..."`). It gives
the native look and feel, accessibility, input methods and right-to-left text editing for free,
but needs a new *retained* interface ("UI host") beside the display ABI, and every backend must
implement every widget.

| Backend | Arch | Native look | Pashto shaping + RTL | Input methods | Accessibility | Platforms | Size / dependency | Licence | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Qt Widgets** (C++ plug-in) | B (or A via QPainter) | Close to native (platform styles) | Excellent | Yes | Yes | Win, Linux, macOS | Large (~30 MB of DLLs) | LGPL | Already the IDE's toolkit; installed here for C++ (Qt 6.10 dev packages, devlog §4) |
| **PySide6 in the Qt IDE** | B or A | as Qt | Excellent | Yes | Yes | wherever the IDE runs | none extra | LGPL | The IDE implements the protocol over its existing pipe, as the Canvas panel does |
| **wxWidgets** | B | **Truly native** (Win32, GTK, Cocoa controls) | Good (uses the OS) | Yes | Partly | Win, Linux, macOS | Medium | wxWindows (permissive) | The "native look" choice; less polished RTL than Qt |
| **libui-ng** | B | Native | Uses the OS; limited | Yes | Partly | Win, Linux, macOS | Small (C) | MIT | Few widgets, young project |
| **Per-OS code** (Win32 common controls, GTK, Cocoa) | B | Native | OS | Yes | Yes | one each | none | - | Three implementations to maintain |
| **System webview** (WebView2, WebKitGTK, WKWebView via the `webview` C library) | B (HTML controls) or A (HTML canvas) | Modern web, not native | Excellent (browser) | Yes | Yes | Win, Linux, macOS | Small binary; needs the OS engine | MIT | The VS Code canvas already shows the pattern; HTML/CSS styling |
| **SDL2 + SDL_ttf + HarfBuzz + FriBidi** | A | Own | Good once HarfBuzz/FriBidi are added | Basic (SDL text input events) | No | Win, Linux, macOS | Medium | zlib/MIT | Extends the existing SDL plug-in; best for games |
| **Skia** | A | Own | Good (with its shaper) | No (needs a window layer) | No | all | Very large build | BSD | What Flutter and Chrome draw with; heavy to build |
| **Win32 GDI / Direct2D** | A | Own | GDI/DirectWrite shape Pashto | Yes (IMM) | No | Windows | none | - | The existing Win32 plug-in, extended |
| **Dear ImGui, Nuklear, FLTK** | A | Own (tool-like) | Weak RTL and shaping | Limited | No | all | Small | MIT/LGPL | Unsuitable as the main path for a Pashto-first language |

### Recommendation

1. **First, own-drawn (A) on display ABI 2.** It needs no new kind of interface. The SDK is
   readable Thunder, which suits a teaching language and serves the "under the bonnet" goal. It
   runs unchanged in the Qt IDE canvas, the VS Code canvas, SVG export and golden tests. Pashto
   is correct wherever the host has a real text engine (Qt, the browser, GDI/DirectWrite). The SDL
   plug-in needs SDL_ttf + HarfBuzz to get there; until then it is the "games" display.
2. **Then native widgets (B) with Qt Widgets as the main backend**, behind the *same* Thunder `ui`
   API: the widget objects talk to a backend through an interface (§3), one implementation drawing
   itself and one forwarding to native controls. Qt is chosen for its unmatched Pashto and
   right-to-left support, input methods and accessibility on all three platforms. The Qt IDE
   implements the same protocol in PySide6, so no C++ Qt is needed for runs inside the IDE. VS
   Code implements it with HTML controls in a webview. **wxWidgets** is the alternative if a
   *strictly* native look matters more than right-to-left polish. A **webview** backend is the
   lightweight option for packaged apps.
3. **Avoid** making Dear ImGui, Nuklear or FLTK the main path: their right-to-left and shaping
   support does not meet the project's Pashto requirement.

## 6. Roadmap and effort

| Step | Contents | Size (relative to past iterations) |
| --- | --- | --- |
| 1 | Conditional expression, default values, named arguments, method-call syntax | one small iteration |
| 2 | Interfaces with dynamic dispatch | one medium iteration (about the enum iteration) |
| 3 | Closures | one medium-large iteration (VM values, captures, debugger and trace display) |
| 4 | Event-driven waiting and timers | part of the concurrency work (`concurrency_report.md`, phases 1-2) |
| 5 | Display ABI 2 + host protocol 2: queries, drawing vocabulary, events, `wait`; the Qt canvas, VS Code canvas, Win32 and text plug-ins updated; SDL with SDL_ttf | one large iteration |
| 6 | The own-drawn `ui` SDK (window, panel, row/column/grid layouts, label, button, check box, text field, list, scroll view, dialog; a theme) in both languages, with samples and docs | one or two iterations |
| 7 | The native "UI host" interface; Qt Widgets plug-in, PySide6 IDE host, VS Code webview host | two iterations |

Steps 1-3 are worth doing for their own sake; they make the whole language more expressive, not
just GUIs. Step 5 is the prerequisite for any serious UI, own-drawn or not.

## 7. Pitfalls and limits

* **Synchronous text measurement over a pipe** (IDE runs) is a round trip per string. It needs a
  cache in the VM, and the host must answer measurement requests while it is also painting.
* **Closures and reference counting**: a handler that captures its own window makes a cycle. The
  shutdown sweep frees it at exit, but a long-running GUI that creates and drops windows would
  hold them until then. A real cycle collector (or weak references for parent links) becomes
  worth considering.
* **Text editing in an own-drawn toolkit** - caret movement and selection in bidirectional text,
  and input-method composition - is the single hardest widget. The work done for the two editors
  can be reused, but it remains the largest item of step 6.
* **One thread**: a long handler freezes the UI. The concurrency report's tasks are the cure,
  together with a rule that widgets are touched only from the UI task.
* **Native look via Qt** is "native-like", not native: Qt draws its own controls in the platform
  style. Only wxWidgets, libui or per-OS code use the OS controls themselves.
* **Packaging size**: Qt adds tens of megabytes to a packaged app; the own-drawn path on Win32/SDL
  stays small.
* **Testing**: own-drawn widgets render to the text display and SVG, so golden tests keep working;
  native widgets need offscreen Qt tests, as the IDE already uses.

## 8. Decisions

1. Language features: go ahead with the ranked list (§2.3)? Which first - closures or interfaces?
2. OOP subset: methods + interfaces + closures, no inheritance - agreed?
3. Interfaces: nominal (`implements`, recommended) or structural (Go)?
4. GUI path: own-drawn first, native (Qt) second, as recommended - or native first?
5. Native backend: Qt (best Pashto/RTL), wxWidgets (most native), or a webview (lightest)?
