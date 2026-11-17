# A multi-file Thunder project

The numbered programs in `samples/en` and `samples/ps` are lessons: one file each, opened from
the Thunder IDE's **Sample** list. This folder is the other thing - a small **project**, of the
shape the VS Code extension is built for: an entry point, library files in `lib/` that declare
modules, a `thunder.json` that ties them together, and the VS Code tasks and launch
configurations that go with it.

It is **not** in the IDE's sample list (that list is `samples/<language>/*.th`, and this is
neither), and no golden test covers it. It is here to be opened in VS Code.

```
samples/project/
    thunder.json          the project: entry point, language, module paths, arguments
    main.th               the program: uses geometry.Rectangle, geometry.area, statistics.mean, ...
    lib/geometry.th       module geometry: the Rectangle struct and what can be done with one
    lib/statistics.th     module statistics, which imports lib/geometry.th and uses geometry
    .vscode/tasks.json    Thunder: build / run / package
    .vscode/launch.json   three ways to debug it
    pashto/               the same idea in Pashto, for the right-to-left editor
        thunder.json
        main.th
        lib/shapes.th     ټولګی هندسه (module geometry): څلور_اړخیز, مساحت, ...
        lib/statistics.th ټولګی احصایه (module statistics), which imports lib/shapes.th
```

## Trying it

Open **this folder** in VS Code (`code samples/project`) with the Thunder extension installed
(`ide/vscode/scripts/install.sh`, or `install.ps1` on Windows). Then:

| | |
| --- | --- |
| `F7` | Compile `main.th`; the Problems panel stays empty |
| `Ctrl+F5` | Run it in a terminal (the arguments in `thunder.json` arrive in `os.args()`) |
| `F5` | Debug it - step with `F10`/`F11`, and **step back** from the debug toolbar; the call stack shows qualified names such as `geometry.describe` |
| `F12` on `geometry.area`, `geometry.Rectangle` or `statistics.mean` | Jumps into `lib/geometry.th` or `lib/statistics.th` |
| Type `geometry.` | Offers the module's members: `Rectangle`, `area`, `perimeter`, `is_square`, `describe` |
| Type `room.` inside `report` | Offers `width` and `height`, from the struct in the other file |
| `Shift+F12` on `Rectangle` | Every use of `geometry.Rectangle` |
| The **Thunder** icon in the activity bar | The project, its programs, and each phase of the compiler |

Things worth breaking on purpose:

* Put `var x: int = "text"` inside `module geometry` in `lib/geometry.th`: the error appears
  **in that file**, with the code `S005`, while `main.th` is what is being compiled.
* Write `;` at the end of a line: the compiler suggests `,` and the quick fix (`Ctrl+.`) applies
  it.
* Use `os.platform()` without `import os`: `S049`, and the quick fix adds the import.
* Write `area(room)` instead of `geometry.area(room)` in `main.th`: `S067` says which module it
  belongs to, and the quick fix writes `geometry.area`. Plain `format` instead of `string.format`
  in `lib/geometry.th` is reported the same way, naming the built-in module `string`.
* Rename `describe` in `lib/geometry.th` to `_describe`: `geometry.describe` in `main.th` no
  longer exists (`S070`), and calling it `geometry._describe` there is `S066` - a name that starts
  with `_` is private to its module.

## The Pashto project

`pashto/` is the same program with Pashto names: `lib/shapes.th` declares the module `هندسه`
(geometry) and `lib/statistics.th` the module `احصایه` (statistics), and `main.th` calls
`هندسه.مساحت`, `احصایه.منځنی` and the built-in `سیسټم.دلیلونه()`; `F12` on `هندسه.مساحت` jumps
into `lib/shapes.th`. Open `pashto/main.th` and the extension says
**پښتو** in the status bar and opens it in the **right-to-left editor**, where the program
is laid out token by token as the Thunder IDE lays it out - and edited there: typing, Enter with
indentation, Tab, Backspace, undo, completion (start typing `کر` and the list offers `کرښه چاپ`)
and the compiler's problems all work. The button in the editor's title bar switches back to
VS Code's text editor (always left to right) and forth.

## A note on imports

`import` names a **file**; the modules it declares then become usable. A file is looked for
**next to the file that imports it** (then in the project's `modulePaths`, then in the SDK's own
`lib/thunder`). So `main.th` says `import "lib/geometry"` while `lib/statistics.th`, which sits
in the same directory as its sibling, says `import "geometry"` - and both then write
`geometry.area(...)`, because the module is called `geometry` wherever it is imported from. In
the Pashto project, `lib/statistics.th` imports `"shapes"` and writes `هندسه.مساحت(...)`.
