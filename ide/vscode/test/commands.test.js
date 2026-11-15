// The commands, run the way a user runs them: through the command palette.

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

const { assert, before, requireToolchain, suite, test, toolchainDirectory, removeOnExit } = require("./harness");
const vscode = require("./fake-vscode");

const { splitArguments } = require("../src/commands");

suite("commands: arguments", () =>
{
    test("a command line is split the way a shell would split it", () =>
    {
        assert.deepStrictEqual(splitArguments("one two"), ["one", "two"]);
        assert.deepStrictEqual(splitArguments("\"two words\" three"), ["two words", "three"]);
        assert.deepStrictEqual(splitArguments("'single quoted' x"), ["single quoted", "x"]);
        assert.deepStrictEqual(splitArguments(""), []);
        assert.deepStrictEqual(splitArguments("   "), []);
    });
});

suite("commands: the project and the toolchain", () =>
{
    let directory = null;

    before(() =>
    {
        requireToolchain();
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-commands-"));
        vscode._setFolders([directory]);
        vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        vscode._state.configuration["thunder.diagnostics.delay"] = 0;
        require("../src/extension").activate({
            subscriptions: [],
            extensionPath: path.resolve(__dirname, ".."),
        });
        removeOnExit(directory);
        return { directory };
    });

    test("New Project writes a program, a module and the VS Code files", async () =>
    {
        await vscode.commands.executeCommand("thunder.newProject");
        for (const file of ["thunder.json", "main.th", path.join("lib", "greeting.th"),
            path.join(".vscode", "tasks.json"), path.join(".vscode", "launch.json"),
            path.join(".vscode", "thunder-project.schema.json")])
        {
            assert.ok(fs.existsSync(path.join(directory, file)), `${file} was not written`);
        }
        const project = JSON.parse(fs.readFileSync(path.join(directory, "thunder.json"), "utf8"));
        assert.strictEqual(project.entry, "main.th");
        assert.strictEqual(project.name, path.basename(directory));
        const tasks = JSON.parse(fs.readFileSync(path.join(directory, ".vscode", "tasks.json"), "utf8"));
        assert.deepStrictEqual(tasks.tasks.map((task) => task.task), ["build", "run", "package"]);
        const launch = JSON.parse(fs.readFileSync(path.join(directory, ".vscode", "launch.json"), "utf8"));
        assert.ok(launch.configurations.every((configuration) => configuration.type === "thunder"));
    });

    test("New Project does not overwrite what is already there", async () =>
    {
        fs.writeFileSync(path.join(directory, "main.th"), "# mine\n", "utf8");
        await vscode.commands.executeCommand("thunder.newProject");
        assert.strictEqual(fs.readFileSync(path.join(directory, "main.th"), "utf8"), "# mine\n");
    });

    test("Set as the Entry Point records it in thunder.json", async () =>
    {
        const other = path.join(directory, "other.th");
        fs.writeFileSync(other, "println(1)\n", "utf8");
        await vscode.commands.executeCommand("thunder.setEntryPoint", vscode.Uri.file(other));
        const project = JSON.parse(fs.readFileSync(path.join(directory, "thunder.json"), "utf8"));
        assert.strictEqual(project.entry, "other.th");
    });

    test("Compile writes the bytecode and says nothing went wrong", async () =>
    {
        const file = path.join(directory, "good.th");
        fs.writeFileSync(file, "println(\"hi\")\n", "utf8");
        vscode._openDocument(file, "println(\"hi\")\n");
        vscode._state.messages = [];
        await vscode.commands.executeCommand("thunder.compile");
        assert.ok(fs.existsSync(path.join(directory, "good.thc")), "the bytecode was not written");
        assert.deepStrictEqual(vscode._state.messages.filter((entry) => entry.kind === "error"), []);
    });

    test("Compile reports the errors of a program that does not compile", async () =>
    {
        const file = path.join(directory, "bad.th");
        const text = "var n: int = \"text\"\n";
        fs.writeFileSync(file, text, "utf8");
        vscode._openDocument(file, text);
        vscode._state.messages = [];
        await vscode.commands.executeCommand("thunder.compile");
        const errors = vscode._state.messages.filter((entry) => entry.kind === "error");
        assert.strictEqual(errors.length, 1);
        assert.ok(errors[0].message.includes("1 error(s)"));
    });

    test("Run starts the build-and-run task", async () =>
    {
        const file = path.join(directory, "good.th");
        vscode._openDocument(file, "println(\"hi\")\n");
        await vscode.commands.executeCommand("thunder.run");
        const executed = vscode._state.registered.tasks.filter((entry) => entry.executed);
        assert.ok(executed.length >= 1, "no task was executed");
        const line = executed[executed.length - 1].executed.execution.commandLine;
        assert.ok(line.includes("thunderc") && line.includes("thundervm"));
    });

    test("Debug starts a debug session on the open file", async () =>
    {
        const file = path.join(directory, "good.th");
        vscode._openDocument(file, "println(\"hi\")\n");
        await vscode.commands.executeCommand("thunder.debug");
        const started = vscode._state.registered.debug.filter((entry) => entry.kind === "started");
        assert.ok(started.length >= 1);
        assert.strictEqual(started[started.length - 1].configuration.type, "thunder");
        assert.ok(started[started.length - 1].configuration.program.endsWith("good.th"));
    });

    test("Translate turns the program in front into the other language, in place", async () =>
    {
        const file = path.join(directory, "translate.th");
        const text = "var total = 1\nprintln(total)\n";
        fs.writeFileSync(file, text, "utf8");
        vscode._openDocument(file, text);
        vscode._state.applied = [];
        const reopened = vscode._state.executed.length;
        assert.strictEqual(await vscode.commands.executeCommand("thunder.translate"), "ps");
        // The document's text is replaced (undoable), and the file itself is not written.
        assert.strictEqual(fs.readFileSync(file, "utf8"), text);
        const replaced = vscode._state.applied.pop().edits[0].newText;
        assert.ok(replaced.includes("متغیر") && replaced.includes("کرښه چاپ"), `not translated: ${replaced}`);
        assert.ok(replaced.includes("total"), "the programmer's names stay");
        // Pashto is written right to left: the right-to-left editor takes over.
        const opened = vscode._state.executed.slice(reopened).find((entry) => entry.name === "vscode.openWith");
        assert.ok(opened && opened.args[1] === "thunder.rightToLeftEditor", "the file moves to the right-to-left editor");
    });

    test("Translate works from the right-to-left editor too, and back to the text editor", async () =>
    {
        const file = path.join(directory, "translate-ps.th");
        const text = "# ژبه: ps\nمتغیر ټول = ۱\nکرښه چاپ(ټول)\n";
        fs.writeFileSync(file, text, "utf8");
        const document = vscode._document(file, text);
        vscode._state.documents.push(document);
        vscode._state.activeEditor = null; // a custom editor is not a TextEditor
        const tab = { input: { uri: document.uri, viewType: "thunder.rightToLeftEditor" } };
        vscode._state.tabGroups.activeTabGroup.activeTab = tab;
        vscode._state.tabGroups.activeTabGroup.tabs = [tab];
        vscode._state.applied = [];
        const before = vscode._state.executed.length;
        assert.strictEqual(await vscode.commands.executeCommand("thunder.translate"), "en");
        const replaced = vscode._state.applied.pop().edits[0].newText;
        assert.ok(replaced.includes("var") && replaced.includes("println"), `not translated: ${replaced}`);
        const opened = vscode._state.executed.slice(before).find((entry) => entry.name === "vscode.openWith");
        assert.ok(opened && opened.args[1] === "default", "English goes back to the text editor");
        vscode._state.tabGroups.activeTabGroup.activeTab = null;
    });

    test("a translation takes the document's line breaks, never doubled", () =>
    {
        const { withLineEndings } = require("../src/commands");
        // thunderc on Windows writes \r\n, and used to keep a Windows source's \r as well: \r\r\n
        // showed as an empty line after every line of code.
        assert.strictEqual(withLineEndings("a\r\r\nb\r\n", false), "a\nb\n");
        assert.strictEqual(withLineEndings("a\r\nb\r\n", false), "a\nb\n");
        assert.strictEqual(withLineEndings("a\nb\n", true), "a\r\nb\r\n", "a CRLF document keeps CRLF");
        assert.strictEqual(withLineEndings("a\nb\n", false), "a\nb\n");
    });

    test("Translate with nothing open says so instead of blaming the toolchain", async () =>
    {
        vscode._state.activeEditor = null;
        vscode._state.tabGroups.activeTabGroup.activeTab = null;
        vscode._state.messages = [];
        assert.strictEqual(await vscode.commands.executeCommand("thunder.translate"), null);
        assert.ok(vscode._state.messages.some((m) => m.message.includes("Open a Thunder program")));
    });

    test("Select the Toolchain Directory refuses a directory without the tools", async () =>
    {
        vscode._state.messages = [];
        const empty = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-empty-"));
        const original = vscode.window.showOpenDialog;
        vscode.window.showOpenDialog = () => Promise.resolve([vscode.Uri.file(empty)]);
        try
        {
            await vscode.commands.executeCommand("thunder.selectToolchain");
        }
        finally
        {
            vscode.window.showOpenDialog = original;
            fs.rmSync(empty, { recursive: true, force: true });
        }
        const errors = vscode._state.messages.filter((entry) => entry.kind === "error");
        assert.strictEqual(errors.length, 1);
        assert.ok(errors[0].message.includes("are not in"));
    });

    test("Refresh does not throw with a project open", async () =>
    {
        await vscode.commands.executeCommand("thunder.refreshViews");
    });
});

suite("commands: saving a picture", () =>
{
    let directory = null;

    before(() =>
    {
        requireToolchain();
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-svg-"));
        vscode._setFolders([directory]);
        vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        require("../src/extension").activate({
            subscriptions: [],
            extensionPath: path.resolve(__dirname, ".."),
        });
        removeOnExit(directory);
        return { directory };
    });

    test("a graphics program's picture is written as an SVG", async () =>
    {
        const file = path.join(directory, "drawing.th");
        fs.writeFileSync(file, [
            "import graphics",
            "clear(\"white\")",
            "circle(50, 50, 20, \"red\")",
            "",
        ].join("\n"), "utf8");
        vscode._openDocument(file, fs.readFileSync(file, "utf8"));
        const target = path.join(directory, "drawing.svg");
        const original = vscode.window.showSaveDialog;
        vscode.window.showSaveDialog = () => Promise.resolve(vscode.Uri.file(target));
        try
        {
            await vscode.commands.executeCommand("thunder.saveSvg");
        }
        finally
        {
            vscode.window.showSaveDialog = original;
        }
        assert.ok(fs.existsSync(target), "no picture was written");
        const svg = fs.readFileSync(target, "utf8");
        assert.ok(svg.includes("<svg"), svg.slice(0, 80));
        assert.ok(/circle|ellipse|path/.test(svg), "the circle is in the picture");
    });

    test("a program that does not compile draws nothing and says why", async () =>
    {
        const file = path.join(directory, "broken_drawing.th");
        fs.writeFileSync(file, "import graphics\ncircle(\"not a number\")\n", "utf8");
        vscode._openDocument(file, fs.readFileSync(file, "utf8"));
        const target = path.join(directory, "broken.svg");
        const original = vscode.window.showSaveDialog;
        vscode.window.showSaveDialog = () => Promise.resolve(vscode.Uri.file(target));
        vscode._state.messages = [];
        try
        {
            await vscode.commands.executeCommand("thunder.saveSvg");
        }
        finally
        {
            vscode.window.showSaveDialog = original;
        }
        assert.ok(!fs.existsSync(target));
        const errors = vscode._state.messages.filter((entry) => entry.kind === "error");
        assert.strictEqual(errors.length, 1);
        assert.ok(errors[0].message.includes("nothing was drawn"));
    });
});
