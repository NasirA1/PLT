# The concurrency reference

A Thunder program can do several things at once. A **task** is a piece of the program that
runs beside the rest: a server that waits for a connection while the screen keeps being drawn,
a timer that fires while the program reads a line, five philosophers eating at one table.

Concurrency in Thunder comes in two layers, and both are meant to be read:

* the **kernel**, module `task`, which the virtual machine itself provides because it has to:
  starting and joining tasks, sleeping, yielding, and the two primitives that must be
  indivisible - the **lock** and the **condition**. The module is always there, like `math`;
  no import is needed;
* the **library**, `import "concurrency"`, which wraps the kernel in objects and builds
  everything else out of it - `Task`, `Mutex`, `Condition`, `Semaphore`, `Barrier`,
  `Channel<T>`, `WaitGroup`, `Latch`, `ReadWriteLock` and `timer` - in ordinary Thunder, in
  [`lib/thunder/en/concurrency.th`](../lib/thunder/en/concurrency.th) (Pashto:
  [`lib/thunder/ps/concurrency.th`](../lib/thunder/ps/concurrency.th)). Open it: a barrier is
  fifteen lines, and they are the fifteen lines a textbook writes.

```thunder
import "concurrency"

var total = 0
var guard = Mutex()

func add_up()
    for n in range(1, 101)
        with guard.lock()
            total += n

var one = Task(add_up)
var two = Task(add_up)
one.join()
two.join()
println(total)          # 10100, every time
```

**Try it:** sample [40_concurrency](../samples/en/40_concurrency.th) walks a student through
tasks, the lost update, the lock, channels and sleeping; it is in the IDE's sample list. One
advanced sample then takes each piece further - they are not in the picker, so open them from
here: [tasks](../samples/advanced/en/03_tasks.th), [mutex](../samples/advanced/en/04_mutex.th),
[channels](../samples/advanced/en/05_channels.th), [waiting](../samples/advanced/en/06_waiting.th),
[the dining philosophers](../samples/advanced/en/07_philosophers.th) and
[timers](../samples/advanced/en/08_timers.th).

## 1. How tasks run

Tasks are **green threads**: the virtual machine runs them itself, all on the one thread the
program already uses. A task is nothing but its own operand stack, its own call frames and the
instruction it stopped at, so starting one is cheap and switching between two is three moves.

* **The program starts as one task, called `main`.** `Task(action)` starts another; the new
  task is ready at once but does not run until the running one gives up its turn.
* **The program ends when every task has ended** - not when `main` does. A task still running
  keeps the program alive; `join()` it, or cancel it, if you want the program to stop.
* **A turn lasts 100 instructions** (`thundervm --slice n`), after which the running task is
  set aside and the next ready one runs. A task also gives up its turn whenever it waits
  (`join`, `sleep`, a lock, a condition, a channel, `input()`) or calls `task.yield()`.
  `--slice 0` switches *only* where a task waits or yields: purely cooperative scheduling.
* **Waiting is fair**: tasks waiting for a lock, a condition or a channel are woken in the
  order they began to wait.
* **Nothing really runs at the same time**, so the program is no faster for having tasks -
  but two pieces of code that each *wait* can be written as two straight-line pieces of code,
  which is what concurrency is for. (Using several processor cores is a different question,
  and a later one.)

### The schedule is the same on every run

Because the virtual machine decides every switch, a concurrent Thunder program prints the same
thing every time it runs. That is unusual and deliberate: a race can be shown to a class,
stepped through in the debugger, fixed, and tested against an expected output.

`thundervm --schedule-seed n` varies the length of each turn instead - still reproducibly, so
a program can be run under several seeds to shake out races a fixed schedule happens to hide.

### Races are real

A switch happens *between two instructions*, and `total += 1` is three of them: load, add,
store. Two tasks doing it without a lock lose updates, exactly as they would with real
threads - see samples 41 and advanced 04. The rule is the usual one: **anything two tasks both write, or one
writes while another reads, belongs inside a lock.**

What tasks share is the globals, and any variable their closures capture:

```thunder
var seen = 0                      # a global: every task sees the same one

func watcher(): func()
    var mine = 0                  # a local of this call, captured by the closure below
    var run = func()
        mine += 1                 # this task's own copy
        seen += 1                 # shared with every other task
    return run
```

## 2. The library: `import "concurrency"`

### Task

| Member | What it does |
| --- | --- |
| `Task(action: func())` | Starts the action as a task of its own. |
| `t.join()` | Waits until it has ended; the other tasks run meanwhile. |
| `t.is_done(): bool` | True once it has ended. |
| `t.name(): string` | `"task#2"`, `"task#3"`, ... (`"main"` is the first task). |
| `t.handle(): int` | The number the kernel knows it by. |

The action takes no arguments, so a task that needs something of its own gets it from a
closure. Note that a captured variable is one variable, shared: to give each task its own,
make it in a function of its own.

```thunder
func greeter(who: string): func()
    var run = func() println("hello, ", who)
    return run

var tasks: Task[] = []
for name in ["ann", "bob"]
    push(tasks, Task(greeter(name)))
for t in tasks
    t.join()
```

### Mutex and Lock - mutual exclusion

| Member | What it does |
| --- | --- |
| `Mutex()` | A new lock, free. |
| `m.lock(): Lock` | Takes it, waiting if another task holds it; answers the guard. |
| `m.try_lock(): bool` | Takes it only if it is free; never waits. |
| `m.unlock()` | Gives it back by hand (only the holder may). |
| `m.handle(): int` | The number the kernel knows it by. |
| `guard.close()` | Gives the lock back (what `with` calls). |

`with m.lock()` is the way to write it: the lock is given back at the end of the block however
the block ends, so a `return` or a `break` inside a critical section can no longer leave it
taken.

```thunder
with m.lock()
    balance -= amount
```

Taking a lock a task already holds is an error (`R041`) - it could only wait for itself.
Giving back a lock a task does not hold is an error too (`R040`).

### Condition - waiting inside a lock

A condition is something a task waits *for* while holding a lock: "the queue is not empty",
"everybody is ready". `wait()` gives the lock back while it waits and takes it again before
going on, so the state cannot change underneath it. **Always wait in a loop** - another task
may have been woken first and changed the state back.

| Member | What it does |
| --- | --- |
| `Condition(m: Mutex)` | A condition belonging to that lock. |
| `c.wait()` | Releases the lock, waits for a signal, takes the lock back. |
| `c.signal()` | Wakes the task that has waited longest. |
| `c.broadcast()` | Wakes every waiting task. |

```thunder
var m = Mutex()
var not_empty = Condition(m)
var queue: string[] = []

func take(): string
    with m.lock()
        while len(queue) == 0
            not_empty.wait()
        return remove_at(queue, 0)

func give(item: string)
    with m.lock()
        push(queue, item)
        not_empty.signal()
```

### Semaphore, Barrier, WaitGroup, Latch, ReadWriteLock

| Struct | Members |
| --- | --- |
| `Semaphore(permits)` | `acquire()`, `release()`, `try_acquire(): bool`, `available(): int` |
| `Barrier(parties)` | `wait()`, `parties(): int` |
| `WaitGroup()` | `add(n)`, `done()`, `wait()`, `count(): int` |
| `Latch(count)` | `count_down()`, `wait()`, `count(): int`, `is_open(): bool` |
| `ReadWriteLock()` | `read_lock()`, `read_unlock()`, `write_lock()`, `write_unlock()`, `readers(): int` |

A **semaphore** is a counted permit: at most *n* tasks inside at once (with one permit it is a
lock, but any task may give that permit back). A **barrier** is a meeting point that may be
used again - nobody passes until every party has arrived. A **wait group** waits for a group
of tasks without naming them. A **latch** is a gate that opens once, when its count reaches
zero, and stays open. A **read/write lock** lets any number of readers hold it together, but a
writer hold it alone.

### Channel - communication

A channel is a queue with the waiting built in, so a producer and a consumer can each be an
ordinary loop.

| Member | What it does |
| --- | --- |
| `Channel(capacity)` | A channel with room for that many values (`0`: a rendezvous). |
| `ch.send(value)` | Sends, waiting while the channel is full. |
| `ch.receive(): T` | Takes the next value, waiting while the channel is empty. |
| `ch.has_next(): bool` | Waits until a value is there *for this task*, or the channel is closed and empty. |
| `ch.close()` | Nothing more will be sent; the waiting receivers are woken. |
| `ch.is_closed(): bool`, `ch.count(): int`, `ch.capacity(): int` | What it is doing. |

```thunder
var jobs: Channel<string> = Channel(4)
...
while jobs.has_next()
    work_on(jobs.receive())
```

`has_next()` keeps the value it saw for the task that asked, so two consumers in such a loop
never fight over the last one. A capacity of `0` is a **rendezvous**: the sender waits until a
receiver has taken the value from its hand. Receiving from a channel that is closed and empty,
or sending into a closed one, stops the program (`R043`).

### timer - later, and again and again

| Member | What it does |
| --- | --- |
| `timer.after(ms, action): Timer` | Runs the action once, after that many milliseconds. |
| `timer.every(ms, action): Timer` | Runs it again and again, until it is cancelled. |
| `t.cancel()` | It will not run (again). |
| `t.is_cancelled(): bool`, `t.join()` | What it is doing; wait for its task to end. |

A timer *is* a task that sleeps, so the rest of the program keeps running while it waits -
and while every task is only waiting for time, the virtual machine really sleeps: the program
uses no processor at all until the next one is due. A repeating timer that is never cancelled
keeps the program alive for ever.

## 3. The kernel: module `task`

The library above is written in these. They work with plain whole numbers - a task, a lock and
a condition each have a number, as a socket has a handle - so the type checker cannot tell one
from another: that is what the objects are for. Use them directly to see what is underneath.

| Function | What it does |
| --- | --- |
| `task.spawn(action: func()): int` | Starts a task; answers its number. |
| `task.join(task: int)` | Waits until that task has ended. |
| `task.done(task: int): bool` | True once it has. |
| `task.sleep(ms: int)` | This task waits; the others run. |
| `task.yield()` | Give the others a turn now. |
| `task.current(): int` | The running task's number. |
| `task.name(task: int): string` | `"main"`, `"task#2"`, ... |
| `task.count(): int` | How many tasks have not ended. |
| `task.mutex(): int` | A new lock. |
| `task.lock(mutex: int)` / `task.unlock(mutex: int)` | Take it (waiting) / give it back. |
| `task.try_lock(mutex: int): bool` | Take it only if it is free. |
| `task.condition(): int` | A new condition. |
| `task.wait(condition: int, mutex: int)` | Release, wait, take the lock back. |
| `task.signal(condition: int)` / `task.broadcast(condition: int)` | Wake one / all. |

## 4. Waiting for the world outside

| Operation | Under the scheduler |
| --- | --- |
| `os.sleep(ms)`, `task.sleep(ms)` | Only this task waits. When every task is waiting for time, the machine itself sleeps until the earliest is due - no busy waiting. |
| `graphics.frame()` | The task waits for the next frame; the other tasks run meanwhile. |
| `input()` | The task waits. The line is read once nothing else can run, so a program can prompt in one task and go on working in another. A task waiting for input delays a timer that is due. |
| `end_of_input()` | Answers at once; it does not give up the task's turn. |
| Sockets, `os.run`, `file.*` | **Block every task** while they run. Give a socket a timeout of 0 and poll it if that matters (see [the sockets reference](sockets.md)); doing this properly is the next piece of work. |
| Native C++ functions | Block every task, like any other C++ call. |

## 5. What cannot be done (yet)

* **A task cannot wait inside a callback.** The callback of `map`, `filter` or `reduce`, and a
  callback a native function makes, run nested on the C++ stack, which cannot be set aside.
  Joining, locking, or waiting for a condition or a channel in one is a clean error (`R038`).
  Sleeping is allowed there, but it stops the whole program for that time. Outside callbacks
  there is no such restriction - a task may wait anywhere, however deep in its own functions.
* **The debugger does not list the tasks yet.** It stops in whichever task was running and
  shows that task's call stack and variables, and a step belongs to the task it was asked in
  (see the [debugging reference](debugging.md)) - but VS Code still sees one thread, so another
  task cannot be inspected from there while the program is stopped.
* **Several processor cores** are not used: every task is on one thread.

## 6. When nothing can go on: deadlock

If every remaining task waits for something only another waiting task could do, nothing will
ever happen again. The virtual machine notices at once and stops the program with `R037`,
naming what each task waits for:

```
error R037: deadlock: every task is waiting and none of them can go on (main waits for
the lock task#2 holds; task#2 waits for main to end)
```

That is the whole point of running the schedule inside the virtual machine: a deadlock is a
message with a line number, not a program that hangs.

## 7. Errors

| Code | When |
| --- | --- |
| `R037` | Deadlock: every task waits and none can go on. |
| `R038` | A task tried to wait inside a callback (`map`, `filter`, `reduce`, a native call). |
| `R039` | A number that is not a task, a lock or a condition of this program. |
| `R040` | `task.unlock` / `task.wait` on a lock this task does not hold. |
| `R041` | Taking a lock this task already holds. |
| `R042` | A task tried to wait for itself to end. |
| `R043` | `fail("...")`: the program stopped itself (the channel uses it to say what went wrong). |

A runtime error in any task stops the whole program, and the report names the task it happened
in.

## 8. `fail` - stopping the program on purpose

`fail(message)` stops the program with a message of your own, the way a wrong built-in call
stops it: with the source line and the call stack. A library uses it to say what the program
did wrong - the channel does, above - and a program can use it for a check of its own.

```thunder
func withdraw(amount: int)
    if amount > balance
        fail("cannot take " + str(amount) + " from " + str(balance))
    balance -= amount
```

## 9. The command line

| Option | What it does |
| --- | --- |
| `thundervm --slice n` | A turn is n instructions (default 100); `--slice 0` is cooperative. |
| `thundervm --schedule-seed n` | Vary the turns, reproducibly, to shake out races. |

## 10. The words in Pashto

| English | Pashto |
| --- | --- |
| module `task` | `کارونه` |
| `spawn` / `join` / `done` | `پیل کړه` / `راته ودرېږه` / `پای ته رسېدلی` |
| `sleep` / `yield` / `current` / `name` / `count` | `بیده شه` / `لار ورکړه` / `اوسنی` / `نوم` / `شمېر` |
| `mutex` / `lock` / `try_lock` / `unlock` | `نوی قلف` / `قلف کړه` / `هڅه وکړه` / `خلاص کړه` |
| `condition` / `wait` / `signal` / `broadcast` | `نوی حالت` / `انتظار وباسه` / `خبر` / `ټولو ته خبر` |
| `fail` | `ناکام شه` |
| `Task` / `Mutex` / `Lock` / `Condition` | `کار` / `قلف` / `ساتونکی` / `حالت` |
| `Semaphore` / `Barrier` / `Channel` | `سیمافور` / `خنډ` / `کانال` |
| `acquire` / `release` / `try_acquire` | `اجازه_واخله` / `بېرته_ورکړه` / `د_اجازې_هڅه` |
| `wait` (every kind) | `انتظار_وباسه` |
| `WaitGroup` / `Latch` / `ReadWriteLock` | `ډله` / `دروازه` / `لوست_لیک_قلف` |
| `timer` / `after` / `every` / `cancel` | `مهالګر` / `وروسته` / `هر_ځل` / `لغوه_کړه` |

The Pashto reference is [docs/ps/concurrency.md](ps/concurrency.md).
