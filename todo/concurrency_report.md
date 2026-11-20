# Concurrency in Thunder: where it stands, and how to add it

*Exploration report, 2026-11-17. Nothing in the toolchain was changed for it. The two programs
quoted in Part 1 were compiled and run against the current toolchain (0.7.0).*

> **Implemented in 0.10.0** (devlog §67, 2026-11-20): recommendation **C**, green threads
> scheduled by the VM, with all five "Decisions for you" answered as recommended. Closures and
> objects arrived in between, so `spawn_with` was dropped (a closure carries the state) and the
> library is objects rather than modules over handles. See
> [docs/concurrency.md](../docs/concurrency.md); the kernel is the module `task`
> (`src/core/vm_tasks.cpp`) and the library is `lib/thunder/<language>/concurrency.th`.
> Still open, as the report's later phases: sockets and `os.run` without blocking the other
> tasks, DAP threads and a task timeline in the IDEs, and share-nothing `worker`s on real OS
> threads.

## Summary

1. **Thunder has no concurrency today: not threads, not tasks, not timers, not an event loop.**
   A program is one thread of instructions, and every waiting operation (`input()`, `os.sleep`,
   `os.run`, a socket read without a timeout) stops the whole program. You *can* write
   non-blocking programs, though, by polling. The key and mouse functions never block, sockets
   accept a timeout of 0 (non-blocking accept and receive), and `os.clock_ms()` gives a clock.
   With function values, a setTimeout/setInterval emulation is about 40 lines of Thunder (shown
   below). What is missing is any way to wait for "whichever happens first" without spinning,
   and any way to run two pieces of code that each wait.
2. **Recommendation: green threads ("tasks") run by the VM itself, with a deterministic
   scheduler**, plus a small kernel of synchronisation primitives in the VM (mutex, condition)
   and the rest written in Thunder in the SDK: semaphore, barrier, channel, wait group, latch,
   read-write lock. This gives every mechanism the brief asks for, behind a small API. It cannot
   cause undefined behaviour, because it stays on one OS thread. Races and deadlocks become
   *reproducible*, which no OS-thread design offers and which is worth a great deal in a
   teaching language. The VM's design makes it cheap, too: a task is just its own operand stack
   and call frames. setTimeout/setInterval-style timers then become a few lines of library code
   on top. Real multi-core parallelism can come later as a separate, share-nothing `worker`
   module.

---

## Part 1 - What the language offers today

### 1.1 The execution model

* **One thread of execution.** The VM (`include/thunder/vm.hpp`) has one operand stack
  (`stack_`, which also holds every frame's local variables) and one call-frame vector
  (`frames_`). `Vm::execute` is a loop that decodes and runs one instruction at a time, until
  the program ends.
* **Callbacks run nested.** A function value passed to `map`, `filter` or `reduce`, or called
  back by a native C++ function, runs through `Vm::invoke`. That starts a *nested* `execute` on
  the C++ stack, which runs until the callback returns.
* **The only OS threads are host-side.** A background reader takes key and mouse event lines
  (and input lines) off standard input when a canvas is shown (`graphics::EventReader`). The live
  debugger reads its commands on a thread so that "pause" can arrive mid-run (`StreamChannel`).
  Neither runs Thunder code.
* **The VM runs in its own process** (`thundervm`), so the IDEs stay responsive and can stop a
  runaway program. That is isolation, not concurrency within the program.
* **No closures.** A function value can only name a top-level (or module-level) function.
  Nothing is captured, so any state a callback needs lives in globals, module variables, struct
  fields or arguments.

### 1.2 Inventory: what waits, and what does not

| Operation | Blocks the program? | What it offers |
| --- | --- | --- |
| `input()` | Yes, until a line arrives | - |
| `end_of_input()` | Yes, until it is known whether more input will come | - |
| `os.sleep(ms)` | Yes | Throttling a loop |
| `os.run`, `os.run_output` | Yes, until the command ends (output is captured) | - |
| `file.*` | Yes (normally short) | - |
| `socket.receive*`, `tcp.accept`, `udp.receive*` | By default yes, for ever | `socket.set_timeout(s, ms)`: at most `ms`; **0 = do not wait** (`select()` with a zero timeout: "" / `nothing` at once) |
| `tcp.connect` | Yes, until connected or refused | - |
| `graphics.frame()` | Yes, until the next frame time (paced only when a display watches) | A steady "tick", the natural loop of an animation or game |
| `graphics.key_pressed()`, `key_down`, `mouse_*` | **No**: they read a queue that a background thread fills | Polling input |
| `os.clock_ms()`, `os.time()` | No | A clock for timers |
| Native C++ functions | Block while they run | A library may start its own threads, but the native interface defines no way for such a thread to call Thunder or to wake the program, so this is outside the contract |

### 1.3 Is there a setTimeout / setInterval?

**No.** There is no scheduler, no registry of pending callbacks and no event loop that would call
them, and no closures to carry their state. A blocking call cannot be "left running" while
something else happens.

### 1.4 What can be done anyway (non-blocking by hand)

**a) The polling loop.** One loop that checks each source in turn and never waits. This program
runs a 50 ms timer, a non-blocking `accept` and a non-blocking `receive` together, and it works:

```thunder
import os
import "sockets"

var server = tcp.listen("127.0.0.1", 0)
socket.set_timeout(server, 0)                 # accept() returns at once: nothing if nobody came
var client = tcp.connect("127.0.0.1", socket.port(server))
var connection: socket.Socket = nothing
var next_tick = os.clock_ms() + 50
var ticks = 0
var got = ""
while got == "" or ticks < 3
    if os.clock_ms() >= next_tick             # job 1: a timer
        ticks += 1
        next_tick += 50
        if ticks == 2
            socket.send_line(client, "hello")
    if connection == nothing                  # job 2: accept a client, without waiting
        connection = tcp.accept(server)
        if connection != nothing
            socket.set_timeout(connection, 0)
    else if got == ""                         # job 3: read a line, without waiting
        got = socket.receive_line(connection)
println("ticks ", ticks, ", received \"", got, "\"")
```

The cost is a **busy wait**. With the release build this loop went round about 25,000 times in
roughly 150 ms of waiting, using a whole CPU core to do nothing. An `os.sleep(1)` in the loop
throttles it, at the price of up to 1 ms of latency per event. There is no `select`-like "wait
until any of these is ready, or until the next timer", which is what an OS-level event loop
uses to sleep efficiently.

**b) The frame loop as an event loop.** A graphics program's `while true ... graphics.frame()`
loop is a cooperative scheduler in disguise. Each frame, every "actor" (the snake, the
particles of the fireworks, the cells of Life) gets a step, and input is polled. Samples 30-36
are built this way. It only works for code that can be cut into short steps by hand (state
machines).

**c) setTimeout / setInterval, emulated in Thunder.** Function values (to top-level functions)
are enough to build a timer table and a dispatch loop. Unlike (a), this one sleeps until the next
timer is due instead of spinning:

```thunder
import os

module timer
    struct Timer
        due: int          # os.clock_ms() at which it fires next
        period: int       # 0: once (setTimeout); otherwise every period ms (setInterval)
        action: func()
        cancelled: bool

    var _timers: Timer[] = []

    func after(ms: int, action: func()): Timer
        var t = Timer(os.clock_ms() + ms, 0, action, false)
        push(_timers, t)
        return t

    func every(ms: int, action: func()): Timer
        var t = Timer(os.clock_ms() + ms, ms, action, false)
        push(_timers, t)
        return t

    func cancel(t: Timer)
        t.cancelled = true

    # The event loop: runs until no timer is left, sleeping until the next one is due.
    func run_loop()
        while len(_timers) > 0
            var now = os.clock_ms()
            var next = now + 1000
            var keep: Timer[] = []
            for t in _timers
                if not t.cancelled and t.due <= now
                    t.action()
                    if t.period > 0
                        t.due += t.period
                if not t.cancelled and (t.period > 0 or t.due > now)
                    push(keep, t)
                    next = math.min(next, t.due)
            _timers = keep
            if len(_timers) > 0 and next > os.clock_ms()
                os.sleep(next - os.clock_ms())

var ticks = 0
var ticker = timer.every(30, tick)

func tick()
    ticks += 1
    println("tick ", ticks)
    if ticks == 3
        timer.cancel(ticker)

func hello()
    println("hello, after 50 ms (ticks so far: ", ticks, ")")

timer.after(50, hello)
timer.run_loop()          # tick 1 / hello ... / tick 2 / tick 3
```

This is exactly JavaScript's model: callbacks run one at a time, to completion, from one loop.
It has the same limits. A callback that blocks (`input()`, a socket read) freezes every timer.
The callbacks' state lives in globals, because there are no closures. Timer callbacks cannot
also be combined with socket waits without going back to polling (a), because nothing can wait
for both at once.

**d) Not really options.** `os.run("command &")` backgrounds a process on POSIX shells but not
on `cmd.exe`, and there is no way to talk to the process afterwards. A native C++ library could
start threads, but nothing lets them hand results back safely.

### 1.5 Verdict

Thunder today supports **cooperative, hand-written multiplexing**: polling, frame loops, and a
timer loop of your own. It has no real concurrency. Two jobs that each need to *wait* - a
server and a keyboard prompt, two network conversations, a producer and a consumer - cannot be
written as two straight-line pieces of code. Every synchronisation concept a course would want
to show (mutual exclusion, condition waiting, barriers, deadlock) has nothing to attach to.

---

## Part 2 - Options for proper concurrency

### 2.1 What the design must satisfy

From the request:
* a simple, easy API;
* every mechanism concurrent programming needs: running things concurrently, waiting for them,
  mutual exclusion, conditional waiting, barrier synchronisation, and communication.

From the project's principles (devlog §7 and since):
* **no undefined behaviour**, ever: every error is a clean runtime error with a source line;
* **deterministic, golden-testable programs** (the seeded random numbers exist for this reason);
* **visible under the bonnet**: the debugger, the trace, the IDE's VM view;
* **both languages**, and the existing features (`with`, modules, generics, the native
  interface) should fit;
* the **teaching value** of what students see.

### 2.2 Constraints in the current implementation

1. **Values are not thread-safe.** Arrays, maps and structs are shared, mutable
   `std::shared_ptr` objects with no locking (`value.hpp`). Two OS threads touching one array
   would be a data race - undefined behaviour in C++, which the sanitizer build would rightly
   flag. Any design that runs Thunder code on several OS threads must either lock every object
   or never share one.
2. **Callbacks nest on the C++ stack** (`Vm::invoke`). A task that waits *inside* a `map`
   callback or a native callback cannot be switched out without its own C++ stack.
3. **No closures.** Starting concurrent work means naming a function and passing it arguments,
   not writing `spawn { ... }` around local state.
4. **No exceptions.** A runtime error ends the program. That is a simple, consistent rule for
   concurrent code too.
5. **On the plus side**, the interpreter loop already counts every instruction (the step limit),
   and a task's whole state is just its operand stack, frames and instruction offset. Switching
   tasks is swapping three members.

### 2.3 The options

| | Model | Concurrency | Parallel (several cores) | Shared memory + mutex/condition/barrier | Undefined-behaviour risk | Deterministic tests | Size of the change |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **A** | Event loop + timers (JavaScript) | Callbacks only | No | Nothing to synchronise | None | Yes | Small |
| **B** | `async`/`await` coroutines (C#, JS, Python) | Yes, at `await` points | No | Partly (locks are rarely needed) | None | Yes | Large: new keywords, two kinds of functions, a compiler transformation |
| **C** | **Green threads (tasks) scheduled by the VM** | **Yes** | No | **Yes, all of it** | **None** (one OS thread) | **Yes** (seeded scheduler) | **Medium** |
| **D** | OS threads + a global interpreter lock (CPython) | Yes | No (the lock serialises) | Yes | Low, but the lock is a new failure point | No | Medium-large |
| **E** | Isolated workers + message passing (Web Workers, Erlang) | Yes | **Yes** | No shared memory; channels only | None (nothing shared; messages are deep copies) | Largely | Medium (one VM per worker) |
| **F** | OS threads on a shared heap with per-object locks (Java) | Yes | Yes | Yes | High; the whole VM must become thread-safe | No | Very large |

**A** is attractive for its simplicity, and Part 1 shows that it can already be built as a
library. But it does not *have* the mechanisms the brief lists: with callbacks that always run
to completion, a mutex, a condition wait or a barrier has no meaning. Without closures it is
also clumsier than in JavaScript.

**B** gives tidy code, but at a high price for a teaching language. Every function becomes
either `async` or not ("function colouring"), the compiler gains a state-machine
transformation, and the classic primitives fit badly. It hides *where* the interleaving
happens, which is the thing a course wants to show.

**C** is the classic "threads" model of every operating-systems and concurrency course, run
inside the VM:
* Each task has its own stack and frames; the globals and the heap are shared.
* The scheduler switches tasks at instruction boundaries: when a task waits (join, lock,
  condition, sleep, channel, I/O), and also every *n* instructions (preemption).
* Because everything stays on one OS thread, nothing in C++ can race, so there is no undefined
  behaviour. Races still happen *at the Thunder level*: `count += 1` is a load, an add and a
  store, and a switch between them loses an update. That is the real phenomenon, and here it
  can be made *reproducible*: a seeded scheduler interleaves the same way every run, so a race
  can be shown, stepped through in the debugger, fixed with a mutex, and golden-tested.
* Deadlock detection comes almost for free. When every task is waiting and nothing can wake
  one, the VM stops with a runtime error listing what each task waits for.
* The cost is that it uses one core.

**D** has the semantics of C without its determinism. **F** is the Java model: large,
nondeterministic, and it puts the "no undefined behaviour" guarantee at risk throughout the VM.
Neither suits this project.

**E** is the right answer to a different question - *speed* on several cores. It is safe
because nothing is shared: a message is a deep copy (`copy()` exists). But it teaches
message passing rather than synchronisation.

### 2.4 Recommendation

**C as the foundation**, with **A as library sugar on top** (timers become tasks that sleep).
**E** stays a possible later addition, as a separate `worker` module for parallel computation.

### 2.5 The proposed design

#### A small kernel in the VM, the rest in Thunder

Mutual exclusion needs atomicity that only the VM can guarantee: no switch between "is it
locked?" and "take it". So the VM provides a minimal kernel. Everything else is ordinary Thunder
in the SDK, built from the kernel the way textbooks build it. Students can read the barrier's
code, as they can read the stack's today.

| Where | What |
| --- | --- |
| **VM** - module `task` (always available, like `math`) | `spawn`, `join`, `sleep`, `yield`, `current`, and the scheduler; `mutex` and `condition` (the monitor primitives); deadlock detection |
| **SDK** - `lib/thunder/<lang>/concurrency.th` (`import "concurrency"`) | `semaphore`, `barrier`, `channel<T>`, `wait_group`, `latch`, `rwlock` - each a module with a struct, written with mutex + condition and documented line by line |

#### The API (English; Pashto names below)

```thunder
import "concurrency"

# Tasks
var t = task.spawn(worker)                 # worker: func(); starts now, runs concurrently
var u = task.spawn_with(fill, items)       # spawn_with<T>(f: func(T), argument: T): no closures, so one argument
task.join(t)                               # waits until t has finished
task.sleep(100)                            # this task waits 100 ms; the others run
task.yield()                               # let the others run now
println(task.name(task.current()))         # "main", "worker#2", ...

# Mutual exclusion - "with" unlocks at the end of the block, however the block ends
var m = sync.mutex()
with guard = sync.lock(m)
    count += 1

# Conditional waiting (a monitor)
var not_empty = sync.condition()
with guard = sync.lock(m)
    while len(queue) == 0
        sync.wait(not_empty, guard)        # releases the lock, waits, takes it back
    var item = remove_at(queue, 0)
sync.signal(not_empty)                     # wakes one waiter; sync.broadcast wakes all

# Built in Thunder on the kernel (import "concurrency")
var b = barrier.new(3)                     # three tasks meet here
barrier.wait(b)                            # nobody passes until all three have arrived
var c: channel.Channel<int> = channel.new(10)   # buffered; channel.new(0): a rendezvous
channel.send(c, 42)                        # waits while the channel is full
var x = channel.receive(c)                 # waits while it is empty
channel.close(c)                           # receivers see the end (channel.is_closed)
var s = semaphore.new(2)                   # semaphore.acquire / semaphore.release
var g = wait_group.new()                   # wait_group.add / done / wait
```

The barrier, as it would appear in the SDK - a textbook construction, in about 15 lines:

```thunder
module barrier
    struct Barrier
        parties: int
        waiting: int
        round: int
        lock: sync.Mutex
        all_here: sync.Condition

    func new(parties: int): Barrier
        return Barrier(parties, 0, 0, sync.mutex(), sync.condition())

    # Waits until 'parties' tasks have called wait; then all of them go on, and the barrier
    # can be used again (the round number tells this round's waiters from the next one's).
    func wait(b: Barrier)
        with guard = sync.lock(b.lock)
            var my_round = b.round
            b.waiting += 1
            if b.waiting == b.parties
                b.waiting = 0
                b.round += 1
                sync.broadcast(b.all_here)
            else
                while my_round == b.round
                    sync.wait(b.all_here, guard)
```

How it fits what already exists:
* **`with`** is the natural RAII for locks. `sync.lock(m)` returns a guard struct whose module's
  `close` unlocks. It is exactly the resource pattern of the last iteration: an early `return`
  or `break` inside a critical section can no longer leave the mutex locked.
* **Handles** follow the sockets pattern. The VM works with plain int ids; the SDK wraps them in
  structs (`sync.Mutex`, `channel.Channel<T>`), so the type checker keeps a mutex from being
  passed where a condition is wanted.
* **Generics** give `channel.Channel<T>` with no new machinery.
* **Timers** (the setTimeout/setInterval the question mentions) become sugar:
  `timer.after(ms, f)` spawns a task that sleeps and then calls `f`; `timer.every(ms, f)` loops.
  `timer.cancel(t)` stops it.

#### Scheduling semantics

* **Run queue**: first in, first out. The program begins as the task `main`. **The program ends
  when every task has ended**, which is simpler to explain than Go's "main returns, the rest
  dies". Tasks that are still waiting at that point mean a deadlock (below).
* **Preemption by instruction count**: a switch every *n* instructions (default around 100;
  `thundervm --slice n`, and `--slice 0` for purely cooperative scheduling). Switches happen only
  at instruction boundaries, so every instruction is atomic and the memory model is simply
  sequential consistency.
* **Determinism**: by default the schedule is fixed (round robin), so a program prints the same
  thing every run. `--schedule-seed n` varies slice lengths and choices *reproducibly*, to shake
  out races. A test can run under several seeds.
* **Deadlock**: no task can run, none is sleeping, none is waiting for I/O. This ends the
  program with a new runtime error, e.g. "deadlock: main waits for worker#2 (join); worker#2
  waits for mutex m (held by main)", in both languages.
* **Errors**: a runtime error in any task stops the program, and the report names the task.
* **Fairness**: waiters on a mutex, condition or channel are woken in arrival order.

#### Waiting operations that exist today

| Operation | Under the scheduler |
| --- | --- |
| `os.sleep(ms)` | The same as `task.sleep`: only this task waits. When every task sleeps, the VM really sleeps until the earliest wake-up, so there is no busy wait. |
| `input()`, `end_of_input()` | The task waits while the existing background reader (`EventReader`, generalised beyond canvas mode) collects the line; the other tasks run. |
| `graphics.frame()` | The task waits for the next frame time; background tasks run meanwhile. |
| Sockets | Phase 2. Either an extension of the native interface (a native function says "wait until this descriptor is readable" and the scheduler `select()`s over every waiting task), or the native function runs on a helper thread while its task waits. Until then, a blocking socket call blocks every task, which is documented. |
| `os.run` | Runs on a helper thread; only the task waits. |

#### The one real limitation: callbacks

A task cannot be switched out while it runs a callback of `map`, `filter` or `reduce`, or a
native callback, because those run nested on the C++ stack. The first version should:
* not preempt inside such a callback (it runs to its end);
* report waiting inside one (join, lock, sleep, channel) as a runtime error with a clear
  message.

The lasting fix is to compile `map`, `filter` and `reduce` as ordinary bytecode loops instead of
built-ins that call back. They would then behave like any other code, and the restriction would
remain only for native callbacks.

#### Tooling - where the teaching value is

* **Debugger**: the Debug Adapter Protocol already has threads. Each task becomes one, with its
  own call stack in VS Code's Call Stack view. Stopping stops all tasks, and stepping steps the
  selected task. The adapter currently reports a single hard-coded thread
  (`ide/vscode/src/debug/adapter.js`).
* **Trace and Qt IDE**: each step of `--trace-json` gains the task id. The VM tab gains a task
  list (state, what each task waits for, the mutex owners). A **timeline** - one row per task,
  coloured by running, ready or waiting - makes interleaving, starvation and deadlock visible.
  This would be the feature no mainstream tool offers students.
* **Samples**, in both languages, each golden-tested under a fixed seed:
  1. a lost update without a mutex, then fixed with one;
  2. producer/consumer over a channel;
  3. a bounded buffer with conditions;
  4. a barrier with three phases;
  5. the dining philosophers, deadlocking, then fixed;
  6. timers (setTimeout/setInterval).

#### Pashto names (drafts for a native speaker, like all the vocabulary)

`task` کار, `spawn` پیل کړه, `join` انتظار یې وکړه, `sleep` ویده شه, `yield` لار ورکړه, `current`
اوسنی; `sync` همغږي, `mutex` قلف, `lock` قلف کړه, `condition` حالت, `wait` انتظار وباسه, `signal`
خبر ورکړه, `broadcast` ټولو ته خبر ورکړه; `barrier` خنډ, `semaphore` سیمافور, `channel` کانال,
`send`/`receive` ولېږه/واخله.

Example, with the new word order of `with`:

```thunder
ساتونکی سره = همغږي.قلف کړه(ق)
    شمېر += ۱
```

#### Phases

1. **The core** (one medium iteration): tasks in the VM (stack and frame swap, run queue,
   preemption, seed); `spawn`/`join`/`sleep`/`yield`/`current`; mutex and condition with
   deadlock detection; `os.sleep` and `input()` integrated; the SDK's semaphore, barrier,
   channel, wait group and latch in both languages; docs, samples 1-5, C++ and golden tests.
   No new opcodes are needed (everything is a built-in), so the bytecode format stays
   compatible apart from the new built-ins.
2. **I/O and timers**: sockets and `os.run` without blocking the other tasks; the `timer`
   module; `graphics.frame()` as a wait point.
3. **Tooling**: DAP threads, the task list and the timeline in the Qt IDE, tasks in the trace.
4. **Optional**: share-nothing `worker`s on real OS threads for multi-core computation;
   **closures**, which would make `task.spawn` and timers far nicer (`task.spawn(func() ...)`)
   and are worth considering on their own.

### 2.6 Decisions for you

1. **Model**: green threads (C), as recommended - or event loop only (A, small, no
   synchronisation), or workers (E, parallel, no shared memory)?
2. **Preemptive or cooperative by default?** Preemption shows real races; cooperative is easier
   to reason about. The recommendation is preemptive with a fixed default schedule, and
   `--slice 0` for cooperative.
3. **What ends the program**: when all tasks end (recommended) or when `main` ends (Go)?
4. **Which primitives live in the VM**: only mutex + condition (recommended, the rest readable
   Thunder), or semaphore and channel natively too, for speed?
5. **Closures**: add them first, which makes every concurrent API nicer, or live with
   `spawn_with(f, argument)` for now?
