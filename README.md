# PLT — DLang Toolchain

**Programming Language Technology: a project centred on a custom DLang language and its development toolchain.**

PLT is a collection of programming-language technology projects. This README focuses on **DLang**, and on the four connected aspects of its toolchain: the language, compiler, virtual machine (VM), and integrated development environment (IDE).

> **Naming note:** DLang here refers to the project's own language/toolchain under `dlang/`, not necessarily the independently developed [D programming language](https://dlang.org/).

## Toolchain overview

The DLang project can be understood through four layers:

| Component | Responsibility |
| --- | --- |
| **Language** | Defines source syntax and semantics: the programs that DLang accepts and what they mean. |
| **Compiler** | Translates DLang source into the representation expected by its execution environment. |
| **Virtual machine** | Executes the compiler's output according to the project's runtime model. |
| **IDE** | Provides the development-facing environment for editing and working with DLang programs. |

A conceptual workflow is:

```text
   DLang source code
          |
          v
     DLang compiler
          |
          v
   Compiled representation
          |
          v
    DLang virtual machine
          |
          v
      Program execution

   DLang IDE: authoring and development interface
```

This diagram explains the intended separation of concerns; the exact intermediate representation, execution model, and IDE integrations must be verified against the source.

## DLang language

The language is the foundation of the toolchain. Its documentation should describe the actual grammar, types, expressions, statements, functions, and other supported constructs using examples drawn from the implementation. Until the parser and sample programs are reviewed, this README does not claim support for particular language features.

## Compiler

The compiler forms the bridge between DLang source programs and the runtime. Areas worth exploring in the implementation include lexical analysis, parsing, diagnostics, symbol handling, and code generation. The existence and completeness of each stage should be checked in the source before describing it as implemented.

## Virtual machine

The VM is the execution component of the DLang toolchain. Its instruction set, runtime data representation, call model, memory management, and error handling should be documented from the actual implementation rather than assumed.

## Integrated development environment

The IDE is the user-facing part of the DLang development workflow. Specific features such as syntax highlighting, code completion, debugging, project management, or one-click compilation are not asserted here without inspection of the IDE source.

## Repository layout

| Path | Purpose |
| --- | --- |
| [`dlang/`](dlang/) | Primary focus: DLang language and toolchain. |
| [`CppLex/`](CppLex/) | Separate lexer-related project. |
| [`MyXML/`](MyXML/) | Separate XML-related project. |

## Getting started

Clone the repository:

```bash
git clone https://github.com/NasirA1/PLT.git
cd PLT/dlang
```

The exact build dependencies, compiler/VM entry points, IDE launch procedure, and sample program commands are not established by the root README. Consult the project files under `dlang/` before attempting to build or run individual components.

## Project status

This repository is presented as a programming-language technology project, not as a verified production-ready compiler distribution. The implementation and maturity of each DLang component should be assessed independently.

## Author

Developed by [NasirA1](https://github.com/NasirA1).
