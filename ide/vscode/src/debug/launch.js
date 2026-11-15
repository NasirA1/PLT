// Getting a program ready to be debugged: compile it, then start it under the live debugger.
//
// `thunderc --json` compiles the program and says which files it is made of (the program and
// the modules it imports, numbered as the machine numbers them); `thundervm --debug` then runs
// the bytecode and waits for the adapter's commands. Starting happens here, outside the adapter,
// so the adapter itself stays a pure translator between the two protocols.

"use strict";

const path = require("path");

const { Debuggee, spawnTransport } = require("./debuggee");
const { Model } = require("../report");
const { formatCommand, readFileOrEmpty } = require("../util");
const { workDirectory } = require("../toolchain");

/**
 * Compiles and starts a program.
 *
 * Returns {debuggee, files, program, language, diagnostics, message}. `debuggee` is null when the program
 * did not compile; `diagnostics` then holds the compiler's own lines, which the adapter prints to
 * the debug console. `files` maps the machine's file indices to absolute paths.
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

    let bytecode = program;
    let files = [program];

    const language = await services.languageCodeFor(program);
    if (!program.endsWith(".thc"))
    {
        const modulePaths = (configuration.modulePaths || []).concat(services.modulePathsForFile(program));
        const output = path.join(directory, path.basename(program, path.extname(program)) + ".thc");
        const outcome = await toolchain.compile(program, {
            language,
            modulePaths,
            output,
            cwd,
        });
        if (!outcome.report.success)
        {
            return {
                debuggee: null,
                files,
                program,
                language,
                message: `${path.basename(program)} has ${outcome.report.error_count || 0} error(s)`,
                diagnostics: (outcome.report.diagnostics || []).map((diagnostic) => diagnostic.text),
            };
        }
        bytecode = output;
        const model = new Model(outcome.report, readFileOrEmpty(program), { file: program });
        files = filesOf(model, program);
    }

    const command = toolchain.debugCommand(bytecode, {
        arguments: configuration.arguments || [],
        history: configuration.history === undefined ? services.setting(program, "debug.history") : configuration.history,
        maxSteps: configuration.maxSteps || services.setting(program, "maxSteps") || 0,
        display: configuration.display === undefined ? services.setting(program, "display") : configuration.display,
        nativePaths: services.nativePathsForFile(program),
    });
    services.write(formatCommand(command));
    const transport = spawnTransport(command, {
        cwd,
        onStderr: (text) => services.write(text.trimEnd()),
        onError: (error) => services.write(`${command[0]} could not be started: ${error.message}`),
    });
    return {
        debuggee: new Debuggee(transport),
        files,
        program,
        language,
        diagnostics: [],
        message: "",
    };
}

/** The program's files by the machine's index: the program itself, then its modules. */
function filesOf(model, program)
{
    const files = [program];
    for (const module of model.importedFiles)
    {
        files[module.index] = model.pathOfFileIndex(module.index);
    }
    for (let index = 0; index < files.length; ++index)
    {
        files[index] = files[index] || program;
    }
    return files;
}

module.exports = { filesOf, prepareDebugSession };
