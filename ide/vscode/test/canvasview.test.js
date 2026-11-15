// The Thunder Canvas: the panel that draws a debugged graphics program's frames
// (src/canvasview.js). The page's own painting is checked in Chromium by
// tests/python/rtleditor_browser.py; here, what the panel does with frames and sessions.

"use strict";

const fs = require("fs");
const path = require("path");

const { EXTENSION, assert, before, suite, test } = require("./harness");
const vscode = require("./fake-vscode");

const { CanvasPanel, VIEW_TYPE } = require("../src/canvasview");

const FRAME = { type: "frame", n: 1, width: 50, height: 40, keep: 0, shapes: [["rect", 5, 6, 10, 10, "#ff0000"]] };

function makeSession(name)
{
    return {
        name: name || "Debug main.th",
        requests: [],
        async customRequest(command, args)
        {
            this.requests.push({ command, args });
        },
    };
}

suite("Thunder Canvas: a graphics program's picture", () =>
{
    before(() => vscode._reset());

    test("the first frame opens the panel beside the editor, without taking the focus", () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        const session = makeSession();
        canvas.showFrame(session, FRAME);
        const panel = vscode._lastWebview();
        assert.ok(panel, "a panel was opened");
        assert.strictEqual(panel.viewType, VIEW_TYPE);
        assert.strictEqual(panel.showOptions.preserveFocus, true, "stepping must go on undisturbed");
        // Nothing reaches the page until it says it is ready; then everything held arrives.
        assert.deepStrictEqual(panel.messages, []);
        panel.receive({ type: "ready" });
        assert.strictEqual(panel.messages[0].type, "begin");
        assert.deepStrictEqual(panel.messages[1], { type: "frame", record: FRAME });
        // Every file the page loads is there.
        for (const reference of panel.webview.html.matchAll(/(?:src|href)="([^"]+)"/g))
        {
            const file = vscode.Uri.parse(reference[1]).fsPath;
            assert.ok(fs.existsSync(path.resolve(file)), `${file} is missing`);
        }
    });

    test("the page's keys and mouse travel to the session as canvas events", async () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        const session = makeSession();
        canvas.showFrame(session, FRAME);
        const panel = vscode._lastWebview();
        panel.receive({ type: "ready" });
        await panel.receive({ type: "event", text: "key down left" });
        await panel.receive({ type: "event", text: "mouse 20 30 1" });
        assert.deepStrictEqual(session.requests, [
            { command: "thunder/canvasEvent", args: { text: "key down left" } },
            { command: "thunder/canvasEvent", args: { text: "mouse 20 30 1" } },
        ]);
    });

    test("a panel the user closed stays closed for that run, and a new session opens it again", () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        const opened = () => vscode._state.webviews.length - before;
        const before = vscode._state.webviews.length;
        const first = makeSession("first");
        canvas.showFrame(first, FRAME);
        const panel = vscode._lastWebview();
        panel.dispose();
        canvas.showFrame(first, FRAME);
        assert.strictEqual(opened(), 1, "it must not fight the user");
        canvas.end(first);
        const second = makeSession("second");
        canvas.showFrame(second, FRAME);
        assert.strictEqual(opened(), 2, "the next program gets its canvas");
        const reopened = vscode._lastWebview();
        reopened.receive({ type: "ready" });
        assert.strictEqual(reopened.messages[0].type, "begin", "a new session starts with a clean page");
    });
});
