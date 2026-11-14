// From a source file to a debug session: compile, record, replay - with the real toolchain.

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

const { assert, before, requireToolchain, suite, test, toolchainDirectory } = require("./harness");
const vscode = require("./fake-vscode");

const { DebugSession } = require("../src/debug/adapter");
const { Services } = require("../src/services");
const { prepareDebugSession } = require("../src/debug/launch");

const PROGRAM = [
    "func twice(n: int): int",
    "    return n * 2",
    "",
    "var answer = twice(21)",
    "println(answer)",
    "",
].join("\n");

suite("debugging: a real program, end to end", () =>
{
    let services = null;
    let directory = null;

    before(() =>
    {
        requireToolchain();
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-launch-"));
        vscode._setFolders([directory]);
        vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        services = new Services({ extensionPath: path.resolve(__dirname, "..") });
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { services, directory };
    });

    test("a program is compiled and recorded, and the session steps through it", async () =>
    {
        const file = path.join(directory, "twice.th");
        fs.writeFileSync(file, PROGRAM, "utf8");

        const messages = [];
        const session = new DebugSession((message) => messages.push(message), {
            prepare: (configuration) => prepareDebugSession(services, configuration),
        });
        let sequence = 1;
        const send = async (command, args) =>
        {
            const request = { seq: sequence++, type: "request", command, arguments: args || {} };
            await session.handleMessage(request);
            return messages.find((message) => message.type === "response" && message.request_seq === request.seq);
        };

        assert.ok((await send("initialize", {})).success);
        const launched = await send("launch", { program: file, stopOnEntry: true });
        assert.ok(launched.success, JSON.stringify(launched));
        assert.ok(messages.some((message) => message.event === "initialized"));

        // A breakpoint on the function's header line (1) moves down to line 2, which has code.
        const breakpoints = await send("setBreakpoints", {
            source: { path: file },
            breakpoints: [{ line: 1 }],
        });
        assert.deepStrictEqual(breakpoints.body.breakpoints.map((breakpoint) => breakpoint.line), [2]);
        assert.ok(breakpoints.body.breakpoints[0].verified);

        await send("configurationDone", {});
        const stopped = messages.filter((message) => message.event === "stopped");
        assert.strictEqual(stopped[0].body.reason, "entry");
        let stack = await send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].line, 4, "the first line with code");
        assert.strictEqual(path.resolve(stack.body.stackFrames[0].source.path), path.resolve(file));

        // Run on: the breakpoint inside twice, with the parameter it was called with.
        await send("continue", {});
        const atBreakpoint = messages.filter((message) => message.event === "stopped").pop();
        assert.strictEqual(atBreakpoint.body.reason, "breakpoint");
        stack = await send("stackTrace", { threadId: 1 });
        assert.deepStrictEqual(stack.body.stackFrames.map((frame) => frame.name), ["twice", "<main>"]);
        assert.strictEqual(stack.body.stackFrames[0].line, 2);
        const scopes = await send("scopes", { frameId: stack.body.stackFrames[0].id });
        const locals = await send("variables", { variablesReference: scopes.body.scopes[0].variablesReference });
        assert.deepStrictEqual(locals.body.variables.map((variable) => [variable.name, variable.value]),
            [["n", "21"]]);

        // Out of twice, and then backwards into it again.
        await send("stepOut", { threadId: 1 });
        stack = await send("stackTrace", { threadId: 1 });
        assert.deepStrictEqual(stack.body.stackFrames.map((frame) => frame.name), ["<main>"]);
        assert.strictEqual(stack.body.stackFrames[0].line, 4);

        await send("stepBack", { threadId: 1 });
        stack = await send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].name, "twice");
        assert.strictEqual(stack.body.stackFrames[0].line, 2);

        await send("continue", {});
        const printed = messages
            .filter((message) => message.event === "output" && message.body.category === "stdout")
            .map((message) => message.body.output)
            .join("");
        assert.strictEqual(printed, "42\n");
        assert.ok(messages.some((message) => message.event === "terminated"));
    });

    test("a Pashto program is debugged exactly like an English one", async () =>
    {
        // F5 on a Pashto file: the directive names the language, the compiler gets --lang ps
        // and the recording carries Pashto output. (When this fails on one machine only, the
        // usual cause is a build/release older than the Pashto vocabulary of the samples.)
        const file = path.join(directory, "pashto.th");
        fs.writeFileSync(file, "# ژبه: ps\nدنده دوه_چنده(ن: عدد): عدد\n    راګرځه ن * ۲\nکرښه چاپ(دوه_چنده(۲۱))\n", "utf8");

        const messages = [];
        const session = new DebugSession((message) => messages.push(message), {
            prepare: (configuration) => prepareDebugSession(services, configuration),
        });
        let sequence = 1;
        const send = async (command, args) =>
        {
            const request = { seq: sequence++, type: "request", command, arguments: args || {} };
            await session.handleMessage(request);
            return messages.find((message) => message.type === "response" && message.request_seq === request.seq);
        };

        assert.ok((await send("initialize", {})).success);
        const launched = await send("launch", { program: file, stopOnEntry: true });
        assert.ok(launched.success, JSON.stringify(launched));
        await send("configurationDone", {});
        assert.strictEqual(messages.filter((message) => message.event === "stopped")[0].body.reason, "entry");
        const stack = await send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].line, 4, "the first line with code");
        await send("continue", {});
        const printed = messages
            .filter((message) => message.event === "output" && message.body.category === "stdout")
            .map((message) => message.body.output)
            .join("");
        assert.strictEqual(printed, "۴۲\n", "the output is in Pashto digits");
        assert.ok(messages.some((message) => message.event === "terminated"));
    });

    test("a program that does not compile reports its errors instead of a recording", async () =>
    {
        const file = path.join(directory, "broken.th");
        fs.writeFileSync(file, "var n: int = \"text\"\n", "utf8");
        const prepared = await prepareDebugSession(services, { program: file });
        assert.strictEqual(prepared.trace, null);
        assert.ok(prepared.message.includes("1 error(s)"));
        assert.ok(prepared.diagnostics[0].includes("S005"));
    });

    test("the arguments of the launch configuration reach the program", async () =>
    {
        const file = path.join(directory, "arguments.th");
        fs.writeFileSync(file, "import os\nprintln(args())\n", "utf8");
        const prepared = await prepareDebugSession(services, { program: file, arguments: ["one", "two"] });
        assert.ok(prepared.trace, prepared.message);
        const output = (prepared.trace.output || []).map((record) => record.text).join("");
        assert.ok(output.includes("one") && output.includes("two"), output);
    });

    test("what the launch configuration offers as input is read by input()", async () =>
    {
        const file = path.join(directory, "input.th");
        fs.writeFileSync(file, "var name = input(\"Name? \")\nprintln(\"Hello, \", name)\n", "utf8");
        const prepared = await prepareDebugSession(services, { program: file, input: "Ada\n" });
        assert.ok(prepared.trace, prepared.message);
        const output = (prepared.trace.output || []).map((record) => record.text).join("");
        assert.ok(output.includes("Hello, Ada"), output);
    });

    test("a module's lines are part of the program's breakpoints", async () =>
    {
        fs.mkdirSync(path.join(directory, "lib"), { recursive: true });
        fs.writeFileSync(path.join(directory, "lib", "triple.th"),
            "func triple(n: int): int\n    return n * 3\n", "utf8");
        const file = path.join(directory, "uses_module.th");
        fs.writeFileSync(file, "import \"lib/triple\"\nprintln(triple(14))\n", "utf8");
        const prepared = await prepareDebugSession(services, { program: file });
        assert.ok(prepared.trace, prepared.message);
        assert.ok(prepared.modules.some((module) => module.name.includes("triple")));
        const indexes = [...prepared.executableLines.keys()];
        assert.ok(indexes.length >= 2, "the program and its module both have lines with code");
        const moduleIndex = indexes.find((index) => index !== 0);
        assert.ok(path.basename(prepared.sourcePathOfIndex(moduleIndex)), "triple.th");
    });

    test("compiled bytecode can be debugged directly", async () =>
    {
        const source = path.join(directory, "twice.th");
        const bytecode = path.join(directory, "twice.thc");
        const compiled = await services.toolchain().compile(source, { output: bytecode, cwd: directory });
        assert.ok(compiled.report.success);
        const prepared = await prepareDebugSession(services, { program: bytecode });
        assert.ok(prepared.trace, prepared.message);
        assert.strictEqual(prepared.trace.ok, true);
    });
});
