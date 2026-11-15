// The right-to-left editor's language features: the page asks, the bridge runs VS Code's
// `vscode.execute...Provider` commands on the document and answers with plain data.
//
// The stand-in for VS Code has no providers behind those commands, so each test registers the
// command it needs - and the last suite registers the *real* completion provider behind
// `vscode.executeCompletionItemProvider`, to show that a Pashto program gets the same list as
// the text editor.

"use strict";

const fs = require("fs");
const path = require("path");

const { EXTENSION, assert, before, requireToolchain, suite, temporaryDirectory, test } = require("./harness");
const vscode = require("./fake-vscode");

const { LanguageBridge, expandSnippet, wordStartBefore } = require("../src/rtlservices");
const { CompletionProvider } = require("../src/providers/completion");
const { Analyser } = require("../src/analysis");
const { LanguageData } = require("../src/language");
const { Toolchain } = require("../src/toolchain");

const TEXT = "# ژبه: ps\nمتغیر نوم = ۱\nکرښه چاپ(نو)\n";

function documentOf(text)
{
    return vscode._document(path.join(EXTENSION, "work", "bridge.th"), text || TEXT);
}

function bridge(revealed)
{
    return new LanguageBridge(async (uri, offset, position) => revealed && revealed.push({ uri, offset, position }));
}

function command(name, implementation)
{
    vscode._commands.set(name, implementation);
}

suite("right-to-left editor: the language bridge", () =>
{
    before(() => vscode._reset());

    test("a snippet becomes plain text with the caret on its first tab stop", () =>
    {
        assert.deepStrictEqual(expandSnippet("کرښه چاپ($0)"), { text: "کرښه چاپ()", caret: "کرښه چاپ(".length });
        assert.deepStrictEqual(expandSnippet("area(${1:r})"), { text: "area(r)", caret: 5 });
        assert.deepStrictEqual(expandSnippet("f(${1:a}، ${2:b})$0"), { text: "f(a، b)", caret: 2 });
        assert.deepStrictEqual(expandSnippet("plain"), { text: "plain", caret: 5 });
        assert.deepStrictEqual(expandSnippet("cost \\$5"), { text: "cost $5", caret: 7 });
    });

    test("the word before an offset is found in any script", () =>
    {
        const text = "کرښه چاپ(نو";
        assert.strictEqual(wordStartBefore(text, text.length), text.length - 2);
        assert.strictEqual(wordStartBefore("a.b", 2), 2);
    });

    test("completion items keep the providers' order, kinds, details, ranges and snippets", async () =>
    {
        const document = documentOf();
        const offset = TEXT.indexOf("نو)") + 2;
        let asked = null;
        command("vscode.executeCompletionItemProvider", (uri, position, trigger) =>
        {
            asked = { uri, position, trigger };
            const builtin = new vscode.CompletionItem("کرښه چاپ", 2);
            builtin.insertText = new vscode.SnippetString("کرښه چاپ($0)");
            builtin.detail = "کرښه چاپ(ارزښت، ...)";
            builtin.sortText = "2";
            const name = new vscode.CompletionItem("نوم", 5);
            name.sortText = "0نوم";
            name.range = new vscode.Range(document.positionAt(offset - 2), document.positionAt(offset));
            const keyword = new vscode.CompletionItem("که", 13);
            keyword.sortText = "1";
            return { items: [builtin, keyword, name] };
        });
        const answer = await bridge().handle(document, { feature: "completion", offset, trigger: "" });
        assert.strictEqual(String(asked.uri), String(document.uri));
        assert.deepStrictEqual([asked.position.line, asked.position.character], [2, 11]);
        assert.strictEqual(answer.wordStart, offset - 2);
        assert.deepStrictEqual(answer.items.map((item) => item.label), ["نوم", "که", "کرښه چاپ"]);
        assert.deepStrictEqual(answer.items.map((item) => item.kind), ["name", "keyword", "function"]);
        const name = answer.items[0];
        assert.deepStrictEqual([name.start, name.end, name.text], [offset - 2, offset, "نوم"]);
        const builtin = answer.items[2];
        assert.strictEqual(builtin.text, "کرښه چاپ()");
        assert.strictEqual(builtin.caret, "کرښه چاپ(".length, "between the brackets");
        assert.strictEqual(builtin.detail, "کرښه چاپ(ارزښت، ...)");
        assert.strictEqual(builtin.start, offset - 2, "no range: the word typed so far is replaced");
    });

    test("parameter hints say which parameter the caret is on", async () =>
    {
        command("vscode.executeSignatureHelpProvider", () => ({
            signatures: [{ label: "area(r: Rectangle): int", parameters: [{ label: "r: Rectangle" }], documentation: "**area**" }],
            activeSignature: 0,
            activeParameter: 0,
        }));
        const answer = await bridge().handle(documentOf(), { feature: "signature", offset: 5 });
        assert.strictEqual(answer.label, "area(r: Rectangle): int");
        assert.deepStrictEqual(answer.highlight, { start: 5, end: 17 });
        assert.strictEqual(answer.documentation, "**area**");
        command("vscode.executeSignatureHelpProvider", () => undefined);
        assert.strictEqual(await bridge().handle(documentOf(), { feature: "signature", offset: 5 }), null);
    });

    test("hovers join every provider's contents; nothing to say is null", async () =>
    {
        command("vscode.executeHoverProvider", () => [{ contents: [new vscode.MarkdownString("```thunder\nنوم: عدد\n```"), "Pashto: نوم"] }]);
        const answer = await bridge().handle(documentOf(), { feature: "hover", offset: 15 });
        assert.ok(answer.markdown.includes("نوم: عدد") && answer.markdown.includes("Pashto: نوم"));
        command("vscode.executeHoverProvider", () => []);
        assert.strictEqual(await bridge().handle(documentOf(), { feature: "hover", offset: 15 }), null);
    });

    test("occurrences come back as offsets, writes told apart from reads", async () =>
    {
        const document = documentOf();
        command("vscode.executeDocumentHighlights", () => [
            { range: new vscode.Range(1, 6, 1, 9), kind: 2 },
            { range: new vscode.Range(2, 9, 2, 11), kind: 1 },
        ]);
        const answer = await bridge().handle(document, { feature: "highlights", offset: 15 });
        assert.deepStrictEqual(answer, [
            { start: document.offsetAt(new vscode.Position(1, 6)), end: document.offsetAt(new vscode.Position(1, 9)), kind: "write" },
            { start: document.offsetAt(new vscode.Position(2, 9)), end: document.offsetAt(new vscode.Position(2, 11)), kind: "read" },
        ]);
    });

    test("go to definition reveals the place, in this editor or another file's", async () =>
    {
        const document = documentOf();
        const target = vscode.Uri.file(path.join(EXTENSION, "work", "lib.th"));
        command("vscode.executeDefinitionProvider", () => [{ targetUri: target, targetRange: new vscode.Range(3, 0, 3, 4),
            targetSelectionRange: new vscode.Range(3, 5, 3, 9) }]);
        const revealed = [];
        assert.deepStrictEqual(await bridge(revealed).handle(document, { feature: "definition", offset: 15 }), { found: true });
        assert.strictEqual(String(revealed[0].uri), String(target));
        assert.deepStrictEqual([revealed[0].position.line, revealed[0].position.character], [3, 5]);
        command("vscode.executeDefinitionProvider", () => []);
        assert.strictEqual(await bridge(revealed).handle(document, { feature: "definition", offset: 15 }), null);
    });

    test("references are listed to choose from, and the choice is revealed", async () =>
    {
        const document = documentOf();
        vscode._state.documents.push(document);
        command("vscode.executeReferenceProvider", () => [
            new vscode.Location(document.uri, new vscode.Range(1, 6, 1, 9)),
            new vscode.Location(document.uri, new vscode.Range(2, 9, 2, 11)),
        ]);
        const revealed = [];
        const answer = await bridge(revealed).handle(document, { feature: "references", offset: 15 });
        assert.strictEqual(answer.count, 2);
        assert.strictEqual(revealed.length, 1, "the first reference was chosen in the quick pick");
        assert.strictEqual(revealed[0].position.line, 1);
    });

    test("rename asks for the new name and applies the provider's edit", async () =>
    {
        const document = documentOf();
        let renamed = null;
        command("vscode.executeDocumentRenameProvider", (uri, position, name) =>
        {
            renamed = { position, name };
            return new vscode.WorkspaceEdit();
        });
        const original = vscode.window.showInputBox;
        vscode.window.showInputBox = () => Promise.resolve("شمېره");
        try
        {
            const offset = TEXT.indexOf("نوم") + 1;
            const answer = await bridge().handle(document, { feature: "rename", offset });
            assert.deepStrictEqual(answer, { applied: true });
            assert.strictEqual(renamed.name, "شمېره");
            assert.deepStrictEqual([renamed.position.line, renamed.position.character], [1, 6], "from the start of the word");
            assert.strictEqual(vscode._state.applied.length, 1);
        }
        finally
        {
            vscode.window.showInputBox = original;
        }
    });

    test("format applies the formatting provider's edits", async () =>
    {
        vscode._state.applied = [];
        command("vscode.executeFormatDocumentProvider", () => [vscode.TextEdit.replace(new vscode.Range(0, 0, 0, 1), "#")]);
        assert.deepStrictEqual(await bridge().handle(documentOf(), { feature: "format" }), { applied: true });
        assert.strictEqual(vscode._state.applied.length, 1);
        command("vscode.executeFormatDocumentProvider", () => []);
        assert.deepStrictEqual(await bridge().handle(documentOf(), { feature: "format" }), { applied: false });
    });

    test("a quick fix is chosen and its edit applied", async () =>
    {
        vscode._state.applied = [];
        const fix = { title: "Import graphics", edit: new vscode.WorkspaceEdit() };
        command("vscode.executeCodeActionProvider", () => [{ title: "nothing to do" }, fix]);
        assert.deepStrictEqual(await bridge().handle(documentOf(), { feature: "quickFix", offset: 3 }), { applied: true });
        assert.strictEqual(vscode._state.applied[0], fix.edit);
        command("vscode.executeCodeActionProvider", () => []);
        assert.strictEqual(await bridge().handle(documentOf(), { feature: "quickFix", offset: 3 }), null);
    });

    test("go to symbol lists the outline, nested symbols with their container", async () =>
    {
        const document = documentOf();
        command("vscode.executeDocumentSymbolProvider", () => [
            { name: "مساحت", range: new vscode.Range(1, 0, 2, 0), selectionRange: new vscode.Range(1, 6, 1, 9),
                children: [{ name: "r", range: new vscode.Range(1, 10, 1, 11), children: [] }] },
        ]);
        const revealed = [];
        assert.deepStrictEqual(await bridge(revealed).handle(document, { feature: "symbols" }), { count: 2 });
        assert.strictEqual(revealed[0].offset, document.offsetAt(new vscode.Position(1, 6)));
    });

    test("semantic tokens are decoded into ranges by type", async () =>
    {
        const document = documentOf();
        command("vscode.provideDocumentSemanticTokensLegend", () => ({
            tokenTypes: ["function", "parameter", "variable"], tokenModifiers: ["declaration", "readonly"],
        }));
        // line 1 col 6 len 3 variable+readonly; line 2 col 0 len 8 function; same line col 9 len 2 parameter
        command("vscode.provideDocumentSemanticTokens", () => ({ data: new Uint32Array([1, 6, 3, 2, 2, 1, 0, 8, 0, 0, 0, 9, 2, 1, 0]) }));
        const answer = await bridge().handle(document, { feature: "semanticTokens" });
        const at = (line, character) => document.offsetAt(new vscode.Position(line, character));
        assert.deepStrictEqual(answer.constant, [[at(1, 6), at(1, 9)]], "a readonly variable is a constant");
        assert.deepStrictEqual(answer.function, [[at(2, 0), at(2, 8)]]);
        assert.deepStrictEqual(answer.parameter, [[at(2, 9), at(2, 11)]]);
    });

    test("an unknown feature or a failing provider answers null, never throws", async () =>
    {
        assert.strictEqual(await bridge().handle(documentOf(), { feature: "dance" }), null);
        command("vscode.executeHoverProvider", () => { throw new Error("broken"); });
        assert.strictEqual(await bridge().handle(documentOf(), { feature: "hover", offset: 1 }), null);
    });
});

suite("right-to-left editor: the same completion list as the text editor", () =>
{
    let directory = null;
    let provider = null;

    before(() =>
    {
        const toolchain = new Toolchain(requireToolchain());
        directory = temporaryDirectory("rtl-completion");
        const language = (code) => new LanguageData(
            JSON.parse(fs.readFileSync(path.join(EXTENSION, "data", `language-info.${code}.json`), "utf8")));
        const languages = new Map([["en", language("en")], ["ps", language("ps")]]);
        const analyser = new Analyser({
            toolchain: () => toolchain,
            settings: () => ({ language: "ps", modulePaths: [directory] }),
        });
        provider = new CompletionProvider({
            analyser,
            toolchain: () => toolchain,
            languageFor: async () => languages.get("ps"),
            allLanguages: async () => languages,
            configuration: () => ({ get: (key, fallback) => fallback }),
            modulePathsFor: () => [directory],
            nativePathsFor: () => [],
        });
        // What VS Code does for the command: ask every completion provider of the document.
        vscode._commands.set("vscode.executeCompletionItemProvider",
            (uri, position) => provider.provideCompletionItems(vscode._state.documents.find((d) => String(d.uri) === String(uri)), position));
    });

    test("Ctrl+Space in a Pashto program offers the whole SDK, not a handful of keywords", async () =>
    {
        const text = "# ژبه: ps\nمتغیر شمېر = ۱\n\n";
        const file = path.join(directory, "full.th");
        fs.writeFileSync(file, text, "utf8");
        const document = vscode._document(file, text);
        vscode._state.documents.push(document);
        const answer = await new LanguageBridge(async () => {}).handle(document, { feature: "completion", offset: text.length - 1 });
        const labels = answer.items.map((item) => item.label);
        assert.ok(labels.length > 60, `only ${labels.length} items`);
        for (const expected of ["کرښه چاپ", "که", "دنده", "اوږدوالی", "شمېر"])
        {
            assert.ok(labels.includes(expected), `${expected} is offered`);
        }
        const println = answer.items.find((item) => item.label === "کرښه چاپ");
        assert.ok(println.text.startsWith("کرښه چاپ("), "a built-in is inserted with its brackets");
        assert.ok(println.detail, "with its signature");
    });
});
