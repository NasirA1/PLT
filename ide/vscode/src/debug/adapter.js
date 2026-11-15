// The Thunder debug adapter: the Debug Adapter Protocol on one side, `thundervm --debug` on the
// other.
//
// VS Code talks DAP; the virtual machine talks its own protocol of JSON lines (docs/debugging.md).
// This class translates between them and keeps the little state the translation needs: which
// file index a path is, which variablesReference means which scope or value, what the last stop
// was. It is written against a small `services` object (prepare a program for debugging, log a
// line) rather than against the VS Code API or child processes, so the whole protocol can be
// driven in a unit test with a scripted virtual machine - see test/adapter.test.js.
//
// The machine keeps a history of the lines it ran, so besides stepping forwards the adapter
// offers "step back" and "reverse continue" (DAP's supportsStepBack): a student can walk the
// program backwards from a wrong value to the step that produced it.

"use strict";

const path = require("path");

const { sameFile } = require("../util");
const { messagesFor } = require("./messages");

const THREAD_ID = 1;

// How long to wait for the machine's first stop (compiling is done before, so this is the
// process starting and verifying the bytecode).
const START_TIMEOUT = 10000;

class DebugSession
{
    /**
     * @param send     called with every protocol message to deliver to the client
     * @param services {prepare(configuration) -> {debuggee, files, program, diagnostics, message},
     *                  log(text), hidesSource(file) -> bool (a file whose position the editor
     *                  draws itself)} - see debug/launch.js and extension.js for the real ones
     */
    constructor(send, services)
    {
        this.send = send;
        this.services = services;
        this.sequence = 1;
        this.configuration = null;
        this.debuggee = null;
        this.files = [];              // file index -> absolute path
        this.program = "";
        this.lastStop = null;         // the machine's last "stopped" event
        this.references = [];         // variablesReference - 1 -> {frame, scope, path}
        this.breakpointRequests = new Map(); // file index -> the breakpoints asked for (for restarts)
        this.nextBreakpointId = 1;
        this.inputPending = false;
        this.terminated = false;
        this.exited = false;
        this.startWaiter = null;      // resolves when the machine first stops (or ends)
        this.messages = messagesFor("en"); // the debugger's own remarks, in the program's language
    }

    // ------------------------------------------------------------------ protocol plumbing

    handleMessage(message)
    {
        if (!message || message.type !== "request")
        {
            return Promise.resolve();
        }
        return this._dispatch(message).catch((error) =>
        {
            this.respondError(message, error && error.message ? error.message : String(error));
        });
    }

    async _dispatch(request)
    {
        switch (request.command)
        {
        case "initialize":
            return this.onInitialize(request);
        case "launch":
            return this.onLaunch(request);
        case "setBreakpoints":
            return this.onSetBreakpoints(request);
        case "setExceptionBreakpoints":
            return this.respond(request, {});
        case "configurationDone":
            return this.onConfigurationDone(request);
        case "threads":
            return this.respond(request, { threads: [{ id: THREAD_ID, name: "Thunder program" }] });
        case "stackTrace":
            return this.onStackTrace(request);
        case "scopes":
            return this.onScopes(request);
        case "variables":
            return this.onVariables(request);
        case "evaluate":
            return this.onEvaluate(request);
        case "exceptionInfo":
            return this.onExceptionInfo(request);
        case "continue":
            return this.onResume(request, "continue", { allThreadsContinued: true });
        case "next":
        case "stepIn":
        case "stepOut":
        case "stepBack":
        case "reverseContinue":
        case "pause":
            return this.onResume(request, request.command, {});
        case "restart":
            return this.onRestart(request);
        case "disconnect":
        case "terminate":
            return this.onTerminate(request);
        case "thunder/canvasEvent":
            return this.onCanvasEvent(request);
        default:
            return this.respond(request, {});
        }
    }

    /** A key or mouse line from the Thunder Canvas, passed on to the machine's canvas events. */
    async onCanvasEvent(request)
    {
        const text = String((request.arguments || {}).text || "");
        if (this.debuggee && !this.exited && text)
        {
            this.debuggee.request("canvasEvent", { text }).catch(() => {}); // the program may just have ended
        }
        this.respond(request, {});
    }

    respond(request, body)
    {
        this.send({
            seq: this.sequence++,
            type: "response",
            request_seq: request.seq,
            success: true,
            command: request.command,
            body,
        });
    }

    respondError(request, message)
    {
        this.send({
            seq: this.sequence++,
            type: "response",
            request_seq: request.seq,
            success: false,
            command: request.command,
            message,
            body: { error: { id: 1, format: message, showUser: true } },
        });
    }

    event(name, body)
    {
        this.send({ seq: this.sequence++, type: "event", event: name, body: body || {} });
    }

    output(text, category)
    {
        if (text)
        {
            this.event("output", { category: category || "stdout", output: text });
        }
    }

    // ------------------------------------------------------------------ starting

    onInitialize(request)
    {
        this.respond(request, {
            supportsConfigurationDoneRequest: true,
            supportsConditionalBreakpoints: true,
            supportsHitConditionalBreakpoints: true,
            supportsStepBack: true,
            supportsEvaluateForHovers: true,
            supportsExceptionInfoRequest: true,
            supportsTerminateRequest: true,
            supportsRestartRequest: true,
            supportsValueFormattingOptions: false,
            supportsDelayedStackTraceLoading: false,
            exceptionBreakpointFilters: [],
            supportsSingleThreadExecutionRequests: false,
        });
    }

    async onLaunch(request)
    {
        this.configuration = request.arguments || {};
        if (!this.configuration.program)
        {
            this.respondError(request, "No program to debug: set \"program\" in the launch configuration.");
            this.finish(-1);
            return;
        }
        const started = await this.startProgram();
        if (!started)
        {
            this.respondError(request, this.startFailure || "the program could not be started");
            this.finish(-1);
            return;
        }
        this.respond(request, {});
        // Breakpoints are accepted from here on; the machine waits at its entry until
        // configurationDone says whether to show that stop or run on.
        this.event("initialized");
    }

    /**
     * Compiles and starts the program, and waits until the machine has stopped at its entry.
     * True on success; otherwise `startFailure` says what went wrong (and the client has been
     * told through the console).
     */
    async startProgram()
    {
        this.startFailure = "";
        let prepared = null;
        try
        {
            prepared = await this.services.prepare(this.configuration);
        }
        catch (error)
        {
            this.startFailure = error && error.message ? error.message : String(error);
            return false;
        }
        for (const diagnostic of (prepared.diagnostics || []))
        {
            this.output(diagnostic + "\n", "stderr");
        }
        if (!prepared.debuggee)
        {
            this.startFailure = prepared.message || "the program could not be compiled";
            return false;
        }
        this.debuggee = prepared.debuggee;
        this.files = (prepared.files || [path.resolve(this.configuration.program)]).map((file) => path.resolve(file));
        this.program = prepared.program || path.resolve(this.configuration.program);
        this.messages = messagesFor(prepared.language);
        this.lastStop = null;
        this.exited = false;
        this.inputPending = false;
        this.attach(this.debuggee);
        const entry = new Promise((resolve) =>
        {
            this.startWaiter = resolve;
            setTimeout(() => resolve(null), START_TIMEOUT);
        });
        this.debuggee.start();
        const stop = await entry;
        this.startWaiter = null;
        if (!stop)
        {
            this.startFailure = this.exited
                ? "the program ended before the debugger could stop it"
                : "the virtual machine did not answer";
            return false;
        }
        return true;
    }

    /**
     * Hooks the machine's events up to the client. A machine that was replaced (restart) may
     * still speak while it ends; only the current one is listened to.
     */
    attach(debuggee)
    {
        const current = (handler) => (event) =>
        {
            if (this.debuggee === debuggee)
            {
                handler(event);
            }
        };
        debuggee.on("stopped", current((event) => this.onMachineStopped(event)));
        debuggee.on("continued", current(() =>
        {
            this.references = [];
            this.event("continued", { threadId: THREAD_ID, allThreadsContinued: true });
            this.event("thunder/continued", {});
        }));
        debuggee.on("output", current((event) => this.output(event.text, "stdout")));
        // A graphics program's frames (the records of thundervm --canvas): the extension draws
        // them on its Thunder Canvas, and sends the keys and the mouse back (thunder/canvasEvent).
        debuggee.on("frame", current((event) => this.event("thunder/frame", { record: event.record })));
        debuggee.on("input", current(() =>
        {
            this.inputPending = true;
            this.output(`${this.messages.waitsForInput}\n`, "console");
            this.event("thunder/input", {
                program: this.program,
                title: this.messages.inputTitle,
                prompt: this.messages.inputPrompt(path.basename(this.program)),
                placeholder: this.messages.inputPlaceholder,
            });
        }));
        debuggee.on("exited", current((event) => this.onMachineExited(event)));
        debuggee.on("error", current((event) => this.output(`${event.message}\n`, "stderr")));
        debuggee.on("garbage", current((event) => this.output(`${event.text}\n`, "stderr")));
        debuggee.on("close", current(() =>
        {
            if (!this.exited)
            {
                this.exited = true;
                this.output(`${this.messages.endedUnexpectedly}\n`, "stderr");
                this.finish(-1);
            }
            if (this.startWaiter)
            {
                this.startWaiter(null);
            }
        }));
    }

    onMachineStopped(event)
    {
        this.lastStop = event;
        this.references = [];
        this.inputPending = false;
        if (this.startWaiter)
        {
            // The entry stop: the client is told about it from configurationDone, or not at all.
            this.startWaiter(event);
            return;
        }
        this.reportStop(event);
    }

    reportStop(event)
    {
        const body = {
            reason: event.reason || "step",
            threadId: THREAD_ID,
            allThreadsStopped: true,
        };
        if (event.description)
        {
            body.description = event.description;
        }
        if (event.text)
        {
            body.text = event.text;
        }
        if (event.historic)
        {
            body.description = `${body.description ? body.description + " - " : ""}an earlier step, from the history`;
        }
        this.event("stopped", body);
        this.event("thunder/position", {
            file: this.pathOfFile(event.file),
            line: event.line,
            column: event.column,
            historic: Boolean(event.historic),
            reason: event.reason,
        });
    }

    onMachineExited(event)
    {
        this.exited = true;
        if (event.error && event.error.text)
        {
            this.output(`${event.error.text}\n`, "stderr");
        }
        else if (event.error && event.error.message)
        {
            this.output(`${event.error.code || this.messages.runtimeError}: ${event.error.message}\n`, "stderr");
        }
        if (event.aborted)
        {
            this.output(`${this.messages.stoppedByDebugger}\n`, "console");
        }
        else
        {
            this.output(`${this.messages.exited(event.exit_code || 0)}\n`, "console");
        }
        this.finish(event.exit_code || 0);
    }

    /** Tells the client the session is over (once). */
    finish(exitCode)
    {
        if (this.terminated)
        {
            return;
        }
        this.terminated = true;
        this.event("thunder/continued", {});
        this.event("exited", { exitCode });
        this.event("terminated");
    }

    async onConfigurationDone(request)
    {
        this.respond(request, {});
        await this.showEntryOrRun();
    }

    /** After the entry stop: show it (stopOnEntry) or let the program run to a breakpoint. */
    async showEntryOrRun()
    {
        if (!this.lastStop)
        {
            return;
        }
        if (this.configuration.stopOnEntry !== false)
        {
            this.reportStop(this.lastStop);
            return;
        }
        await this.resume("continue");
    }

    // ------------------------------------------------------------------ breakpoints

    async onSetBreakpoints(request)
    {
        const args = request.arguments || {};
        const source = args.source || {};
        const wanted = args.breakpoints || (args.lines || []).map((line) => ({ line }));
        const file = this.fileIndexOf(source.path || "");
        if (file < 0)
        {
            this.respond(request, {
                breakpoints: wanted.map((breakpoint) => ({
                    id: this.nextBreakpointId++,
                    verified: false,
                    line: breakpoint.line,
                    message: "this file is not part of the program being debugged",
                })),
            });
            return;
        }
        const breakpoints = wanted.map((breakpoint) => ({
            line: breakpoint.line,
            condition: breakpoint.condition || "",
            hitCondition: breakpoint.hitCondition || "",
        }));
        this.breakpointRequests.set(file, breakpoints);
        const placed = await this.placeBreakpoints(file, breakpoints);
        this.respond(request, {
            breakpoints: placed.map((entry) => ({
                id: this.nextBreakpointId++,
                verified: Boolean(entry.verified),
                line: entry.line,
                source: { path: this.pathOfFile(file), name: path.basename(this.pathOfFile(file)) },
                message: entry.message,
            })),
        });
    }

    async placeBreakpoints(file, breakpoints)
    {
        if (!this.debuggee || this.exited)
        {
            return breakpoints.map((breakpoint) => ({ line: breakpoint.line, verified: false }));
        }
        const body = await this.debuggee.request("setBreakpoints", { file, breakpoints });
        return body.breakpoints || [];
    }

    fileIndexOf(file)
    {
        const wanted = path.resolve(file || "");
        return this.files.findIndex((candidate) => sameFile(candidate, wanted));
    }

    pathOfFile(index)
    {
        return this.files[index || 0] || this.program;
    }

    // ------------------------------------------------------------------ running

    async onResume(request, command, body)
    {
        if (!this.debuggee || this.exited)
        {
            this.respondError(request, "the program has ended");
            return;
        }
        try
        {
            await this.debuggee.request(command);
        }
        catch (error)
        {
            this.respondError(request, error.message);
            return;
        }
        this.respond(request, body);
    }

    async resume(command)
    {
        if (this.debuggee && !this.exited)
        {
            await this.debuggee.request(command).catch(() => undefined);
        }
    }

    async onRestart(request)
    {
        this.respond(request, {});
        const previous = this.debuggee;
        this.debuggee = null; // whatever the old machine still says is not for the client
        if (previous)
        {
            await previous.terminate();
        }
        this.terminated = false;
        const started = await this.startProgram();
        if (!started)
        {
            this.output(`${this.startFailure}\n`, "stderr");
            this.finish(-1);
            return;
        }
        for (const [file, breakpoints] of this.breakpointRequests)
        {
            await this.placeBreakpoints(file, breakpoints);
        }
        await this.showEntryOrRun();
    }

    async onTerminate(request)
    {
        this.respond(request, {});
        if (this.debuggee)
        {
            await this.debuggee.terminate();
        }
        this.finish(0);
    }

    // ------------------------------------------------------------------ the state

    async onStackTrace(request)
    {
        if (!this.debuggee || this.exited)
        {
            this.respond(request, { stackFrames: [], totalFrames: 0 });
            return;
        }
        const body = await this.debuggee.request("stackTrace");
        const frames = (body.frames || []).map((frame) =>
        {
            const file = this.pathOfFile(frame.file);
            const shown = {
                id: frame.index + 1,
                name: frame.function,
                line: frame.line || 1,
                column: frame.column || 1,
                presentationHint: body.historic ? "subtle" : "normal",
                thunderFile: file, // for the extension, whether or not VS Code is given the source
            };
            // A file shown in the right-to-left editor has no `source`: given one, VS Code would
            // open its text editor at every stop, which the extension then swaps back - a
            // flicker on every step. The right-to-left editor draws the position itself.
            if (!(this.services.hidesSource && this.services.hidesSource(file)))
            {
                shown.source = { path: file, name: path.basename(file) };
            }
            return shown;
        });
        this.respond(request, { stackFrames: frames, totalFrames: frames.length });
    }

    onScopes(request)
    {
        const frame = Math.max(0, ((request.arguments || {}).frameId || 1) - 1);
        const scopes = [
            { name: "Locals", variablesReference: this.reference(frame, "locals", ""), expensive: false },
            { name: "Globals", variablesReference: this.reference(frame, "globals", ""), expensive: false },
        ];
        if (frame === 0)
        {
            scopes.push({ name: "Operand stack", variablesReference: this.reference(0, "stack", ""), expensive: false });
            scopes.push({ name: "Execution", variablesReference: this.reference(0, "execution", ""), expensive: false });
        }
        this.respond(request, { scopes });
    }

    /** A variablesReference for a scope or for the contents of a value; valid until the next stop. */
    reference(frame, scope, valuePath)
    {
        this.references.push({ frame, scope, path: valuePath });
        return this.references.length;
    }

    async onVariables(request)
    {
        const entry = this.references[((request.arguments || {}).variablesReference || 0) - 1];
        if (!entry || !this.debuggee || this.exited)
        {
            this.respond(request, { variables: [] });
            return;
        }
        const body = await this.debuggee.request("variables", {
            frame: entry.frame,
            scope: entry.scope === "evaluate" ? "evaluate" : entry.scope,
            path: entry.path,
        });
        const variables = (body.variables || []).map((variable) => ({
            name: variable.name,
            value: variable.value,
            type: variable.type || undefined,
            variablesReference: variable.expandable
                ? this.reference(entry.frame, entry.scope, childPath(entry.path, variable.name))
                : 0,
        }));
        if (body.message && variables.length === 0)
        {
            variables.push({ name: "", value: body.message, variablesReference: 0 });
        }
        this.respond(request, { variables });
    }

    async onEvaluate(request)
    {
        const args = request.arguments || {};
        const expression = String(args.expression || "").trim();
        if (!this.debuggee || this.exited)
        {
            this.respond(request, { result: "", variablesReference: 0 });
            return;
        }
        if (args.context === "repl" && this.inputPending)
        {
            // The program asked for a line: what is typed in the console is that line.
            this.inputPending = false;
            await this.debuggee.request("input", { text: String(args.expression || "") });
            this.respond(request, { result: "", variablesReference: 0 });
            return;
        }
        const frame = Math.max(0, (args.frameId || 1) - 1);
        try
        {
            const body = await this.debuggee.request("evaluate", { expression, frame });
            this.respond(request, {
                result: body.value,
                type: body.type || undefined,
                variablesReference: body.expandable ? this.reference(frame, "evaluate", expression) : 0,
            });
        }
        catch (error)
        {
            if (args.context === "hover")
            {
                // Nothing to say about a word that is not a variable here; an error would show a
                // tooltip over every keyword.
                this.respond(request, { result: "", variablesReference: 0 });
                return;
            }
            this.respondError(request, error.message);
        }
    }

    onExceptionInfo(request)
    {
        const stop = this.lastStop || {};
        const description = stop.description || "runtime error";
        this.respond(request, {
            exceptionId: description.split(":")[0],
            description,
            breakMode: "always",
            details: { message: description, stackTrace: stop.text || "" },
        });
    }
}

/** The path of an element or field, as the machine resolves it: "a" + "[1]" -> "a[1]", "a" + "x" -> "a.x". */
function childPath(parent, name)
{
    if (!parent)
    {
        return name;
    }
    return name.startsWith("[") ? parent + name : `${parent}.${name}`;
}

module.exports = { DebugSession, childPath };
