// The Thunder Canvas: a debugged graphics program's picture, drawn in a webview panel.
//
// The virtual machine sends every frame of a graphics program to the debug adapter (a "frame"
// event with the record of thundervm --canvas), the adapter hands it on as the custom DAP event
// "thunder/frame" (extension.js), and this panel draws it - the way the Thunder IDE's Canvas
// panel does. The keys and the mouse travel the other way: the page reports them, and they reach
// the machine as "thunder/canvasEvent" requests, so key_pressed() and mouse_x() answer as they
// do under the IDE.

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
            this.post({ type: "begin", title: session && session.name ? session.name : "" });
        }
        this.ensurePanel();
        this.post({ type: "frame", record });
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
        this.panel.onDidDispose(() =>
        {
            this.panel = null;
            this.ready = false;
            this.pending = [];
            this.closedFor = this.session; // closed by hand: not again for this session
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
