// The recorded execution of a program, and what stepping through it means.
//
// Thunder's virtual machine does not stop and wait: `thundervm --trace-json` records what every
// instruction did - the position, the call frames, the locals, the globals and the operand stack
// - and the debugger replays that recording. The program therefore runs to its end (or to the
// recording limit) before the first breakpoint is shown, and in exchange the debugger can step
// *backwards* as easily as forwards, which is what makes it a teaching tool: you can go back and
// look again at the step that produced a wrong value.
//
// Everything here is pure (no VS Code API, no processes): see test/trace.test.js.

"use strict";

const path = require("path");

// The reasons a replay stops, as the Debug Adapter Protocol names them.
const STOP_STEP = "step";
const STOP_BREAKPOINT = "breakpoint";
const STOP_ENTRY = "entry";
const STOP_EXCEPTION = "exception";

class Recording
{
    /**
     * @param trace the parsed `thundervm --trace-json` document
     * @param options {program: the main source file, modules: [{name, path}] from the compile report}
     */
    constructor(trace, options)
    {
        const settings = options || {};
        this.trace = trace || {};
        this.steps = this.trace.steps || [];
        this.program = settings.program || "";
        this.modules = settings.modules || [];
        this._lineStarts = null;
    }

    get length()
    {
        return this.steps.length;
    }

    get truncated()
    {
        return Boolean(this.trace.truncated);
    }

    get exitCode()
    {
        return Number(this.trace.exit_code || 0);
    }

    get ok()
    {
        return Boolean(this.trace.ok);
    }

    /** The runtime error that ended the program, or null. */
    get error()
    {
        return this.trace.error || null;
    }

    step(index)
    {
        return this.steps[index] || null;
    }

    /** The depth of the call stack at a step (1 = the program's own body). */
    depthAt(index)
    {
        const step = this.step(index);
        return step ? (step.frames || []).length : 0;
    }

    /** Where a step is in the source: {file, line, column, name}. */
    positionAt(index)
    {
        const step = this.step(index);
        if (!step)
        {
            return null;
        }
        return {
            file: this.pathOfStep(step),
            fileIndex: step.file || 0,
            line: step.line,
            column: step.column,
            name: step.function,
        };
    }

    /** The file a step belongs to: the program itself, or one of the modules it imported. */
    pathOfStep(step)
    {
        if (!step.file)
        {
            return this.program;
        }
        const name = step.file_name || "";
        const module = this.modules.find(
            (entry) => entry.name === name || entry.path === name
                || path.basename(entry.path || entry.name || "") === path.basename(name));
        if (module && module.path)
        {
            return path.isAbsolute(module.path)
                ? module.path
                : path.resolve(path.dirname(this.program), module.path);
        }
        return path.isAbsolute(name) ? name : path.resolve(path.dirname(this.program), name);
    }

    /**
     * True when a step is where the debugger should stop: the first instruction of a source
     * line *in its own frame*.
     *
     * The test is not simply "the line changed". `var answer = twice(21)` is one line whose
     * instructions are interrupted by the whole of `twice`: when the call returns, the next
     * instruction is on line 4 again, and stopping there a second time would make a breakpoint
     * on that line fire twice for one execution of it. The line each frame was last on is
     * therefore remembered per depth, and a frame that is left forgets its line, so a second
     * call to the same function does stop again.
     */
    isLineStart(index)
    {
        if (!this._lineStarts)
        {
            this._lineStarts = this._computeLineStarts();
        }
        return Boolean(this._lineStarts[index]);
    }

    _computeLineStarts()
    {
        const starts = new Array(this.steps.length);
        const lineOfDepth = [];   // the line the frame at each depth was last seen on
        for (let i = 0; i < this.steps.length; ++i)
        {
            const step = this.steps[i];
            const depth = (step.frames || []).length;
            lineOfDepth.length = Math.min(lineOfDepth.length, depth + 1); // deeper frames are gone
            const previous = lineOfDepth[depth];
            starts[i] = !previous || previous.line !== step.line || previous.file !== (step.file || 0);
            lineOfDepth[depth] = { line: step.line, file: step.file || 0 };
        }
        return starts;
    }

    /** Everything the program printed up to and including a step. */
    outputUpTo(index)
    {
        let text = "";
        for (const record of (this.trace.output || []))
        {
            if (record.step <= index)
            {
                text += record.text;
            }
        }
        return text;
    }

    /** What the program printed between two steps (used while stepping forwards). */
    outputBetween(from, to)
    {
        let text = "";
        for (const record of (this.trace.output || []))
        {
            if (record.step > from && record.step <= to)
            {
                text += record.text;
            }
        }
        return text;
    }

    // ------------------------------------------------------------------ stepping

    /**
     * The first step at or after `index` where execution would be shown to the user: the start
     * of a source line.
     */
    firstStop(index)
    {
        for (let i = Math.max(0, index); i < this.length; ++i)
        {
            if (this.isLineStart(i))
            {
                return i;
            }
        }
        return this.length ? this.length - 1 : -1;
    }

    /** Step into: the next source line, whatever function it is in. */
    stepIn(index)
    {
        for (let i = index + 1; i < this.length; ++i)
        {
            if (this.isLineStart(i))
            {
                return i;
            }
        }
        return -1;
    }

    /** Step over: the next source line in this function or in one that called it. */
    next(index)
    {
        const depth = this.depthAt(index);
        for (let i = index + 1; i < this.length; ++i)
        {
            if (this.depthAt(i) <= depth && this.isLineStart(i))
            {
                return i;
            }
        }
        return -1;
    }

    /** Step out: the next step in the function that called this one. */
    stepOut(index)
    {
        const depth = this.depthAt(index);
        for (let i = index + 1; i < this.length; ++i)
        {
            if (this.depthAt(i) < depth)
            {
                return i;
            }
        }
        return -1;
    }

    /** Step back one source line (the mirror image of stepIn). */
    stepBack(index)
    {
        for (let i = index - 1; i >= 0; --i)
        {
            if (this.isLineStart(i))
            {
                return i;
            }
        }
        return -1;
    }

    /** Step back over a call: the previous line in this function or in its caller. */
    previous(index)
    {
        const depth = this.depthAt(index);
        for (let i = index - 1; i >= 0; --i)
        {
            if (this.depthAt(i) <= depth && this.isLineStart(i))
            {
                return i;
            }
        }
        return -1;
    }

    /**
     * Run on until a breakpoint is reached.
     *
     * `breakpoints` is a Map from a file path to a Set of 1-based line numbers. The step
     * returned is always the first instruction of its line, so a breakpoint on a line with
     * seven instructions stops once, not seven times.
     */
    continueFrom(index, breakpoints)
    {
        for (let i = index + 1; i < this.length; ++i)
        {
            if (this.isLineStart(i) && this.isBreakpoint(i, breakpoints))
            {
                return i;
            }
        }
        return -1;
    }

    /** Run backwards until a breakpoint is reached. */
    reverseContinueFrom(index, breakpoints)
    {
        for (let i = index - 1; i >= 0; --i)
        {
            if (this.isLineStart(i) && this.isBreakpoint(i, breakpoints))
            {
                return i;
            }
        }
        return -1;
    }

    isBreakpoint(index, breakpoints)
    {
        if (!breakpoints || breakpoints.size === 0)
        {
            return false;
        }
        const position = this.positionAt(index);
        if (!position)
        {
            return false;
        }
        const lines = breakpoints.get(position.file) || breakpoints.get(path.normalize(position.file));
        return Boolean(lines && lines.has(position.line));
    }

    /** The step at which the program failed, or -1. */
    get errorStep()
    {
        return this.error && this.length ? this.length - 1 : -1;
    }

    // ------------------------------------------------------------------ state

    /**
     * The call stack at a step, innermost frame first, as the debugger shows it.
     *
     * The recording carries the locals of the running frame only (the virtual machine's own
     * view), so the frames below it are listed with their function and position but without
     * variables - stepping back to them shows everything again.
     */
    stackAt(index)
    {
        const step = this.step(index);
        if (!step)
        {
            return [];
        }
        const frames = (step.frames || []).slice().reverse();
        return frames.map((frame, position) => ({
            name: frame.function,
            top: position === 0,
            base: frame.base,
            locals: position === 0 ? (step.locals || []) : null,
            file: position === 0 ? this.pathOfStep(step) : this.program,
            line: position === 0 ? step.line : null,
            column: position === 0 ? step.column : null,
        }));
    }

    globalsAt(index)
    {
        const step = this.step(index);
        return step ? (step.globals || []) : [];
    }

    stackValuesAt(index)
    {
        const step = this.step(index);
        return step ? (step.stack || []) : [];
    }

    instructionAt(index)
    {
        const step = this.step(index);
        return step ? step.instruction : "";
    }
}

module.exports = { Recording, STOP_BREAKPOINT, STOP_ENTRY, STOP_EXCEPTION, STOP_STEP };
