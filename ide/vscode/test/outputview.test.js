// The Thunder Output view: the debugged program's output in the program's direction, with an
// input line for input(). The page's layout is checked in Chromium by
// tests/python/rtleditor_browser.py; here, what the view tells the page and does with its answers.

"use strict";

const fs = require("fs");
const path = require("path");

const { EXTENSION, assert, before, suite, test } = require("./harness");
const vscode = require("./fake-vscode");

const { ProgramOutputView, VIEW_ID } = require("../src/outputview");

function makeView()
{
    const listeners = [];
    return {
        visible: true,
        shown: [],
        messages: [],
        webview: {
            html: "",
            options: {},
            cspSource: "vscode-webview:",
            asWebviewUri: (uri) => uri,
            postMessage(message)
            {
                this.owner.messages.push(message);
                return Promise.resolve(true);
            },
            onDidReceiveMessage: (callback) => listeners.push(callback),
        },
        show(preserveFocus)
        {
            this.shown.push(preserveFocus);
        },
        receive: (message) => Promise.all(listeners.map((callback) => callback(message))),
    };
}

function makeServices(settings, rightToLeft)
{
    return {
        context: { extensionPath: EXTENSION },
        configuration: () => ({ get: (key) => settings[key] }),
        languageCodeFor: async (file) => (file.includes("pashto") ? "ps" : "en"),
        languageData: async (code) => ({ code, rightToLeft: rightToLeft === undefined ? code === "ps" : rightToLeft }),
    };
}

suite("Thunder Output: the program's output in its direction", () =>
{
    before(() => vscode._reset());

    test("the page loads only its own files, and every file it loads is there", () =>
    {
        const output = new ProgramOutputView(makeServices({}));
        const view = makeView();
        view.webview.owner = view;
        output.resolveWebviewView(view);
        assert.strictEqual(VIEW_ID, "thunder.programOutput");
        assert.ok(/script-src 'nonce-\d+'/.test(view.webview.html));
        for (const reference of view.webview.html.matchAll(/(?:src|href)="([^"]+)"/g))
        {
            const file = String(reference[1]).replace(/^file:/, "");
            assert.ok(fs.existsSync(path.resolve(vscode.Uri.parse(reference[1]).fsPath)), `${file} is missing`);
        }
    });

    test("a Pashto program's session starts right to left and brings the view forward", async () =>
    {
        const output = new ProgramOutputView(makeServices({ "debug.outputView": "auto" }));
        const view = makeView();
        view.webview.owner = view;
        output.resolveWebviewView(view);
        await output.begin({ id: "s1" }, "/work/pashto.th");
        assert.deepStrictEqual(view.messages[0], { type: "begin", rightToLeft: true, title: "pashto.th" });
        assert.deepStrictEqual(view.shown, [true], "shown without taking the focus");
        output.append("۴۲\n", "stdout");
        output.append("", "stdout");
        output.end();
        assert.deepStrictEqual(view.messages.slice(1), [
            { type: "append", text: "۴۲\n", category: "stdout" },
            { type: "end", text: "" },
        ]);
    });

    test("an English program stays in the panel unless the setting says always", async () =>
    {
        const quiet = new ProgramOutputView(makeServices({ "debug.outputView": "auto" }));
        const view = makeView();
        view.webview.owner = view;
        quiet.resolveWebviewView(view);
        await quiet.begin({ id: "s2" }, "/work/english.th");
        assert.strictEqual(view.messages[0].rightToLeft, false);
        assert.deepStrictEqual(view.shown, []);
        const always = new ProgramOutputView(makeServices({ "debug.outputView": "always" }));
        assert.ok(always.wanted(false, "/work/english.th"));
        const off = new ProgramOutputView(makeServices({ "debug.outputView": "off" }));
        assert.ok(!off.wanted(true, "/work/pashto.th"));
    });

    test("takes() tells the launch, ahead of the session, whether the view will show it", async () =>
    {
        // The launch configuration asks before the session starts, to keep the Debug Console
        // from opening over the view (extension.js sets internalConsoleOptions from this).
        const auto = new ProgramOutputView(makeServices({ "debug.outputView": "auto" }));
        assert.ok(await auto.takes("/work/pashto.th"));
        assert.ok(!(await auto.takes("/work/english.th")));
        const always = new ProgramOutputView(makeServices({ "debug.outputView": "always" }));
        assert.ok(await always.takes("/work/english.th"));
    });

    test("what arrives before the view exists is kept until the page is ready", async () =>
    {
        const output = new ProgramOutputView(makeServices({ "debug.outputView": "off" }));
        output.append("early\n");
        const view = makeView();
        view.webview.owner = view;
        output.resolveWebviewView(view);
        assert.strictEqual(view.messages.length, 0);
        await view.receive({ type: "ready" });
        assert.deepStrictEqual(view.messages, [{ type: "append", text: "early\n", category: "stdout" }]);
    });

    test("input() is answered from the view's input line, through the session", async () =>
    {
        const requests = [];
        const session = { customRequest: async (command, args) => requests.push({ command, args }) };
        const output = new ProgramOutputView(makeServices({ "debug.outputView": "auto" }));
        const view = makeView();
        view.webview.owner = view;
        output.resolveWebviewView(view);
        await output.begin(session, "/work/pashto.th");
        assert.ok(output.askForInput(), "a visible view asks itself");
        assert.deepStrictEqual(view.messages.pop(), { type: "input" });
        await view.receive({ type: "input", text: "ناصر" });
        assert.deepStrictEqual(requests, [{ command: "evaluate", args: { expression: "ناصر", context: "repl" } }]);
        await view.receive({ type: "input", text: "again" });
        assert.strictEqual(requests.length, 1, "a line nobody asked for is not sent");
        view.visible = false;
        assert.ok(!output.askForInput(), "a hidden view leaves it to the input box");
    });
});
