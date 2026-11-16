// The Thunder Canvas: a debugged graphics program's picture, drawn in a webview panel.
//
// The virtual machine sends every frame of a graphics program to the debug adapter (a "frame"
// event with the record of thundervm --canvas), the adapter hands it on as the custom DAP event
// "thunder/frame" (extension.js), and this panel draws it - the way the Thunder IDE's Canvas
// panel does. The keys and the mouse travel the other way: the page reports them, and they reach
// the machine as "thunder/canvasEvent" requests, so key_pressed() and mouse_x() answer as they
// do under the IDE.
//
// The panel is an editor tab, so it can go wherever a tab can: beside the program (the
// default), into a group of its own, or - VS Code 1.85 and later - into a window of its own
// that the desktop resizes, maximises and minimises like any other. "Thunder: Move the Canvas
// to Its Own Window" does that on request, `thunder.canvas.window` does it at the first frame,
// and "Thunder: Bring the Canvas Back" docks it beside the editor again. Because the host keeps
// the shapes drawn so far (applying each frame's `keep` as the page does), a canvas can be
// closed, moved or re-created at any moment without losing the picture.

"use strict";

const path = require("path");
const vscode = require("vscode");

const VIEW_TYPE = "thunder.canvas";

class CanvasPanel
{
    /**
     * @param services the extension's services (the extension's path)
     */
    constructor(services)
    {
        this.services = services;
        this.panel = null;
        this.session = null;     // the debug session whose picture is shown
        this.pending = [];       // messages for a page that is not ready yet
        this.ready = false;
        this.closedFor = null;   // the session whose panel the user closed: stay closed for it
        this.picture = null;     // {width, height, n, shapes}: the picture so far, to re-create a panel
        this.floating = false;   // the panel was moved to a window of its own
        this.setContext("thunder.canvasFloating", false);
    }

    /** The `thunder.canvas.window` setting: "beside" (the default) or "newWindow". */
    placement()
    {
        try
        {
            return vscode.workspace.getConfiguration("thunder").get("canvas.window", "beside");
        }
        catch (error)
        {
            return "beside";
        }
    }

    setContext(key, value)
    {
        try
        {
            const outcome = vscode.commands.executeCommand("setContext", key, value);
            if (outcome && typeof outcome.catch === "function")
            {
                outcome.catch(() => {});
            }
        }
        catch (error)
        {
            // the tests' stand-in may not know the command
        }
    }

    /** A frame from a session: the panel opens by itself on the first one, beside the editor. */
    showFrame(session, record)
    {
        if (!record)
        {
            return;
        }
        if (this.closedFor === session)
        {
            return; // the user closed the canvas for this program's run: leave it closed
        }
        if (this.session !== session)
        {
            this.session = session;
            this.pending = []; // whatever an earlier session left unposted is history
            this.picture = null;
            this.post({ type: "begin", title: session && session.name ? session.name : "" });
        }
        this.remember(record);
        const opening = !this.panel;
        this.ensurePanel();
        this.post({ type: "frame", record });
        if (opening && this.placement() === "newWindow")
        {
            this.popOut();
        }
    }

    /** Keeps the picture as the page will have it: the first `keep` shapes, then the new ones. */
    remember(record)
    {
        if (!record || record.type !== "frame")
        {
            return;
        }
        const previous = this.picture || { width: 400, height: 300, n: 0, shapes: [] };
        const keep = Math.max(0, Number(record.keep) || 0);
        const added = Array.isArray(record.shapes) ? record.shapes.filter(Array.isArray) : [];
        this.picture = {
            width: Math.max(1, Number(record.width) || previous.width),
            height: Math.max(1, Number(record.height) || previous.height),
            n: Number(record.n) || 0,
            shapes: previous.shapes.slice(0, keep).concat(added),
        };
    }

    /** Opens the canvas (again) with the picture so far - "Thunder: Show the Canvas". */
    show()
    {
        this.closedFor = null;
        this.ensurePanel();
        this.replay();
        if (this.panel)
        {
            this.panel.reveal(undefined, true);
        }
    }

    /** The picture so far, sent as a whole to a freshly created page. */
    replay()
    {
        if (!this.picture)
        {
            return;
        }
        this.post({ type: "begin", title: this.session && this.session.name ? this.session.name : "" });
        this.post({ type: "frame", record: Object.assign({ type: "frame", keep: 0 }, this.picture) });
    }

    /**
     * Moves the canvas into a window of its own (VS Code 1.85+: "Move Editor into New
     * Window"), which the desktop then sizes, maximises and minimises like any window.
     */
    async popOut()
    {
        this.closedFor = null;
        const created = !this.panel;
        this.ensurePanel();
        if (created)
        {
            this.replay();
        }
        // The workbench command acts on the active editor: bring the canvas forward with the focus.
        this.panel.reveal(undefined, false);
        try
        {
            await vscode.commands.executeCommand("workbench.action.moveEditorToNewWindow");
            this.floating = true;
            this.setContext("thunder.canvasFloating", true);
            return true;
        }
        catch (error)
        {
            vscode.window.showWarningMessage(
                "This VS Code cannot move an editor into its own window (that needs VS Code 1.85 or later). "
                + "The canvas can still be dragged to another editor group.");
            return false;
        }
    }

    /**
     * Brings the canvas back beside the program: the floating panel is closed and a new one is
     * created in the main window with the picture so far. (Closing the last editor of a floating
     * window closes that window too, so nothing is left behind.)
     */
    dock()
    {
        if (this.panel)
        {
            const old = this.panel;
            this.panel = null; // so its dispose event reads as ours, not as the user closing it
            this.ready = false;
            this.pending = [];
            old.dispose();
        }
        this.floating = false;
        this.setContext("thunder.canvasFloating", false);
        this.closedFor = null;
        this.ensurePanel();
        this.replay();
    }

    /** The session ended: the picture stays, the page may say so. */
    end(session)
    {
        if (this.session === session)
        {
            if (this.panel)
            {
                this.post({ type: "end" });
            }
            this.session = null;
        }
        if (this.closedFor === session)
        {
            this.closedFor = null;
        }
    }

    ensurePanel()
    {
        if (this.panel)
        {
            return;
        }
        const root = this.services.context.extensionPath;
        this.panel = vscode.window.createWebviewPanel(VIEW_TYPE, "Thunder Canvas",
            { viewColumn: vscode.ViewColumn.Beside, preserveFocus: true },
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                localResourceRoots: [vscode.Uri.file(path.join(root, "media"))],
            });
        this.ready = false;
        this.panel.webview.html = this.html(this.panel.webview);
        this.panel.webview.onDidReceiveMessage((message) => this.onMessage(message));
        const panel = this.panel;
        panel.onDidDispose(() =>
        {
            if (this.panel !== panel)
            {
                return; // a panel dock() replaced: its closing is our doing, not the user's
            }
            this.panel = null;
            this.ready = false;
            this.pending = [];
            this.closedFor = this.session; // closed by hand: not again for this session
            this.floating = false;
            this.setContext("thunder.canvasFloating", false);
        });
    }

    async onMessage(message)
    {
        if (!message)
        {
            return;
        }
        if (message.type === "ready")
        {
            this.ready = true;
            const pending = this.pending;
            this.pending = [];
            for (const held of pending)
            {
                this.panel.webview.postMessage(held);
            }
            return;
        }
        if (message.type === "event" && typeof message.text === "string" && this.session)
        {
            try
            {
                await this.session.customRequest("thunder/canvasEvent", { text: message.text });
            }
            catch (error)
            {
                // the program may just have ended; the next session starts afresh
            }
        }
    }

    post(message)
    {
        if (this.panel && this.ready)
        {
            this.panel.webview.postMessage(message);
        }
        else
        {
            this.pending.push(message);
        }
    }

    html(webview)
    {
        const media = (...parts) => webview.asWebviewUri(
            vscode.Uri.file(path.join(this.services.context.extensionPath, "media", ...parts)));
        const nonce = String(Math.random()).slice(2) + String(Date.now());
        return `<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${webview.cspSource}; script-src 'nonce-${nonce}';">
<link rel="stylesheet" href="${media("canvas", "canvas.css")}">
<title>Thunder Canvas</title>
</head>
<body>
<canvas id="picture" tabindex="0"></canvas>
<div id="status" hidden></div>
<script nonce="${nonce}" src="${media("canvas", "canvas.js")}"></script>
</body>
</html>`;
    }
}

module.exports = { CanvasPanel, VIEW_TYPE };
