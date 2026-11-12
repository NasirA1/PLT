# Changelog

## 0.2.0 - 2026-11-12

The first release, shipped with Thunder 0.2.0.

* **Highlighting.** A TextMate grammar generated from the compiler's language packs (English and
  Pashto, including built-ins written as several words), refined by semantic tokens from
  `thunderc --json`.
* **Diagnostics** from the compiler itself, as you type or on save, with the phase and the error
  code; problems inside an imported module appear in that module's file; names the program never
  reads are greyed out.
* **IntelliSense:** completion (keywords, types, built-ins by imported module, names in scope,
  functions, structs, struct fields after `.`, modules after `import`), signature help, hovers
  with inferred types and the word's counterpart in the other language, go to definition across
  modules, references, rename, document and workspace symbols, inlay hints for inferred types.
* **Projects:** `thunder.json` with a JSON schema, a project view, several folders per workspace,
  and **Thunder: New Project…** with English and Pashto templates.
* **Building and running:** `thunder` tasks (build, run, check, package) with the `$thunderc`
  problem matcher; programs run in a terminal so `input()` works.
* **Debugging:** a debug adapter over `thundervm --trace-json` with breakpoints, stepping in both
  directions, reverse continue, the call stack, locals, globals, the operand stack and the
  current instruction.
* **Under the bonnet:** the token stream, syntax tree, symbols, bytecode listing and hex dump as
  documents beside the program.
* **Translate** a file between English and Pashto (`thunderc --translate`), and
  **Run and Save the Picture (SVG)** for a graphics program (`thundervm --svg`).
