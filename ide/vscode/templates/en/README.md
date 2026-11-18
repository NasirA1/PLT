# {{name}}

A Thunder project.

```
main.th              the program: the entry point named in thunder.json
lib/greeting.th      a library file, declaring the module greeting
tests/               the tests (each file ends with test.finish())
thunder.json         the project: its entry point, language and output
.vscode/             the build task, the debug configuration and the editor's settings
```

## In VS Code

| Key | What it does |
| --- | --- |
| **F5** | Debug the project — breakpoints, stepping forwards *and backwards*, variables, the call stack |
| **Ctrl+F5** | Run it |
| **F7** | Compile it (the problems appear in the Problems panel) |
| **Ctrl+Shift+P** → *Thunder* | Every Thunder command: the canvas, the compiler's phases (tokens, syntax tree, symbols, bytecode), translating between English and Pashto, packaging the program as an application |

The **Testing** view lists the tests in `tests/`; *Thunder: Run All Tests* runs them all.

## From the command line

```sh
thunderc main.th -o main.thc     # compile
thundervm main.thc               # run
thunderc --package myapp main.th # one native executable, no toolchain needed
```

## Learning Thunder

The **Thunder** view in the side bar opens the reference for the language, the library, the data
structures, the algorithms and the testing module, with every sample program a click away.
