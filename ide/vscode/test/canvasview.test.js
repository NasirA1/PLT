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

    const SECOND = { type: "frame", n: 2, width: 50, height: 40, keep: 1, shapes: [["line", 0, 0, 9, 9, "#000000"]] };
    const THIRD = { type: "frame", n: 3, width: 50, height: 40, keep: 0, shapes: [["clear", "#ffffff"]] };
    const moved = () => vscode._state.executed.filter((entry) => entry.name === "workbench.action.moveEditorToNewWindow");
    const floating = () => vscode._state.executed.filter((entry) => entry.name === "setContext" && entry.args[0] === "thunder.canvasFloating")
        .map((entry) => entry.args[1]);

    test("the host keeps the picture as the page does: each frame's keep, then its shapes", () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        const session = makeSession();
        canvas.showFrame(session, FRAME);
        canvas.showFrame(session, SECOND);
        assert.deepStrictEqual(canvas.picture.shapes, [FRAME.shapes[0], SECOND.shapes[0]], "keep: 1 keeps the first shape");
        assert.strictEqual(canvas.picture.n, 2);
        canvas.showFrame(session, THIRD);
        assert.deepStrictEqual(canvas.picture.shapes, THIRD.shapes, "keep: 0 starts the picture afresh");
    });

    test("Move the Canvas to Its Own Window brings the canvas forward with the focus and asks the workbench to move it", async () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        const session = makeSession();
        canvas.showFrame(session, FRAME);
        const panel = vscode._lastWebview();
        assert.strictEqual(moved().length, 0, "beside is the default: nothing moves by itself");
        assert.strictEqual(await canvas.popOut(), true);
        assert.deepStrictEqual(panel.revealed.at(-1), { column: undefined, preserveFocus: false },
            "the workbench command acts on the active editor, so the canvas must be it");
        assert.strictEqual(moved().length, 1);
        assert.strictEqual(floating().at(-1), true, "the title bar now offers Bring the Canvas Back");
        assert.strictEqual(vscode._state.webviews.at(-1), panel, "the same panel: the picture is not re-created");
    });

    test("Bring the Canvas Back re-creates the panel beside the program with the picture so far, and the run goes on", () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        const session = makeSession();
        canvas.showFrame(session, FRAME);
        canvas.showFrame(session, SECOND);
        const before = vscode._lastWebview();
        canvas.floating = true;
        canvas.dock();
        const after = vscode._lastWebview();
        assert.ok(before.disposed, "the floating panel is closed (and with it its window)");
        assert.notStrictEqual(after, before, "a new panel in the main window");
        assert.strictEqual(after.showOptions.viewColumn, vscode.ViewColumn.Beside);
        after.receive({ type: "ready" });
        assert.strictEqual(after.messages[0].type, "begin");
        assert.deepStrictEqual(after.messages[1].record.shapes, [FRAME.shapes[0], SECOND.shapes[0]], "the whole picture, as one frame");
        assert.strictEqual(after.messages[1].record.keep, 0);
        assert.strictEqual(floating().at(-1), false);
        // Our own dispose was not the user closing the canvas: the next frame still arrives.
        canvas.showFrame(session, THIRD);
        assert.deepStrictEqual(after.messages.at(-1), { type: "frame", record: THIRD });
        assert.strictEqual(canvas.closedFor, null);
    });

    test("thunder.canvas.window = newWindow moves the canvas out at the first frame, once", () =>
    {
        vscode._state.configuration["thunder.canvas.window"] = "newWindow";
        try
        {
            const already = moved().length;
            const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
            const session = makeSession();
            canvas.showFrame(session, FRAME);
            canvas.showFrame(session, SECOND);
            assert.strictEqual(moved().length - already, 1, "the first frame opens and moves it; later frames just draw");
        }
        finally
        {
            delete vscode._state.configuration["thunder.canvas.window"];
        }
    });

    test("Show the Canvas opens a closed canvas again with the picture so far", () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        const session = makeSession();
        canvas.showFrame(session, FRAME);
        vscode._lastWebview().dispose();
        assert.strictEqual(canvas.closedFor, session);
        canvas.show();
        const panel = vscode._lastWebview();
        assert.ok(!panel.disposed);
        panel.receive({ type: "ready" });
        assert.deepStrictEqual(panel.messages[1].record.shapes, FRAME.shapes);
        canvas.showFrame(session, SECOND);
        assert.deepStrictEqual(panel.messages.at(-1), { type: "frame", record: SECOND }, "the run is shown again");
    });

    test("an older VS Code that cannot move editors into windows is told so, and nothing breaks", async () =>
    {
        const canvas = new CanvasPanel({ context: { extensionPath: EXTENSION } });
        canvas.showFrame(makeSession(), FRAME);
        vscode._commands.set("workbench.action.moveEditorToNewWindow", () => { throw new Error("command not found"); });
        try
        {
            const warnings = vscode._state.messages.length;
            assert.strictEqual(await canvas.popOut(), false);
            assert.strictEqual(canvas.floating, false);
            assert.strictEqual(vscode._state.messages.length, warnings + 1, "the user is told why");
            assert.strictEqual(vscode._state.messages.at(-1).kind, "warning");
        }
        finally
        {
            vscode._commands.delete("workbench.action.moveEditorToNewWindow");
        }
    });
});
