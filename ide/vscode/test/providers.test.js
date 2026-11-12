// The language providers, driven through the stand-in for the VS Code API with the real
// compiler behind them.

"use strict";

const fs = require("fs");
const path = require("path");

const { EXTENSION, assert, before, requireToolchain, suite, temporaryDirectory, test } = require("./harness");
const vscode = require("./fake-vscode");

const { Analyser } = require("../src/analysis");
const { CodeActionProvider, FormattingProvider, importPosition } = require("../src/providers/edits");
const { CompletionProvider, SignatureHelpProvider, longestReservedName } = require("../src/providers/completion");
const { DocumentHighlightProvider, SemanticTokensProvider, TOKEN_TYPES } = require("../src/providers/highlight");
const { HoverProvider } = require("../src/providers/hover");
const { LanguageData } = require("../src/language");
const { Toolchain } = require("../src/toolchain");
const { reservedWordAt } = require("../src/providers/support");
const navigation = require("../src/providers/navigation");

const SETTINGS = {
    "completion.moduleBuiltins": "whenImported",
    "diagnostics.unusedNames": true,
};

function languageOf(code)
{
    return new LanguageData(
        JSON.parse(fs.readFileSync(path.join(EXTENSION, "data", `language-info.${code}.json`), "utf8")));
}

/** Everything the providers ask of the extension, with the real toolchain behind it. */
function makeServices(directory, toolchain)
{
    const languages = new Map([["en", languageOf("en")], ["ps", languageOf("ps")]]);
    const analyser = new Analyser({
        toolchain: () => toolchain,
        settings: (file) => ({
            language: /^\/\/ (lang|ژبه):\s*ps/.test(fs.existsSync(file) ? fs.readFileSync(file, "utf8") : "")
                ? "ps"
                : "en",
            modulePaths: [directory],
        }),
    });
    return {
        analyser,
        toolchain: () => toolchain,
        languageFor: async (document) => languages.get(/^\/\/ (?:lang|ژبه):\s*ps/.test(document.getText()) ? "ps" : "en"),
        allLanguages: async () => languages,
        configuration: () => ({ get: (key, fallback) => (key in SETTINGS ? SETTINGS[key] : fallback) }),
        modulePathsFor: () => [directory],
        nativePathsFor: () => [],
    };
}

/** Writes a program and returns the document for it (as VS Code would hand it to a provider). */
function program(directory, name, text)
{
    const file = path.join(directory, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text, "utf8");
    return vscode._document(file, text);
}

function position(document, line, text)
{
    const lineText = document.lineAt(line).text;
    const index = text === undefined ? lineText.length : lineText.indexOf(text) + text.length;
    return new vscode.Position(line, index);
}

const SAMPLE = [
    "struct Rectangle",
    "    width: int",
    "    height: int",
    "",
    "func area(r: Rectangle): int",
    "    return r.width * r.height",
    "",
    "var room = Rectangle(4, 3)",
    "const unused_name = 7",
    "println(area(room))",
    "",
].join("\n");

suite("providers: completion, hovers and navigation", () =>
{
    let services = null;
    let directory = null;

    before(() =>
    {
        const toolchain = new Toolchain(requireToolchain());
        directory = temporaryDirectory("providers");
        services = makeServices(directory, toolchain);
    });

    test("completion offers the keywords, the built-ins and the program's own names", async () =>
    {
        const document = program(directory, "complete.th", SAMPLE);
        const provider = new CompletionProvider(services);
        const result = await provider.provideCompletionItems(document, new vscode.Position(9, 0));
        const labels = result.items.map((item) => item.label);
        assert.ok(labels.includes("func"), "a keyword");
        assert.ok(labels.includes("println"), "a built-in");
        assert.ok(labels.includes("area"), "a function of the program");
        assert.ok(labels.includes("Rectangle"), "a struct of the program");
        assert.ok(labels.includes("room"), "a variable in scope");
        assert.ok(!labels.includes("platform"), "os has not been imported");

        const println = result.items.find((item) => item.label === "println");
        assert.strictEqual(println.detail, "println(value, ...)");
        assert.ok(println.insertText.value.startsWith("println("));
    });

    test("after a dot, the fields of the struct are offered", async () =>
    {
        const document = program(directory, "member.th", SAMPLE);
        const provider = new CompletionProvider(services);
        await provider.provideCompletionItems(document, new vscode.Position(9, 0)); // compile it once
        const typing = vscode._document(path.join(directory, "member.th"),
            SAMPLE.replace("    return r.width * r.height", "    return r."));
        const result = await provider.provideCompletionItems(typing, new vscode.Position(5, 13));
        assert.deepStrictEqual(result.items.map((item) => item.label), ["width", "height"]);
        assert.strictEqual(result.items[0].detail, "int");
    });

    test("after import, the modules and the files of the project are offered", async () =>
    {
        program(directory, path.join("lib", "helpers.th"), "func help(): int\n    return 1\n");
        const document = program(directory, "imports.th", "import \n");
        const provider = new CompletionProvider(services);
        const result = await provider.provideCompletionItems(document, new vscode.Position(0, 7));
        const labels = result.items.map((item) => item.label);
        assert.ok(labels.includes("os"));
        assert.ok(labels.includes("graphics"));
        assert.ok(labels.includes("native"));
        assert.ok(labels.some((label) => label.endsWith("lib/helpers")), `files were ${labels.join(", ")}`);
    });

    test("a Pashto program is completed with Pashto words", async () =>
    {
        const text = ["// ژبه: ps", "متغیر ش = ۱", ""].join("\n");
        const document = program(directory, "pashto.th", text);
        const provider = new CompletionProvider(services);
        const result = await provider.provideCompletionItems(document, new vscode.Position(2, 0));
        const labels = result.items.map((item) => item.label);
        assert.ok(labels.includes("کرښه چاپ"), "the Pashto println");
        assert.ok(labels.includes("ترڅو"), "the Pashto while");
        assert.ok(labels.includes("ش"), "the variable the program declared");
        const println = result.items.find((item) => item.label === "کرښه چاپ");
        assert.ok(println.insertText.value.includes("("));
    });

    test("hovering a built-in shows its signature, what it does and its other name", async () =>
    {
        const document = program(directory, "hover.th", SAMPLE);
        const provider = new HoverProvider(services);
        const hover = await provider.provideHover(document, position(document, 9, "print"));
        const text = hover.contents[0].value;
        assert.ok(text.includes("println(value, ...)"));
        assert.ok(text.includes("کرښه چاپ"), "the Pashto name is shown");
    });

    test("hovering a name shows what the compiler made of it", async () =>
    {
        const document = program(directory, "hover2.th", SAMPLE);
        const provider = new HoverProvider(services);
        const variable = await provider.provideHover(document, position(document, 7, "roo"));
        assert.ok(variable.contents[0].value.includes("Rectangle"));
        const struct = await provider.provideHover(document, position(document, 0, "Rectan"));
        assert.ok(struct.contents[0].value.includes("width: int"));
        const func = await provider.provideHover(document, position(document, 4, "are"));
        assert.ok(func.contents[0].value.includes("area(r: Rectangle): int"));
    });

    test("hovering a Pashto keyword shows the English word", async () =>
    {
        const document = program(directory, "hover3.th", ["// ژبه: ps", "که ۱ < ۲", "    تېر", ""].join("\n"));
        const provider = new HoverProvider(services);
        const hover = await provider.provideHover(document, new vscode.Position(1, 1));
        assert.ok(hover.contents[0].value.includes("if"));
    });

    test("signature help names the parameter the caret is in", async () =>
    {
        const document = program(directory, "signature.th", SAMPLE);
        const provider = new SignatureHelpProvider(services);
        await new CompletionProvider(services).provideCompletionItems(document, new vscode.Position(9, 0));
        const help = await provider.provideSignatureHelp(document, position(document, 9, "area("));
        assert.strictEqual(help.signatures[0].label, "area(r: Rectangle): int");
        assert.deepStrictEqual(help.signatures[0].parameters.map((parameter) => parameter.label), ["r: Rectangle"]);
        assert.strictEqual(help.activeParameter, 0);
    });

    test("signature help works while the line is still being typed", async () =>
    {
        const document = program(directory, "typing.th", SAMPLE);
        await new CompletionProvider(services).provideCompletionItems(document, new vscode.Position(9, 0));
        const typing = vscode._document(path.join(directory, "typing.th"), SAMPLE + "println(area(");
        const provider = new SignatureHelpProvider(services);
        const help = await provider.provideSignatureHelp(typing, new vscode.Position(11, 13));
        assert.ok(help, "the call is found by reading the brackets backwards");
        assert.strictEqual(help.signatures[0].label, "area(r: Rectangle): int");
    });

    test("a struct is called like a function, with its fields as parameters", async () =>
    {
        const document = program(directory, "construct.th", SAMPLE);
        await new CompletionProvider(services).provideCompletionItems(document, new vscode.Position(9, 0));
        const provider = new SignatureHelpProvider(services);
        const help = await provider.provideSignatureHelp(document, position(document, 7, "Rectangle("));
        assert.strictEqual(help.signatures[0].label, "Rectangle(width: int, height: int)");
    });

    test("go to definition finds the declaration, in this file or in a module", async () =>
    {
        const document = program(directory, "define.th", SAMPLE);
        const provider = new navigation.DefinitionProvider(services);
        const location = await provider.provideDefinition(document, position(document, 9, "are"));
        assert.strictEqual(path.basename(location.uri.fsPath), "define.th");
        assert.strictEqual(location.range.start.line, 4);
    });

    test("the outline lists the structs, the functions and the variables", async () =>
    {
        const document = program(directory, "outline.th", SAMPLE);
        const provider = new navigation.DocumentSymbolProvider(services);
        const symbols = await provider.provideDocumentSymbols(document);
        assert.deepStrictEqual(symbols.map((symbol) => symbol.name),
            ["Rectangle", "area", "room", "unused_name"]);
        assert.deepStrictEqual(symbols[0].children.map((child) => child.name), ["width", "height"]);
        assert.strictEqual(symbols[1].detail, "(r: Rectangle): int");
    });

    test("renaming changes every use of the name", async () =>
    {
        const document = program(directory, "rename.th", SAMPLE);
        const provider = new navigation.RenameProvider(services);
        const prepared = await provider.prepareRename(document, position(document, 7, "roo"));
        assert.strictEqual(prepared.placeholder, "room");
        const edit = await provider.provideRenameEdits(document, position(document, 7, "roo"), "hall");
        assert.strictEqual(edit.edits.length, 2, "the declaration and the use");
        assert.ok(edit.edits.every((entry) => entry.newText === "hall"));
    });

    test("a keyword cannot be renamed", async () =>
    {
        const document = program(directory, "rename2.th", SAMPLE);
        const provider = new navigation.RenameProvider(services);
        await assert.rejects(() => provider.prepareRename(document, new vscode.Position(0, 1)));
    });

    test("an inferred type is shown as an inlay hint, a written one is not", async () =>
    {
        const text = ["var guessed = 41 + 1", "var written: int = 1", ""].join("\n");
        const document = program(directory, "hints.th", text);
        const provider = new navigation.InlayHintsProvider(services);
        const hints = await provider.provideInlayHints(document,
            new vscode.Range(new vscode.Position(0, 0), new vscode.Position(2, 0)));
        assert.deepStrictEqual(hints.map((hint) => hint.label), [": int"]);
        assert.strictEqual(hints[0].position.line, 0);
    });

    test("every use of a name is highlighted, and only names are", async () =>
    {
        const document = program(directory, "highlight.th", SAMPLE);
        const provider = new DocumentHighlightProvider(services);
        const highlights = await provider.provideDocumentHighlights(document, position(document, 7, "roo"));
        assert.strictEqual(highlights.length, 2);
        const none = await provider.provideDocumentHighlights(document, new vscode.Position(0, 1));
        assert.deepStrictEqual(none, []);
    });

    test("semantic tokens classify what only the compiler knows", async () =>
    {
        const document = program(directory, "semantic.th", SAMPLE);
        const provider = new SemanticTokensProvider(services);
        const tokens = await provider.provideDocumentSemanticTokens(document);
        const byPosition = new Map(tokens.pushed.map(
            (token) => [`${token.line}:${token.character}`, TOKEN_TYPES[token.type]]));
        assert.strictEqual(byPosition.get("0:0"), "keyword", "struct");
        assert.strictEqual(byPosition.get("0:7"), "struct", "the name Rectangle");
        assert.strictEqual(byPosition.get("1:4"), "property", "the field width");
        assert.strictEqual(byPosition.get("4:5"), "function", "the function area");
        assert.strictEqual(byPosition.get("4:10"), "parameter", "the parameter r");
        assert.strictEqual(byPosition.get("9:0"), "builtinFunction", "println");
        assert.strictEqual(byPosition.get("1:11"), "type", "int");
    });

    test("formatting tidies the layout and leaves a tidy file alone", () =>
    {
        const provider = new FormattingProvider(services);
        const messy = vscode._document(path.join(directory, "messy.th"), "var x = 1   \n\n\n\nvar y = 2");
        const edits = provider.provideDocumentFormattingEdits(messy);
        assert.strictEqual(edits.length, 1);
        assert.strictEqual(edits[0].newText, "var x = 1\n\nvar y = 2\n");
        const tidy = vscode._document(path.join(directory, "tidy.th"), "var x = 1\n");
        assert.deepStrictEqual(provider.provideDocumentFormattingEdits(tidy), []);
    });

    test("a quick fix does what the compiler suggested", async () =>
    {
        const document = program(directory, "fix.th", "var a = 1;\n");
        const provider = new CodeActionProvider(services);
        const diagnostic = new vscode.Diagnostic(
            new vscode.Range(0, 9, 0, 10), "unexpected character ';'; did you mean ','?", 0);
        const actions = await provider.provideCodeActions(document, diagnostic.range, { diagnostics: [diagnostic] });
        assert.strictEqual(actions.length, 1);
        assert.strictEqual(actions[0].title, "Replace with ','");
        assert.strictEqual(actions[0].edit.edits[0].newText, ",");
    });

    test("a missing import is offered as a quick fix", async () =>
    {
        const document = program(directory, "needs_os.th", "println(platform())\n");
        const provider = new CodeActionProvider(services);
        const diagnostic = new vscode.Diagnostic(
            new vscode.Range(0, 8, 0, 16), "'platform' is not declared", 0);
        const actions = await provider.provideCodeActions(document, diagnostic.range, { diagnostics: [diagnostic] });
        assert.strictEqual(actions.length, 1);
        assert.strictEqual(actions[0].title, "Add \"import os\"");
        assert.strictEqual(actions[0].edit.edits[0].newText, "import os\n");
        assert.strictEqual(actions[0].edit.edits[0].position.line, 0);
    });

    test("a new import goes under the comments and the imports already there", () =>
    {
        const document = vscode._document("x.th", [
            "// lang: ps", "// a comment", "import graphics", "", "var x = 1", "",
        ].join("\n"));
        assert.strictEqual(importPosition(document).line, 3);
    });
});

suite("providers: words under the pointer", () =>
{
    test("a built-in written as several words is found as one", () =>
    {
        const pashto = languageOf("ps");
        const document = vscode._document("x.th", "کرښه چاپ(\"a\")\n");
        for (const character of [0, 3, 6, 7])
        {
            const found = reservedWordAt(document, new vscode.Position(0, character), pashto);
            assert.strictEqual(found.word, "کرښه چاپ", `at character ${character}`);
            assert.strictEqual(found.meaning.category, "builtin");
        }
    });

    test("a word typed with an Arabic keyboard is recognised too", () =>
    {
        const pashto = languageOf("ps");
        // كرښه with the Arabic kaf (U+0643) instead of the Pashto keheh (U+06A9).
        const document = vscode._document("x.th", "كرښه چاپ(\"a\")\n");
        const found = reservedWordAt(document, new vscode.Position(0, 1), pashto);
        assert.ok(found.meaning, "the folded word was not recognised");
        assert.strictEqual(found.meaning.category, "builtin");
    });

    test("an ordinary name is returned without a meaning", () =>
    {
        const english = languageOf("en");
        const document = vscode._document("x.th", "var total = 1\n");
        const found = reservedWordAt(document, new vscode.Position(0, 6), english);
        assert.strictEqual(found.word, "total");
        assert.strictEqual(found.meaning, null);
    });

    test("the longest reserved name in a phrase is the call", () =>
    {
        const pashto = languageOf("ps");
        assert.strictEqual(longestReservedName("متغیر کرښه چاپ", pashto), "کرښه چاپ");
        assert.strictEqual(longestReservedName("my_function", pashto), "my_function");
    });
});

suite("providers: before the toolchain has been built", () =>
{
    // A fresh checkout has no thunderc. The editor must still be useful: the bundled copy of
    // the language information carries every keyword and built-in.
    const { Analyser } = require("../src/analysis");

    function servicesWithoutToolchain()
    {
        const languages = new Map([["en", languageOf("en")], ["ps", languageOf("ps")]]);
        return {
            analyser: new Analyser({ toolchain: () => null, settings: () => ({}) }),
            toolchain: () => null,
            languageFor: async () => languages.get("en"),
            allLanguages: async () => languages,
            configuration: () => ({ get: (key, fallback) => fallback }),
            modulePathsFor: () => [],
        };
    }

    test("completion still offers the language itself", async () =>
    {
        const services = servicesWithoutToolchain();
        const document = vscode._document(path.join(require("os").tmpdir(), "fresh.th"), "var x = 1\n");
        const result = await new CompletionProvider(services).provideCompletionItems(
            document, new vscode.Position(1, 0));
        const labels = result.items.map((item) => item.label);
        assert.ok(labels.includes("func"));
        assert.ok(labels.includes("println"));
    });

    test("hovers still explain the built-ins", async () =>
    {
        const services = servicesWithoutToolchain();
        const document = vscode._document(path.join(require("os").tmpdir(), "fresh2.th"), "println(1)\n");
        const hover = await new HoverProvider(services).provideHover(document, new vscode.Position(0, 2));
        assert.ok(hover.contents[0].value.includes("println(value, ...)"));
    });

    test("the features that need the compiler answer nothing rather than failing", async () =>
    {
        const services = servicesWithoutToolchain();
        const document = vscode._document(path.join(require("os").tmpdir(), "fresh3.th"), "var x = 1\n");
        assert.strictEqual(
            await new navigation.DefinitionProvider(services).provideDefinition(document, new vscode.Position(0, 5)),
            null);
        assert.deepStrictEqual(
            await new navigation.DocumentSymbolProvider(services).provideDocumentSymbols(document), []);
        assert.strictEqual(
            await new SemanticTokensProvider(services).provideDocumentSemanticTokens(document), null);
        assert.deepStrictEqual(
            await new DocumentHighlightProvider(services).provideDocumentHighlights(document,
                new vscode.Position(0, 5)), []);
    });
});
