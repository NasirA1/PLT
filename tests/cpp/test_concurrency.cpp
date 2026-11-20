// Concurrency: the kernel (module task) and the SDK's concurrency.th.
//
// Everything here runs on one thread by design, so these tests are as repeatable as any other:
// the scheduler is part of the virtual machine, and its decisions are the same on every run.

#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"
#include "thunder/report.hpp"
#include "thunder/vm.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace thunder;

namespace
{
    // Compiles and runs 'text' with the SDK in reach (import "concurrency"), returning the
    // output; a runtime error is appended as "ERROR <code>: <message>".
    std::string run_sdk(const std::string& text, const LanguagePack& language = english_pack(),
        VmOptions vm_options = {})
    {
        const SourceFile source("program.th", text);
        CompileOptions options;
        options.sdk_module_path = (fs::path(THUNDER_SOURCE_DIR) / "lib" / "thunder").generic_string();
        const CompileResult result = compile(source, language, options);
        std::string messages;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            messages += format_short(diagnostic, source.name(), *result.language) + "\n";
        }
        if (!result.program)
        {
            return messages;
        }
        CHECK_EQ(messages, "");
        std::ostringstream output;
        Vm vm(*result.program, output, vm_options);
        const ExecResult exec = vm.run();
        std::string text_out = output.str();
        if (exec.error)
        {
            text_out += "ERROR " + exec.error->code + ": " + exec.error->message;
            if (!exec.error->task.empty())
            {
                text_out += " [in " + exec.error->task + "]";
            }
        }
        return text_out;
    }
}

// ---------------------------------------------------------------- the kernel

TEST_CASE("task: a program without tasks behaves exactly as before")
{
    CHECK_EQ(test::run_output("var n = 0\nfor i in range(0, 5)\n    n += i\nprintln(n)\n"), "10\n");
}

TEST_CASE("task: spawn and join run a second piece of code to its end")
{
    CHECK_EQ(test::run_output("var log: string[] = []\n"
                              "func work()\n"
                              "    push(log, \"worker\")\n"
                              "var t = task.spawn(work)\n"
                              "push(log, \"main\")\n"
                              "task.join(t)\n"
                              "println(string.join(log, \",\"))\n"),
        "main,worker\n");
}

TEST_CASE("task: the program ends only when every task has ended")
{
    // main returns first; the task started below still has work to do.
    CHECK_EQ(test::run_output("func later()\n"
                              "    task.sleep(1)\n"
                              "    println(\"the task finished last\")\n"
                              "var t = task.spawn(later)\n"
                              "println(\"main finished first\")\n"),
        "main finished first\nthe task finished last\n");
}

TEST_CASE("task: names, numbers and how many are left")
{
    CHECK_EQ(test::run_output("func quiet()\n"
                              "    task.sleep(5)\n"
                              "var t = task.spawn(quiet)\n"
                              "println(task.name(task.current()), \" \", task.name(t), \" \", task.count(),"
                              " \" \", task.done(t))\n"
                              "task.join(t)\n"
                              "println(task.done(t), \" \", task.count())\n"),
        "main task#2 2 false\ntrue 1\n");
}

TEST_CASE("task: yield gives the other tasks a turn at once")
{
    CHECK_EQ(test::run_output("var log = \"\"\n"
                              "func other()\n"
                              "    log += \"b\"\n"
                              "    task.yield()\n"
                              "    log += \"d\"\n"
                              "var t = task.spawn(other)\n"
                              "log += \"a\"\n"
                              "task.yield()\n"
                              "log += \"c\"\n"
                              "task.join(t)\n"
                              "println(log)\n"),
        "abcd\n");
}

TEST_CASE("task: a closure carries what a task needs")
{
    CHECK_EQ(test::run_output("var total = 0\n"
                              "func adder(n: int): func()\n"
                              "    var run = func() total += n\n"
                              "    return run\n"
                              "var ids: int[] = []\n"
                              "for n in range(1, 4)\n"
                              "    push(ids, task.spawn(adder(n)))\n"
                              "for id in ids\n"
                              "    task.join(id)\n"
                              "println(total)\n"),
        "6\n");
}

TEST_CASE("task: a lock makes the increment indivisible")
{
    // Without the lock the two tasks lose updates; with it they never do.
    const std::string with_lock = "var total = 0\n"
                                  "var m = task.mutex()\n"
                                  "func add()\n"
                                  "    for i in range(0, 500)\n"
                                  "        task.lock(m)\n"
                                  "        total += 1\n"
                                  "        task.unlock(m)\n"
                                  "var a = task.spawn(add)\n"
                                  "var b = task.spawn(add)\n"
                                  "task.join(a)\n"
                                  "task.join(b)\n"
                                  "println(total)\n";
    CHECK_EQ(test::run_output(with_lock), "1000\n");
}

TEST_CASE("task: try_lock never waits")
{
    CHECK_EQ(test::run_output("var m = task.mutex()\n"
                              "println(task.try_lock(m))\n"
                              "println(task.try_lock(m))\n"
                              "task.unlock(m)\n"
                              "println(task.try_lock(m))\n"),
        "true\nfalse\ntrue\n");
}

TEST_CASE("task: a condition releases the lock while it waits")
{
    CHECK_EQ(test::run_output("var ready = false\n"
                              "var m = task.mutex()\n"
                              "var c = task.condition()\n"
                              "func waiter()\n"
                              "    task.lock(m)\n"
                              "    while not ready\n"
                              "        task.wait(c, m)\n"
                              "    println(\"the waiter woke up\")\n"
                              "    task.unlock(m)\n"
                              "var t = task.spawn(waiter)\n"
                              "task.yield()\n"
                              "task.lock(m)\n"
                              "ready = true\n"
                              "task.signal(c)\n"
                              "task.unlock(m)\n"
                              "task.join(t)\n"
                              "println(\"done\")\n"),
        "the waiter woke up\ndone\n");
}

TEST_CASE("task: os.sleep lets the other tasks run")
{
    CHECK_EQ(test::run_output("import os\n"
                              "var log = \"\"\n"
                              "func quick()\n"
                              "    log += \"b\"\n"
                              "var t = task.spawn(quick)\n"
                              "log += \"a\"\n"
                              "os.sleep(5)\n"
                              "log += \"c\"\n"
                              "task.join(t)\n"
                              "println(log)\n"),
        "abc\n");
}

// ---------------------------------------------------------------- the errors

TEST_CASE("task: two tasks waiting for each other is a deadlock, not a hang")
{
    const test::RunResult result = test::run("var m = task.mutex()\n"
                                             "var n = task.mutex()\n"
                                             "func other()\n"
                                             "    task.lock(n)\n"
                                             "    task.yield()\n"
                                             "    task.lock(m)\n"
                                             "var t = task.spawn(other)\n"
                                             "task.lock(m)\n"
                                             "task.yield()\n"
                                             "task.lock(n)\n");
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R037");
    CHECK(result.error->message.find("main waits for the lock task#2 holds") != std::string::npos);
    CHECK(result.error->message.find("task#2 waits for the lock main holds") != std::string::npos);
}

TEST_CASE("task: joining a task that waits for this one is a deadlock")
{
    const test::RunResult result = test::run("var there = 0\n"
                                             "func other()\n"
                                             "    task.join(0)\n"
                                             "var t = task.spawn(other)\n"
                                             "task.join(t)\n");
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R037");
}

TEST_CASE("task: the wrong handle, the wrong lock and waiting for oneself are clean errors")
{
    CHECK_EQ(test::run("task.join(7)\n").error->code, "R039");
    CHECK_EQ(test::run("task.lock(3)\n").error->code, "R039");
    CHECK_EQ(test::run("var m = task.mutex()\ntask.unlock(m)\n").error->code, "R040");
    CHECK_EQ(test::run("var m = task.mutex()\ntask.lock(m)\ntask.lock(m)\n").error->code, "R041");
    CHECK_EQ(test::run("task.join(task.current())\n").error->code, "R042");
}

TEST_CASE("task: waiting inside a callback of map is refused, not undefined")
{
    const test::RunResult result = test::run("func quiet()\n"
                                             "    task.sleep(50)\n"
                                             "var other = task.spawn(quiet)\n"
                                             "func slow(n: int): int\n"
                                             "    task.join(other)\n"
                                             "    return n\n"
                                             "var ns = map([1, 2], slow)\n");
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R038");
}

TEST_CASE("task: a runtime error names the task it happened in")
{
    const test::RunResult result = test::run("func bad()\n"
                                             "    var a = [1]\n"
                                             "    println(a[5])\n"
                                             "var t = task.spawn(bad)\n"
                                             "task.join(t)\n");
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R003");
    CHECK_EQ(result.error->task, "task#2");
}

TEST_CASE("fail: a program can stop itself with its own message")
{
    const test::RunResult result = test::run("func check(n: int)\n"
                                             "    if n < 0\n"
                                             "        fail(\"n must not be negative\")\n"
                                             "check(-1)\n");
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R043");
    CHECK_EQ(result.error->message, "the program stopped itself: n must not be negative");
}

// ---------------------------------------------------------------- scheduling options

TEST_CASE("task: --slice 0 switches only where a task waits")
{
    VmOptions cooperative;
    cooperative.slice = 0;
    // Without preemption the counter is safe even without a lock: nothing interrupts the loop.
    const test::RunResult result = test::run("var total = 0\n"
                                             "func add()\n"
                                             "    for i in range(0, 2000)\n"
                                             "        total += 1\n"
                                             "var a = task.spawn(add)\n"
                                             "var b = task.spawn(add)\n"
                                             "task.join(a)\n"
                                             "task.join(b)\n"
                                             "println(total)\n",
        cooperative);
    CHECK_EQ(result.output, "4000\n");
}

TEST_CASE("task: the schedule is the same on every run")
{
    const std::string program = "var log = \"\"\n"
                                "func mark(c: string): func()\n"
                                "    var run = func()\n"
                                "        for i in range(0, 20)\n"
                                "            log += c\n"
                                "    return run\n"
                                "var a = task.spawn(mark(\"a\"))\n"
                                "var b = task.spawn(mark(\"b\"))\n"
                                "task.join(a)\n"
                                "task.join(b)\n"
                                "println(log)\n";
    const std::string once = test::run_output(program);
    CHECK_EQ(test::run_output(program), once);
    CHECK_EQ(test::run_output(program), once);
}

TEST_CASE("task: a seeded schedule varies the turns, and the same seed gives the same run")
{
    // A lock keeps the work right whatever the schedule, so only the *order* varies.
    const std::string program = "var log: string[] = []\n"
                                "var m = task.mutex()\n"
                                "func mark(c: string): func()\n"
                                "    var run = func()\n"
                                "        for i in range(0, 20)\n"
                                "            task.lock(m)\n"
                                "            push(log, c + str(i))\n"
                                "            task.unlock(m)\n"
                                "    return run\n"
                                "var a = task.spawn(mark(\"a\"))\n"
                                "var b = task.spawn(mark(\"b\"))\n"
                                "task.join(a)\n"
                                "task.join(b)\n"
                                "println(string.join(log, \"\"))\n";
    VmOptions seeded;
    seeded.schedule_seed = 7;
    const std::string once = test::run(program, seeded).output;
    CHECK_EQ(test::run(program, seeded).output, once); // the same seed, the same interleaving
    VmOptions other;
    other.schedule_seed = 1234;
    const std::string again = test::run(program, other).output;
    CHECK_EQ(again.size(), once.size()); // another seed: the same work, in another order
    CHECK_EQ(test::run(program, other).output, again);
    // Every run does all the work; the fixed schedule is one order among the possible ones.
    CHECK_EQ(test::run_output(program).size(), once.size());
}

TEST_CASE("task: broadcast wakes every waiter, signal only one")
{
    CHECK_EQ(test::run_output("var m = task.mutex()\n"
                              "var c = task.condition()\n"
                              "var go = false\n"
                              "var awake = 0\n"
                              "func waiter()\n"
                              "    task.lock(m)\n"
                              "    while not go\n"
                              "        task.wait(c, m)\n"
                              "    awake += 1\n"
                              "    task.unlock(m)\n"
                              "var one = task.spawn(waiter)\n"
                              "var two = task.spawn(waiter)\n"
                              "task.yield()\n"
                              "task.lock(m)\n"
                              "go = true\n"
                              "task.broadcast(c)\n"
                              "task.unlock(m)\n"
                              "task.join(one)\n"
                              "task.join(two)\n"
                              "println(awake)\n"),
        "2\n");
}

// ---------------------------------------------------------------- the SDK

TEST_CASE("concurrency: Task and Mutex keep a shared counter right")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var total = 0\n"
                     "var guard = Mutex()\n"
                     "func add()\n"
                     "    var left = 300\n"
                     "    while left > 0\n"
                     "        with guard.lock()\n"
                     "            total += 1\n"
                     "        left -= 1\n"
                     "var a = Task(add)\n"
                     "var b = Task(add)\n"
                     "a.join()\n"
                     "b.join()\n"
                     "println(total, \" \", a.is_done(), \" \", a.name())\n"),
        "600 true task#2\n");
}

TEST_CASE("concurrency: with gives the lock back on an early return")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var m = Mutex()\n"
                     "func pick(): int\n"
                     "    with m.lock()\n"
                     "        return 7\n"
                     "println(pick(), \" \", m.try_lock())\n"),
        "7 true\n");
}

TEST_CASE("concurrency: a channel carries values from one task to another")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var got: int[] = []\n"
                     "var ch: Channel<int> = Channel(2)\n"
                     "func produce()\n"
                     "    for i in range(1, 6)\n"
                     "        ch.send(i)\n"
                     "    ch.close()\n"
                     "func consume()\n"
                     "    while ch.has_next()\n"
                     "        push(got, ch.receive())\n"
                     "var p = Task(produce)\n"
                     "var c = Task(consume)\n"
                     "p.join()\n"
                     "c.join()\n"
                     "println(got, \" \", ch.is_closed(), \" \", ch.count())\n"),
        "[1, 2, 3, 4, 5] true 0\n");
}

TEST_CASE("concurrency: a channel of capacity 0 is a rendezvous")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var ch: Channel<string> = Channel(0)\n"
                     "var note = \"\"\n"
                     "func take()\n"
                     "    note = ch.receive()\n"
                     "var t = Task(take)\n"
                     "ch.send(\"hello\")\n"
                     "println(\"the sender went on: \", note)\n"
                     "t.join()\n"),
        "the sender went on: hello\n");
}

TEST_CASE("concurrency: receiving from a closed and empty channel stops the program")
{
    const std::string out = run_sdk("import \"concurrency\"\n"
                                    "var ch: Channel<int> = Channel(1)\n"
                                    "ch.close()\n"
                                    "println(ch.receive())\n");
    CHECK(out.find("ERROR R043") != std::string::npos);
}

TEST_CASE("concurrency: a barrier holds everyone until the last one arrives")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var log: string[] = []\n"
                     "var b = Barrier(3)\n"
                     "func phases(who: string): func()\n"
                     "    var run = func()\n"
                     "        for phase in range(1, 3)\n"
                     "            push(log, who + str(phase))\n"
                     "            b.wait()\n"
                     "    return run\n"
                     "var ts: Task[] = []\n"
                     "for name in [\"x\", \"y\", \"z\"]\n"
                     "    push(ts, Task(phases(name)))\n"
                     "for t in ts\n"
                     "    t.join()\n"
                     "println(string.join(log, \" \"))\n"),
        "x1 y1 z1 x2 z2 y2\n");
}

TEST_CASE("concurrency: a wait group, a latch and a semaphore")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var finished: int[] = []\n"
                     "var group = WaitGroup()\n"
                     "func piece(which: int): func()\n"
                     "    var run = func()\n"
                     "        push(finished, which)\n"
                     "        group.done()\n"
                     "    return run\n"
                     "group.add(4)\n"
                     "var ts: Task[] = []\n"
                     "for n in range(1, 5)\n"
                     "    push(ts, Task(piece(n)))\n"
                     "group.wait()\n"
                     "println(sort(finished), \" \", group.count())\n"
                     "var gate = Latch(1)\n"
                     "var opened = false\n"
                     "var watcher = func()\n"
                     "    gate.wait()\n"
                     "    opened = true\n"
                     "var w = Task(watcher)\n"
                     "gate.count_down()\n"
                     "w.join()\n"
                     "println(opened, \" \", gate.is_open())\n"
                     "var s = Semaphore(2)\n"
                     "s.acquire()\n"
                     "s.acquire()\n"
                     "println(s.available(), \" \", s.try_acquire())\n"
                     "s.release()\n"
                     "println(s.available())\n"),
        "[1, 2, 3, 4] 0\ntrue true\n0 false\n1\n");
}

TEST_CASE("concurrency: a read/write lock lets readers in together")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var rw = ReadWriteLock()\n"
                     "rw.read_lock()\n"
                     "rw.read_lock()\n"
                     "println(rw.readers())\n"
                     "rw.read_unlock()\n"
                     "rw.read_unlock()\n"
                     "rw.write_lock()\n"
                     "println(rw.readers())\n"
                     "rw.write_unlock()\n"),
        "2\n0\n");
}

TEST_CASE("concurrency: timers run later, and can be cancelled")
{
    CHECK_EQ(run_sdk("import \"concurrency\"\n"
                     "var ticks = 0\n"
                     "var three = Latch(3)\n"
                     "var counting = func()\n"
                     "    ticks += 1\n"
                     "    three.count_down()\n"
                     "var every = timer.every(1, counting)\n"
                     "three.wait()\n"
                     "every.cancel()\n"
                     "println(ticks >= 3)\n"
                     "var ran = false\n"
                     "var never = func()\n"
                     "    ran = true\n"
                     "var once = timer.after(5, never)\n"
                     "once.cancel()\n"
                     "once.join()\n"
                     "every.join()\n"
                     "println(ran, \" \", once.is_cancelled())\n"),
        "true\nfalse true\n");
}

TEST_CASE("concurrency: the Pashto module is the same module")
{
    CHECK_EQ(run_sdk("# ژبه: ps\n"
                     "راوړه \"concurrency\"\n"
                     "متغیر ټولټال = ۰\n"
                     "متغیر ق = قلف()\n"
                     "دنده ورزیاته()\n"
                     "    متغیر پاتې = ۱۰۰\n"
                     "    ترڅو پاتې > ۰\n"
                     "        ق.قلف_کړه() سره\n"
                     "            ټولټال += ۱\n"
                     "        پاتې -= ۱\n"
                     "متغیر الف = کار(ورزیاته)\n"
                     "متغیر ب = کار(ورزیاته)\n"
                     "الف.راته_ودرېږه()\n"
                     "ب.راته_ودرېږه()\n"
                     "کرښه چاپ(ټولټال، \" \"، الف.نوم())\n",
             pashto_pack()),
        "۲۰۰ کار#۲\n");
}
