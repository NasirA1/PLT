// From a source file to a debug session: compile, start `thundervm --debug`, step - with the
// real toolchain. The machine answers in its own time, so the test waits for its events.

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

const { assert, before, requireToolchain, suite, test, toolchainDirectory, waitFor, removeOnExit } = require("./harness");
const vscode = require("./fake-vscode");

const { DebugSession } = require("../src/debug/adapter");
const { Services } = require("../src/services");
const { filesOf, prepareDebugSession } = require("../src/debug/launch");

const PROGRAM = [
    "func twice(n: int): int",
    "    return n * 2",
    "",
    "var answer = twice(21)",
    "println(answer)",
    "var name = input(\"Name? \")",
    "println(\"Hello, \", name)",
    "",
].join("\n");

/** A client of the adapter that waits for the machine's events. */
class LiveClient
{
    constructor(services)
    {
        this.messages = [];
        this.sequence = 1;
        this.session = new DebugSession((message) => this.messages.push(message), {
            prepare: (configuration) => prepareDebugSession(services, configuration),
            log: () => {},
        });
    }

    async send(command, args)
    {
        const request = { seq: this.sequence++, type: "request", command, arguments: args || {} };
        await this.session.handleMessage(request);
        return this.messages.find((message) => message.type === "response" && message.request_seq === request.seq);
    }

    events(name)
    {
        return this.messages.filter((message) => message.type === "event" && message.event === name);
    }

    /** Waits for one more event of a kind than there was, and returns it. */
    async nextEvent(name, after)
    {
        await waitFor(() => this.events(name).length > after, `a ${name} event`, 20000);
        return this.events(name).pop();
    }

    printed()
    {
        return this.events("output")
            .filter((event) => event.body.category === "stdout")
            .map((event) => event.body.output)
            .join("");
    }
}

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
        removeOnExit(directory);
        return { services, directory };
    });

    test("a program is compiled and started, and the session steps through it", async () =>
    {
        const file = path.join(directory, "twice.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        const client = new LiveClient(services);

        assert.ok((await client.send("initialize", {})).success);
        const launched = await client.send("launch", { program: file, stopOnEntry: true });
        assert.ok(launched.success, JSON.stringify(launched));
        assert.strictEqual(client.events("initialized").length, 1);

        // A breakpoint on the function's header line (1) moves down to line 2, which has code.
        const breakpoints = await client.send("setBreakpoints", {
            source: { path: file },
            breakpoints: [{ line: 1 }],
        });
        assert.deepStrictEqual(breakpoints.body.breakpoints.map((breakpoint) => breakpoint.line), [2]);
        assert.ok(breakpoints.body.breakpoints[0].verified);

        await client.send("configurationDone", {});
        assert.strictEqual(client.events("stopped")[0].body.reason, "entry");
        let stack = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].line, 4, "the first line with code");
        assert.strictEqual(path.resolve(stack.body.stackFrames[0].source.path), path.resolve(file));
        const position = client.events("thunder/position").pop();
        assert.strictEqual(position.body.line, 4);

        // Run on: the breakpoint inside twice, with the parameter it was called with.
        let stops = client.events("stopped").length;
        await client.send("continue", { threadId: 1 });
        const atBreakpoint = await client.nextEvent("stopped", stops);
        assert.strictEqual(atBreakpoint.body.reason, "breakpoint");
        stack = await client.send("stackTrace", { threadId: 1 });
        assert.deepStrictEqual(stack.body.stackFrames.map((frame) => frame.name), ["twice", "<main>"]);
        assert.strictEqual(stack.body.stackFrames[0].line, 2);
        assert.strictEqual(stack.body.stackFrames[1].line, 4, "the caller is shown on the line of the call");
        const scopes = await client.send("scopes", { frameId: stack.body.stackFrames[0].id });
        const locals = await client.send("variables", { variablesReference: scopes.body.scopes[0].variablesReference });
        assert.deepStrictEqual(locals.body.variables.map((variable) => [variable.name, variable.value, variable.type]),
            [["n", "21", "int"]]);
        const evaluated = await client.send("evaluate", { expression: "n * 2", frameId: 1, context: "watch" });
        assert.strictEqual(evaluated.success, false, "arithmetic is not evaluated");
        const compared = await client.send("evaluate", { expression: "n > 20", frameId: 1, context: "watch" });
        assert.strictEqual(compared.body.result, "true");

        // Out of twice, and then backwards into it again.
        stops = client.events("stopped").length;
        await client.send("stepOut", { threadId: 1 });
        await client.nextEvent("stopped", stops);
        stack = await client.send("stackTrace", { threadId: 1 });
        assert.deepStrictEqual(stack.body.stackFrames.map((frame) => frame.name), ["<main>"]);
        assert.strictEqual(stack.body.stackFrames[0].line, 4);

        stops = client.events("stopped").length;
        await client.send("stepBack", { threadId: 1 });
        const back = await client.nextEvent("stopped", stops);
        assert.ok(back.body.description.includes("history"));
        stack = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].name, "twice");
        assert.strictEqual(stack.body.stackFrames[0].line, 2);

        // Forwards again to the input: the machine asks, the console answers.
        const inputs = client.events("thunder/input").length;
        await client.send("continue", { threadId: 1 });
        await client.nextEvent("thunder/input", inputs);
        await waitFor(() => client.printed().includes("42\n"), "the first line of output");
        await client.send("evaluate", { expression: "Ada", context: "repl" });
        await client.nextEvent("terminated", 0);
        assert.strictEqual(client.printed(), "42\nName? Hello, Ada\n");
        assert.strictEqual(client.events("exited")[0].body.exitCode, 0);
    });

    test("a graphics program's frames arrive as events, and the canvas's mouse answers it", async () =>
    {
        const file = path.join(directory, "picture.th");
        fs.writeFileSync(file, [
            "import graphics",
            "canvas(50, 40)",
            "rect(5, 6, 10, 10, \"red\")",
            "frame()",
            "var waited = input()", // holds the program while the canvas's mouse is reported
            "println(mouse_x(), \" \", mouse_y())",
            "",
        ].join("\n"), "utf8");
        const client = new LiveClient(services);
        await client.send("initialize", {});
        assert.ok((await client.send("launch", { program: file, stopOnEntry: false })).success);
        await client.send("configurationDone", {});

        const frame = await client.nextEvent("thunder/frame", 0);
        const record = frame.body.record;
        assert.strictEqual(record.type, "frame");
        assert.strictEqual(record.width, 50);
        assert.strictEqual(record.height, 40);
        assert.ok(record.shapes.some((shape) => Array.isArray(shape) && shape[0] === "rect"));

        await waitFor(() => client.events("thunder/input").length > 0, "the program asks for input", 20000);
        assert.ok((await client.send("thunder/canvasEvent", { text: "mouse 20 30 1" })).success);
        await client.send("evaluate", { expression: "go", context: "repl" });
        await client.nextEvent("terminated", 0);
        assert.ok(client.printed().includes("20 30"), client.printed());
    });

    test("a Pashto program is debugged exactly like an English one", async () =>
    {
        // F5 on a Pashto file: the directive names the language, the compiler gets --lang ps
        // and the output is Pashto. (When this fails on one machine only, the usual cause is a
        // build/release older than the Pashto vocabulary of the samples.)
        const file = path.join(directory, "pashto.th");
        fs.writeFileSync(file, "# ژبه: ps\nدنده دوه_چنده(ن: عدد): عدد\n    راګرځه ن * ۲\nکرښه چاپ(دوه_چنده(۲۱))\n", "utf8");
        const client = new LiveClient(services);

        assert.ok((await client.send("initialize", {})).success);
        const launched = await client.send("launch", { program: file, stopOnEntry: true });
        assert.ok(launched.success, JSON.stringify(launched));
        await client.send("configurationDone", {});
        assert.strictEqual(client.events("stopped")[0].body.reason, "entry");
        const stack = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].line, 4, "the first line with code");
        const stops = client.events("stopped").length;
        await client.send("stepIn", { threadId: 1 });
        await client.nextEvent("stopped", stops);
        const inside = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(inside.body.stackFrames[0].name, "دوه_چنده");
        const scopes = await client.send("scopes", { frameId: 1 });
        const locals = await client.send("variables", { variablesReference: scopes.body.scopes[0].variablesReference });
        assert.deepStrictEqual(locals.body.variables.map((variable) => [variable.name, variable.value]), [["ن", "۲۱"]]);
        await client.send("continue", { threadId: 1 });
        await client.nextEvent("terminated", 0);
        assert.strictEqual(client.printed(), "۴۲\n");
    });

    test("a program with a runtime error stops where it failed, and terminate ends it", async () =>
    {
        const file = path.join(directory, "failing.th");
        fs.writeFileSync(file, "func divide(a: int, b: int): int\n    return a / b\nprintln(divide(1, 0))\n", "utf8");
        const client = new LiveClient(services);
        await client.send("initialize", {});
        assert.ok((await client.send("launch", { program: file, stopOnEntry: false })).success);
        await client.send("configurationDone", {});
        const failed = await client.nextEvent("stopped", 0);
        assert.strictEqual(failed.body.reason, "exception");
        assert.ok(failed.body.description.includes("R0"), failed.body.description);
        const info = await client.send("exceptionInfo", { threadId: 1 });
        assert.ok(info.body.details.stackTrace.includes("divide"));
        const stack = await client.send("stackTrace", { threadId: 1 });
        assert.deepStrictEqual(stack.body.stackFrames.map((frame) => [frame.name, frame.line]), [["divide", 2], ["<main>", 3]]);
        await client.send("terminate", {});
        await client.nextEvent("terminated", 0);
        assert.ok(client.events("output").some((event) => event.body.category === "stderr" && event.body.output.includes("R0")));
    });

    test("the files of a program with modules are numbered as the compiler numbers them", async () =>
    {
        const lib = path.join(directory, "lib");
        fs.mkdirSync(lib, { recursive: true });
        fs.writeFileSync(path.join(lib, "helper.th"), "func twice(n: int): int\n    return n * 2\n", "utf8");
        const file = path.join(directory, "modular.th");
        fs.writeFileSync(file, "import \"lib/helper\"\nprintln(twice(4))\n", "utf8");
        const toolchain = services.toolchain();
        const outcome = await toolchain.compile(file, { language: "en", output: path.join(directory, "modular.thc"), cwd: directory });
        assert.ok(outcome.report.success, JSON.stringify(outcome.report.diagnostics));
        const { Model } = require("../src/report");
        const files = filesOf(new Model(outcome.report, fs.readFileSync(file, "utf8"), { file }), file);
        assert.strictEqual(files.length, 2);
        assert.strictEqual(path.resolve(files[0]), path.resolve(file));
        assert.strictEqual(path.resolve(files[1]), path.resolve(path.join(lib, "helper.th")));

        // And a breakpoint in the module stops there.
        const client = new LiveClient(services);
        await client.send("initialize", {});
        assert.ok((await client.send("launch", { program: file, stopOnEntry: false })).success);
        const placed = await client.send("setBreakpoints", { source: { path: path.join(lib, "helper.th") }, breakpoints: [{ line: 2 }] });
        assert.ok(placed.body.breakpoints[0].verified);
        await client.send("configurationDone", {});
        const stopped = await client.nextEvent("stopped", 0);
        assert.strictEqual(stopped.body.reason, "breakpoint");
        const stack = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(path.resolve(stack.body.stackFrames[0].source.path), path.resolve(path.join(lib, "helper.th")));
        await client.send("continue", { threadId: 1 });
        await client.nextEvent("terminated", 0);
        assert.strictEqual(client.printed(), "8\n");
    });
});
