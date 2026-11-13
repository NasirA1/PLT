# A multi-file Thunder project

The numbered programs in `samples/en` and `samples/ps` are lessons: one file each, opened from
the Thunder IDE's **Sample** list. This folder is the other thing - a small **project**, of the
shape the VS Code extension is built for: an entry point, modules in `lib/`, a `thunder.json`
that ties them together, and the VS Code tasks and launch configurations that go with it.

It is **not** in the IDE's sample list (that list is `samples/<language>/*.th`, and this is
neither), and no golden test covers it. It is here to be opened in VS Code.

```
samples/project/
    thunder.json          the project: entry point, language, module paths, arguments
    main.th               the program
    lib/geometry.th       a module: the Rectangle struct and what can be done with one
    lib/statistics.th     a module that imports another module
    .vscode/tasks.json    Thunder: build / run / package
    .vscode/launch.json   three ways to debug it
    pashto/               the same idea in Pashto, for the right-to-left view
        thunder.json
        main.th
        lib/shapes.th
```

## Trying it

Open **this folder** in VS Code (`code samples/project`) with the Thunder extension installed
(`ide/vscode/scripts/install.sh`, or `install.ps1` on Windows). Then:

| | |
| --- | --- |
| `F7` | Compile `main.th`; the Problems panel stays empty |
| `Ctrl+F5` | Run it in a terminal (the arguments in `thunder.json` arrive in `args()`) |
| `F5` | Debug it - step with `F10`/`F11`, and **step back** from the debug toolbar |
| `F12` on `area`, `Rectangle` or `mean` | Jumps into `lib/geometry.th` or `lib/statistics.th` |
| Type `room.` inside `report` | Offers `width` and `height`, from the struct in the other file |
| `Shift+F12` on `Rectangle` | Every use of it |
| The **Thunder** icon in the activity bar | The project, its programs, and each phase of the compiler |

Things worth breaking on purpose:

* Put `var x: int = "text"` in `lib/geometry.th`: the error appears **in that file**, with the
  code `S005`, while `main.th` is what is being compiled.
* Write `;` at the end of a line: the compiler suggests `,` and the quick fix (`Ctrl+.`) applies
  it.
* Use `platform()` without `import os`: the quick fix adds the import.

## The Pashto project

`pashto/` is the same program with Pashto names. Open `pashto/main.th` and the extension says
**پښتو** in the status bar and opens the **right-to-left view** beside the editor, where the
program is laid out token by token as the Thunder IDE lays it out. (VS Code's editor itself is
always left to right - see the extension's README.)

## A note on imports

A module is looked for **next to the file that imports it** (then in the project's
`modulePaths`, then in the SDK's own `lib/thunder`). So `main.th` says `import "lib/geometry"`
while `lib/statistics.th`, which sits in the same directory as its sibling, says
`import "geometry"`.
