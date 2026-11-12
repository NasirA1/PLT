// Finding and driving the Thunder command-line tools.
//
// The extension never parses Thunder itself: it runs `thunderc --json` and reads the report,
// and runs `thundervm` to execute the bytecode - exactly what the Qt IDE does
// (ide/thunder_ide/core/toolchain.py), and exactly what a student would type in a terminal.
// Every command the extension runs is written to the build log, so it can be repeated by hand.

"use strict";

const childProcess = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

const { EXE_SUFFIX, bytecodePath, formatCommand } = require("./util");

// The build directories the repository's own scripts use, in the order the Qt IDE searches them.
const BUILD_DIRECTORIES = [
    path.join("build", "release", "bin"),
    path.join("build", "dev", "bin"),
    path.join("build", "coverage", "bin"),
];

class ToolchainError extends Error {}

/**
 * Where thunderc and thundervm are.
 *
 * Search order: the `thunder.toolchainPath` setting, the THUNDER_BIN environment variable, the
 * build directories under every workspace folder (and under the repository above them, so that
 * the extension works when it is run from inside a clone of the Thunder repository), then PATH.
 *
 * `options.exists` is only replaced by the tests.
 */
function findToolchain(options)
{
    const settings = options || {};
    const exists = settings.exists || ((candidate) => fs.existsSync(candidate) && fs.statSync(candidate).isFile());
    const folders = settings.folders || [];
    const candidates = [];
    const add = (directory) =>
    {
        if (directory && !candidates.includes(directory))
        {
            candidates.push(directory);
        }
    };

    add(settings.configured);
    add(settings.environment);
    for (const folder of folders)
    {
        for (const relative of BUILD_DIRECTORIES)
        {
            add(path.join(folder, relative));
        }
        // A project folder inside the repository: look upwards for the repository's own build.
        let parent = path.dirname(folder);
        for (let depth = 0; depth < 4 && parent && parent !== path.dirname(parent); ++depth)
        {
            for (const relative of BUILD_DIRECTORIES)
            {
                add(path.join(parent, relative));
            }
            parent = path.dirname(parent);
        }
    }

    for (const directory of candidates)
    {
        if (exists(path.join(directory, "thunderc" + EXE_SUFFIX))
            && exists(path.join(directory, "thundervm" + EXE_SUFFIX)))
        {
            return { directory, searched: candidates };
        }
    }
    // Last resort: the tools may be installed on PATH.
    for (const directory of (settings.pathEntries || searchPath()))
    {
        if (exists(path.join(directory, "thunderc" + EXE_SUFFIX))
            && exists(path.join(directory, "thundervm" + EXE_SUFFIX)))
        {
            return { directory, searched: candidates.concat([directory]) };
        }
    }
    return { directory: null, searched: candidates };
}

function searchPath()
{
    const value = process.env.PATH || "";
    return value.split(path.delimiter).filter(Boolean);
}

/** The absolute path of one of the tools inside a toolchain directory. */
function toolPath(directory, name)
{
    return path.join(directory, name + EXE_SUFFIX);
}

// ---------------------------------------------------------------------- command lines
//
// Every command is built by a pure function, so the tests can check exactly what the extension
// would run without running anything.

/**
 * `thunderc` for a compile.
 *
 * options: {source, output|null, json, emit[], language, modulePaths[], noOutput, translateTo,
 *           package, display, quiet}
 */
function compileCommand(thunderc, options)
{
    const command = [thunderc];
    if (options.language)
    {
        command.push("--lang", options.language);
    }
    if (options.json)
    {
        command.push("--json");
    }
    for (const stage of options.emit || [])
    {
        command.push("--emit", stage);
    }
    for (const directory of options.modulePaths || [])
    {
        command.push("--module-path", directory);
    }
    if (options.translateTo)
    {
        command.push("--translate", options.translateTo);
    }
    if (options.package)
    {
        command.push("--package", options.package);
        if (options.display)
        {
            command.push("--display", options.display);
        }
    }
    if (options.noOutput)
    {
        command.push("--no-output");
    }
    else if (options.output)
    {
        command.push("-o", options.output);
    }
    if (options.quiet)
    {
        command.push("--quiet");
    }
    command.push(options.source);
    return command;
}

/**
 * `thundervm` for a run.
 *
 * options: {bytecode, arguments[], maxSteps, display, nativePaths[], traceJson, traceLimit,
 *           resultJson, svg, seed, disassemble}
 */
function runCommand(thundervm, options)
{
    const command = [thundervm];
    if (options.disassemble)
    {
        command.push("--disassemble");
    }
    if (options.maxSteps)
    {
        command.push("--max-steps", String(options.maxSteps));
    }
    if (options.display)
    {
        command.push("--display", options.display);
    }
    for (const directory of options.nativePaths || [])
    {
        command.push("--native-path", directory);
    }
    if (options.traceJson)
    {
        command.push("--trace-json", options.traceJson);
        command.push("--trace-limit", String(options.traceLimit || 200000));
    }
    if (options.resultJson)
    {
        command.push("--result-json", options.resultJson);
    }
    if (options.svg)
    {
        command.push("--svg", options.svg);
    }
    if (options.seed !== undefined && options.seed !== null)
    {
        command.push("--seed", String(options.seed));
    }
    command.push(options.bytecode);
    for (const argument of options.arguments || [])
    {
        command.push(argument);
    }
    return command;
}

// ---------------------------------------------------------------------- running

/**
 * Runs a command and collects its output.
 *
 * Returns a promise of {code, stdout, stderr, command}. The promise is never rejected for a
 * non-zero exit code - a failed compile is a normal outcome - only when the tool cannot be
 * started at all.
 */
function run(command, options)
{
    const settings = options || {};
    return new Promise((resolve, reject) =>
    {
        const child = childProcess.spawn(command[0], command.slice(1), {
            cwd: settings.cwd,
            env: settings.env || process.env,
            windowsHide: true,
        });
        let stdout = "";
        let stderr = "";
        let settled = false;
        const timer = settings.timeout
            ? setTimeout(() =>
            {
                child.kill();
            }, settings.timeout)
            : null;
        child.stdout.setEncoding("utf8");
        child.stderr.setEncoding("utf8");
        child.stdout.on("data", (text) => { stdout += text; });
        child.stderr.on("data", (text) => { stderr += text; });
        if (settings.input !== undefined && settings.input !== null)
        {
            child.stdin.end(settings.input, "utf8");
        }
        else
        {
            child.stdin.end();
        }
        child.on("error", (error) =>
        {
            if (timer)
            {
                clearTimeout(timer);
            }
            if (!settled)
            {
                settled = true;
                reject(new ToolchainError(`${command[0]} could not be started: ${error.message}`));
            }
        });
        child.on("close", (code) =>
        {
            if (timer)
            {
                clearTimeout(timer);
            }
            if (!settled)
            {
                settled = true;
                resolve({ code: code === null ? -1 : code, stdout, stderr, command });
            }
        });
        if (settings.cancellation)
        {
            settings.cancellation.onCancellationRequested(() => child.kill());
        }
    });
}

/** A scratch directory of this extension's own, for unsaved buffers and debug recordings. */
function workDirectory(name)
{
    const directory = path.join(os.tmpdir(), "thunder-vscode", name || "work");
    fs.mkdirSync(directory, { recursive: true });
    return directory;
}

/**
 * The Thunder toolchain: the two tools, and the operations the extension performs with them.
 */
class Toolchain
{
    constructor(directory, log)
    {
        this.directory = directory;
        this.log = log || (() => {});
        this.thunderc = toolPath(directory, "thunderc");
        this.thundervm = toolPath(directory, "thundervm");
        this._languageInfo = new Map();
    }

    /** Runs a command, writing it to the build log first. */
    async execute(command, options)
    {
        this.log(formatCommand(command));
        const result = await run(command, options);
        if (result.code !== 0 && result.stderr && (options || {}).logErrors !== false)
        {
            this.log(result.stderr.trimEnd());
        }
        return result;
    }

    /** `thunderc --language-info`: every keyword, built-in, module and opcode of a language. */
    async languageInfo(language)
    {
        const code = language || "en";
        if (this._languageInfo.has(code))
        {
            return this._languageInfo.get(code);
        }
        const result = await run([this.thunderc, "--language-info", "--lang", code], { timeout: 20000 });
        if (result.code !== 0)
        {
            throw new ToolchainError(`thunderc --language-info failed: ${result.stderr.trim()}`);
        }
        const info = JSON.parse(result.stdout);
        this._languageInfo.set(code, info);
        return info;
    }

    /**
     * Compiles a file and returns the JSON report of every phase.
     *
     * `options.output` names the bytecode file; with `options.noOutput` nothing is written
     * (used while typing, so that a check never overwrites a good .thc).
     */
    async compile(source, options)
    {
        const settings = options || {};
        const command = compileCommand(this.thunderc, {
            source,
            json: true,
            language: settings.language,
            modulePaths: settings.modulePaths,
            noOutput: settings.noOutput,
            output: settings.output,
            quiet: true,
        });
        const result = await this.execute(command, {
            cwd: settings.cwd,
            timeout: settings.timeout || 60000,
            cancellation: settings.cancellation,
            logErrors: false,
        });
        let report = null;
        try
        {
            report = JSON.parse(result.stdout);
        }
        catch (error)
        {
            throw new ToolchainError(
                `thunderc did not produce a report (exit code ${result.code}): ${result.stderr.trim() || "no output"}`);
        }
        return { report, exitCode: result.code, stderr: result.stderr, command, bytecode: settings.output || null };
    }

    /** Translates a program into another language, as `thunderc --translate` prints it. */
    async translate(source, options)
    {
        const settings = options || {};
        const command = compileCommand(this.thunderc, {
            source,
            language: settings.language,
            translateTo: settings.to,
            modulePaths: settings.modulePaths,
        });
        const result = await this.execute(command, { cwd: settings.cwd, timeout: 30000 });
        return { ok: result.code === 0, text: result.stdout, error: result.stderr.trim(), command };
    }

    /** Writes a native executable that runs the program on its own. */
    async package(source, output, options)
    {
        const settings = options || {};
        const command = compileCommand(this.thunderc, {
            source,
            package: output,
            display: settings.display,
            language: settings.language,
            modulePaths: settings.modulePaths,
        });
        const result = await this.execute(command, { cwd: settings.cwd, timeout: 300000 });
        return {
            ok: result.code === 0,
            output,
            message: (result.stdout + result.stderr).trim(),
            command,
        };
    }

    /** Runs a program, recording every instruction for the debugger. */
    async record(bytecode, options)
    {
        const settings = options || {};
        const command = runCommand(this.thundervm, {
            bytecode,
            arguments: settings.arguments,
            maxSteps: settings.maxSteps,
            nativePaths: settings.nativePaths,
            traceJson: settings.traceJson,
            traceLimit: settings.traceLimit,
            resultJson: settings.resultJson,
        });
        const result = await this.execute(command, {
            cwd: settings.cwd,
            input: settings.input,
            timeout: settings.timeout || 300000,
            logErrors: false,
        });
        return result;
    }
}

module.exports = {
    BUILD_DIRECTORIES,
    Toolchain,
    ToolchainError,
    bytecodePath,
    compileCommand,
    findToolchain,
    run,
    runCommand,
    toolPath,
    workDirectory,
};
