// The three views of the Thunder side bar.

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

const { ROOT, assert, before, requireToolchain, suite, test, toolchainDirectory } = require("./harness");
const vscode = require("./fake-vscode");

const { PipelineView, ProjectView, SamplesView, documentsIn } = require("../src/views");
const { Services } = require("../src/services");

suite("views: the side bar", () =>
{
    let services = null;
    let directory = null;

    before(() =>
    {
        requireToolchain();
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-views-"));
        fs.mkdirSync(path.join(directory, "lib"), { recursive: true });
        // The project says Pashto, but this file's own directive says English - and wins.
        fs.writeFileSync(path.join(directory, "main.th"), "// lang: en\nprintln(1)\n", "utf8");
        fs.writeFileSync(path.join(directory, "lib", "helper.th"), "func help(): int\n    return 1\n", "utf8");
        fs.writeFileSync(path.join(directory, "thunder.json"),
            JSON.stringify({ name: "views", entry: "main.th", language: "ps" }), "utf8");
        vscode._setFolders([directory]);
        vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        services = new Services({ extensionPath: path.resolve(__dirname, "..") });
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { services, directory };
    });

    test("the project view shows the toolchain, the project and its programs", async () =>
    {
        const view = new ProjectView(services);
        const roots = await view.getChildren();
        assert.ok(String(roots[0].label).startsWith("Toolchain: ready"));
        assert.strictEqual(roots[0].description, toolchainDirectory());
        const folder = roots.find((node) => node.kind === "folder");
        const children = await view.getChildren(folder);
        const labels = children.map((node) => String(node.label));
        assert.ok(labels.includes("views"), "the project's name");
        assert.ok(labels.includes("main.th"), "the entry point");
        assert.ok(labels.some((label) => label.includes("پښتو")), "the project's language");
        const programs = children.find((node) => node.kind === "programs");
        const files = await view.getChildren(programs);
        const names = files.map((node) => String(node.label));
        assert.ok(names.includes("main.th"));
        assert.ok(names.includes("helper.th"), "a module in lib/ is a program of the project too");
        assert.strictEqual(files.find((node) => node.label === "main.th").description, "entry point");
        assert.deepStrictEqual(await view.getChildren(files[0]), []);
    });

    test("the project view says so when there is no toolchain", async () =>
    {
        const empty = new Services({ extensionPath: path.resolve(__dirname, "..") });
        empty.toolchainInstance = null;
        const roots = await new ProjectView(empty).getChildren();
        assert.ok(String(roots[0].label).includes("not found"));
        assert.strictEqual(roots[0].command.command, "thunder.selectToolchain");
    });

    test("the pipeline view lists the phases and the artefacts of the open program", async () =>
    {
        const file = path.join(directory, "main.th");
        const model = await services.analyser.analyse(file, "// lang: en\nprintln(1)\n",
            { version: "1", dirty: false });
        const view = new PipelineView(services);
        view.show(file, model);
        const nodes = await view.getChildren();
        const labels = nodes.map((node) => String(node.label));
        assert.strictEqual(labels[0], "main.th");
        assert.ok(labels.includes("Lexical analysis"));
        assert.ok(labels.includes("Code generation"));
        assert.ok(labels.includes("Tokens"));
        assert.ok(labels.includes("Bytecode"));
        assert.ok(labels.some((label) => label.includes("0 error(s)")),
            "the file's own // lang: directive wins over the project's language");
        const tokens = nodes.find((node) => node.label === "Tokens");
        assert.strictEqual(tokens.command.command, "thunder.showArtefact");
        assert.deepStrictEqual(tokens.command.arguments, ["tokens", file]);
        assert.ok(nodes[1].description.endsWith("ms"), "each phase shows how long it took");
    });

    test("the samples view finds the SDK beside the toolchain", async () =>
    {
        const view = new SamplesView(services);
        const roots = await view.getChildren();
        const labels = roots.map((node) => String(node.label));
        assert.ok(labels.some((label) => label.startsWith("Samples")), labels.join(", "));
        assert.ok(labels.includes("SDK reference"));
        const samples = await view.getChildren(roots.find((node) => node.kind === "samples"));
        assert.ok(samples.some((node) => String(node.label).startsWith("01_hello")));
        const documents = await view.getChildren(roots.find((node) => node.kind === "docs"));
        assert.ok(documents.some((node) => node.label === "language.md"));
        assert.strictEqual(documents[0].command.command, "markdown.showPreview");
    });

    test("the reference documents of a directory are listed in order", () =>
    {
        const documents = documentsIn(path.join(ROOT, "docs"));
        assert.ok(documents.length >= 8);
        assert.deepStrictEqual(documents, documents.slice().sort());
        assert.deepStrictEqual(documentsIn(path.join(ROOT, "does-not-exist")), []);
    });
});
