// Publishing the compiler's problems, and the artefact documents ("under the bonnet").

"use strict";

const fs = require("fs");
const path = require("path");

const { assert, before, requireToolchain, suite, temporaryDirectory, test } = require("./harness");
const vscode = require("./fake-vscode");

const { Analyser } = require("../src/analysis");
const { ArtefactProvider, artefactUri } = require("../src/artefacts");
const { DiagnosticsManager, unusedNameHints } = require("../src/diagnostics");
const { Toolchain } = require("../src/toolchain");

function makeServices(directory, toolchain, settings)
{
    const values = Object.assign({ "diagnostics.run": "onType", "diagnostics.delay": 0 }, settings || {});
    const analyser = new Analyser({
        toolchain: () => toolchain,
        settings: () => ({ language: "en", modulePaths: [directory] }),
    });
    const services = {
        analyser,
        toolchain: () => toolchain,
        configuration: () => ({ get: (key, fallback) => (key in values ? values[key] : fallback) }),
        modulePathsFor: () => [directory],
        missingToolchainMessage: () => "no toolchain",
        languageCodeFor: async () => "en",
        sourceFileFor: async (file) => ({ path: file, modulePaths: [directory] }),
        analyse: (document) => analyser.analyse(document.uri.fsPath, document.getText(), {
            version: `${document.version}`,
            dirty: false,
        }),
    };
    return services;
}

function write(directory, name, text)
{
    const file = path.join(directory, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text, "utf8");
    return vscode._document(file, text);
}

suite("diagnostics: the Problems panel", () =>
{
    let services = null;
    let directory = null;
    let toolchain = null;

    before(() =>
    {
        toolchain = new Toolchain(requireToolchain());
        directory = temporaryDirectory("diagnostics");
        services = makeServices(directory, toolchain);
    });

    test("an error is published against the file it is in", async () =>
    {
        const manager = new DiagnosticsManager(services);
        const document = write(directory, "wrong.th", "var count: int = \"ten\"\n");
        const model = await manager.check(document);
        assert.ok(model);
        const published = [...manager.collection.entries.entries()];
        const own = published.find(([uri]) => uri.includes("wrong.th"));
        assert.ok(own, `nothing was published: ${published.map(([uri]) => uri).join(", ")}`);
        const [, items] = own;
        assert.ok(items.length >= 1);
        assert.strictEqual(items[0].code, "S005");
        assert.strictEqual(items[0].severity, vscode.DiagnosticSeverity.Error);
        assert.ok(items[0].source.includes("semantic"));
        manager.dispose();
    });

    test("a name the program never reads is a hint, not an error", async () =>
    {
        const manager = new DiagnosticsManager(services);
        const document = write(directory, "unused.th", "var used = 1\nvar never_read = 2\nprintln(used)\n");
        await manager.check(document);
        const [, items] = [...manager.collection.entries.entries()].find(([uri]) => uri.includes("unused.th"));
        const hints = items.filter((item) => item.severity === vscode.DiagnosticSeverity.Hint);
        assert.strictEqual(hints.length, 1);
        assert.ok(hints[0].message.includes("never_read"));
        assert.deepStrictEqual(hints[0].tags, [vscode.DiagnosticTag.Unnecessary]);
        manager.dispose();
    });

    test("the hints can be turned off", async () =>
    {
        const quiet = makeServices(directory, toolchain, { "diagnostics.unusedNames": false });
        const manager = new DiagnosticsManager(quiet);
        const document = write(directory, "unused2.th", "var never_read = 2\n");
        await manager.check(document);
        const [, items] = [...manager.collection.entries.entries()].find(([uri]) => uri.includes("unused2.th"));
        assert.deepStrictEqual(items.filter((item) => item.severity === vscode.DiagnosticSeverity.Hint), []);
        manager.dispose();
    });

    test("an error inside a module is published against the module", async () =>
    {
        write(directory, path.join("lib", "wrong_module.th"), "var x: int = \"text\"\n");
        const manager = new DiagnosticsManager(services);
        const document = write(directory, "uses_wrong.th", "import \"lib/wrong_module\"\n");
        await manager.check(document);
        const published = [...manager.collection.entries.entries()];
        const module = published.find(([uri]) => uri.includes("wrong_module.th"));
        assert.ok(module, `published: ${published.map(([uri]) => uri).join(", ")}`);
        assert.ok(module[1].length >= 1);
        assert.ok(module[1][0].relatedInformation, "it says which program was being compiled");
        manager.dispose();
    });

    test("a diagnostic that is gone is withdrawn", async () =>
    {
        const manager = new DiagnosticsManager(services);
        const file = path.join(directory, "fixed.th");
        fs.writeFileSync(file, "var count: int = \"ten\"\n", "utf8");
        await manager.check(vscode._document(file, "var count: int = \"ten\"\n"));
        let items = [...manager.collection.entries.entries()].find(([uri]) => uri.includes("fixed.th"))[1];
        assert.ok(items.length >= 1);
        fs.writeFileSync(file, "var count: int = 10\n", "utf8");
        await manager.check(vscode._document(file, "var count: int = 10\n", { version: 2 }));
        items = [...manager.collection.entries.entries()].find(([uri]) => uri.includes("fixed.th"))[1];
        assert.deepStrictEqual(items.filter((item) => item.severity === vscode.DiagnosticSeverity.Error), []);
        manager.dispose();
    });

    test("with diagnostics turned off nothing is published", async () =>
    {
        const off = makeServices(directory, toolchain, { "diagnostics.run": "off" });
        const manager = new DiagnosticsManager(off);
        const document = write(directory, "quiet.th", "var count: int = \"ten\"\n");
        manager.schedule(document);
        // Deterministic, where a wait would not be: with the setting off, schedule() must not
        // even have started a timer, so nothing can be published later either.
        assert.strictEqual(manager.timers.size, 0, "no check may be pending");
        assert.strictEqual(manager.collection.entries.size, 0);
        manager.dispose();
    });

    test("unusedNameHints reads the compiler's own flag", async () =>
    {
        const model = await services.analyser.analyse(
            path.join(directory, "flags.th"), "", { version: "x", dirty: false, force: true });
        assert.ok(model === null || model, "the analyser answered");
        const document = write(directory, "flags.th", "func f(unused_parameter: int): int\n    return 1\n");
        const compiled = await services.analyse(document);
        const hints = unusedNameHints(compiled);
        assert.ok(hints.some((hint) => hint.message.includes("unused_parameter")));
    });
});

suite("artefacts: under the bonnet", () =>
{
    let services = null;
    let directory = null;

    before(() =>
    {
        const toolchain = new Toolchain(requireToolchain());
        directory = temporaryDirectory("artefacts");
        services = makeServices(directory, toolchain);
    });

    test("every phase can be shown as a document", async () =>
    {
        const provider = new ArtefactProvider(services);
        const file = path.join(directory, "show.th");
        fs.writeFileSync(file, "var x = 40 + 2\nprintln(x)\n", "utf8");
        const tokens = await provider.provideTextDocumentContent(artefactUri("tokens", file));
        assert.ok(tokens.includes("== tokens =="));
        assert.ok(tokens.includes("IntLiteral") || tokens.includes("40"));
        const ast = await provider.provideTextDocumentContent(artefactUri("ast", file));
        assert.ok(ast.includes("Program"));
        const symbols = await provider.provideTextDocumentContent(artefactUri("symbols", file));
        assert.ok(symbols.includes("x"));
        const assembly = await provider.provideTextDocumentContent(artefactUri("asm", file));
        assert.ok(assembly.includes("CONST") || assembly.includes("ADD"));
        const hex = await provider.provideTextDocumentContent(artefactUri("hex", file));
        assert.ok(/[0-9a-f]{2} [0-9a-f]{2}/.test(hex), "a hex dump");
    });

    test("an unknown artefact says so rather than failing", async () =>
    {
        const provider = new ArtefactProvider(services);
        const text = await provider.provideTextDocumentContent(artefactUri("nonsense", "x.th"));
        assert.ok(text.includes("Unknown artefact"));
    });

    test("the document says which program and phase it belongs to", async () =>
    {
        const provider = new ArtefactProvider(services);
        const file = path.join(directory, "named.th");
        fs.writeFileSync(file, "println(1)\n", "utf8");
        const text = await provider.provideTextDocumentContent(artefactUri("tokens", file));
        assert.ok(text.includes("named.th"));
        assert.ok(text.includes("lexical analysis"));
    });
});
