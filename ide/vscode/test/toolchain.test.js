"use strict";

const path = require("path");

const { absolute, assert, suite, test } = require("./harness");
const { EXE_SUFFIX } = require("../src/util");
const toolchain = require("../src/toolchain");

function fakeExists(files)
{
    const wanted = new Set(files.map((file) => path.normalize(file)));
    return (candidate) => wanted.has(path.normalize(candidate));
}

function tools(directory)
{
    return [path.join(directory, "thunderc" + EXE_SUFFIX), path.join(directory, "thundervm" + EXE_SUFFIX)];
}

suite("toolchain: finding the tools", () =>
{
    test("the setting wins over everything else", () =>
    {
        const chosen = absolute("opt", "thunder");
        const found = toolchain.findToolchain({
            configured: chosen,
            environment: absolute("env"),
            folders: [absolute("work")],
            exists: fakeExists([...tools(chosen), ...tools(absolute("env"))]),
            pathEntries: [],
        });
        assert.strictEqual(found.directory, chosen);
    });

    test("THUNDER_BIN comes before the build directories", () =>
    {
        const environment = absolute("env");
        const folder = absolute("work");
        const found = toolchain.findToolchain({
            environment,
            folders: [folder],
            exists: fakeExists([...tools(environment), ...tools(path.join(folder, "build", "release", "bin"))]),
            pathEntries: [],
        });
        assert.strictEqual(found.directory, environment);
    });

    test("release comes before dev and coverage inside a workspace folder", () =>
    {
        const folder = absolute("work");
        const release = path.join(folder, "build", "release", "bin");
        const coverage = path.join(folder, "build", "coverage", "bin");
        const found = toolchain.findToolchain({
            folders: [folder],
            exists: fakeExists([...tools(release), ...tools(coverage)]),
            pathEntries: [],
        });
        assert.strictEqual(found.directory, release);
    });

    test("a project inside the repository finds the repository's build", () =>
    {
        const repository = absolute("src", "thunder");
        const project = path.join(repository, "examples", "mine");
        const build = path.join(repository, "build", "release", "bin");
        const found = toolchain.findToolchain({
            folders: [project],
            exists: fakeExists(tools(build)),
            pathEntries: [],
        });
        assert.strictEqual(found.directory, build);
    });

    test("PATH is the last resort, and what was searched is reported", () =>
    {
        const onPath = absolute("usr", "local", "bin");
        const found = toolchain.findToolchain({
            folders: [absolute("work")],
            exists: fakeExists(tools(onPath)),
            pathEntries: [onPath],
        });
        assert.strictEqual(found.directory, onPath);

        const missing = toolchain.findToolchain({
            folders: [absolute("work")],
            exists: () => false,
            pathEntries: [onPath],
        });
        assert.strictEqual(missing.directory, null);
        assert.ok(missing.searched.some((entry) => entry.includes("release")));
    });

    test("one of the tools alone is not a toolchain", () =>
    {
        const directory = absolute("half");
        const found = toolchain.findToolchain({
            configured: directory,
            exists: fakeExists([path.join(directory, "thunderc" + EXE_SUFFIX)]),
            pathEntries: [],
        });
        assert.strictEqual(found.directory, null);
    });
});

suite("toolchain: command lines", () =>
{
    test("a compile asks for the JSON report and names the output", () =>
    {
        const command = toolchain.compileCommand("thunderc", {
            source: "main.th",
            output: "build/main.thc",
            json: true,
            language: "ps",
            modulePaths: ["lib", "shared"],
            quiet: true,
        });
        assert.deepStrictEqual(command, [
            "thunderc", "--lang", "ps", "--json",
            "--module-path", "lib", "--module-path", "shared",
            "-o", "build/main.thc", "--quiet", "main.th",
        ]);
    });

    test("--no-output replaces -o, so a check never overwrites a good .thc", () =>
    {
        const command = toolchain.compileCommand("thunderc", {
            source: "main.th", output: "main.thc", noOutput: true,
        });
        assert.ok(command.includes("--no-output"));
        assert.ok(!command.includes("-o"));
    });

    test("packaging passes the display plug-in and translating the target language", () =>
    {
        const packaged = toolchain.compileCommand("thunderc", {
            source: "game.th", package: "game", display: "sdl",
        });
        assert.deepStrictEqual(packaged.slice(1), ["--package", "game", "--display", "sdl", "game.th"]);
        const translated = toolchain.compileCommand("thunderc", { source: "a.th", language: "en", translateTo: "ps" });
        assert.deepStrictEqual(translated.slice(1), ["--lang", "en", "--translate", "ps", "a.th"]);
    });

    test("a run puts the program's own arguments last", () =>
    {
        const command = toolchain.runCommand("thundervm", {
            bytecode: "main.thc",
            arguments: ["one", "two"],
            maxSteps: 1000,
            display: "sdl",
            nativePaths: ["libs"],
            traceJson: "trace.json",
            traceLimit: 50,
            resultJson: "result.json",
        });
        assert.deepStrictEqual(command, [
            "thundervm", "--max-steps", "1000", "--display", "sdl", "--native-path", "libs",
            "--trace-json", "trace.json", "--trace-limit", "50", "--result-json", "result.json",
            "main.thc", "one", "two",
        ]);
    });

    test("a plain run has no options at all", () =>
    {
        assert.deepStrictEqual(toolchain.runCommand("thundervm", { bytecode: "a.thc" }), ["thundervm", "a.thc"]);
    });
});

suite("tasks: one terminal, two commands", () =>
{
    const { commandLine, quoteArgument } = require("../src/tasks");

    test("on a POSIX shell the run follows the compile only if it worked", () =>
    {
        const line = commandLine([["thunderc", "-o", "a.thc", "a.th"], ["thundervm", "a.thc"]], "linux");
        assert.strictEqual(line, "thunderc -o a.thc a.th && thundervm a.thc");
    });

    test("on PowerShell it is wrapped in if ($?)", () =>
    {
        const line = commandLine([["thunderc", "a.th"], ["thundervm", "a.thc"]], "win32");
        assert.strictEqual(line, "thunderc a.th; if ($?) { thundervm a.thc }");
        const three = commandLine([["a"], ["b"], ["c"]], "win32");
        assert.strictEqual(three, "a; if ($?) { b; if ($?) { c } }");
    });

    test("a path with spaces is quoted for the shell it will run in", () =>
    {
        assert.strictEqual(quoteArgument("C:\\Program Files\\thunderc.exe", true),
            "'C:\\Program Files\\thunderc.exe'");
        assert.strictEqual(quoteArgument("/home/a b/thunderc", false), "'/home/a b/thunderc'");
        assert.strictEqual(quoteArgument("plain", false), "plain");
        // A quote inside the text is doubled for PowerShell and escaped for POSIX shells.
        assert.strictEqual(quoteArgument("it's", true), "'it''s'");
        assert.strictEqual(quoteArgument("it's", false), "'it'\\''s'");
    });
});

suite("tasks: the arguments a program is run with", () =>
{
    const { TASK_TYPE, TaskProvider, argumentsFor, commandLine } = require("../src/tasks");

    test("the task's arguments win, then the project's, then the setting's", () =>
    {
        const configuration = { get: (key) => (key === "programArguments" ? ["from-settings"] : undefined) };
        assert.deepStrictEqual(
            argumentsFor({ arguments: ["from-task"] }, { arguments: ["from-project"] }, configuration),
            ["from-task"]);
        assert.deepStrictEqual(argumentsFor({}, { arguments: ["from-project"] }, configuration), ["from-project"]);
        assert.deepStrictEqual(argumentsFor({}, { arguments: [] }, configuration), ["from-settings"]);
        assert.deepStrictEqual(argumentsFor({}, null, { get: () => undefined }), []);
    });

    test("the task module still exports what the extension uses", () =>
    {
        assert.strictEqual(typeof TaskProvider, "function");
        assert.strictEqual(typeof commandLine, "function");
        assert.strictEqual(TASK_TYPE, "thunder");
    });
});
