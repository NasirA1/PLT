// The Thunder debug adapter: the Debug Adapter Protocol spoken over a recorded execution.
//
// VS Code talks DAP; thundervm records a trace. This class is the translator between them. It
// is written against a small `services` object (compile, record, read a file) rather than
// against the VS Code API or child processes, so the whole protocol can be driven in a unit
// test with a fake toolchain - see test/adapter.test.js.
//
// Because the execution is a recording, the adapter offers what a live debugger cannot:
// "step back" and "reverse continue" (DAP's supportsStepBack), so a student can walk the
// program backwards from a wrong value to the step that produced it.

"use strict";

const path = require("path");

const { Recording, STOP_BREAKPOINT, STOP_ENTRY, STOP_EXCEPTION, STOP_STEP } = require("./trace");

const THREAD_ID = 1;

// Scope handles. Variable references must be positive and unique; the frame and scope they
// mean are packed into the number so no table has to be kept between requests.
const SCOPE_LOCALS = 1;
const SCOPE_GLOBALS = 2;
const SCOPE_STACK = 3;
const SCOPE_EXECUTION = 4;
const SCOPE_COUNT = 8;

class DebugSession
{
    /**
     * @param send     called with every protocol message to deliver to the client
     * @param services {compile, record, readFile, log} - see extension.js for the real ones
     */
    constructor(send, services)
    {
        this.send = send;
        this.services = services;
        this.sequence = 1;
        this.recording = null;
        this.position = -1;
        this.breakpoints = new Map();   // file -> Set of lines
        this.breakpointIds = new Map(); // file -> [{id, line}]
        this.nextBreakpointId = 1;
        this.executableLines = new Map();
        this.configuration = null;
        this.configurationDone = false;
        this.pendingStart = null;
        this.terminated = false;
        this.stopOnEntry = true;
        this.stopAtRuntimeError = true;
        this.reportedOutputUpTo = -1;
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
        case "continue":
            return this.onContinue(request);
        case "reverseContinue":
            return this.onReverseContinue(request);
        case "next":
            return this.onNext(request);
        case "stepIn":
            return this.onStepIn(request);
        case "stepOut":
            return this.onStepOut(request);
        case "stepBack":
            return this.onStepBack(request);
        case "evaluate":
            return this.onEvaluate(request);
        case "restart":
            return this.onRestart(request);
        case "disconnect":
        case "terminate":
            this.respond(request, {});
            this.terminated = true;
            return undefined;
        default:
            return this.respond(request, {});
        }
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
        if (!text)
        {
            return;
        }
        this.event("output", { category: category || "stdout", output: text });
    }

    // ------------------------------------------------------------------ requests

    onInitialize(request)
    {
        this.respond(request, {
            supportsConfigurationDoneRequest: true,
            supportsStepBack: true,
            supportsEvaluateForHovers: true,
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
        const configuration = request.arguments || {};
        this.configuration = configuration;
        this.stopOnEntry = configuration.stopOnEntry !== false;
        this.stopAtRuntimeError = configuration.stopAtRuntimeError !== false;

        const program = configuration.program;
        if (!program)
        {
            this.respondError(request, "No program to debug: set \"program\" in the launch configuration.");
            this.event("terminated");
            return;
        }

        let prepared = null;
        try
        {
            prepared = await this.services.prepare(configuration);
        }
        catch (error)
        {
            this.respondError(request, error && error.message ? error.message : String(error));
            this.event("terminated");
            return;
        }

        if (prepared.diagnostics && prepared.diagnostics.length)
        {
            for (const diagnostic of prepared.diagnostics)
            {
                this.output(diagnostic + "\n", "stderr");
            }
        }
        if (!prepared.trace)
        {
            this.respondError(request, prepared.message || "the program could not be compiled");
            this.event("terminated");
            return;
        }

        this.recording = new Recording(prepared.trace, {
            program: path.resolve(program),
            modules: prepared.modules || [],
        });
        this.executableLines = prepared.executableLines || new Map();
        this.sourcePathOfIndex = prepared.sourcePathOfIndex || (() => path.resolve(program));

        this.output(`Recorded ${this.recording.length} instruction(s) of ${path.basename(program)}.\n`, "console");
        if (this.recording.truncated)
        {
            this.output(
                `Only the first ${this.recording.length} instructions were recorded `
                + "(raise \"traceLimit\" in the launch configuration to record more).\n", "console");
        }

        this.respond(request, {});
        // Breakpoints can be accepted from here on; the first stop waits for configurationDone.
        this.event("initialized");
    }

    onSetBreakpoints(request)
    {
        const source = (request.arguments || {}).source || {};
        const wanted = (request.arguments || {}).breakpoints
            || ((request.arguments || {}).lines || []).map((line) => ({ line }));
        const file = path.resolve(source.path || "");
        const verified = [];
        const lines = new Set();
        for (const breakpoint of wanted)
        {
            const placed = this.placeBreakpoint(file, breakpoint.line);
            verified.push({
                id: this.nextBreakpointId++,
                verified: placed !== null,
                line: placed === null ? breakpoint.line : placed,
                source: { path: file, name: path.basename(file) },
                message: placed === null
                    ? "there is no code on this line, so the program never stops here"
                    : undefined,
            });
            if (placed !== null)
            {
                lines.add(placed);
            }
        }
        this.breakpoints.set(file, lines);
        this.respond(request, { breakpoints: verified });
    }

    /**
     * Where a breakpoint on a line really goes.
     *
     * A line without instructions (a comment, a blank line, a `struct` header) can never stop
     * the program, so the breakpoint moves down to the next line that has code - as every
     * other debugger does - and is reported as unverified when there is none.
     */
    placeBreakpoint(file, line)
    {
        const available = this.linesOfFile(file);
        if (!available)
        {
            return line; // the file is not part of this program (yet): trust the user
        }
        if (available.has(line))
        {
            return line;
        }
        const next = [...available].filter((candidate) => candidate > line).sort((a, b) => a - b)[0];
        return next === undefined ? null : next;
    }

    linesOfFile(file)
    {
        for (const [index, lines] of this.executableLines)
        {
            if (path.resolve(this.sourcePathOfIndex(index)) === path.resolve(file))
            {
                return lines;
            }
        }
        return null;
    }

    onConfigurationDone(request)
    {
        this.respond(request, {});
        this.configurationDone = true;
        this.startReplay();
    }

    startReplay()
    {
        if (!this.recording || this.recording.length === 0)
        {
            this.finish();
            return;
        }
        this.position = this.recording.firstStop(0);
        this.reportedOutputUpTo = -1;
        if (this.stopOnEntry)
        {
            this.flushOutput();
            this.event("stopped", { reason: STOP_ENTRY, threadId: THREAD_ID, allThreadsStopped: true });
            return;
        }
        this.resume();
    }

    /** Moves to a step and tells the client why execution stopped there. */
    settle(target, reason)
    {
        if (target < 0 || target >= this.recording.length)
        {
            this.finish();
            return;
        }
        this.position = target;
        this.flushOutput();
        this.event("stopped", { reason, threadId: THREAD_ID, allThreadsStopped: true });
    }

    /** Sends whatever the program printed up to the current step. */
    flushOutput()
    {
        const text = this.recording.outputBetween(this.reportedOutputUpTo, this.position);
        this.reportedOutputUpTo = this.position;
        this.output(text);
    }

    /** The program has run out of recorded steps: report the end, and the error if there was one. */
    finish()
    {
        if (this.terminated)
        {
            return;
        }
        if (this.recording)
        {
            const remaining = this.recording.outputBetween(this.reportedOutputUpTo, this.recording.length);
            this.reportedOutputUpTo = this.recording.length;
            this.output(remaining);
            const error = this.recording.error;
            if (error)
            {
                this.output(`${error.text || error.message || "runtime error"}\n`, "stderr");
            }
            this.output(`Program exited with code ${this.recording.exitCode}.\n`, "console");
        }
        this.terminated = true;
        this.event("exited", { exitCode: this.recording ? this.recording.exitCode : 0 });
        this.event("terminated");
    }

    /** Runs on to the next breakpoint, or stops at the failing step if the program failed. */
    resume()
    {
        const target = this.recording.continueFrom(this.position, this.breakpoints);
        if (target >= 0)
        {
            this.settle(target, STOP_BREAKPOINT);
            return;
        }
        if (this.stopAtRuntimeError && this.recording.error && this.recording.errorStep > this.position)
        {
            const error = this.recording.error;
            this.settle(this.recording.errorStep, STOP_EXCEPTION);
            this.event("output", {
                category: "stderr",
                output: `${error.text || error.message || "runtime error"}\n`,
            });
            return;
        }
        this.finish();
    }

    onContinue(request)
    {
        this.respond(request, { allThreadsContinued: true });
        this.resume();
    }

    onReverseContinue(request)
    {
        this.respond(request, {});
        const target = this.recording.reverseContinueFrom(this.position, this.breakpoints);
        this.settle(target >= 0 ? target : this.recording.firstStop(0), target >= 0 ? STOP_BREAKPOINT : STOP_ENTRY);
    }

    onNext(request)
    {
        this.respond(request, {});
        this.stepTo(this.recording.next(this.position));
    }

    onStepIn(request)
    {
        this.respond(request, {});
        this.stepTo(this.recording.stepIn(this.position));
    }

    onStepOut(request)
    {
        this.respond(request, {});
        this.stepTo(this.recording.stepOut(this.position));
    }

    onStepBack(request)
    {
        this.respond(request, {});
        const target = (request.arguments || {}).granularity === "instruction"
            ? this.position - 1
            : this.recording.stepBack(this.position);
        if (target < 0)
        {
            // Already at the beginning: stay there rather than ending the session.
            this.event("stopped", { reason: STOP_STEP, threadId: THREAD_ID, allThreadsStopped: true });
            return;
        }
        this.position = target;
        this.event("stopped", { reason: STOP_STEP, threadId: THREAD_ID, allThreadsStopped: true });
    }

    stepTo(target)
    {
        if (target < 0)
        {
            if (this.stopAtRuntimeError && this.recording.error && this.recording.errorStep > this.position)
            {
                this.settle(this.recording.errorStep, STOP_EXCEPTION);
                return;
            }
            this.finish();
            return;
        }
        this.settle(target, STOP_STEP);
    }

    onStackTrace(request)
    {
        const frames = this.recording ? this.recording.stackAt(this.position) : [];
        const stack = frames.map((frame, index) => ({
            id: this.position * 100 + index,
            name: frame.name === "<main>" ? "<main>" : frame.name,
            line: frame.line || (index === 0 ? 1 : 1),
            column: frame.column || 1,
            source: frame.file ? { path: frame.file, name: path.basename(frame.file) } : undefined,
            presentationHint: frame.top ? "normal" : "subtle",
        }));
        this.respond(request, { stackFrames: stack, totalFrames: stack.length });
    }

    onScopes(request)
    {
        const frameId = (request.arguments || {}).frameId || 0;
        const frameIndex = frameId % 100;
        const base = (frameIndex + 1) * SCOPE_COUNT;
        this.respond(request, {
            scopes: [
                { name: "Locals", variablesReference: base + SCOPE_LOCALS, expensive: false },
                { name: "Globals", variablesReference: base + SCOPE_GLOBALS, expensive: false },
                { name: "Operand stack", variablesReference: base + SCOPE_STACK, expensive: false },
                { name: "Execution", variablesReference: base + SCOPE_EXECUTION, expensive: false },
            ],
        });
    }

    onVariables(request)
    {
        const reference = (request.arguments || {}).variablesReference || 0;
        const frameIndex = Math.floor(reference / SCOPE_COUNT) - 1;
        const scope = reference % SCOPE_COUNT;
        const variables = [];
        if (!this.recording)
        {
            this.respond(request, { variables });
            return;
        }
        const frames = this.recording.stackAt(this.position);
        const frame = frames[frameIndex] || frames[0];
        if (scope === SCOPE_LOCALS)
        {
            if (frame && frame.locals)
            {
                for (const local of frame.locals)
                {
                    variables.push(variable(local.name, local.value));
                }
            }
            else if (frame)
            {
                variables.push(variable("<not recorded>",
                    "the virtual machine records the locals of the running function; "
                    + "step back into this frame to see them"));
            }
        }
        else if (scope === SCOPE_GLOBALS)
        {
            for (const global of this.recording.globalsAt(this.position))
            {
                variables.push(variable(global.name, global.value));
            }
        }
        else if (scope === SCOPE_STACK)
        {
            const values = this.recording.stackValuesAt(this.position);
            values.forEach((value, index) =>
            {
                variables.push(variable(`[${index}]`, value));
            });
        }
        else if (scope === SCOPE_EXECUTION)
        {
            const position = this.recording.positionAt(this.position);
            variables.push(variable("step", `${this.position + 1} of ${this.recording.length}`));
            variables.push(variable("instruction", this.recording.instructionAt(this.position)));
            if (position)
            {
                variables.push(variable("function", position.name));
                variables.push(variable("line", String(position.line)));
                variables.push(variable("file", path.basename(position.file || "")));
            }
            if (this.recording.truncated)
            {
                variables.push(variable("recording", "truncated at the trace limit"));
            }
        }
        this.respond(request, { variables });
    }

    onEvaluate(request)
    {
        const settings = request.arguments || {};
        const expression = String(settings.expression || "").trim();
        if (!this.recording)
        {
            this.respond(request, { result: "", variablesReference: 0 });
            return;
        }
        const frames = this.recording.stackAt(this.position);
        const locals = (frames[0] && frames[0].locals) || [];
        const globals = this.recording.globalsAt(this.position);
        const found = locals.find((entry) => entry.name === expression)
            || globals.find((entry) => entry.name === expression);
        if (found)
        {
            this.respond(request, { result: found.value, variablesReference: 0 });
            return;
        }
        if (expression === "$step")
        {
            this.respond(request, { result: String(this.position + 1), variablesReference: 0 });
            return;
        }
        if (expression === "$instruction")
        {
            this.respond(request, { result: this.recording.instructionAt(this.position), variablesReference: 0 });
            return;
        }
        if (settings.context === "hover")
        {
            // Nothing to say about a word that is not a variable here; an error would show a
            // tooltip over every keyword.
            this.respond(request, { result: "", variablesReference: 0 });
            return;
        }
        this.respondError(request,
            `'${expression}' is not a variable at this step (the debugger replays a recording, `
            + "so expressions are not evaluated - try a variable name, $step or $instruction)");
    }

    async onRestart(request)
    {
        this.respond(request, {});
        if (this.recording)
        {
            this.terminated = false;
            this.startReplay();
        }
    }
}

function variable(name, value)
{
    return { name, value: String(value), variablesReference: 0 };
}

module.exports = { DebugSession, SCOPE_COUNT, SCOPE_EXECUTION, SCOPE_GLOBALS, SCOPE_LOCALS, SCOPE_STACK, THREAD_ID };
