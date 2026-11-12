// The debug adapter, driven exactly as VS Code drives it: requests in, responses and events
// out. The recording is handed in, so no process is started here - the real recording is
// covered by trace.test.js and by the Python end-to-end test.

"use strict";

const path = require("path");

const { assert, before, suite, test } = require("./harness");
const { DebugSession } = require("../src/debug/adapter");

const PROGRAM = path.join(path.sep, "work", "main.th");

function trace()
{
    const frames = (depth) => Array.from({ length: depth }, (ignored, index) => ({
        function: index === 0 ? "<main>" : "twice",
        base: index,
        locals: 1,
    }));
    const steps = [
        { step: 0, function: "<main>", line: 4, column: 1, instruction: "CONST 0", mnemonic: "CONST", stack: [], frames: frames(1), locals: [], globals: [] },
        { step: 1, function: "<main>", line: 5, column: 1, instruction: "CALL 0", mnemonic: "CALL", stack: ["3"], frames: frames(1), locals: [], globals: [{ name: "answer", value: "void" }] },
        { step: 2, function: "twice", line: 2, column: 5, instruction: "LOAD_LOCAL 0", mnemonic: "LOAD_LOCAL", stack: ["3"], frames: frames(2), locals: [{ name: "n", value: "3" }], globals: [] },
        { step: 3, function: "twice", line: 2, column: 9, instruction: "RETURN", mnemonic: "RETURN", stack: ["6"], frames: frames(2), locals: [{ name: "n", value: "3" }], globals: [] },
        { step: 4, function: "<main>", line: 6, column: 1, instruction: "PRINT", mnemonic: "CALL_BUILTIN", stack: [], frames: frames(1), locals: [], globals: [{ name: "answer", value: "6" }] },
    ];
    return {
        tool: "thundervm",
        ok: true,
        exit_code: 0,
        truncated: false,
        error: null,
        output: [{ step: 4, text: "6\n" }],
        steps,
    };
}

/** Collects what the adapter sends and lets a test drive it request by request. */
class Client
{
    constructor(services)
    {
        this.messages = [];
        this.sequence = 1;
        this.session = new DebugSession((message) => this.messages.push(message), services);
    }

    async send(command, args)
    {
        const request = { seq: this.sequence++, type: "request", command, arguments: args || {} };
        await this.session.handleMessage(request);
        return this.responseTo(request.seq);
    }

    responseTo(seq)
    {
        return this.messages.find((message) => message.type === "response" && message.request_seq === seq);
    }

    events(name)
    {
        return this.messages.filter((message) => message.type === "event" && message.event === name);
    }

    lastEvent(name)
    {
        const all = this.events(name);
        return all.length ? all[all.length - 1] : null;
    }

    outputText(category)
    {
        return this.events("output")
            .filter((event) => !category || event.body.category === category)
            .map((event) => event.body.output)
            .join("");
    }
}

function services(overrides)
{
    return Object.assign({
        prepare: async () => ({
            trace: trace(),
            modules: [],
            executableLines: new Map([[0, new Set([2, 4, 5, 6])]]),
            sourcePathOfIndex: () => PROGRAM,
            diagnostics: [],
        }),
    }, overrides || {});
}

async function startedSession(options)
{
    const settings = options || {};
    const client = new Client(services(settings.services));
    await client.send("initialize", { adapterID: "thunder" });
    await client.send("launch", Object.assign({ program: PROGRAM, stopOnEntry: true }, settings.launch || {}));
    if (settings.breakpoints)
    {
        await client.send("setBreakpoints", {
            source: { path: PROGRAM },
            breakpoints: settings.breakpoints.map((line) => ({ line })),
        });
    }
    await client.send("configurationDone", {});
    return client;
}

suite("debug adapter: the protocol", () =>
{
    test("initialize offers stepping backwards", async () =>
    {
        const client = new Client(services());
        const response = await client.send("initialize", { adapterID: "thunder" });
        assert.ok(response.success);
        assert.strictEqual(response.body.supportsStepBack, true);
        assert.strictEqual(response.body.supportsConfigurationDoneRequest, true);
        assert.strictEqual(response.body.supportsTerminateRequest, true);
    });

    test("launch records the program and asks for the breakpoints", async () =>
    {
        const client = new Client(services());
        await client.send("initialize", {});
        const response = await client.send("launch", { program: PROGRAM });
        assert.ok(response.success);
        assert.ok(client.lastEvent("initialized"), "the client is asked to send its breakpoints");
        assert.ok(client.outputText("console").includes("Recorded 5 instruction(s)"));
    });

    test("a launch without a program fails and ends the session", async () =>
    {
        const client = new Client(services());
        await client.send("initialize", {});
        const response = await client.send("launch", {});
        assert.ok(!response.success);
        assert.ok(client.lastEvent("terminated"));
    });

    test("a program that does not compile reports the compiler's own lines", async () =>
    {
        const client = new Client(services({
            prepare: async () => ({ trace: null, message: "main.th has 1 error(s)", diagnostics: ["main.th:2:1: oops"] }),
        }));
        await client.send("initialize", {});
        const response = await client.send("launch", { program: PROGRAM });
        assert.ok(!response.success);
        assert.ok(client.outputText("stderr").includes("main.th:2:1: oops"));
        assert.ok(client.lastEvent("terminated"));
    });

    test("stopOnEntry stops at the first line", async () =>
    {
        const client = await startedSession();
        const stopped = client.lastEvent("stopped");
        assert.strictEqual(stopped.body.reason, "entry");
        const stack = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].line, 4);
        assert.strictEqual(stack.body.stackFrames[0].name, "<main>");
        assert.strictEqual(stack.body.stackFrames[0].source.path, PROGRAM);
    });

    test("a breakpoint on a line without code moves to the next line that has some", async () =>
    {
        const client = new Client(services());
        await client.send("initialize", {});
        await client.send("launch", { program: PROGRAM });
        const response = await client.send("setBreakpoints", {
            source: { path: PROGRAM },
            breakpoints: [{ line: 3 }, { line: 4 }, { line: 99 }],
        });
        const breakpoints = response.body.breakpoints;
        assert.deepStrictEqual(breakpoints.map((breakpoint) => breakpoint.verified), [true, true, false]);
        assert.strictEqual(breakpoints[0].line, 4, "line 3 has no code, so the breakpoint moves to line 4");
        assert.strictEqual(breakpoints[1].line, 4);
        assert.ok(breakpoints[2].message.includes("no code"));
    });

    test("continue stops at a breakpoint", async () =>
    {
        const client = await startedSession({ breakpoints: [2], launch: { stopOnEntry: true } });
        await client.send("continue", { threadId: 1 });
        const stopped = client.lastEvent("stopped");
        assert.strictEqual(stopped.body.reason, "breakpoint");
        const stack = await client.send("stackTrace", { threadId: 1 });
        assert.deepStrictEqual(stack.body.stackFrames.map((frame) => frame.name), ["twice", "<main>"]);
        assert.strictEqual(stack.body.stackFrames[0].line, 2);
    });

    test("the scopes show the locals, the globals, the operand stack and the step", async () =>
    {
        const client = await startedSession({ breakpoints: [2] });
        await client.send("continue", { threadId: 1 });
        const stack = await client.send("stackTrace", { threadId: 1 });
        const scopes = await client.send("scopes", { frameId: stack.body.stackFrames[0].id });
        assert.deepStrictEqual(scopes.body.scopes.map((scope) => scope.name),
            ["Locals", "Globals", "Operand stack", "Execution"]);

        const locals = await client.send("variables", { variablesReference: scopes.body.scopes[0].variablesReference });
        assert.deepStrictEqual(locals.body.variables.map((variable) => [variable.name, variable.value]),
            [["n", "3"]]);

        const operands = await client.send("variables",
            { variablesReference: scopes.body.scopes[2].variablesReference });
        assert.deepStrictEqual(operands.body.variables.map((variable) => variable.value), ["3"]);

        const execution = await client.send("variables",
            { variablesReference: scopes.body.scopes[3].variablesReference });
        const named = new Map(execution.body.variables.map((variable) => [variable.name, variable.value]));
        assert.strictEqual(named.get("step"), "3 of 5");
        assert.strictEqual(named.get("instruction"), "LOAD_LOCAL 0");
        assert.strictEqual(named.get("function"), "twice");
    });

    test("a frame below the top says why it has no locals", async () =>
    {
        const client = await startedSession({ breakpoints: [2] });
        await client.send("continue", { threadId: 1 });
        const stack = await client.send("stackTrace", { threadId: 1 });
        const scopes = await client.send("scopes", { frameId: stack.body.stackFrames[1].id });
        const locals = await client.send("variables", { variablesReference: scopes.body.scopes[0].variablesReference });
        assert.ok(locals.body.variables[0].name.includes("not recorded"));
    });

    test("stepping forwards and backwards moves between lines", async () =>
    {
        const client = await startedSession();
        const lineNow = async () =>
        {
            const stack = await client.send("stackTrace", { threadId: 1 });
            return stack.body.stackFrames[0].line;
        };
        assert.strictEqual(await lineNow(), 4);
        await client.send("stepIn", { threadId: 1 });
        assert.strictEqual(await lineNow(), 5);
        await client.send("stepIn", { threadId: 1 });
        assert.strictEqual(await lineNow(), 2, "into twice");
        await client.send("stepOut", { threadId: 1 });
        assert.strictEqual(await lineNow(), 6, "back in <main>");
        await client.send("stepBack", { threadId: 1 });
        assert.strictEqual(await lineNow(), 2, "backwards into twice again");
        assert.strictEqual(client.lastEvent("stopped").body.reason, "step");
    });

    test("step over skips a call", async () =>
    {
        const client = await startedSession();
        await client.send("stepIn", { threadId: 1 });   // line 5, the call
        await client.send("next", { threadId: 1 });
        const stack = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].line, 6);
        assert.strictEqual(stack.body.stackFrames.length, 1);
    });

    test("stepping back at the beginning stays at the beginning", async () =>
    {
        const client = await startedSession();
        await client.send("stepBack", { threadId: 1 });
        const stack = await client.send("stackTrace", { threadId: 1 });
        assert.strictEqual(stack.body.stackFrames[0].line, 4);
        assert.ok(!client.lastEvent("terminated"), "the session does not end by stepping back");
    });

    test("running to the end reports the output and the exit code", async () =>
    {
        const client = await startedSession();
        await client.send("continue", { threadId: 1 });
        assert.ok(client.outputText("stdout").includes("6\n"), "what the program printed");
        assert.ok(client.outputText("console").includes("exited with code 0"));
        assert.ok(client.lastEvent("exited"));
        assert.ok(client.lastEvent("terminated"));
    });

    test("a runtime error stops at the failing step", async () =>
    {
        const failing = trace();
        failing.ok = false;
        failing.exit_code = 1;
        failing.error = { text: "main.th:6:1: runtime error [R012]: index out of range" };
        const client = await startedSession({
            services: {
                prepare: async () => ({
                    trace: failing,
                    modules: [],
                    executableLines: new Map([[0, new Set([2, 4, 5, 6])]]),
                    sourcePathOfIndex: () => PROGRAM,
                }),
            },
        });
        await client.send("continue", { threadId: 1 });
        const stopped = client.lastEvent("stopped");
        assert.strictEqual(stopped.body.reason, "exception");
        assert.ok(client.outputText("stderr").includes("R012"));
        assert.ok(!client.lastEvent("terminated"), "the program stops where it failed, it does not vanish");
    });

    test("evaluate reads a variable, the step and the instruction", async () =>
    {
        const client = await startedSession({ breakpoints: [2] });
        await client.send("continue", { threadId: 1 });
        assert.strictEqual((await client.send("evaluate", { expression: "n", context: "watch" })).body.result, "3");
        assert.strictEqual((await client.send("evaluate", { expression: "$step" })).body.result, "3");
        assert.strictEqual((await client.send("evaluate", { expression: "$instruction" })).body.result,
            "LOAD_LOCAL 0");
        // A hover over something that is not a variable says nothing rather than complaining.
        const hover = await client.send("evaluate", { expression: "while", context: "hover" });
        assert.ok(hover.success);
        assert.strictEqual(hover.body.result, "");
        // In the REPL it does explain itself.
        const repl = await client.send("evaluate", { expression: "while", context: "repl" });
        assert.ok(!repl.success);
        assert.ok(repl.message.includes("replays a recording"));
    });

    test("a truncated recording says so", async () =>
    {
        const truncated = trace();
        truncated.truncated = true;
        const client = await startedSession({
            services: {
                prepare: async () => ({
                    trace: truncated,
                    modules: [],
                    executableLines: new Map(),
                    sourcePathOfIndex: () => PROGRAM,
                }),
            },
        });
        assert.ok(client.outputText("console").includes("traceLimit"));
    });

    test("threads reports the one thread a Thunder program has", async () =>
    {
        const client = await startedSession();
        const response = await client.send("threads", {});
        assert.deepStrictEqual(response.body.threads, [{ id: 1, name: "Thunder program" }]);
    });

    test("disconnect ends the session", async () =>
    {
        const client = await startedSession();
        const response = await client.send("disconnect", {});
        assert.ok(response.success);
        assert.ok(client.session.terminated);
    });
});
