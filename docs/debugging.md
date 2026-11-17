# Thunder Debugging Reference

How a program is debugged while it runs, and the protocol a debugger front end speaks to the
virtual machine. The VS Code extension (`ide/vscode`) is one such front end; the Qt IDE's
step-by-step debugger works from a recording instead (`thundervm --trace-json`, described in the
[bytecode and VM reference](bytecode.md)).

## 1. Two ways to look inside a running program

| | `thundervm --debug` (live) | `thundervm --trace-json` (recorded) |
| --- | --- | --- |
| The program | runs under a debugger, stopping where it is told | runs to its end, every instruction is written down |
| Breakpoints | stop the program as it reaches them; conditions and hit counts | applied afterwards, to the recording |
| Stepping | into, over and out of calls, and **back** through the lines kept in the history | forwards and backwards over the whole recording |
| Variables | every frame's locals, the globals, elements and fields one level at a time | the running frame's locals and the globals, as text |
| `input()` | asks the front end, which asks the user | taken from text supplied in advance |
| Graphics | a window through a display plug-in (`--display`) | the final picture only |
| Used by | the VS Code extension | the Qt IDE |

The rest of this document is about the live debugger.

## 2. Starting a session

```
thundervm --debug [--history <n>] [--display <name>] [--native-path <dir>] program.thc [arguments...]
```

Standard input and standard output carry the conversation: one JSON object per line in each
direction. The program's own output does **not** go to standard output - it arrives as `output`
events - and `input()` does not read standard input - it sends an `input` event and waits for an
`input` command. A graphics program's frames travel the same way: each one is a `frame` event
carrying the record of `thundervm --canvas`, and the canvas's keys and mouse come back as
`canvasEvent` commands - unless `--display` gives the program a window of its own, which then
shows the frames itself. (The literal `--canvas` flag is still refused together with `--debug`:
both would write records to standard output.) Standard error is untouched.

`--history <n>` is how many executed lines the debugger remembers for stepping back (default
10000; `0` keeps none). Each entry holds the call stack and every variable as text, so the limit
bounds memory rather than time.

The session begins with a `ready` event, then the debugger **stops before the first instruction**
and reports `stopped` with the reason `entry`. This is where a front end places its breakpoints;
a front end that does not want to show this stop simply sends `continue`. A breakpoint placed on
the first line during this stop fires when the program is continued.

When the program ends - normally, by `os.exit()`, by a runtime error or because the front end asked
it to - the debugger sends `exited` and the process finishes. A runtime error is reported as a
stop first (reason `exception`), so the state the program failed in can be inspected; any
stepping command then lets the program end.

## 3. Messages

Every message is a JSON object on a line of its own.

**Requests** (front end → debugger) carry a `seq` of the front end's choosing and a `command`:

```json
{"seq": 7, "command": "next"}
```

**Responses** repeat the request's number in `request_seq`:

```json
{"type": "response", "seq": 12, "request_seq": 7, "command": "next", "success": true, "body": {}}
{"type": "response", "seq": 13, "request_seq": 8, "command": "evaluate", "success": false, "message": "'n * 2' is not a variable here ..."}
```

**Events** (debugger → front end) are unsolicited:

```json
{"type": "event", "seq": 14, "event": "stopped", "reason": "breakpoint", "step": 36, "historic": false,
 "history": {"index": 9, "size": 10}, "function": "factorial", "file": 0, "line": 12, "column": 12}
```

A line that is not a JSON object is answered with an `error` event and otherwise ignored; an
unknown command gets an unsuccessful response. Requests that only ask about the state
(`stackTrace`, `variables`, `evaluate`, `setBreakpoints`) are answered whether the program is
stopped, running (between instructions) or waiting for input; the stepping commands are refused
while the program runs.

### Events

| Event | Fields | When |
| --- | --- | --- |
| `ready` | `program` (the source name), `files` (the source files, in `file` index order) | once, at the start |
| `stopped` | `reason`: `entry`, `breakpoint`, `step`, `pause` or `exception`; `description` and `text` (an exception's message and full report, or why a breakpoint fired unexpectedly); `step`; `historic`; `history` `{index, size}`; the innermost frame's `function`, `file`, `line`, `column` | the program stopped |
| `continued` | | the program runs on |
| `output` | `text` | the program wrote something |
| `input` | | `input()` was called: answer with an `input` command |
| `frame` | `record`: one frame of the graphics canvas, exactly as `thundervm --canvas` writes it (`{type: "frame", n, width, height, keep, shapes}`; see docs/graphics.md) | the program showed a frame |
| `exited` | `ok`, `exit_code`, `aborted`, `steps`, `error` (the runtime error as in `--result-json`, or `null`) | the end |
| `error` | `message` | a line that was not a request |

### Commands

| Command | Arguments | Response body |
| --- | --- | --- |
| `setBreakpoints` | `file` (index), `breakpoints`: `[{line, condition?, hitCondition?}]` - replaces every breakpoint of that file | `breakpoints`: `[{line, verified, message?}]` in the same order; a breakpoint on a line without code moves to the next line that has some, or is unverified |
| `continue` | | runs to the next breakpoint |
| `next` | | runs to the next line in this function or in a caller |
| `stepIn` | | runs to the next line, wherever it is |
| `stepOut` | | runs until the current function has returned |
| `stepBack` | | shows the previous line from the history |
| `reverseContinue` | | shows the previous line in the history that has a breakpoint, or the earliest line remembered |
| `pause` | | stops a running program at its next instruction |
| `stackTrace` | | `frames`: `[{index, function, file, line, column}]` innermost first; `historic`; `step` |
| `variables` | `frame` (index, default 0), `scope`: `locals`, `globals`, `stack` (the operands of the innermost frame) or `execution` (the step, the instruction, the position); `path` (optional: a variable, field or element whose contents are wanted, e.g. `items[1]`) | `variables`: `[{name, value, type, expandable}]`; `message` when nothing could be listed |
| `evaluate` | `expression`, `frame` | `value`, `type`, `expandable` |
| `input` | `text`, or `end: true` for the end of the input | |
| `canvasEvent` | `text`: a key or mouse line as on the `--canvas` input stream, without the record separator (`key down left`, `key up a`, `mouse 20 30 1`) | |
| `terminate`, `disconnect` | | ends the program; `exited` follows |

**File indices.** Positions name their file by index into the program's source files: `0` is
the program itself, then the files it imports (`import "sockets"`, `import "lib/geometry"`) in
the order the compiler loaded them - the same numbering as `thunderc --json` reports and the
`ready` event lists.

**Function names.** A frame's `function` (in `stopped` and in `stackTrace`) is the name the
compiler gave it: `<main>` for the top-level statements, the function's own name for a
function declared at the top level, and for a member of a [module](language.md) its
**qualified name** - `geometry.area`, `socket.send_line` - exactly as the bytecode listing, the
call stack of a runtime error and `name_of` show it. A module's variables are globals with
qualified names too: the `globals` scope lists them as `geometry.made`. (`evaluate` does not
find them under that name: it reads `geometry.made` as the field `made` of a variable
`geometry`. Look them up in the `globals` scope instead.)

**Lines.** A stop is always reported at the first instruction of a line *in its own frame*, so a
breakpoint on `var answer = twice(21)` fires once, not again when `twice` returns to the rest of
the line. `stepOut` is the exception: it stops right after the call, in the middle of the
caller's line, as other debuggers do.

**Conditions.** A breakpoint's `condition` is a comparison between a variable (or a field or
element of one) and a value or another variable: `n == 2`, `person.born > 1800`,
`items[0] != "x"`, `done` (a `bool` on its own). A condition that cannot be evaluated does not
hide the stop; the `stopped` event says so in `description`. `hitCondition` counts how often the
line was reached: `3` or `== 3` (the third time), `>= 3`, `> 3`, `< 3`, `<= 3`, `% 3` (every
third time).

**Expressions.** `evaluate` understands the same language as conditions: a variable, `a.b`,
`a[2]`, `a["key"]`, `a[1].b[0]`, and one comparison. It does not call functions or do
arithmetic. A variable is looked up in the chosen frame first, then among the globals; an inner
block's variable shadows an outer one of the same name.

### The history

Each line the program executes is remembered (up to `--history`) with its call stack and its
variables as text. `stepBack` and `reverseContinue` move through this history; the `stopped`
event then says `historic: true`, `stackTrace`, `variables` and `evaluate` answer from the
remembered state, and values cannot be expanded (`expandable` is `false`). Stepping forwards from
a remembered line moves through the history until it reaches the live state and then runs the
program on; `continue` in the history stops at remembered lines that have a breakpoint now
(conditions are not re-evaluated). Output is sent once, when it happens - never again while the
history is shown.

## 4. A session, line by line

The front end's lines are marked `>`, the debugger's `<`.

```
< {"type":"event","seq":1,"event":"ready","program":"main.th","files":["main.th"]}
< {"type":"event","seq":2,"event":"stopped","reason":"entry","step":0,"historic":false,"history":{"index":0,"size":1},"function":"<main>","file":0,"line":5,"column":1}
> {"seq":1,"command":"setBreakpoints","file":0,"breakpoints":[{"line":7}]}
< {"type":"response","seq":3,"request_seq":1,"command":"setBreakpoints","success":true,"body":{"breakpoints":[{"line":7,"verified":true}]}}
> {"seq":2,"command":"continue"}
< {"type":"response","seq":4,"request_seq":2,"command":"continue","success":true,"body":{}}
< {"type":"event","seq":5,"event":"continued"}
< {"type":"event","seq":6,"event":"output","text":"42\n"}
< {"type":"event","seq":7,"event":"stopped","reason":"breakpoint","step":31,"historic":false,"history":{"index":4,"size":5},"function":"<main>","file":0,"line":7,"column":1}
> {"seq":3,"command":"variables","scope":"globals"}
< {"type":"response","seq":8,"request_seq":3,"command":"variables","success":true,"body":{"variables":[{"name":"answer","value":"42","type":"int","expandable":false}]}}
> {"seq":4,"command":"continue"}
< {"type":"response","seq":9,"request_seq":4,"command":"continue","success":true,"body":{}}
< {"type":"event","seq":10,"event":"continued"}
< {"type":"event","seq":11,"event":"output","text":"84\n"}
< {"type":"event","seq":12,"event":"exited","ok":true,"exit_code":0,"aborted":false,"steps":40,"error":null}
```

## 5. Inside the toolchain

The debugger is `thunder::Debugger` (`include/thunder/debugger.hpp`), a `VmObserver`: the virtual
machine calls it before every instruction, and when the program should stop there it reads
commands from its `DebugChannel` until one of them resumes the program. `StreamChannel` is the
channel over standard input and output, reading on a background thread so that `pause` can arrive
while the program runs; the tests drive the debugger through a scripted channel instead
(`tests/cpp/test_debugger.cpp`). A front end in another language only needs a process, two
pipes and a JSON library.
