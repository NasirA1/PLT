// The program's output while it is debugged, laid out in the program's direction.
//
// VS Code's Debug Console is part of VS Code: an extension can write to it but cannot set its
// alignment or direction, so a Pashto program's output sits on the left there, numbers and
// words alike. This view (Panel > Thunder Output) shows the same output the way the Thunder IDE's
// Output panel does: every line aligned to the program's side - right for Pashto - each line
// reading in the direction of its own text, and an input line beneath for input().
//
// The debug adapter's output reaches it through the inline adapter (extension.js), so the Debug
// Console keeps everything it had; this view is an additional, better-shaped copy.

"use strict";

const path = require("path");
const vscode = require("vscode");

const VIEW_ID = "thunder.programOutput";

class ProgramOutputView
{
    /**
     * @param services the extension's services (configuration, languages, the extension's path)
     */
    constructor(services)
    {
        this.services = services;
        this.view = null;
        this.pending = [];          // messages for a view that is not resolved yet
        this.session = null;        // the debug session whose output is shown
        this.rightToLeft = false;
        this.waitingForInput = false;
    }

    resolveWebviewView(view)
    {
        this.view = view;
        const root = this.services.context.extensionPath;
        view.webview.options = {
            enableScripts: true,
            localResourceRoots: [vscode.Uri.file(path.join(root, "media"))],
        };
        view.webview.html = this.html(view.webview);
        view.webview.onDidReceiveMessage((message) => this.onMessage(message));
        if (view.onDidDispose)
        {
            view.onDidDispose(() =>
            {
                this.view = null;
            });
        }
    }

    /** True when a program's output should come here: its setting, and whether it reads right to left. */
    wanted(rightToLeft, file)
    {
        const setting = this.services.configuration(file ? vscode.Uri.file(file) : null).get("debug.outputView") || "auto";
        return setting === "always" || (setting === "auto" && rightToLeft);
    }

    /** A debug session starts: a clean view, in the program's direction, brought into sight if wanted. */
    async begin(session, program)
    {
        this.session = session;
        this.waitingForInput = false;
        let rightToLeft = false;
        try
        {
            const code = await this.services.languageCodeFor(program);
            rightToLeft = Boolean((await this.services.languageData(code)).rightToLeft);
        }
        catch (error)
        {
            rightToLeft = false; // the language could not be told: left to right, like the console
        }
        this.rightToLeft = rightToLeft;
        this.post({ type: "begin", rightToLeft, title: path.basename(program) });
        if (this.wanted(rightToLeft, program))
        {
            await this.reveal();
        }
    }

    append(text, category)
    {
        if (text)
        {
            this.post({ type: "append", text, category: category || "stdout" });
        }
    }

    /** The program waits for input(): the view's input line takes it. True if the view will ask. */
    askForInput()
    {
        if (!this.view || !this.view.visible)
        {
            return false;
        }
        this.waitingForInput = true;
        this.post({ type: "input" });
        return true;
    }

    end(text)
    {
        this.waitingForInput = false;
        this.post({ type: "end", text: text || "" });
    }

    async onMessage(message)
    {
        if (message && message.type === "ready")
        {
            this.flush();
            return;
        }
        if (message && message.type === "input" && this.session && this.waitingForInput)
        {
            this.waitingForInput = false;
            await this.session.customRequest("evaluate", { expression: String(message.text || ""), context: "repl" });
        }
    }

    async reveal()
    {
        if (this.view && this.view.show)
        {
            this.view.show(true);
            return;
        }
        await vscode.commands.executeCommand(`${VIEW_ID}.focus`, { preserveFocus: true });
    }

    post(message)
    {
        if (this.view)
        {
            this.view.webview.postMessage(message);
        }
        else
        {
            this.pending.push(message);
        }
    }

    /** Called by the page once it listens: what came before it was ready. */
    flush()
    {
        const pending = this.pending;
        this.pending = [];
        for (const message of pending)
        {
            this.view.webview.postMessage(message);
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
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; font-src ${webview.cspSource}; style-src ${webview.cspSource} 'unsafe-inline'; script-src 'nonce-${nonce}';">
<link rel="stylesheet" href="${media("output", "output.css")}">
<style>
@font-face {
    font-family: "Thunder Pashto";
    src: url("${media("fonts", "Vazirmatn[wght].ttf")}") format("truetype");
    unicode-range: U+0600-06FF, U+0750-077F, U+08A0-08FF, U+FB50-FDFF, U+FE70-FEFF;
}
</style>
<title>Thunder Output</title>
</head>
<body>
<div id="output" role="log" aria-live="polite"></div>
<form id="input-row" hidden><input id="input" autocomplete="off" spellcheck="false"></form>
<script nonce="${nonce}" src="${media("output", "output.js")}"></script>
</body>
</html>`;
    }
}

module.exports = { ProgramOutputView, VIEW_ID };
