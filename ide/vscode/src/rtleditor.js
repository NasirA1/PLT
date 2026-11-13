// The right-to-left editor: a second editor for a .th file, laid out the way Pashto is read.
//
// VS Code's text editor always lays code out left to right; nothing an extension may set
// changes that (see README.md, "Right-to-left programs"). So Pashto programs get an editor of
// their own: a *custom text editor*, which VS Code shows in a tab like any other and backs by
// the same TextDocument as the text editor. Saving, the dirty marker, Undo in the Edit menu, git
// and every other editor of the file therefore keep working - the two editors are two views of
// one document.
//
// The editing itself happens in a webview (media/rtleditor), where the direction is ours. This
// file only keeps the two sides in step: every change the webview makes becomes the smallest
// WorkspaceEdit of the document, every change from elsewhere is sent to the webview, and the
// compiler's problems and the names worth completing follow the document as it is analysed.

"use strict";

const path = require("path");
const vscode = require("vscode");

const model = require("../media/rtleditor/model");

const VIEW_TYPE = "thunder.rightToLeftEditor";

/** Every reserved word of a language, folded, mapped to how the editor colours it. */
function wordsFor(language)
{
    const words = {};
    for (const [word, meaning] of language.reservedWords())
    {
        const kind = meaning.category === "operator" ? "keyword" : meaning.category;
        words[model.foldLetters(word)] = kind;
    }
    return words;
}

/** The words after which Enter indents (`if`, `که` ...) and after which it outdents (`return` ...). */
function blockWordsFor(languages)
{
    const opening = new Set();
    const leaving = new Set();
    const OPENING = ["KwIf", "KwElse", "KwWhile", "KwFor", "KwFunc", "KwStruct"];
    const LEAVING = ["KwReturn", "KwBreak", "KwContinue", "KwPass"];
    for (const language of languages)
    {
        for (const keyword of language.keywords)
        {
            const target = OPENING.includes(keyword.kind) ? opening : (LEAVING.includes(keyword.kind) ? leaving : null);
            if (target)
            {
                for (const spelling of (keyword.spellings || [keyword.text]))
                {
                    target.add(model.foldLetters(spelling));
                }
            }
        }
    }
    return { opening: [...opening], leaving: [...leaving] };
}

/** What may be completed: the program's own names first, then the language's words. */
function candidatesFor(language, analysed)
{
    const candidates = [];
    if (analysed)
    {
        for (const declaration of analysed.allFunctions())
        {
            candidates.push({ label: declaration.name, kind: "function", detail: analysed.signatureOf(declaration) });
        }
        for (const declaration of analysed.allStructs())
        {
            candidates.push({ label: declaration.name, kind: "struct", detail: "struct" });
        }
        for (const symbol of analysed.allSymbols())
        {
            if (!symbol.fileIndex)
            {
                candidates.push({ label: symbol.name, kind: "name", detail: symbol.type });
            }
        }
    }
    for (const keyword of language.keywords)
    {
        candidates.push({ label: keyword.text, kind: keyword.category === "operator" ? "keyword" : keyword.category });
    }
    for (const builtin of language.builtins)
    {
        candidates.push({ label: builtin.name, kind: "builtin", detail: language.signatureOf(builtin) });
    }
    for (const module of language.modules)
    {
        candidates.push({ label: module.name, kind: "module", detail: module.canonical_name });
    }
    return candidates;
}

/** The compiler's problems in this file, as the webview shows them. */
function diagnosticsFor(analysed, file)
{
    if (!analysed)
    {
        return [];
    }
    return analysed.diagnostics()
        .filter((diagnostic) => !diagnostic.file || path.resolve(diagnostic.file) === path.resolve(file))
        .map((diagnostic) => ({
            line: diagnostic.range.start.line,
            character: diagnostic.range.start.character,
            code: diagnostic.code,
            message: diagnostic.message,
            severity: diagnostic.severity,
        }));
}

class RightToLeftEditorProvider
{
    /**
     * @param services the extension's services
     * @param options  {applyEdit} - replaced by the tests; defaults to vscode.workspace.applyEdit
     */
    constructor(services, options)
    {
        this.services = services;
        this.applyEdit = (options && options.applyEdit) || ((edit) => vscode.workspace.applyEdit(edit));
        this.sessions = new Set();
    }

    /** The webviews currently open, for the tests and for commands. */
    get open()
    {
        return [...this.sessions];
    }

    async resolveCustomTextEditor(document, panel)
    {
        const session = {
            document,
            panel,
            pending: 0,         // edits of ours still on their way into the document
            disposables: [],
        };
        this.sessions.add(session);
        const webview = panel.webview;
        webview.options = { enableScripts: true, localResourceRoots: this.resourceRoots() };
        webview.html = this.html(webview, document);

        const send = (message) => webview.postMessage(message);
        session.send = send;

        session.disposables.push(webview.onDidReceiveMessage((message) => this.onMessage(session, message)));
        session.disposables.push(vscode.workspace.onDidChangeTextDocument((event) =>
        {
            if (event.document !== document || event.contentChanges.length === 0)
            {
                return;
            }
            if (session.pending > 0)
            {
                --session.pending; // our own edit coming back
                return;
            }
            send({ type: "update", text: document.getText() });
        }));
        session.disposables.push(this.services.analyser.onDidAnalyse((file, analysed) =>
        {
            if (path.resolve(file) === path.resolve(document.uri.fsPath))
            {
                this.sendAnalysis(session, analysed);
            }
        }));
        panel.onDidDispose(() =>
        {
            for (const disposable of session.disposables)
            {
                disposable.dispose();
            }
            this.sessions.delete(session);
        });
        if (panel.onDidChangeViewState)
        {
            session.disposables.push(panel.onDidChangeViewState(() =>
            {
                if (panel.active && this.services.showLanguageStatus)
                {
                    this.services.showLanguageStatus(document);
                }
            }));
        }
        return session;
    }

    async onMessage(session, message)
    {
        if (!message)
        {
            return;
        }
        if (message.type === "loaded")
        {
            await this.sendInit(session);
            return;
        }
        if (message.type === "edit" && typeof message.text === "string")
        {
            await this.applyText(session, message.text);
        }
    }

    async sendInit(session)
    {
        const document = session.document;
        const code = this.services.languageOfText(document.getText(), document.uri.fsPath);
        const language = await this.services.languageData(code);
        const languages = [...(await this.services.allLanguages()).values()];
        const analysed = this.services.analyser.modelFor(document.uri.fsPath);
        session.language = language;
        session.send({
            type: "init",
            text: document.getText(),
            language: language.code,
            rightToLeft: language.rightToLeft,
            words: wordsFor(language),
            blockWords: blockWordsFor(languages),
            candidates: candidatesFor(language, analysed),
            diagnostics: diagnosticsFor(analysed, document.uri.fsPath),
        });
        // A first analysis, so the problems and the program's names arrive without an edit.
        if (this.services.diagnostics)
        {
            this.services.diagnostics.schedule(document);
        }
    }

    sendAnalysis(session, analysed)
    {
        if (!session.language)
        {
            return;
        }
        session.send({ type: "diagnostics", diagnostics: diagnosticsFor(analysed, session.document.uri.fsPath) });
        session.send({ type: "candidates", candidates: candidatesFor(session.language, analysed) });
    }

    /** Makes the document's text what the webview says it is, with the smallest edit. */
    async applyText(session, text)
    {
        const document = session.document;
        const current = document.getText();
        const change = model.difference(current, text);
        if (!change)
        {
            return true;
        }
        const edit = new vscode.WorkspaceEdit();
        edit.replace(document.uri, new vscode.Range(
            document.positionAt(change.start), document.positionAt(change.end)), change.text);
        ++session.pending;
        const applied = await this.applyEdit(edit);
        if (!applied)
        {
            --session.pending;
            session.send({ type: "update", text: document.getText() }); // put the webview back
        }
        return applied;
    }

    /**
     * The only directories the page may load from: its own files, and src/ for direction.js -
     * the same file the extension uses, so the editor and the IDE lay lines out alike.
     */
    resourceRoots()
    {
        const root = this.services.context.extensionPath;
        return [vscode.Uri.file(path.join(root, "media")), vscode.Uri.file(path.join(root, "src"))];
    }

    /** The webview's page: the editor, its model, the direction algorithm and the Pashto font. */
    html(webview, document)
    {
        const media = (...parts) => webview.asWebviewUri(
            vscode.Uri.file(path.join(this.services.context.extensionPath, "media", ...parts)));
        const direction = webview.asWebviewUri(
            vscode.Uri.file(path.join(this.services.context.extensionPath, "src", "direction.js")));
        const nonce = String(Math.random()).slice(2) + String(Date.now());
        const configured = this.services.configuration(document.uri).get("rightToLeft.pashtoFont") || "Vazirmatn";
        // The Pashto font covers only the Arabic script (unicode-range), so Latin letters, digits
        // and symbols stay in the editor's own font whatever that font is.
        const source = configured === "Vazirmatn"
            ? `url("${media("fonts", "Vazirmatn[wght].ttf")}") format("truetype")`
            : `local("${configured.replace(/"/g, "")}")`;
        return `<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src ${webview.cspSource}; font-src ${webview.cspSource}; style-src ${webview.cspSource} 'unsafe-inline'; script-src 'nonce-${nonce}';">
<link rel="stylesheet" href="${media("rtleditor", "editor.css")}">
<style>
@font-face {
    font-family: "Thunder Pashto";
    src: ${source};
    unicode-range: U+0600-06FF, U+0750-077F, U+08A0-08FF, U+FB50-FDFF, U+FE70-FEFF;
}
:root { --thunder-font-family: "Thunder Pashto", var(--vscode-editor-font-family), monospace; }
</style>
<title>${path.basename(document.uri.fsPath)}</title>
</head>
<body>
<div id="code" contenteditable="true" spellcheck="false" autocorrect="off" autocapitalize="off" role="textbox" aria-multiline="true"></div>
<div id="problems" hidden></div>
<div id="completion" hidden></div>
<script nonce="${nonce}" src="${direction}"></script>
<script nonce="${nonce}" src="${media("rtleditor", "model.js")}"></script>
<script nonce="${nonce}" src="${media("rtleditor", "editor.js")}"></script>
</body>
</html>`;
    }
}

/** True when a document is a Pashto (right-to-left) Thunder program. */
async function isRightToLeftProgram(services, document)
{
    if (!document || document.languageId !== "thunder" || document.uri.scheme !== "file")
    {
        return false;
    }
    const language = await services.languageData(services.languageOfText(document.getText(), document.uri.fsPath));
    return Boolean(language.rightToLeft);
}

/** The file of the editor in front of the user, text editor or right-to-left editor. */
function activeThunderUri()
{
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === "thunder")
    {
        return editor.document.uri;
    }
    const groups = vscode.window.tabGroups;
    const tab = groups && groups.activeTabGroup ? groups.activeTabGroup.activeTab : null;
    if (tab && tab.input && tab.input.uri && String(tab.input.uri.fsPath).endsWith(".th"))
    {
        return tab.input.uri;
    }
    return null;
}

/**
 * Opens a file in the right-to-left editor in place of its text editor (or the other way round).
 *
 * `vscode.openWith` opens the other editor in the same group; the tab of the editor it replaces
 * is then closed, unless it has unsaved changes of its own (a custom text editor shares the
 * document, so it never does - but the check costs nothing).
 */
async function reopenWith(uri, viewType)
{
    const groups = vscode.window.tabGroups;
    const previous = groups && groups.activeTabGroup ? groups.activeTabGroup.activeTab : null;
    await vscode.commands.executeCommand("vscode.openWith", uri, viewType);
    if (previous && previous.input && previous.input.uri
        && String(previous.input.uri) === String(uri) && !previous.isDirty
        && (previous.input.viewType || "default") !== viewType)
    {
        await groups.close(previous, true);
    }
}

module.exports = {
    RightToLeftEditorProvider,
    VIEW_TYPE,
    activeThunderUri,
    blockWordsFor,
    candidatesFor,
    diagnosticsFor,
    isRightToLeftProgram,
    reopenWith,
    wordsFor,
};
