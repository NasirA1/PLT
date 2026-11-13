// Activating the extension, and checking that what package.json promises is what the code
// delivers: every command registered, every view filled, every task built.

"use strict";

const fs = require("fs");
const path = require("path");

const { EXTENSION, ROOT, assert, before, suite, test, toolchainDirectory } = require("./harness");
const vscode = require("./fake-vscode");

const manifest = JSON.parse(fs.readFileSync(path.join(EXTENSION, "package.json"), "utf8"));

function fakeContext()
{
    return { subscriptions: [], extensionPath: EXTENSION, globalState: new Map(), workspaceState: new Map() };
}

suite("extension: activation", () =>
{
    let api = null;
    let context = null;
    let directory = null;

    before(() =>
    {
        vscode._reset();
        directory = fs.mkdtempSync(path.join(require("os").tmpdir(), "thunder-vscode-activate-"));
        fs.writeFileSync(path.join(directory, "main.th"), "println(\"hello\")\n", "utf8");
        fs.writeFileSync(path.join(directory, "thunder.json"),
            JSON.stringify({ name: "demo", entry: "main.th", modulePaths: ["lib"] }), "utf8");
        vscode._setFolders([directory]);
        if (toolchainDirectory())
        {
            vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        }
        context = fakeContext();
        api = require("../src/extension").activate(context);
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { api, context, directory };
    });

    test("every command in package.json is registered", () =>
    {
        const promised = manifest.contributes.commands.map((command) => command.command);
        const registered = new Set(vscode._state.registered.commands);
        const missing = promised.filter((command) => !registered.has(command));
        assert.deepStrictEqual(missing, [], `not registered: ${missing.join(", ")}`);
    });

    test("no command is registered twice", () =>
    {
        const registered = vscode._state.registered.commands;
        const duplicates = registered.filter((command, index) => registered.indexOf(command) !== index);
        assert.deepStrictEqual(duplicates, []);
    });

    test("every language feature is provided", () =>
    {
        const kinds = new Set(vscode._state.registered.providers.map((entry) => entry.kind));
        for (const kind of ["completion", "signature", "hover", "definition", "reference", "rename",
            "documentSymbol", "workspaceSymbol", "inlayHints", "semanticTokens", "documentHighlight",
            "formatting", "codeActions", "content"])
        {
            assert.ok(kinds.has(kind), `${kind} was not registered`);
        }
    });

    test("the three views of the side bar are filled", () =>
    {
        const ids = vscode._state.registered.views.map((entry) => entry.id);
        assert.deepStrictEqual(ids.sort(), ["thunderPipeline", "thunderProject", "thunderSamples"]);
        for (const view of manifest.contributes.views.thunder)
        {
            assert.ok(ids.includes(view.id), `${view.id} has no provider`);
        }
    });

    test("the debugger and the task provider are registered under the names in package.json", () =>
    {
        assert.ok(vscode._state.registered.tasks.some((entry) => entry.type === manifest
            .contributes.taskDefinitions[0].type));
        const kinds = vscode._state.registered.debug.map((entry) => entry.kind);
        assert.ok(kinds.includes("configuration"));
        assert.ok(kinds.includes("factory"));
    });

    test("the project view shows the project and its programs", async () =>
    {
        const view = vscode._state.registered.views.find((entry) => entry.id === "thunderProject").provider;
        const roots = await view.getChildren();
        assert.ok(roots.length >= 2, "the toolchain and the folder");
        const folder = roots.find((node) => node.kind === "folder");
        assert.ok(folder, "the workspace folder is listed");
        const children = await view.getChildren(folder);
        const labels = children.map((node) => String(node.label));
        assert.ok(labels.includes("demo"), "the project's name");
        assert.ok(labels.includes("main.th"), "the entry point");
        const programs = children.find((node) => node.kind === "programs");
        assert.ok(programs);
        const files = await view.getChildren(programs);
        assert.ok(files.some((node) => node.label === "main.th"));
    });

    test("the pipeline view asks for a program when none is open", async () =>
    {
        const view = vscode._state.registered.views.find((entry) => entry.id === "thunderPipeline").provider;
        const nodes = await view.getChildren();
        assert.strictEqual(nodes.length, 1);
        assert.ok(String(nodes[0].label).includes("Open a Thunder program"));
    });

    test("the extension exposes the model of a document", async function exposes()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const document = vscode._openDocument(path.join(directory, "main.th"), "println(\"hello\")\n");
        const model = await api.analyse(document);
        assert.ok(model, "the document was not compiled");
        assert.ok(model.succeeded);
        assert.deepStrictEqual(model.diagnostics(), []);
    });

    test("a build task runs the real commands, in the right order", async function builds()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const provider = vscode._state.registered.tasks.find((entry) => entry.type === "thunder").provider;
        const folder = vscode._state.workspaceFolders[0];
        const build = await provider.createTask({ type: "thunder", task: "build", file: path.join(directory, "main.th") },
            folder);
        assert.ok(build.execution.commandLine.includes("thunderc"));
        assert.ok(build.execution.commandLine.includes("main.th"));
        assert.ok(build.execution.commandLine.includes("-o"));
        assert.deepStrictEqual(build.problemMatchers, ["$thunderc"]);

        const run = await provider.createTask({ type: "thunder", task: "run", file: path.join(directory, "main.th") },
            folder);
        const line = run.execution.commandLine;
        assert.ok(line.includes("thunderc"), "it compiles first");
        assert.ok(line.includes("thundervm"), "and then runs");
        assert.ok(line.indexOf("thunderc") < line.indexOf("thundervm"));

        const packaged = await provider.createTask(
            { type: "thunder", task: "package", file: path.join(directory, "main.th") }, folder);
        assert.ok(packaged.execution.commandLine.includes("--package"));

        const check = await provider.createTask(
            { type: "thunder", task: "check", file: path.join(directory, "main.th") }, folder);
        assert.ok(check.execution.commandLine.includes("--no-output"));
    });

    test("debugging the current file is offered without a launch.json", async () =>
    {
        const entry = vscode._state.registered.debug.find((candidate) => candidate.kind === "configuration");
        vscode._openDocument(path.join(directory, "main.th"), "println(\"hello\")\n");
        const resolved = await entry.provider.resolveDebugConfiguration(vscode._state.workspaceFolders[0], {});
        assert.strictEqual(resolved.type, "thunder");
        assert.strictEqual(resolved.request, "launch");
        assert.ok(resolved.program.endsWith("main.th"));
    });

    test("the extension cleans up after itself", () =>
    {
        assert.ok(context.subscriptions.length > 10, "everything registered is disposable");
        for (const subscription of context.subscriptions)
        {
            assert.ok(typeof subscription.dispose === "function", "a subscription without dispose()");
        }
    });
});

suite("extension: the manifest and the repository agree", () =>
{
    test("the files package.json points at are there", () =>
    {
        const files = [
            manifest.main,
            manifest.contributes.languages[0].configuration,
            manifest.contributes.grammars[0].path,
            manifest.contributes.snippets[0].path,
            manifest.contributes.languages[0].icon.dark,
            manifest.contributes.viewsContainers.activitybar[0].icon,
            manifest.contributes.jsonValidation[0].url,
        ];
        for (const file of files)
        {
            assert.ok(fs.existsSync(path.join(EXTENSION, file)), `${file} is missing`);
        }
    });

    test("every setting it offers is read somewhere", () =>
    {
        // A setting in package.json that nothing reads is a promise the extension does not keep.
        const sources = [];
        const walk = (directory) =>
        {
            for (const entry of fs.readdirSync(directory, { withFileTypes: true }))
            {
                const full = path.join(directory, entry.name);
                if (entry.isDirectory())
                {
                    walk(full);
                }
                else if (entry.name.endsWith(".js"))
                {
                    sources.push(fs.readFileSync(full, "utf8"));
                }
            }
        };
        walk(path.join(EXTENSION, "src"));
        const code = sources.join("\n");
        for (const key of Object.keys(manifest.contributes.configuration.properties))
        {
            const name = key.replace(/^thunder\./, "");
            assert.ok(code.includes(`"${name}"`), `nothing reads the setting ${key}`);
        }
    });

    test("the extension has no dependencies, so there is nothing to install", () =>
    {
        assert.ok(!manifest.dependencies, "a dependency would need npm install");
        assert.ok(!manifest.devDependencies);
        assert.ok(!fs.existsSync(path.join(EXTENSION, "node_modules")));
    });

    test("the version follows the toolchain's", () =>
    {
        // The extension may have patch releases of its own (an installer fix needs no new
        // compiler), but its major.minor says which toolchain it was built against.
        const cmake = fs.readFileSync(path.join(ROOT, "CMakeLists.txt"), "utf8");
        const match = /project\s*\(\s*\w+[\s\S]*?VERSION\s+([0-9.]+)/.exec(cmake);
        assert.ok(match, "the project version was not found in CMakeLists.txt");
        const toolchain = match[1].split(".");
        const extension = manifest.version.split(".");
        assert.deepStrictEqual(extension.slice(0, 2), toolchain.slice(0, 2));
        assert.ok(Number(extension[2]) >= Number(toolchain[2]), "the patch version may only move forward");
    });

    test("the project templates are shipped", () =>
    {
        for (const language of ["en", "ps"])
        {
            const main = path.join(EXTENSION, "templates", language, "main.th");
            assert.ok(fs.existsSync(main));
            assert.ok(fs.existsSync(path.join(EXTENSION, "templates", language, "lib", "greeting.th")));
        }
    });
});

suite("extension: a Pashto program in view", () =>
{
    let directory = null;

    before(() =>
    {
        vscode._reset();
        directory = fs.mkdtempSync(path.join(require("os").tmpdir(), "thunder-vscode-rtl-auto-"));
        vscode._setFolders([directory]);
        if (toolchainDirectory())
        {
            vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        }
        require("../src/extension").activate({
            subscriptions: [],
            extensionPath: EXTENSION,
        });
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { directory };
    });

    test("the status bar names the language the program is written in", async () =>
    {
        const pashto = path.join(directory, "pashto.th");
        fs.writeFileSync(pashto, "// ژبه: ps\nکرښه چاپ(\"سلام\")\n", "utf8");
        const document = vscode._openDocument(pashto, fs.readFileSync(pashto, "utf8"));
        await vscode._fire("changeEditor", { document, selection: new vscode.Selection(
            new vscode.Position(0, 0), new vscode.Position(0, 0)) });

        const status = vscode._state.statusBarItems[0];
        assert.ok(status, "no status bar item was created");
        assert.ok(status.visible);
        assert.ok(status.text.includes("پښتو"), status.text);
        assert.ok(status.tooltip.includes("right-to-left"), status.tooltip);
        assert.strictEqual(status.command, "thunder.showRightToLeft");
    });

    test("the right-to-left view opens by itself for a Pashto program", async () =>
    {
        const pashto = path.join(directory, "auto.th");
        fs.writeFileSync(pashto, "// ژبه: ps\nمتغیر ن = ۱\n", "utf8");
        const document = vscode._openDocument(pashto, fs.readFileSync(pashto, "utf8"));
        await vscode._fire("changeEditor", { document, selection: new vscode.Selection(
            new vscode.Position(0, 0), new vscode.Position(0, 0)) });
        const panel = vscode._lastWebview();
        assert.ok(panel, "the view did not open");
        assert.ok(panel.webview.html.includes("dir=\"rtl\""));
        assert.strictEqual(panel.showOptions.preserveFocus, true, "it must not steal the keyboard");
    });

    test("an English program gets the status bar but no panel", async () =>
    {
        vscode._reset();
        vscode._setFolders([directory]);
        if (toolchainDirectory())
        {
            vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        }
        require("../src/extension").activate({ subscriptions: [], extensionPath: EXTENSION });
        const english = path.join(directory, "english.th");
        fs.writeFileSync(english, "println(1)\n", "utf8");
        const document = vscode._openDocument(english, "println(1)\n");
        await vscode._fire("changeEditor", { document, selection: new vscode.Selection(
            new vscode.Position(0, 0), new vscode.Position(0, 0)) });
        assert.ok(vscode._state.statusBarItems.some((item) => item.visible && item.text.includes("English")));
        assert.strictEqual(vscode._lastWebview(), null, "no view for a left-to-right program");
    });

    test("the automatic view can be turned off", async () =>
    {
        vscode._reset();
        vscode._setFolders([directory]);
        vscode._state.configuration["thunder.rightToLeft.autoOpen"] = false;
        if (toolchainDirectory())
        {
            vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        }
        require("../src/extension").activate({ subscriptions: [], extensionPath: EXTENSION });
        const pashto = path.join(directory, "quiet.th");
        fs.writeFileSync(pashto, "// ژبه: ps\nمتغیر ن = ۱\n", "utf8");
        const document = vscode._openDocument(pashto, fs.readFileSync(pashto, "utf8"));
        await vscode._fire("changeEditor", { document, selection: new vscode.Selection(
            new vscode.Position(0, 0), new vscode.Position(0, 0)) });
        assert.strictEqual(vscode._lastWebview(), null, "nothing should open");
        assert.ok(vscode._state.statusBarItems.some((item) => item.visible),
            "but the status bar still says which language it is");
    });
});

suite("extension: the right-to-left view follows the editor", () =>
{
    let directory = null;

    before(() =>
    {
        vscode._reset();
        directory = fs.mkdtempSync(path.join(require("os").tmpdir(), "thunder-vscode-follow-"));
        vscode._setFolders([directory]);
        if (toolchainDirectory())
        {
            vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        }
        require("../src/extension").activate({ subscriptions: [], extensionPath: EXTENSION });
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { directory };
    });

    test("an open view switches to whichever program is in front of the user", async () =>
    {
        const first = path.join(directory, "first.th");
        const second = path.join(directory, "second.th");
        fs.writeFileSync(first, "// ژبه: ps\nمتغیر لومړی = ۱\n", "utf8");
        fs.writeFileSync(second, "// ژبه: ps\nمتغیر دویم = ۲\n", "utf8");
        const at = (file) => ({
            document: vscode._openDocument(file, fs.readFileSync(file, "utf8")),
            selection: new vscode.Selection(new vscode.Position(0, 0), new vscode.Position(0, 0)),
        });

        await vscode._fire("changeEditor", at(first));
        const panel = vscode._lastWebview();
        assert.ok(panel.webview.html.includes("لومړی"));

        await vscode._fire("changeEditor", at(second));
        assert.ok(panel.webview.html.includes("دویم"), "the view follows");
        assert.ok(!panel.webview.html.includes("لومړی"));
        assert.strictEqual(vscode._state.webviews.length, 1, "and it is still the one panel");
    });
});
