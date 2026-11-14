// The right-to-left editor: its model (pure, here in Node) and the provider that keeps the
// webview and the document in step. What the browser does with the model's HTML is tested in a
// real Chromium by tests/python/test_vscode_extension.py (RightToLeftEditorBrowserTests).

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

const { EXTENSION, assert, before, suite, test, toolchainDirectory, waitFor } = require("./harness");
const vscode = require("./fake-vscode");

const model = require("../media/rtleditor/model");
const { LanguageData } = require("../src/language");
const { RightToLeftEditorProvider, blockWordsFor, wordsFor } = require("../src/rtleditor");

function languageOf(code)
{
    return new LanguageData(
        JSON.parse(fs.readFileSync(path.join(EXTENSION, "data", `language-info.${code}.json`), "utf8")));
}

const BLOCK_WORDS = blockWordsFor([languageOf("en"), languageOf("ps")]);

suite("right-to-left editor: editing", () =>
{
    test("an edit replaces a range, whichever way round it is given", () =>
    {
        assert.strictEqual(model.applyEdit("abcdef", 2, 4, "X"), "abXef");
        assert.strictEqual(model.applyEdit("abcdef", 4, 2, "X"), "abXef");
        assert.strictEqual(model.applyEdit("abc", 3, 3, "!"), "abc!");
    });

    test("the difference of two texts is the smallest edit between them", () =>
    {
        assert.deepStrictEqual(model.difference("متغیر ن = ۱", "متغیر ن = ۱۲"), { start: 11, end: 11, text: "۲" });
        assert.deepStrictEqual(model.difference("abc", "aXc"), { start: 1, end: 2, text: "X" });
        assert.deepStrictEqual(model.difference("abc", "ac"), { start: 1, end: 2, text: "" });
        assert.strictEqual(model.difference("same", "same"), null);
        // Applying the difference gives the new text, always.
        for (const [before, after] of [["", "x"], ["x", ""], ["aaa", "aa"], ["کرښه", "کرښه چاپ"], ["😀a", "😀b"]])
        {
            const change = model.difference(before, after);
            assert.strictEqual(model.applyEdit(before, change.start, change.end, change.text), after);
        }
    });

    test("an emoji is never split", () =>
    {
        const change = model.difference("x😀", "x😃");
        assert.ok(change.start <= 1, "the edit starts before the surrogate pair");
    });

    test("positions and offsets agree", () =>
    {
        const text = "که ن\n    تېر\nپای";
        for (let offset = 0; offset <= text.length; ++offset)
        {
            const position = model.positionOf(text, offset);
            assert.strictEqual(model.offsetOf(text, position.line, position.character), offset);
        }
        assert.strictEqual(model.offsetOf(text, 9, 9), text.length, "beyond the end is the end");
    });

    test("Enter keeps the indentation, and indents after a line that opens a block", () =>
    {
        const english = model.newline("    if x > 0", 12, 12, BLOCK_WORDS);
        assert.strictEqual(english.text, "    if x > 0\n        ");
        const pashto = model.newline("که ن > ۰", 8, 8, BLOCK_WORDS);
        assert.strictEqual(pashto.text, "که ن > ۰\n    ");
        assert.strictEqual(pashto.caret, pashto.text.length);
        const plain = model.newline("    متغیر ن = ۱", 15, 15, BLOCK_WORDS);
        assert.strictEqual(plain.text, "    متغیر ن = ۱\n    ");
    });

    test("Enter after return, break or pass goes one level back out", () =>
    {
        const result = model.newline("        راګرځه ن", 16, 16, BLOCK_WORDS);
        assert.strictEqual(result.text, "        راګرځه ن\n    ");
        assert.strictEqual(model.newline("    pass", 8, 8, BLOCK_WORDS).text, "    pass\n");
    });

    test("a keyword typed on an Arabic keyboard still indents", () =>
    {
        // كه with the Arabic kaf (U+0643), not the Pashto keheh.
        assert.strictEqual(model.newline("كه ن", 4, 4, BLOCK_WORDS).text, "كه ن\n    ");
    });

    test("Tab inserts four spaces, or indents every selected line", () =>
    {
        assert.deepStrictEqual(model.indent("ab", 1, 1), { text: "a    b", start: 5, end: 5 });
        const lines = model.indent("a\nb\n\nc", 0, 6);
        assert.strictEqual(lines.text, "    a\n    b\n\n    c", "blank lines stay blank");
        const back = model.outdent(lines.text, 0, lines.text.length);
        assert.strictEqual(back.text, "a\nb\n\nc");
    });

    test("Backspace in the leading spaces takes away a whole level", () =>
    {
        assert.deepStrictEqual(model.backspace("        x", 8, 8), { text: "    x", caret: 4 });
        assert.deepStrictEqual(model.backspace("      x", 6, 6), { text: "    x", caret: 4 });
        assert.deepStrictEqual(model.backspace("ab", 2, 2), { text: "a", caret: 1 });
        assert.deepStrictEqual(model.backspace("abc", 0, 2), { text: "c", caret: 0 });
        assert.deepStrictEqual(model.backspace("x😀", 3, 3), { text: "x", caret: 1 }, "an emoji goes whole");
        assert.deepStrictEqual(model.backspace("", 0, 0), { text: "", caret: 0 });
    });
});

suite("right-to-left editor: undo", () =>
{
    test("a burst of typing undoes as one step, anything else as its own", () =>
    {
        const history = new model.History("", 0);
        history.record("ک", 1, "type", 1000);
        history.record("که", 2, "type", 1100);
        history.record("که\n    ", 7, "newline", 1200);
        history.record("که\n    ن", 8, "type", 1300);
        assert.strictEqual(history.undo().text, "که\n    ");
        assert.strictEqual(history.undo().text, "که");
        assert.strictEqual(history.undo().text, "", "the two letters were one burst");
        assert.strictEqual(history.undo(), null);
        assert.strictEqual(history.redo().text, "که");
        assert.strictEqual(history.redo().text, "که\n    ");
    });

    test("a pause ends a burst", () =>
    {
        const history = new model.History("", 0);
        history.record("a", 1, "type", 0);
        history.record("ab", 2, "type", 5000);
        assert.strictEqual(history.undo().text, "a");
    });

    test("typing after undo forgets what could be redone", () =>
    {
        const history = new model.History("", 0);
        history.record("a", 1, "type", 0);
        history.undo();
        history.record("b", 1, "type", 10);
        assert.strictEqual(history.redo(), null);
    });

    test("a change from another editor becomes one step back", () =>
    {
        const history = new model.History("a", 1);
        history.reset("a and more", 3);
        assert.strictEqual(history.undo().text, "a");
    });
});

suite("right-to-left editor: colouring and layout", () =>
{
    const pashto = wordsFor(languageOf("ps"));
    const english = wordsFor(languageOf("en"));

    test("keywords, types, literals, built-ins, strings, numbers and comments are told apart", () =>
    {
        const line = "متغیر ن: عدد = اوږدوالی(\"متن\") + ۴۲ # یادښت";
        const kinds = model.classify(line, pashto);
        const kindOf = (word) => kinds[line.indexOf(word)];
        assert.strictEqual(kindOf("متغیر"), "keyword");
        assert.strictEqual(kindOf("عدد"), "type");
        assert.strictEqual(kindOf("اوږدوالی"), "builtin");
        assert.strictEqual(kindOf("\"متن\""), "string");
        assert.strictEqual(kindOf("۴۲"), "number");
        assert.strictEqual(kindOf("# یادښت"), "comment");
        assert.strictEqual(kindOf("ن"), "name");
        assert.strictEqual(kinds[line.indexOf("=")], "");
    });

    test("a built-in written as two words is coloured as one", () =>
    {
        const line = "کرښه چاپ(ن)";
        const kinds = model.classify(line, pashto);
        assert.ok(kinds.slice(0, 8).every((kind) => kind === "builtin"), kinds.join(","));
        // ... but not when something other than spaces separates the words.
        const split = model.classify("کرښه(چاپ)", pashto);
        assert.notStrictEqual(split[0], "builtin");
    });

    test("a word typed on an Arabic keyboard is still recognised", () =>
    {
        const kinds = model.classify("كرښه چاپ(ن)", pashto);
        assert.strictEqual(kinds[0], "builtin");
        assert.strictEqual(model.classify("var x", english)[0], "keyword");
    });

    test("every token of a Pashto line is an isolate, numbers left to right", () =>
    {
        const html = model.renderLine("متغیر ن = -۳", true, pashto);
        assert.ok(html.includes('<span class="tok" dir="auto"><span class="k-keyword">متغیر</span></span>'), html);
        // The sign is inside the left-to-right isolate with its digits (it is an operator to
        // the highlighter, so only the digits are coloured).
        assert.ok(html.includes('<span class="tok" dir="ltr">-<span class="k-number">۳</span></span>'), html);
        // The text of the spans is exactly the line: no direction marks are added.
        assert.strictEqual(html.replace(/<[^>]+>/g, ""), "متغیر ن = -۳");
    });

    test("a program's text cannot become markup", () =>
    {
        const html = model.renderLine("var s = \"<b>&</b>\"", false, english);
        assert.ok(!html.includes("<b>"));
        assert.ok(html.includes("&lt;b&gt;&amp;&lt;/b&gt;"));
    });
});

suite("right-to-left editor: completion", () =>
{
    const candidates = [
        { label: "کرښه چاپ", kind: "builtin" },
        { label: "کرښه", kind: "name" },
        { label: "کوچنی توري کړه", kind: "builtin" },
        { label: "که", kind: "keyword" },
        { label: "کښته کړه", kind: "builtin" },
    ];

    test("words that start with what was typed, the program's own names first", () =>
    {
        const labels = model.completions("کر", candidates).map((item) => item.label);
        assert.deepStrictEqual(labels, ["کرښه", "کرښه چاپ"]);
        assert.deepStrictEqual(model.completions("", candidates), []);
        assert.deepStrictEqual(model.completions("که", candidates), [], "a finished word offers nothing");
    });

    test("an Arabic-keyboard prefix finds the Pashto word", () =>
    {
        assert.ok(model.completions("كو", candidates).some((item) => item.label === "کوچنی توري کړه"));
    });

    test("the word before the caret is found in either script", () =>
    {
        assert.deepStrictEqual(model.wordBefore("متغیر نو", 8), { start: 6, prefix: "نو" });
        assert.deepStrictEqual(model.wordBefore("x = ", 4), { start: 4, prefix: "" });
    });
});

suite("right-to-left editor: the provider", () =>
{
    let services = null;
    let directory = null;
    let applied = [];
    let provider = null;

    before(() =>
    {
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-rtleditor-"));
        vscode._setFolders([directory]);
        if (toolchainDirectory())
        {
            vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        }
        const { Services } = require("../src/services");
        services = new Services({ extensionPath: EXTENSION });
        services.diagnostics = { schedule() {} };
        provider = new RightToLeftEditorProvider(services, {
            applyEdit: async (edit) =>
            {
                applied.push(edit.edits[0]);
                return true;
            },
        });
        process.on("exit", () => fs.rmSync(directory, { recursive: true, force: true }));
        return { services, provider };
    });

    /** A document and a webview panel for it, the way VS Code would hand them over. */
    async function open(name, text)
    {
        const file = path.join(directory, name);
        fs.writeFileSync(file, text, "utf8");
        const document = vscode._openDocument(file, text);
        const panel = vscode.window.createWebviewPanel("thunder.rightToLeftEditor", name, {}, {});
        await provider.resolveCustomTextEditor(document, panel);
        return { document, panel };
    }

    test("the page loads the editor, the model and the direction algorithm, and nothing else", async () =>
    {
        const { panel } = await open("page.th", "# ژبه: ps\nمتغیر ن = ۱\n");
        const html = panel.webview.html;
        for (const script of ["direction.js", "model.js", "editor.js"])
        {
            assert.ok(html.includes(script), `${script} is not loaded`);
        }
        assert.ok(/script-src 'nonce-\d+'/.test(html), "only the page's own scripts may run");
        assert.ok(html.includes("contenteditable=\"true\""));
        // Pashto in its own font, the rest in the editor's.
        assert.ok(html.includes("unicode-range: U+0600-06FF"), "the Pashto font covers only the Arabic script");
        assert.ok(html.includes("Vazirmatn"));
        assert.ok(html.includes("var(--vscode-editor-font-family)"));
        assert.strictEqual(panel.webview.options.enableScripts, true);
    });

    test("everything the page loads is inside a directory the webview may read", async () =>
    {
        // VS Code refuses any local file outside localResourceRoots; the stand-in does not, so
        // the rule is checked here: a script from an unlisted directory would break the editor.
        const { panel } = await open("roots.th", "x\n");
        const roots = panel.webview.options.localResourceRoots.map((uri) => path.resolve(uri.fsPath));
        const loaded = [...panel.webview.html.matchAll(/(?:src|href)="([^"]+)"/g)].map((match) => match[1]);
        assert.ok(loaded.length >= 4, loaded.join(", "));
        for (const reference of loaded)
        {
            const file = path.resolve(vscode.Uri.parse(reference).fsPath);
            assert.ok(fs.existsSync(file), `${reference} does not exist`);
            assert.ok(roots.some((root) => file.startsWith(root + path.sep)), `${reference} is outside ${roots.join(", ")}`);
        }
    });

    test("when the page is ready it gets the text, the direction and the words", async () =>
    {
        const { panel } = await open("init.th", "# ژبه: ps\nکرښه چاپ(۱)\n");
        panel.receive({ type: "loaded" });
        // Answering "loaded" asks the toolchain for the language data, which takes as long as
        // it takes; the test waits for the message, not for a fixed moment.
        const init = await waitFor(
            () => panel.messages.find((message) => message.type === "init"), "the init message");
        assert.strictEqual(init.rightToLeft, true);
        assert.strictEqual(init.language, "ps");
        assert.ok(init.text.includes("کرښه چاپ"));
        assert.strictEqual(init.words["کرښه چاپ"], "builtin");
        assert.ok(init.blockWords.opening.includes("که"));
        assert.ok(init.blockWords.leaving.includes("راګرځه"));
        assert.ok(init.candidates.some((item) => item.label === "کرښه چاپ"));
    });

    test("an edit in the page becomes the smallest edit of the document", async () =>
    {
        const { panel, document } = await open("edit.th", "متغیر ن = ۱\n");
        applied = [];
        panel.receive({ type: "edit", text: "متغیر ن = ۱۲\n" });
        await waitFor(() => applied.length === 1, "the edit to reach the document");
        assert.strictEqual(applied[0].newText, "۲");
        assert.strictEqual(String(applied[0].uri), String(document.uri));
        assert.strictEqual(applied[0].range.start.character, 11);
        assert.strictEqual(applied[0].range.end.character, 11);
    });

    test("nothing is sent back for the page's own edit, but another editor's change is", async () =>
    {
        const { panel, document } = await open("echo.th", "متغیر ن = ۱\n");
        const session = provider.open.find((entry) => entry.document === document);
        panel.receive({ type: "edit", text: "متغیر ن = ۲\n" });
        // The document reports the change that came from the page: not echoed back.
        await waitFor(() => session.pending === 1, "the page's edit to be counted");
        const sentBefore = panel.messages.length;
        await vscode._fire("changeDocument", { document, contentChanges: [{}] });
        assert.strictEqual(panel.messages.length, sentBefore);
        assert.strictEqual(session.pending, 0);
        // A change from the text editor next to it is.
        await vscode._fire("changeDocument", { document, contentChanges: [{}] });
        const update = panel.messages[panel.messages.length - 1];
        assert.strictEqual(update.type, "update");
        assert.strictEqual(update.text, document.getText());
    });

    test("the compiler's problems follow the document", async function problems()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const text = "# ژبه: ps\nمتغیر ن: عدد = \"متن\"\n";
        const { panel, document } = await open("problems.th", text);
        panel.receive({ type: "loaded" });
        // The analysis may only be sent once the init message established the language.
        await waitFor(() => panel.messages.some((message) => message.type === "init"), "the init message");
        await services.analyse(document);
        const diagnostics = panel.messages.filter((message) => message.type === "diagnostics").pop();
        assert.ok(diagnostics, "no diagnostics were sent");
        assert.strictEqual(diagnostics.diagnostics[0].code, "S005");
        assert.strictEqual(diagnostics.diagnostics[0].line, 1);
        const candidates = panel.messages.filter((message) => message.type === "candidates").pop();
        assert.ok(candidates.candidates.some((item) => item.label === "ن" && item.kind === "name"),
            "the program's own names are offered");
    });

    test("closing the editor lets go of everything", async () =>
    {
        const { panel, document } = await open("close.th", "x\n");
        assert.ok(provider.open.some((entry) => entry.document === document));
        panel.dispose();
        assert.ok(!provider.open.some((entry) => entry.document === document));
    });
});
