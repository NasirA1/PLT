// Getting a program ready to be debugged: compile it, then record it.
//
// `thundervm --trace-json` writes the whole execution to a file; the debug adapter replays it.
// Recording happens here, outside the adapter, so the adapter itself stays a pure translator
// between the protocol and the recording.

"use strict";

const fs = require("fs");
const path = require("path");

const { Model } = require("../report");
const { workDirectory } = require("../toolchain");

/**
 * Compiles and records a program.
 *
 * Returns {trace, modules, executableLines, sourcePathOfIndex, diagnostics, message}. `trace`
 * is null when the program did not compile; `diagnostics` then holds the compiler's own lines,
 * which the adapter prints to the debug console.
 */
async function prepareDebugSession(services, configuration)
{
    const toolchain = services.toolchain();
    if (!toolchain)
    {
        throw new Error(services.missingToolchainMessage());
    }
    const program = path.resolve(configuration.program);
    const cwd = configuration.cwd ? path.resolve(configuration.cwd) : path.dirname(program);
    const directory = workDirectory("debug");
    const tracePath = path.join(directory, "trace.json");
    const resultPath = path.join(directory, "result.json");
    for (const file of [tracePath, resultPath])
    {
        try
        {
            fs.unlinkSync(file);
        }
        catch (error)
        {
            // it was not there
        }
    }

    let bytecode = program;
    let executableLines = new Map();
    let modules = [];
    let sourcePathOfIndex = () => program;

    if (!program.endsWith(".thc"))
    {
        const modulePaths = (configuration.modulePaths || []).concat(services.modulePathsForFile(program));
        const output = path.join(directory, path.basename(program, path.extname(program)) + ".thc");
        const outcome = await toolchain.compile(program, {
            language: await services.languageCodeFor(program),
            modulePaths,
            output,
            cwd,
        });
        const text = readFile(program);
        const model = new Model(outcome.report, text, { file: program });
        if (!outcome.report.success)
        {
            return {
                trace: null,
                message: `${path.basename(program)} has ${outcome.report.error_count || 0} error(s)`,
                diagnostics: (outcome.report.diagnostics || []).map((diagnostic) => diagnostic.text),
            };
        }
        bytecode = output;
        executableLines = model.executableLines();
        modules = model.importedFiles.map((module) => ({ name: module.name, path: module.path }));
        sourcePathOfIndex = (index) => model.pathOfFileIndex(index);
    }

    const result = await toolchain.record(bytecode, {
        arguments: configuration.arguments || [],
        maxSteps: configuration.maxSteps || services.setting(program, "maxSteps") || 0,
        nativePaths: services.nativePathsForFile(program),
        traceJson: tracePath,
        traceLimit: configuration.traceLimit || services.setting(program, "trace.limit") || 200000,
        resultJson: resultPath,
        cwd,
        input: configuration.input === undefined ? "" : configuration.input,
    });

    let trace = null;
    try
    {
        trace = JSON.parse(fs.readFileSync(tracePath, "utf8"));
    }
    catch (error)
    {
        return {
            trace: null,
            message: `the program could not be recorded (exit code ${result.code}): `
                + (result.stderr.trim() || error.message),
            diagnostics: result.stderr ? [result.stderr.trim()] : [],
        };
    }

    return {
        trace,
        modules,
        executableLines,
        sourcePathOfIndex,
        diagnostics: [],
        message: "",
    };
}

function readFile(file)
{
    try
    {
        return fs.readFileSync(file, "utf8");
    }
    catch (error)
    {
        return "";
    }
}

module.exports = { prepareDebugSession };
