// The debug adapter, driven exactly as VS Code drives it: requests in, responses and events
// out. The virtual machine at the other end is scripted here, so no process is started - the
// real `thundervm --debug` is covered by launch.test.js and by tests/cpp/test_debugger.cpp.

"use strict";

const path = require("path");

const { absolute, assert, suite, test, waitFor } = require("./harness");
const { Debuggee } = require("../src/debug/debuggee");
const { DebugSession, childPath } = require("../src/debug/adapter");

const PROGRAM = absolute("work", "main.th");
const MODULE = absolute("work", "lib", "helper.th");

/**
 * A virtual machine as the protocol sees it. Requests are answered from a table; events are
 * sent by the test ("the program stopped at line 7"). Everything that was asked is recorded.
 */
class ScriptedVm
{
    constructor()
    {
        this.requests = [];
        this.lineListeners = [];
        this.closeListeners = [];
        this.closed = false;
        this.answers = {
            setBreakpoints: (args) => ({
                breakpoints: args.breakpoints.map((breakpoint) => ({
                    line: breakpoint.line === 4 ? 5 : breakpoint.line,
                    verified: breakpoint.line < 40,
                    message: breakpoint.line < 40 ? undefined : "there is no code on this line, so the program never stops here",
                })),
            }),
            stackTrace: () => ({
                historic: false,
                step: 36,
                frames: [
                    { index: 0, function: "twice", file: 1, line: 2, column: 5 },
                    { index: 1, function: "<main>", file: 0, line: 5, column: 14 },
                ],
            }),
            variables: (args) =>
            {
                if (args.scope === "locals" && !args.path)
                {
                    return { variables: [
                        { name: "n", value: "21", type: "int", expandable: false },
                        { name: "pair", value: "(1, 2)", type: "tuple", expandable: true },
                    ] };
                }
                if (args.path === "pair")
                {
                    return { variables: [
                        { name: "[0]", value: "1", type: "int", expandable: false },
                        { name: "[1]", value: "2", type: "int", expandable: false },
                    ] };
                }
                if (args.scope === "globals")
                {
                    return { variables: [{ name: "answer", value: "void", type: "void", expandable: false }] };
                }
                if (args.scope === "stack")
                {
                    return { variables: [{ name: "[0]", value: "21", type: "", expandable: false }] };
                }
                if (args.scope === "execution")
                {
                    return { variables: [{ name: "step", value: "36", type: "int", expandable: false }] };
                }
                return { variables: [], message: "an earlier step keeps its values as text" };
            },
            evaluate: (args) =>
            {
                if (args.expression === "n")
                {
                    return { value: "21", type: "int", expandable: false };
                }
                if (args.expression === "pair")
                {
                    return { value: "(1, 2)", type: "tuple", expandable: true };
                }
                throw new Error(`'${args.expression}' is not a variable here`);
            },
            continue: () => ({}),
            next: () => ({}),
            stepIn: () => ({}),
            stepOut: () => ({}),
            stepBack: () => ({}),
            reverseContinue: () => ({}),
            pause: () => ({}),
            input: () => ({}),
            canvasEvent: () => ({}),
            terminate: () => ({}),
        };
    }

    // The transport the Debuggee is built on.
    get transport()
    {
        return {
            write: (line) => this.receive(line),
            onLine: (callback) => this.lineListeners.push(callback),
            onClose: (callback) => this.closeListeners.push(callback),
            kill: () => this.close(),
        };
    }

    receive(line)
    {
        const request = JSON.parse(line);
        this.requests.push(request);
        const answer = this.answers[request.command];
        if (answer === null)
        {
            return; // scripted to never answer
        }
        try
        {
            const body = answer ? answer(request) : {};
            this.send({ type: "response", request_seq: request.seq, command: request.command, success: true, body });
        }
        catch (error)
        {
            this.send({ type: "response", request_seq: request.seq, command: request.command, success: false, message: error.message });
        }
        if (request.command === "terminate")
        {
            this.event("exited", { ok: true, exit_code: 0, aborted: true, error: null });
            this.close();
        }
    }

    send(message)
    {
        for (const listener of this.lineListeners)
        {
            listener(JSON.stringify(message));
        }
    }

    event(name, body)
    {
        this.send(Object.assign({ type: "event", event: name }, body || {}));
    }

    stopped(body)
    {
        this.event("stopped", Object.assign({ reason: "breakpoint", file: 0, line: 5, column: 1, historic: false }, body || {}));
    }

    close()
    {
        if (this.closed)
        {
            return;
        }
        this.closed = true;
        for (const listener of this.closeListeners)
        {
            listener(0);
        }
    }

    requested(command)
    {
        return this.requests.filter((request) => request.command === command);
    }
}

/** Collects what the adapter sends and lets a test drive it request by request. */
class Client
{
    constructor(vm, options)
    {
        this.vm = vm;
        this.messages = [];
        this.sequence = 1;
        const settings = options || {};
        this.session = new DebugSession((message) => this.messages.push(message), {
            hidesSource: settings.hidesSource,
            prepare: settings.prepare || (async () =>
            {
                const debuggee = new Debuggee(vm.transport);
                // The machine speaks first: ready, then the entry stop.
                setTimeout(() =>
                {
                    vm.event("ready", { program: "main.th", files: ["main.th", "lib/helper.th"] });
                    vm.stopped({ reason: "entry", line: 5 });
                }, 0);
                return { debuggee, files: [PROGRAM, MODULE], program: PROGRAM, diagnostics: [], message: "" };
            }),
            log: () => {},
        });
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

async function startedSession(options)
{
    const settings = options || {};
    const vm = new ScriptedVm();
    const client = new Client(vm, settings);
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
    return { client, vm };
}

suite("debug adapter: starting", () =>
{
    test("initialize says what the adapter can do", async () =>
    {
        const client = new Client(new ScriptedVm());
        const response = await client.send("initialize", {});
        assert.ok(response.success);
        assert.ok(response.body.supportsStepBack, "stepping back through the history");
        assert.ok(response.body.supportsConditionalBreakpoints);
        assert.ok(response.body.supportsHitConditionalBreakpoints);
        assert.ok(response.body.supportsConfigurationDoneRequest);
        assert.ok(response.body.supportsRestartRequest);
        assert.ok(response.body.supportsExceptionInfoRequest);
    });

    test("launch starts the machine, waits for its entry stop and then asks for breakpoints", async () =>
    {
        const vm = new ScriptedVm();
        const client = new Client(vm);
        await client.send("initialize", {});
        const launched = await client.send("launch", { program: PROGRAM });
        assert.ok(launched.success);
        assert.strictEqual(client.events("initialized").length, 1);
        assert.strictEqual(client.events("stopped").length, 0, "the entry stop waits for configurationDone");
    });

    test("configurationDone shows the entry stop, or runs on when stopOnEntry is false", async () =>
    {
        const shown = await startedSession({ launch: { stopOnEntry: true } });
        assert.strictEqual(shown.client.lastEvent("stopped").body.reason, "entry");
        assert.strictEqual(shown.vm.requested("continue").length, 0);
        const position = shown.client.lastEvent("thunder/position");
        assert.strictEqual(position.body.line, 5);
        assert.strictEqual(path.resolve(position.body.file), path.resolve(PROGRAM));

        const running = await startedSession({ launch: { stopOnEntry: false } });
        assert.strictEqual(running.client.events("stopped").length, 0);
        assert.strictEqual(running.vm.requested("continue").length, 1, "the program was continued quietly");
    });

    test("a program that does not compile ends the session with the compiler's words", async () =>
    {
        const vm = new ScriptedVm();
        const client = new Client(vm, {
            prepare: async () => ({
                debuggee: null,
                files: [PROGRAM],
                program: PROGRAM,
                diagnostics: ["main.th:3:1: error [S001]: expected a statement"],
                message: "main.th has 1 error(s)",
            }),
        });
        await client.send("initialize", {});
        const launched = await client.send("launch", { program: PROGRAM });
        assert.strictEqual(launched.success, false);
        assert.ok(launched.message.includes("1 error"));
        assert.ok(client.outputText("stderr").includes("S001"));
        assert.strictEqual(client.events("terminated").length, 1);
    });

    test("a launch without a program is refused", async () =>
    {
        const client = new Client(new ScriptedVm());
        await client.send("initialize", {});
        const launched = await client.send("launch", {});
        assert.strictEqual(launched.success, false);
        assert.ok(launched.message.includes("program"));
    });

    test("a machine that never stops is reported, not waited for for ever", async () =>
    {
        const vm = new ScriptedVm();
        const client = new Client(vm, {
            prepare: async () =>
            {
                const debuggee = new Debuggee(vm.transport);
                setTimeout(() => vm.close(), 0); // the process dies at once
                return { debuggee, files: [PROGRAM], program: PROGRAM, diagnostics: [], message: "" };
            },
        });
        await client.send("initialize", {});
        const launched = await client.send("launch", { program: PROGRAM });
        assert.strictEqual(launched.success, false);
        assert.ok(launched.message.includes("ended"), launched.message);
    });
});

suite("debug adapter: breakpoints", () =>
{
    test("breakpoints are sent to the machine by file index and reported where they really went", async () =>
    {
        const { client, vm } = await startedSession();
        const response = await client.send("setBreakpoints", {
            source: { path: MODULE },
            breakpoints: [{ line: 2 }, { line: 4, condition: "n > 1" }, { line: 99 }],
        });
        const sent = vm.requested("setBreakpoints")[0];
        assert.strictEqual(sent.file, 1, "the module is file 1");
        assert.deepStrictEqual(sent.breakpoints[1], { line: 4, condition: "n > 1", hitCondition: "" });
        const placed = response.body.breakpoints;
        assert.deepStrictEqual(placed.map((breakpoint) => breakpoint.line), [2, 5, 99]);
        assert.deepStrictEqual(placed.map((breakpoint) => breakpoint.verified), [true, true, false]);
        assert.ok(placed[2].message.includes("no code"));
        assert.strictEqual(path.resolve(placed[0].source.path), path.resolve(MODULE));
        assert.ok(placed.every((breakpoint) => breakpoint.id > 0));
    });

    test("a breakpoint in a file that is not part of the program is unverified", async () =>
    {
        const { client, vm } = await startedSession();
        const response = await client.send("setBreakpoints", {
            source: { path: absolute("elsewhere", "other.th") },
            lines: [3],
        });
        assert.strictEqual(response.body.breakpoints[0].verified, false);
        assert.ok(response.body.breakpoints[0].message.includes("not part"));
        assert.strictEqual(vm.requested("setBreakpoints").length, 0);
    });
});

suite("debug adapter: running and stopping", () =>
{
    test("the machine's stops become DAP stops, with the position for the right-to-left editor", async () =>
    {
        const { client, vm } = await startedSession();
        await client.send("continue", { threadId: 1 });
        assert.strictEqual(client.events("continued").length, 0, "the machine has not said it runs yet");
        vm.event("continued");
        assert.strictEqual(client.lastEvent("continued").body.allThreadsContinued, true);
        assert.strictEqual(client.events("thunder/continued").length, 1);

        vm.stopped({ reason: "breakpoint", file: 1, line: 2, column: 5 });
        const stopped = client.lastEvent("stopped");
        assert.strictEqual(stopped.body.reason, "breakpoint");
        assert.strictEqual(stopped.body.threadId, 1);
        const position = client.lastEvent("thunder/position");
        assert.strictEqual(path.resolve(position.body.file), path.resolve(MODULE));
        assert.strictEqual(position.body.line, 2);
        assert.strictEqual(position.body.column, 5);
    });

    test("every stepping command is passed on, and a refusal becomes an error response", async () =>
    {
        const { client, vm } = await startedSession();
        for (const command of ["next", "stepIn", "stepOut", "stepBack", "reverseContinue", "pause"])
        {
            const response = await client.send(command, { threadId: 1 });
            assert.ok(response.success, command);
            assert.strictEqual(vm.requested(command).length, 1, command);
        }
        vm.answers.next = () => { throw new Error("the program is running"); };
        const refused = await client.send("next", { threadId: 1 });
        assert.strictEqual(refused.success, false);
        assert.ok(refused.message.includes("running"));
    });

    test("a stop from the history says so", async () =>
    {
        const { client, vm } = await startedSession();
        vm.stopped({ reason: "step", line: 3, historic: true });
        const stopped = client.lastEvent("stopped");
        assert.ok(stopped.body.description.includes("history"));
        assert.strictEqual(client.lastEvent("thunder/position").body.historic, true);
    });

    test("a runtime error is a stop with exception details", async () =>
    {
        const { client, vm } = await startedSession();
        vm.stopped({
            reason: "exception",
            line: 2,
            description: "R003: division by zero",
            text: "main.th:2:12: runtime error [R003]: division by zero\n  in divide",
        });
        const stopped = client.lastEvent("stopped");
        assert.strictEqual(stopped.body.reason, "exception");
        assert.strictEqual(stopped.body.description, "R003: division by zero");
        const info = await client.send("exceptionInfo", { threadId: 1 });
        assert.strictEqual(info.body.exceptionId, "R003");
        assert.ok(info.body.details.stackTrace.includes("in divide"));
    });

    test("output arrives as it is written and the end of the program ends the session", async () =>
    {
        const { client, vm } = await startedSession();
        vm.event("output", { text: "42\n" });
        assert.strictEqual(client.outputText("stdout"), "42\n");
        vm.event("exited", { ok: true, exit_code: 3, aborted: false, error: null });
        assert.strictEqual(client.lastEvent("exited").body.exitCode, 3);
        assert.strictEqual(client.events("terminated").length, 1);
        assert.ok(client.outputText("console").includes("exited with code 3"));
        vm.close();
        assert.strictEqual(client.events("terminated").length, 1, "the process closing is not a second end");
    });

    test("a graphics frame becomes a thunder/frame event, and the canvas's keys go back", async () =>
    {
        const { client, vm } = await startedSession({});
        vm.event("frame", { record: { type: "frame", n: 1, width: 50, height: 40, keep: 0,
            shapes: [["rect", 5, 6, 10, 10, "#ff0000"]] } });
        const frame = client.lastEvent("thunder/frame");
        assert.ok(frame, "the frame reached the client");
        assert.strictEqual(frame.body.record.width, 50);
        assert.strictEqual(frame.body.record.shapes[0][0], "rect");
        const response = await client.send("thunder/canvasEvent", { text: "mouse 20 30 1" });
        assert.ok(response.success);
        await waitFor(() => vm.requested("canvasEvent").length === 1);
        assert.strictEqual(vm.requested("canvasEvent")[0].text, "mouse 20 30 1");
        const empty = await client.send("thunder/canvasEvent", { text: "" });
        assert.ok(empty.success);
        assert.strictEqual(vm.requested("canvasEvent").length, 1, "an empty line is not sent on");
    });

    test("a runtime error at the end is printed to the console", async () =>
    {
        const { client, vm } = await startedSession();
        vm.event("exited", {
            ok: false, exit_code: 1, aborted: false,
            error: { code: "R003", message: "division by zero", text: "main.th:2:12: runtime error [R003]: division by zero" },
        });
        assert.ok(client.outputText("stderr").includes("[R003]"));
        assert.strictEqual(client.lastEvent("exited").body.exitCode, 1);
    });

    test("a machine that dies ends the session with a message", async () =>
    {
        const { client, vm } = await startedSession();
        vm.close();
        assert.ok(client.outputText("stderr").includes("unexpectedly"));
        assert.strictEqual(client.events("terminated").length, 1);
    });

    test("terminate asks the machine to stop and ends the session once", async () =>
    {
        const { client, vm } = await startedSession();
        const response = await client.send("terminate", {});
        assert.ok(response.success);
        assert.strictEqual(vm.requested("terminate").length, 1);
        assert.strictEqual(client.events("terminated").length, 1);
        assert.ok(client.outputText("console").includes("stopped by the debugger"));
    });

    test("restart starts the program again and puts the breakpoints back", async () =>
    {
        const { client, vm } = await startedSession({ breakpoints: [2] });
        const stopsBefore = client.events("stopped").length;
        await client.send("restart", {});
        await waitFor(() => client.events("stopped").length > stopsBefore, "the entry stop after the restart");
        assert.strictEqual(vm.requested("terminate").length, 1);
        assert.strictEqual(vm.requested("setBreakpoints").length, 2, "placed once before and once after");
        assert.strictEqual(client.lastEvent("stopped").body.reason, "entry");
    });
});

suite("debug adapter: the state", () =>
{
    test("the stack names the frames and their files", async () =>
    {
        const { client } = await startedSession();
        const response = await client.send("stackTrace", { threadId: 1 });
        const frames = response.body.stackFrames;
        assert.deepStrictEqual(frames.map((frame) => frame.name), ["twice", "<main>"]);
        assert.deepStrictEqual(frames.map((frame) => frame.id), [1, 2]);
        assert.strictEqual(path.resolve(frames[0].source.path), path.resolve(MODULE));
        assert.strictEqual(path.resolve(frames[1].source.path), path.resolve(PROGRAM));
        assert.strictEqual(frames[1].line, 5);
        assert.strictEqual(response.body.totalFrames, 2);
        assert.ok(frames.every((frame) => frame.thunderFile), "the file travels with every frame");
    });

    test("a frame in a file the right-to-left editor shows is given no source, so VS Code opens nothing", async () =>
    {
        const vm = new ScriptedVm();
        const client = new Client(vm, { hidesSource: (file) => path.resolve(file) === path.resolve(MODULE) });
        await client.send("initialize", {});
        await client.send("launch", { program: PROGRAM });
        await client.send("configurationDone", {});
        const frames = (await client.send("stackTrace", { threadId: 1 })).body.stackFrames;
        assert.strictEqual(frames[0].source, undefined, "the module is shown right to left");
        assert.strictEqual(path.resolve(frames[0].thunderFile), path.resolve(MODULE));
        assert.strictEqual(path.resolve(frames[1].source.path), path.resolve(PROGRAM));
    });

    test("scopes and variables follow the frame, and values expand by path", async () =>
    {
        const { client, vm } = await startedSession();
        const scopes = (await client.send("scopes", { frameId: 1 })).body.scopes;
        assert.deepStrictEqual(scopes.map((scope) => scope.name), ["Locals", "Globals", "Operand stack", "Execution"]);
        const outer = (await client.send("scopes", { frameId: 2 })).body.scopes;
        assert.deepStrictEqual(outer.map((scope) => scope.name), ["Locals", "Globals"], "the stack belongs to the innermost frame");

        const locals = (await client.send("variables", { variablesReference: scopes[0].variablesReference })).body.variables;
        assert.deepStrictEqual(vm.requested("variables").pop(), { seq: vm.requests.length, command: "variables", frame: 0, scope: "locals", path: "" });
        assert.deepStrictEqual(locals.map((variable) => [variable.name, variable.value, variable.type]),
            [["n", "21", "int"], ["pair", "(1, 2)", "tuple"]]);
        assert.strictEqual(locals[0].variablesReference, 0);
        assert.ok(locals[1].variablesReference > 0, "a tuple can be expanded");

        const elements = (await client.send("variables", { variablesReference: locals[1].variablesReference })).body.variables;
        assert.strictEqual(vm.requested("variables").pop().path, "pair");
        assert.deepStrictEqual(elements.map((variable) => variable.name), ["[0]", "[1]"]);

        const globals = (await client.send("variables", { variablesReference: scopes[1].variablesReference })).body.variables;
        assert.strictEqual(globals[0].name, "answer");
        const outerLocals = await client.send("variables", { variablesReference: outer[0].variablesReference });
        assert.strictEqual(vm.requested("variables").pop().frame, 1);
        assert.ok(outerLocals.success);
    });

    test("a reference from before the last stop is empty rather than wrong", async () =>
    {
        const { client, vm } = await startedSession();
        const scopes = (await client.send("scopes", { frameId: 1 })).body.scopes;
        vm.stopped({ reason: "step", line: 6 });
        const stale = await client.send("variables", { variablesReference: scopes[0].variablesReference });
        assert.deepStrictEqual(stale.body.variables, []);
    });

    test("the machine's explanation is shown when there is nothing to list", async () =>
    {
        const { client } = await startedSession();
        const scopes = (await client.send("scopes", { frameId: 1 })).body.scopes;
        const locals = (await client.send("variables", { variablesReference: scopes[0].variablesReference })).body.variables;
        const nested = (await client.send("variables", { variablesReference: locals[1].variablesReference })).body.variables;
        // Expanding one level further is not scripted: the machine answers with a message.
        const deeper = await client.send("variables", { variablesReference: nested[0].variablesReference || 0 });
        assert.ok(Array.isArray(deeper.body.variables));
    });

    test("evaluate answers variables, lets expandable results be opened, and keeps hovers quiet", async () =>
    {
        const { client, vm } = await startedSession();
        const known = await client.send("evaluate", { expression: "n", frameId: 1, context: "watch" });
        assert.strictEqual(known.body.result, "21");
        assert.strictEqual(known.body.type, "int");
        assert.strictEqual(vm.requested("evaluate").pop().frame, 0);

        const pair = await client.send("evaluate", { expression: "pair", frameId: 2, context: "repl" });
        assert.ok(pair.body.variablesReference > 0);
        assert.strictEqual(vm.requested("evaluate").pop().frame, 1);
        const elements = await client.send("variables", { variablesReference: pair.body.variablesReference });
        assert.deepStrictEqual(vm.requested("variables").pop(), {
            seq: vm.requests.length, command: "variables", frame: 1, scope: "evaluate", path: "pair",
        });
        assert.ok(elements.success);

        const unknown = await client.send("evaluate", { expression: "nobody", frameId: 1, context: "watch" });
        assert.strictEqual(unknown.success, false);
        assert.ok(unknown.message.includes("nobody"));

        const hover = await client.send("evaluate", { expression: "println", frameId: 1, context: "hover" });
        assert.ok(hover.success);
        assert.strictEqual(hover.body.result, "");
    });

    test("what is typed in the console while the program waits for input is the input", async () =>
    {
        const { client, vm } = await startedSession();
        vm.event("input");
        assert.strictEqual(client.events("thunder/input").length, 1);
        assert.ok(client.outputText("console").includes("input"));
        const typed = await client.send("evaluate", { expression: "Ada", context: "repl" });
        assert.ok(typed.success);
        assert.deepStrictEqual(vm.requested("input").map((request) => request.text), ["Ada"]);
        // Once answered, the console evaluates again.
        await client.send("evaluate", { expression: "n", context: "repl" });
        assert.strictEqual(vm.requested("evaluate").length, 1);
    });

    test("paths of elements and fields are spelled the way the machine resolves them", () =>
    {
        assert.strictEqual(childPath("", "items"), "items");
        assert.strictEqual(childPath("items", "[2]"), "items[2]");
        assert.strictEqual(childPath("items[2]", "name"), "items[2].name");
        assert.strictEqual(childPath("scores", "[\"ada\"]"), "scores[\"ada\"]");
    });
});

suite("debug adapter: the program's language", () =>
{
    test("a Pashto program is debugged in Pashto: the console remarks and the input request", async () =>
    {
        const vm = new ScriptedVm();
        const client = new Client(vm, {
            prepare: async () =>
            {
                const debuggee = new Debuggee(vm.transport);
                setTimeout(() =>
                {
                    vm.event("ready", { program: "پښتو.th", files: ["پښتو.th"] });
                    vm.stopped({ reason: "entry", line: 2 });
                }, 0);
                return { debuggee, files: [PROGRAM], program: PROGRAM, language: "ps", diagnostics: [], message: "" };
            },
        });
        await client.send("initialize", {});
        await client.send("launch", { program: PROGRAM });
        await client.send("configurationDone", {});
        vm.event("input");
        assert.ok(client.outputText("console").includes("انتظار"), client.outputText("console"));
        const request = client.lastEvent("thunder/input").body;
        assert.strictEqual(request.title, "پروګرام ننوت غواړي");
        assert.ok(request.prompt.startsWith("main.th"));
        vm.event("exited", { ok: true, exit_code: 3, aborted: false, error: null });
        assert.ok(client.outputText("console").includes("د وتلو کوډ ۳"), "the exit code in Pashto digits");
        assert.ok(!/Program exited/.test(client.outputText("console")), "no English remark");
    });

    test("an English program keeps its English remarks", async () =>
    {
        const { client, vm } = await startedSession();
        vm.event("exited", { ok: true, exit_code: 0, aborted: false, error: null });
        assert.ok(client.outputText("console").includes("Program exited with code 0."));
        const { messagesFor, pashtoDigits } = require("../src/debug/messages");
        assert.strictEqual(messagesFor("xx"), messagesFor("en"), "an unknown language falls back to English");
        assert.strictEqual(pashtoDigits(2048), "۲۰۴۸");
    });
});

suite("debug adapter: the connection to the machine", () =>
{
    test("responses are matched to requests and failures become rejections", async () =>
    {
        const vm = new ScriptedVm();
        const debuggee = new Debuggee(vm.transport);
        debuggee.start();
        const body = await debuggee.request("evaluate", { expression: "n" });
        assert.strictEqual(body.value, "21");
        await assert.rejects(debuggee.request("evaluate", { expression: "nobody" }), /nobody/);
    });

    test("events that arrive before the adapter listens are held until start()", async () =>
    {
        const vm = new ScriptedVm();
        const debuggee = new Debuggee(vm.transport);
        vm.event("ready", {});
        const seen = [];
        debuggee.on("ready", () => seen.push("ready"));
        assert.deepStrictEqual(seen, []);
        debuggee.start();
        assert.deepStrictEqual(seen, ["ready"]);
    });

    test("a closed connection rejects what was waiting and refuses new requests", async () =>
    {
        const vm = new ScriptedVm();
        vm.answers.stackTrace = null; // never answered
        const debuggee = new Debuggee(vm.transport);
        debuggee.start();
        const pending = debuggee.request("stackTrace");
        vm.close();
        await assert.rejects(pending, /ended/);
        await assert.rejects(debuggee.request("next"), /ended/);
    });

    test("lines that are not JSON are reported as garbage, not fatal", () =>
    {
        const vm = new ScriptedVm();
        const debuggee = new Debuggee(vm.transport);
        debuggee.start();
        const garbage = [];
        debuggee.on("garbage", (event) => garbage.push(event.text));
        for (const listener of vm.lineListeners)
        {
            listener("Segmentation fault");
            listener("   ");
        }
        assert.deepStrictEqual(garbage, ["Segmentation fault"]);
    });
});
