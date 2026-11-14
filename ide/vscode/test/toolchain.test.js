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
    const { commandLine, quoteArgument, shellKind } = require("../src/tasks");

    test("the shell is read from its path, never assumed from the platform", () =>
    {
        // The bug this pins: Windows with Git Bash as the default profile got PowerShell
        // syntax, and `if ($?) {` is a bash syntax error.
        assert.strictEqual(shellKind("C:\\Program Files\\Git\\usr\\bin\\bash.exe", "win32"), "posix");
        assert.strictEqual(shellKind("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", "win32"), "powershell");
        assert.strictEqual(shellKind("C:\\Program Files\\PowerShell\\7\\pwsh.exe", "win32"), "powershell");
        assert.strictEqual(shellKind("C:\\Windows\\System32\\cmd.exe", "win32"), "cmd");
        assert.strictEqual(shellKind("/usr/bin/zsh", "linux"), "posix");
        // Unknown or unset: VS Code's own default per platform.
        assert.strictEqual(shellKind("", "win32"), "powershell");
        assert.strictEqual(shellKind("", "linux"), "posix");
    });

    test("on a POSIX shell the run follows the compile only if it worked", () =>
    {
        const line = commandLine([["thunderc", "-o", "a.thc", "a.th"], ["thundervm", "a.thc"]], "posix");
        assert.strictEqual(line, "thunderc -o a.thc a.th && thundervm a.thc");
    });

    test("on PowerShell it is wrapped in if ($?)", () =>
    {
        const line = commandLine([["thunderc", "a.th"], ["thundervm", "a.thc"]], "powershell");
        assert.strictEqual(line, "thunderc a.th; if ($?) { thundervm a.thc }");
        const three = commandLine([["a"], ["b"], ["c"]], "powershell");
        assert.strictEqual(three, "a; if ($?) { b; if ($?) { c } }");
        // The old spelling of the argument still means PowerShell.
        assert.strictEqual(commandLine([["a"], ["b"]], "win32"), "a; if ($?) { b }");
    });

    test("on cmd the commands are chained with && too", () =>
    {
        const line = commandLine([["thunderc", "a.th"], ["thundervm", "a b.thc"]], "cmd");
        assert.strictEqual(line, 'thunderc a.th && thundervm "a b.thc"');
    });

    test("Git Bash on Windows gets POSIX syntax and quoted backslash paths", () =>
    {
        const line = commandLine([
            ["C:\\PLT\\build\\release\\bin\\thunderc.exe", "-o", "C:\\PLT\\a.thc", "C:\\PLT\\a.th"],
            ["C:\\PLT\\build\\release\\bin\\thundervm.exe", "C:\\PLT\\a.thc"],
        ], "posix");
        // Unquoted, bash would eat the backslashes; there must be no PowerShell in sight.
        assert.strictEqual(line,
            "'C:\\PLT\\build\\release\\bin\\thunderc.exe' -o 'C:\\PLT\\a.thc' 'C:\\PLT\\a.th'"
            + " && 'C:\\PLT\\build\\release\\bin\\thundervm.exe' 'C:\\PLT\\a.thc'");
        assert.ok(!line.includes("$?"));
    });

    test("a path with spaces is quoted for the shell it will run in", () =>
    {
        assert.strictEqual(quoteArgument("C:\\Program Files\\thunderc.exe", "powershell"),
            "'C:\\Program Files\\thunderc.exe'");
        assert.strictEqual(quoteArgument("/home/a b/thunderc", "posix"), "'/home/a b/thunderc'");
        assert.strictEqual(quoteArgument("plain", "posix"), "plain");
        // A quote inside the text is doubled for PowerShell and cmd, escaped for POSIX shells.
        assert.strictEqual(quoteArgument("it's", "powershell"), "'it''s'");
        assert.strictEqual(quoteArgument("it's", true), "'it''s'"); // the old spelling
        assert.strictEqual(quoteArgument("it's", "posix"), "'it'\\''s'");
        assert.strictEqual(quoteArgument('say "hi"', "cmd"), '"say ""hi"""');
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
