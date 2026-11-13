// The right-to-left view: a Pashto program laid out as it is meant to be read.
//
// VS Code's editor cannot do this itself. Its line renderer puts `dir="ltr"` on every line that
// contains right-to-left text and only lays a line out right-to-left when a *model decoration*
// carries a `textDirection`, which no extension API exposes (see README.md, "Right-to-left
// programs"). So the extension shows the program the way the Qt IDE shows it - token by token
// in the direction of the program's language - in a panel beside the editor, which is a
// webview, where the direction *is* under our control.
//
// The layout comes from src/direction.js, the same algorithm as the IDE's, and the colours from
// the compiler's own token stream, so what is shown agrees with both. The view follows the
// active editor, updates as the file is edited, marks the line the caret is on, and moves the
// caret when a line is clicked.

"use strict";

const path = require("path");
const vscode = require("vscode");

const { analyseLine } = require("./direction");
const { splitLines } = require("./util");

const VIEW_TYPE = "thunder.rightToLeft";

/** The colour each kind of token is drawn in, as VS Code theme variables. */
const TOKEN_COLOURS = {
    keyword: "var(--vscode-charts-blue)",
    type: "var(--vscode-charts-purple)",
    literal: "var(--vscode-charts-orange)",
    builtin: "var(--vscode-charts-green)",
    identifier: "var(--vscode-editor-foreground)",
    operator: "var(--vscode-editor-foreground)",
    punctuation: "var(--vscode-descriptionForeground)",
    comment: "var(--vscode-descriptionForeground)",
    string: "var(--vscode-charts-orange)",
    number: "var(--vscode-charts-orange)",
};

/**
 * The pieces one line is drawn from.
 *
 * `tokens` are the compiler's tokens that start on this line, as {startCharacter, endCharacter,
 * kind}; everything they do not cover (spaces, operators the lexer calls punctuation, comments,
 * which the lexer does not report at all) is drawn in the default colour.
 */
function renderLine(text, rightToLeft, tokens)
{
    const layout = analyseLine(text, rightToLeft);
    const classes = new Array(layout.display.length).fill("");
    for (const token of (tokens || []))
    {
        const start = Math.max(0, Math.min(token.startCharacter, text.length));
        const end = Math.max(start, Math.min(token.endCharacter, text.length));
        if (end <= start)
        {
            continue;
        }
        // The marks around a token are left outside its span, so they stay plain text.
        const from = layout.offsets[start];
        const to = layout.offsets[end - 1] + 1;
        for (let index = from; index < to && index < classes.length; ++index)
        {
            classes[index] = token.kind;
        }
    }
    // A comment is not in the token stream (the lexer skips it), so it is found in the text.
    const comment = text.indexOf("//");
    if (comment >= 0 && !insideString(text, comment))
    {
        for (let index = layout.offsets[comment]; index < classes.length; ++index)
        {
            classes[index] = "comment";
        }
    }

    let html = "";
    let index = 0;
    while (index < layout.display.length)
    {
        const kind = classes[index];
        let end = index;
        while (end < layout.display.length && classes[end] === kind)
        {
            ++end;
        }
        const piece = escapeHtml(layout.display.slice(index, end));
        html += kind ? `<span class="t-${kind}">${piece}</span>` : piece;
        index = end;
    }
    return html;
}

/** True if the `//` at `position` is inside a string literal (then it is not a comment). */
function insideString(text, position)
{
    let quote = null;
    for (let index = 0; index < position; ++index)
    {
        const character = text[index];
        if (quote)
        {
            if (character === "\\")
            {
                ++index;
            }
            else if (character === quote)
            {
                quote = null;
            }
        }
        else if (character === "\"" || character === "«" || character === "“" || character === "„")
        {
            quote = character === "«" ? "»" : (character === "\"" ? "\"" : "”");
        }
    }
    return quote !== null;
}

function escapeHtml(text)
{
    return String(text)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;");
}

/**
 * The whole document as HTML (the body of the view).
 *
 * Pure: `tokensByLine` is a Map from a 0-based line number to that line's tokens, `activeLine`
 * the line the caret is on, or -1.
 */
function renderDocument(text, rightToLeft, tokensByLine, activeLine)
{
    const lines = splitLines(text);
    const rows = lines.map((line, number) =>
    {
        const content = renderLine(line, rightToLeft, (tokensByLine || new Map()).get(number));
        const current = number === activeLine ? " current" : "";
        return `<div class="row${current}" data-line="${number}">`
            + `<span class="number">${number + 1}</span>`
            + `<span class="code">${content || "&#8203;"}</span>`
            + "</div>";
    });
    return rows.join("\n");
}

/**
 * The tokens of a report, grouped by the line they start on.
 *
 * The lexer calls every name an identifier - a built-in is only recognised as one later, by the
 * semantic analyser - so the language's own list of built-ins is used to colour them, exactly
 * as the editor's semantic highlighting does.
 */
function tokensByLine(model, language)
{
    const grouped = new Map();
    if (!model)
    {
        return grouped;
    }
    for (const token of model.tokens())
    {
        if (token.category === "special" || token.range.start.line !== token.range.end.line)
        {
            continue;
        }
        const line = token.range.start.line;
        if (!grouped.has(line))
        {
            grouped.set(line, []);
        }
        grouped.get(line).push({
            startCharacter: token.range.start.character,
            endCharacter: token.range.end.character,
            kind: kindOf(token, language),
        });
    }
    return grouped;
}

function kindOf(token, language)
{
    if (token.category === "identifier" && language && language.builtinNamed(token.lexeme))
    {
        return "builtin";
    }
    if (token.category === "literal")
    {
        if (token.kind === "StringLiteral")
        {
            return "string";
        }
        return token.kind === "IntLiteral" || token.kind === "FloatLiteral" ? "number" : "literal";
    }
    return token.category;
}

class RightToLeftView
{
    constructor(services)
    {
        this.services = services;
        this.panel = null;
        this.file = null;
    }

    dispose()
    {
        if (this.panel)
        {
            this.panel.dispose();
            this.panel = null;
        }
    }

    get isOpen()
    {
        return Boolean(this.panel);
    }

    /** Opens the view (or brings it forward) and shows a document in it. */
    async show(document, options)
    {
        const settings = options || {};
        if (!this.panel)
        {
            this.panel = vscode.window.createWebviewPanel(
                VIEW_TYPE,
                "Thunder: right to left",
                { viewColumn: vscode.ViewColumn.Beside, preserveFocus: settings.preserveFocus !== false },
                { enableScripts: true, retainContextWhenHidden: true });
            this.panel.onDidDispose(() =>
            {
                this.panel = null;
                this.file = null;
            });
            this.panel.webview.onDidReceiveMessage((message) => this.onMessage(message));
        }
        else if (settings.reveal)
        {
            this.panel.reveal(vscode.ViewColumn.Beside, settings.preserveFocus !== false);
        }
        this.file = document.uri.fsPath;   // show() is the only thing that changes which file
        await this.update(document);
    }

    /**
     * Redraws the view for a document.
     *
     * Nothing happens if the view is closed, or if the document is not the one on show: an edit
     * in a file in another tab must not take the view away from the file being read.
     */
    async update(document, activeLine)
    {
        if (!this.panel || !document)
        {
            return;
        }
        if (this.file && path.resolve(this.file) !== path.resolve(document.uri.fsPath))
        {
            return;
        }
        this.file = document.uri.fsPath;
        const language = this.services.languageOfText(document.getText(), document.uri.fsPath);
        const data = await this.services.languageData(language);
        const model = this.services.analyser.modelFor(document.uri.fsPath);
        const line = activeLine === undefined ? caretLineOf(document) : activeLine;
        this.panel.title = `${path.basename(document.uri.fsPath)} (${data.nativeName})`;
        this.panel.webview.html = this.html(
            renderDocument(document.getText(), data.rightToLeft, tokensByLine(model, data), line),
            data);
    }

    onMessage(message)
    {
        if (!message || message.command !== "goto" || !this.file)
        {
            return;
        }
        const editor = vscode.window.visibleTextEditors.find(
            (candidate) => path.resolve(candidate.document.uri.fsPath) === path.resolve(this.file));
        if (!editor)
        {
            return;
        }
        const position = new vscode.Position(Math.max(0, Number(message.line) || 0), 0);
        editor.selection = new vscode.Selection(position, position);
        editor.revealRange(new vscode.Range(position, position), vscode.TextEditorRevealType.InCenterIfOutsideViewport);
    }

    html(body, language)
    {
        const webview = this.panel.webview;
        const nonce = String(Math.random()).slice(2);
        const colours = Object.entries(TOKEN_COLOURS)
            .map(([kind, colour]) => `.t-${kind} { color: ${colour}; }`)
            .join("\n        ");
        return `<!DOCTYPE html>
<html lang="${language.code}" dir="${language.rightToLeft ? "rtl" : "ltr"}">
<head>
    <meta charset="UTF-8">
    <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
    <title>Thunder: right to left</title>
    <style>
        body {
            margin: 0;
            padding: 0.4rem 0;
            background: var(--vscode-editor-background);
            color: var(--vscode-editor-foreground);
            font-family: var(--vscode-editor-font-family), monospace;
            font-size: var(--vscode-editor-font-size, 14px);
            line-height: 1.5;
        }
        .row { display: flex; white-space: pre; }
        .row:hover { background: var(--vscode-editor-hoverHighlightBackground); }
        .row.current { background: var(--vscode-editor-lineHighlightBackground, rgba(128,128,128,0.12)); }
        .number {
            flex: 0 0 3.5rem;
            text-align: ${language.rightToLeft ? "left" : "right"};
            padding: 0 0.8rem;
            color: var(--vscode-editorLineNumber-foreground);
            user-select: none;
            direction: ltr;
        }
        /* The line takes the program's direction (from <html dir>), and the isolate marks that
           src/direction.js put around each token do the rest. "isolate" keeps a line from
           affecting its neighbours. */
        .code { flex: 1 1 auto; unicode-bidi: isolate; }
        .row.current .number { color: var(--vscode-editorLineNumber-activeForeground); }
        ${colours}
    </style>
</head>
<body>
    <div id="program">
${body}
    </div>
    <script nonce="${nonce}">
        const api = acquireVsCodeApi();
        document.getElementById("program").addEventListener("click", (event) => {
            const row = event.target.closest(".row");
            if (row) {
                api.postMessage({ command: "goto", line: Number(row.dataset.line) });
            }
        });
    </script>
</body>
</html>`;
    }
}

function caretLineOf(document)
{
    const editor = vscode.window.visibleTextEditors.find(
        (candidate) => candidate.document === document);
    return editor ? editor.selection.active.line : -1;
}

module.exports = { RightToLeftView, VIEW_TYPE, renderDocument, renderLine, tokensByLine };
