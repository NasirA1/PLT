// The right-to-left view: the panel that shows a Pashto program as it is meant to be read.

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

const { assert, before, requireToolchain, suite, test, toolchainDirectory } = require("./harness");
const vscode = require("./fake-vscode");

const { RightToLeftView, renderDocument, renderLine, tokensByLine } = require("../src/rtlview");
const { FSI, PDI } = require("../src/direction");

const PROGRAM = [
    "// ژبه: ps",
    "متغیر نوم = \"ناصر\"",
    "کرښه چاپ(نوم)",
    "",
].join("\n");

suite("right-to-left view: drawing a program", () =>
{
    test("a line becomes HTML with the isolate marks in it", () =>
    {
        const html = renderLine("متغیر نوم = \"ناصر\"", true, []);
        assert.ok(html.includes(FSI) && html.includes(PDI), "the marks order the tokens");
        assert.ok(html.includes("متغیر"));
        assert.ok(!html.includes("<span"), "with no tokens there is nothing to colour");
    });

    test("the compiler's tokens become coloured spans around the marks", () =>
    {
        const html = renderLine("متغیر نوم = ۱", true, [
            { startCharacter: 0, endCharacter: 5, kind: "keyword" },
            { startCharacter: 6, endCharacter: 9, kind: "identifier" },
            { startCharacter: 12, endCharacter: 13, kind: "number" },
        ]);
        assert.ok(html.includes("<span class=\"t-keyword\">متغیر</span>"), html);
        assert.ok(html.includes("<span class=\"t-identifier\">نوم</span>"), html);
        // The marks stay outside the spans, so they still order the line.
        assert.ok(html.includes(`${FSI}<span class="t-keyword">`), html);
    });

    test("a comment is coloured even though the lexer never reports it", () =>
    {
        const html = renderLine("متغیر ن = ۱ // یادښت", true, [
            { startCharacter: 0, endCharacter: 5, kind: "keyword" },
        ]);
        assert.ok(html.includes("t-comment"), html);
        assert.ok(html.indexOf("t-comment") > html.indexOf("t-keyword"));
    });

    test("a // inside a string is not a comment", () =>
    {
        const html = renderLine("var url = \"http://x\"", false, []);
        assert.ok(!html.includes("t-comment"), html);
    });

    test("HTML in the program cannot escape into the page", () =>
    {
        const html = renderLine("var x = \"<script>alert(1)</script>\"", false, []);
        assert.ok(!html.includes("<script>"), html);
        assert.ok(html.includes("&lt;script&gt;"));
    });

    test("every line of the document is a row, with its number and the caret line marked", () =>
    {
        const html = renderDocument(PROGRAM, true, new Map(), 1);
        const rows = html.split("\n").filter((row) => row.includes("class=\"row"));
        assert.strictEqual(rows.length, 4, "three lines and the empty one at the end");
        assert.ok(rows[0].includes(">1</span>"));
        assert.ok(rows[1].includes("row current"), rows[1]);
        assert.ok(!rows[0].includes("row current"));
        assert.ok(rows[3].includes("&#8203;"), "an empty line still has height");
        assert.ok(rows[1].includes("data-line=\"1\""));
    });
});

suite("right-to-left view: the panel", () =>
{
    let services = null;
    let directory = null;

    before(() =>
    {
        requireToolchain();
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-rtl-"));
        vscode._setFolders([directory]);
        vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        const { Services } = require("../src/services");
        services = new Services({ extensionPath: path.resolve(__dirname, "..") });
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { services, directory };
    });

    test("the panel is opened beside the editor, right to left, and titled in Pashto", async () =>
    {
        const file = path.join(directory, "pashto.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        const document = vscode._openDocument(file, PROGRAM);
        const view = new RightToLeftView(services);
        await view.show(document, { preserveFocus: true });

        const panel = vscode._lastWebview();
        assert.ok(panel, "no panel was created");
        assert.strictEqual(panel.showOptions.viewColumn, vscode.ViewColumn.Beside);
        assert.strictEqual(panel.showOptions.preserveFocus, true);
        assert.ok(panel.title.includes("pashto.th"));
        assert.ok(panel.title.includes("پښتو"), panel.title);
        assert.ok(panel.webview.html.includes("dir=\"rtl\""), "the page itself must be right to left");
        assert.ok(panel.webview.html.includes("متغیر"));
        assert.ok(panel.webview.html.includes("Content-Security-Policy"));
        view.dispose();
    });

    test("an English program is shown left to right", async () =>
    {
        const file = path.join(directory, "english.th");
        const text = "var name = \"Ada\"\nprintln(name)\n";
        fs.writeFileSync(file, text, "utf8");
        const document = vscode._openDocument(file, text);
        const view = new RightToLeftView(services);
        await view.show(document, { preserveFocus: true });
        assert.ok(vscode._lastWebview().webview.html.includes("dir=\"ltr\""));
        view.dispose();
    });

    test("the view is coloured from the compiler's own tokens", async () =>
    {
        const file = path.join(directory, "coloured.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        const document = vscode._openDocument(file, PROGRAM);
        const model = await services.analyse(document);
        assert.ok(model && model.succeeded, "the program should compile");
        const grouped = tokensByLine(model, await services.languageData("ps"));
        assert.ok(grouped.get(1).some((token) => token.kind === "keyword"), JSON.stringify(grouped.get(1)));
        assert.ok(grouped.get(1).some((token) => token.kind === "string"));
        assert.ok(grouped.get(2).some((token) => token.kind === "builtin"), JSON.stringify(grouped.get(2)));

        const view = new RightToLeftView(services);
        await view.show(document, { preserveFocus: true });
        const html = vscode._lastWebview().webview.html;
        assert.ok(html.includes("t-keyword"), "the keywords are coloured");
        assert.ok(html.includes("t-builtin"), "the built-in is coloured");
        view.dispose();
    });

    test("clicking a line moves the caret in the editor", async () =>
    {
        const file = path.join(directory, "click.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        const document = vscode._openDocument(file, PROGRAM);
        const view = new RightToLeftView(services);
        await view.show(document, { preserveFocus: true });
        vscode._lastWebview().receive({ command: "goto", line: 2 });
        assert.strictEqual(vscode.window.activeTextEditor.selection.active.line, 2);
        vscode._lastWebview().receive({ command: "nonsense" });
        assert.strictEqual(vscode.window.activeTextEditor.selection.active.line, 2);
        view.dispose();
    });

    test("the view redraws when the program changes, and closes with its panel", async () =>
    {
        const file = path.join(directory, "live.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        const document = vscode._openDocument(file, PROGRAM);
        const view = new RightToLeftView(services);
        await view.show(document, { preserveFocus: true });
        const panel = vscode._lastWebview();
        const before = panel.webview.html;

        const changed = vscode._openDocument(file, PROGRAM.replace("ناصر", "احمد"), { version: 2 });
        await view.update(changed);
        assert.notStrictEqual(panel.webview.html, before);
        assert.ok(panel.webview.html.includes("احمد"));

        assert.ok(view.isOpen);
        panel.dispose();
        assert.ok(!view.isOpen, "closing the panel closes the view");
        await view.update(changed); // must not throw now that there is no panel
    });
});

suite("right-to-left view: which file it follows", () =>
{
    let services = null;
    let directory = null;

    before(() =>
    {
        requireToolchain();
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-rtl-follow-"));
        vscode._setFolders([directory]);
        vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        const { Services } = require("../src/services");
        services = new Services({ extensionPath: path.resolve(__dirname, "..") });
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { services };
    });

    test("an edit in another file does not take the view away from this one", async () =>
    {
        const shown = path.join(directory, "shown.th");
        const other = path.join(directory, "other.th");
        fs.writeFileSync(shown, PROGRAM, "utf8");
        fs.writeFileSync(other, "// ژبه: ps\nمتغیر بل = ۲\n", "utf8");
        const view = new RightToLeftView(services);
        await view.show(vscode._document(shown, PROGRAM), { preserveFocus: true });
        const panel = vscode._lastWebview();
        assert.ok(panel.webview.html.includes("ناصر"));

        await view.update(vscode._document(other, fs.readFileSync(other, "utf8")));
        assert.ok(panel.webview.html.includes("ناصر"), "the view still shows the file it was opened for");
        assert.ok(!panel.webview.html.includes("بل"));

        // Showing the other file explicitly does switch.
        await view.show(vscode._document(other, fs.readFileSync(other, "utf8")), { preserveFocus: true });
        assert.ok(panel.webview.html.includes("بل"));
        view.dispose();
    });
});
