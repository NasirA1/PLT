// Stepping through a recorded execution. The first suite uses a trace written by hand, so the
// rules are visible; the second records a real program with thundervm.

"use strict";

const fs = require("fs");
const path = require("path");

const { assert, before, requireToolchain, suite, temporaryDirectory, test } = require("./harness");
const { Recording } = require("../src/debug/trace");
const { Toolchain, workDirectory } = require("../src/toolchain");

/**
 * A trace of:
 *   line 1  <main>    two instructions
 *   line 2  <main>    calls f
 *   line 5  f         two instructions
 *   line 6  f         returns
 *   line 3  <main>    one instruction
 */
function handMade()
{
    const frame = (depth) => Array.from({ length: depth }, (ignored, index) => ({
        function: index === 0 ? "<main>" : "f",
        base: index,
        locals: 1,
    }));
    const steps = [
        { step: 0, function: "<main>", line: 1, column: 1, instruction: "CONST 0", frames: frame(1), locals: [], globals: [] },
        { step: 1, function: "<main>", line: 1, column: 5, instruction: "STORE_GLOBAL 0", frames: frame(1), locals: [], globals: [{ name: "x", value: "1" }] },
        { step: 2, function: "<main>", line: 2, column: 1, instruction: "CALL 0", frames: frame(1), locals: [], globals: [{ name: "x", value: "1" }] },
        { step: 3, function: "f", line: 5, column: 5, instruction: "LOAD_LOCAL 0", frames: frame(2), locals: [{ name: "n", value: "1" }], globals: [] },
        { step: 4, function: "f", line: 5, column: 9, instruction: "ADD", frames: frame(2), locals: [{ name: "n", value: "1" }], globals: [] },
        { step: 5, function: "f", line: 6, column: 5, instruction: "RETURN", frames: frame(2), locals: [{ name: "n", value: "1" }], globals: [] },
        { step: 6, function: "<main>", line: 3, column: 1, instruction: "POP", frames: frame(1), locals: [], globals: [{ name: "x", value: "2" }] },
    ];
    return {
        tool: "thundervm",
        ok: true,
        exit_code: 0,
        truncated: false,
        recorded_steps: steps.length,
        error: null,
        output: [{ step: 4, text: "hello\n" }, { step: 6, text: "done\n" }],
        steps,
    };
}

suite("trace: stepping", () =>
{
    let recording = null;

    before(() =>
    {
        recording = new Recording(handMade(), { program: path.join(path.sep, "work", "main.th") });
    });

    test("a stop is the first instruction of a line", () =>
    {
        assert.ok(recording.isLineStart(0));
        assert.ok(!recording.isLineStart(1), "the second instruction of line 1 is not a stop");
        assert.ok(recording.isLineStart(2));
        assert.ok(recording.isLineStart(3), "entering a function is a stop");
        assert.strictEqual(recording.firstStop(1), 2);
    });

    test("step into goes to the next line, wherever it is", () =>
    {
        assert.strictEqual(recording.stepIn(0), 2);
        assert.strictEqual(recording.stepIn(2), 3, "into f");
        assert.strictEqual(recording.stepIn(3), 5);
        assert.strictEqual(recording.stepIn(6), -1, "nothing follows the last step");
    });

    test("step over skips the call", () =>
    {
        assert.strictEqual(recording.next(2), 6, "over the call to f, back in <main>");
        assert.strictEqual(recording.next(3), 5, "inside f it steps line by line");
    });

    test("step out leaves the function", () =>
    {
        assert.strictEqual(recording.stepOut(3), 6);
        assert.strictEqual(recording.stepOut(0), -1, "<main> has nothing to return to");
    });

    test("stepping backwards mirrors stepping forwards", () =>
    {
        assert.strictEqual(recording.stepBack(6), 5);
        assert.strictEqual(recording.stepBack(3), 2);
        assert.strictEqual(recording.stepBack(0), -1);
        assert.strictEqual(recording.previous(6), 2, "back over the call");
    });

    test("continue stops at a breakpoint, once per line", () =>
    {
        const breakpoints = new Map([[path.join(path.sep, "work", "main.th"), new Set([5])]]);
        assert.strictEqual(recording.continueFrom(0, breakpoints), 3);
        assert.strictEqual(recording.continueFrom(3, breakpoints), -1, "line 5 does not stop twice");
        assert.strictEqual(recording.reverseContinueFrom(6, breakpoints), 3);
        assert.strictEqual(recording.continueFrom(0, new Map()), -1, "no breakpoints, no stop");
    });

    test("a breakpoint in another file does not stop this one", () =>
    {
        const breakpoints = new Map([[path.join(path.sep, "work", "other.th"), new Set([5])]]);
        assert.strictEqual(recording.continueFrom(0, breakpoints), -1);
    });

    test("the output is attributed to the step that produced it", () =>
    {
        assert.strictEqual(recording.outputUpTo(3), "");
        assert.strictEqual(recording.outputUpTo(4), "hello\n");
        assert.strictEqual(recording.outputBetween(4, 6), "done\n");
        assert.strictEqual(recording.outputBetween(-1, 6), "hello\ndone\n");
    });

    test("the call stack is innermost first, with the locals of the running frame", () =>
    {
        const stack = recording.stackAt(3);
        assert.deepStrictEqual(stack.map((frame) => frame.name), ["f", "<main>"]);
        assert.ok(stack[0].top);
        assert.deepStrictEqual(stack[0].locals, [{ name: "n", value: "1" }]);
        assert.strictEqual(stack[1].locals, null, "the virtual machine records only the running frame's locals");
        assert.strictEqual(stack[0].line, 5);
    });
});

suite("trace: a real recording", () =>
{
    let toolchain = null;
    let directory = null;

    before(() =>
    {
        toolchain = new Toolchain(requireToolchain());
        directory = temporaryDirectory("trace");
    });

    test("a recursive program can be stepped in both directions", async () =>
    {
        const source = path.join(directory, "fact.th");
        fs.writeFileSync(source, [
            "func fact(n: int): int",
            "    if n < 2",
            "        return 1",
            "    return n * fact(n - 1)",
            "",
            "var answer = fact(4)",
            "println(answer)",
            "",
        ].join("\n"), "utf8");
        const bytecode = path.join(directory, "fact.thc");
        const compiled = await toolchain.compile(source, { output: bytecode, cwd: directory });
        assert.ok(compiled.report.success, JSON.stringify(compiled.report.diagnostics));

        const tracePath = path.join(workDirectory("test-trace"), "fact.trace.json");
        const result = await toolchain.record(bytecode, { traceJson: tracePath, traceLimit: 10000, cwd: directory });
        assert.strictEqual(result.code, 0);
        const recording = new Recording(JSON.parse(fs.readFileSync(tracePath, "utf8")), { program: source });

        assert.ok(recording.length > 20);
        assert.ok(recording.ok);
        assert.strictEqual(recording.exitCode, 0);
        assert.ok(!recording.truncated);
        assert.strictEqual(recording.outputUpTo(recording.length), "24\n");

        // Walk forwards to the deepest frame, then back again to the start.
        let position = recording.firstStop(0);
        let deepest = position;
        while (position >= 0)
        {
            if (recording.depthAt(position) > recording.depthAt(deepest))
            {
                deepest = position;
            }
            const next = recording.stepIn(position);
            if (next < 0)
            {
                break;
            }
            position = next;
        }
        assert.ok(recording.depthAt(deepest) >= 4, "fact(4) nests four calls");
        const stack = recording.stackAt(deepest);
        assert.strictEqual(stack[0].name, "fact");
        assert.strictEqual(stack[stack.length - 1].name, "<main>");
        assert.ok(stack[0].locals.some((local) => local.name === "n"));

        let backwards = deepest;
        let steps = 0;
        while (backwards > 0 && steps < 10000)
        {
            backwards = recording.stepBack(backwards);
            ++steps;
            if (backwards <= 0)
            {
                break;
            }
        }
        assert.ok(steps > 1, "stepping back reaches the beginning");
    });

    test("a program that fails stops at the instruction that failed", async () =>
    {
        const source = path.join(directory, "boom.th");
        fs.writeFileSync(source, ["var numbers = [1, 2, 3]", "println(numbers[7])", ""].join("\n"), "utf8");
        const bytecode = path.join(directory, "boom.thc");
        await toolchain.compile(source, { output: bytecode, cwd: directory });
        const tracePath = path.join(workDirectory("test-trace"), "boom.trace.json");
        await toolchain.record(bytecode, { traceJson: tracePath, traceLimit: 10000, cwd: directory });
        const recording = new Recording(JSON.parse(fs.readFileSync(tracePath, "utf8")), { program: source });
        assert.ok(!recording.ok);
        assert.ok(recording.error, "the recording carries the runtime error");
        assert.strictEqual(recording.errorStep, recording.length - 1);
        assert.strictEqual(recording.positionAt(recording.errorStep).line, 2);
    });

    test("a program with a module reports the module's own file", async () =>
    {
        fs.mkdirSync(path.join(directory, "lib"), { recursive: true });
        fs.writeFileSync(path.join(directory, "lib", "double.th"),
            ["func double(n: int): int", "    return n * 2", ""].join("\n"), "utf8");
        const source = path.join(directory, "uses.th");
        fs.writeFileSync(source, ["import \"lib/double\"", "println(double(21))", ""].join("\n"), "utf8");
        const bytecode = path.join(directory, "uses.thc");
        const compiled = await toolchain.compile(source, { output: bytecode, cwd: directory });
        assert.ok(compiled.report.success, JSON.stringify(compiled.report.diagnostics));
        const tracePath = path.join(workDirectory("test-trace"), "uses.trace.json");
        await toolchain.record(bytecode, { traceJson: tracePath, traceLimit: 10000, cwd: directory });
        const modules = (compiled.report.modules || []).map((module) => ({ name: module.name, path: module.path }));
        const recording = new Recording(JSON.parse(fs.readFileSync(tracePath, "utf8")), { program: source, modules });
        const files = new Set();
        for (let i = 0; i < recording.length; ++i)
        {
            files.add(path.basename(recording.positionAt(i).file));
        }
        assert.ok(files.has("uses.th"));
        assert.ok(files.has("double.th"), "stepping into the module shows the module's file");
    });
});

suite("trace: a breakpoint fires once per execution of its line", () =>
{
    test("returning from a call does not re-trigger the line the call was on", () =>
    {
        // var answer = twice(21) is one line whose instructions are interrupted by the whole
        // of twice: lines 4, 4, 4, 4 (the call), 2, 2, 2, 2 (inside twice), 4 (the store), 5.
        const frames = (depth) => Array.from({ length: depth }, (ignored, index) => ({
            function: index === 0 ? "<main>" : "twice", base: index, locals: 1,
        }));
        const lines = [[4, 1], [4, 1], [4, 1], [4, 1], [2, 2], [2, 2], [2, 2], [2, 2], [4, 1], [5, 1]];
        const trace = {
            steps: lines.map(([line, depth], step) => ({
                step, function: depth === 1 ? "<main>" : "twice", line, column: 1,
                instruction: "", frames: frames(depth), locals: [], globals: [],
            })),
            output: [],
            ok: true,
            exit_code: 0,
        };
        const recording = new Recording(trace, { program: path.join(path.sep, "work", "main.th") });
        const starts = trace.steps.map((ignored, index) => recording.isLineStart(index));
        assert.deepStrictEqual(starts,
            [true, false, false, false, true, false, false, false, false, true]);

        const breakpoints = new Map([[path.join(path.sep, "work", "main.th"), new Set([4])]]);
        assert.strictEqual(recording.continueFrom(-1, breakpoints), 0, "line 4 stops once...");
        assert.strictEqual(recording.continueFrom(0, breakpoints), -1, "...and not again on the way back");
    });

    test("a second call to the same function stops again", () =>
    {
        const frames = (depth) => Array.from({ length: depth }, (ignored, index) => ({
            function: index === 0 ? "<main>" : "f", base: index, locals: 0,
        }));
        const lines = [[1, 1], [3, 2], [1, 1], [3, 2], [2, 1]];
        const trace = {
            steps: lines.map(([line, depth], step) => ({
                step, function: depth === 1 ? "<main>" : "f", line, column: 1,
                instruction: "", frames: frames(depth), locals: [], globals: [],
            })),
            output: [],
            ok: true,
        };
        const recording = new Recording(trace, { program: path.join(path.sep, "work", "main.th") });
        const breakpoints = new Map([[path.join(path.sep, "work", "main.th"), new Set([3])]]);
        assert.strictEqual(recording.continueFrom(-1, breakpoints), 1);
        assert.strictEqual(recording.continueFrom(1, breakpoints), 3, "the second call stops too");
    });
});
