// The language features of the right-to-left editor, from the same providers as the text editor.
//
// The right-to-left editor is a webview: VS Code gives it none of the editor's language
// features. Rather than re-implementing them in the page (which is how a separate, smaller
// completion list came about), the page asks for a feature at an offset and this module runs
// VS Code's own `vscode.execute...Provider` commands on the document. Those call every provider
// registered for Thunder - the extension's completion, hover, signature help, definition,
// references, rename, formatting, quick fixes, symbols, highlights and semantic tokens - so the
// two editors offer the same things, computed by the same code.
//
// Everything a provider returns is turned into plain data here (offsets, strings), because the
// page cannot receive VS Code objects.

"use strict";

const vscode = require("vscode");

// VS Code's CompletionItemKind numbers, as the page colours them.
const COMPLETION_KINDS = {
    0: "name", 1: "function", 2: "function", 3: "function", 4: "name", 5: "name", 6: "struct", 7: "struct",
    8: "module", 9: "name", 10: "name", 11: "literal", 12: "literal", 13: "keyword", 14: "keyword",
    16: "name", 17: "module", 19: "module", 20: "name", 21: "struct", 22: "name", 23: "keyword", 24: "type",
};

/** The text of a provider string: a plain string, a MarkdownString or a SnippetString. */
function textOf(value)
{
    if (value === undefined || value === null)
    {
        return "";
    }
    if (typeof value === "string")
    {
        return value;
    }
    return typeof value.value === "string" ? value.value : String(value);
}

/**
 * A snippet as plain text and where the caret goes: `${1:x}` becomes x, `$1` and `$0` become
 * nothing, and the caret lands on the first tab stop (else on $0, else at the end).
 */
function expandSnippet(snippet)
{
    let text = "";
    let caret = -1;
    let caretStop = Infinity;
    const pattern = /\\([$}\\])|\$\{(\d+)(?::([^}]*))?\}|\$(\d+)/g;
    let last = 0;
    let match = pattern.exec(snippet);
    while (match)
    {
        text += snippet.slice(last, match.index);
        if (match[1] !== undefined)
        {
            text += match[1]; // an escaped character
        }
        else
        {
            const stop = Number(match[2] !== undefined ? match[2] : match[4]);
            const rank = stop === 0 ? Number.MAX_SAFE_INTEGER : stop; // $0 only when there is no other stop
            if (rank < caretStop)
            {
                caretStop = rank;
                caret = text.length;
            }
            text += match[3] || "";
        }
        last = pattern.lastIndex;
        match = pattern.exec(snippet);
    }
    text += snippet.slice(last);
    return { text, caret: caret < 0 ? text.length : caret };
}

/** A completion item as the page shows and inserts it. */
function completionItem(item, document, offset, wordStart)
{
    const label = typeof item.label === "string" ? item.label : (item.label && item.label.label) || "";
    const snippet = item.insertText && typeof item.insertText !== "string";
    const inserted = snippet ? expandSnippet(textOf(item.insertText)) : { text: textOf(item.insertText) || label, caret: -1 };
    const range = item.range && (item.range.replacing || item.range.inserting || item.range);
    let start = range && range.start ? document.offsetAt(range.start) : wordStart;
    let end = range && range.end ? document.offsetAt(range.end) : offset;
    if (start > offset)
    {
        start = wordStart;
    }
    end = Math.max(end, offset);
    return {
        label,
        kind: COMPLETION_KINDS[item.kind] || "name",
        detail: textOf(item.detail) || (item.label && item.label.description) || "",
        documentation: textOf(item.documentation),
        text: inserted.text,
        caret: inserted.caret < 0 ? inserted.text.length : inserted.caret,
        start,
        end,
        filterText: item.filterText || label,
        sortText: item.sortText || label,
    };
}

/** The word (letters, digits, underscores, in any script) that ends at an offset. */
function wordStartBefore(text, offset)
{
    let start = offset;
    while (start > 0 && /[\p{L}\p{N}_]/u.test(text[start - 1]))
    {
        --start;
    }
    return start;
}

function rangeOffsets(document, range)
{
    return { start: document.offsetAt(range.start), end: document.offsetAt(range.end) };
}

/** Applies the TextEdits a provider returned to the document. */
async function applyTextEdits(document, edits)
{
    if (!edits || !edits.length)
    {
        return false;
    }
    const edit = new vscode.WorkspaceEdit();
    for (const change of edits)
    {
        edit.replace(document.uri, change.range, change.newText);
    }
    return vscode.workspace.applyEdit(edit);
}

/** Where a definition or reference is: {uri, range}, whether given as a Location or a LocationLink. */
function locationOf(found)
{
    if (!found)
    {
        return null;
    }
    if (found.targetUri)
    {
        return { uri: found.targetUri, range: found.targetSelectionRange || found.targetRange };
    }
    return { uri: found.uri, range: found.range };
}

class LanguageBridge
{
    /**
     * @param reveal (uri, offset) -> Promise: shows a position, in this editor or another file's
     */
    constructor(reveal)
    {
        this.reveal = reveal;
    }

    /** Answers one request from the page; the result is plain data (or null). */
    async handle(document, request)
    {
        const handler = this[`on_${request.feature}`];
        if (typeof handler !== "function")
        {
            return null;
        }
        try
        {
            return await handler.call(this, document, request);
        }
        catch (error)
        {
            return null; // a provider that fails offers nothing, as in the text editor
        }
    }

    async on_completion(document, request)
    {
        const offset = request.offset;
        const position = document.positionAt(offset);
        const list = await vscode.commands.executeCommand("vscode.executeCompletionItemProvider",
            document.uri, position, request.trigger || undefined);
        const items = Array.isArray(list) ? list : ((list && list.items) || []);
        const wordStart = wordStartBefore(document.getText(), offset);
        return {
            wordStart,
            items: items.map((item) => completionItem(item, document, offset, wordStart))
                .sort((a, b) => a.sortText.localeCompare(b.sortText) || a.label.localeCompare(b.label)),
        };
    }

    async on_signature(document, request)
    {
        const help = await vscode.commands.executeCommand("vscode.executeSignatureHelpProvider",
            document.uri, document.positionAt(request.offset));
        if (!help || !help.signatures || !help.signatures.length)
        {
            return null;
        }
        const signature = help.signatures[help.activeSignature || 0] || help.signatures[0];
        const active = signature.activeParameter !== undefined ? signature.activeParameter : help.activeParameter;
        const parameter = (signature.parameters || [])[active || 0];
        let highlight = null;
        if (parameter)
        {
            if (Array.isArray(parameter.label))
            {
                highlight = { start: parameter.label[0], end: parameter.label[1] };
            }
            else
            {
                const at = signature.label.indexOf(parameter.label);
                highlight = at >= 0 ? { start: at, end: at + parameter.label.length } : null;
            }
        }
        return { label: signature.label, documentation: textOf(signature.documentation), highlight };
    }

    async on_hover(document, request)
    {
        const hovers = await vscode.commands.executeCommand("vscode.executeHoverProvider",
            document.uri, document.positionAt(request.offset));
        const parts = [];
        for (const hover of (hovers || []))
        {
            for (const content of (hover.contents || []))
            {
                const text = textOf(content).trim();
                if (text)
                {
                    parts.push(text);
                }
            }
        }
        return parts.length ? { markdown: parts.join("\n\n---\n\n") } : null;
    }

    async on_highlights(document, request)
    {
        const highlights = await vscode.commands.executeCommand("vscode.executeDocumentHighlights",
            document.uri, document.positionAt(request.offset));
        return (highlights || []).map((highlight) => Object.assign(rangeOffsets(document, highlight.range),
            { kind: highlight.kind === 2 ? "write" : "read" }));
    }

    async on_definition(document, request)
    {
        const found = await vscode.commands.executeCommand("vscode.executeDefinitionProvider",
            document.uri, document.positionAt(request.offset));
        const location = locationOf((found || [])[0]);
        if (!location)
        {
            vscode.window.setStatusBarMessage("Thunder: no definition found", 3000);
            return null;
        }
        await this.revealLocation(location);
        return { found: true };
    }

    async on_references(document, request)
    {
        const found = await vscode.commands.executeCommand("vscode.executeReferenceProvider",
            document.uri, document.positionAt(request.offset));
        const locations = (found || []).map(locationOf).filter(Boolean);
        if (!locations.length)
        {
            vscode.window.setStatusBarMessage("Thunder: no references found", 3000);
            return null;
        }
        const picks = [];
        for (const location of locations)
        {
            const source = await vscode.workspace.openTextDocument(location.uri);
            const line = location.range.start.line;
            picks.push({
                label: source.lineAt(line).text.trim() || " ",
                description: `${vscode.workspace.asRelativePath(location.uri)}:${line + 1}`,
                location,
            });
        }
        const chosen = await vscode.window.showQuickPick(picks, {
            title: `${locations.length} reference(s)`,
            matchOnDescription: true,
        });
        if (chosen)
        {
            await this.revealLocation(chosen.location);
        }
        return { count: locations.length };
    }

    async on_rename(document, request)
    {
        const text = document.getText();
        const start = wordStartBefore(text, request.offset);
        let end = request.offset;
        while (end < text.length && /[\p{L}\p{N}_]/u.test(text[end]))
        {
            ++end;
        }
        const name = text.slice(start, end);
        if (!name)
        {
            return null;
        }
        const newName = await vscode.window.showInputBox({ title: "Rename", value: name, prompt: `Rename ${name} to` });
        if (!newName || newName === name)
        {
            return null;
        }
        const edit = await vscode.commands.executeCommand("vscode.executeDocumentRenameProvider",
            document.uri, document.positionAt(start), newName);
        if (!edit)
        {
            vscode.window.showWarningMessage(`${name} cannot be renamed here.`);
            return null;
        }
        return { applied: await vscode.workspace.applyEdit(edit) };
    }

    async on_format(document)
    {
        const edits = await vscode.commands.executeCommand("vscode.executeFormatDocumentProvider",
            document.uri, { tabSize: 4, insertSpaces: true });
        return { applied: await applyTextEdits(document, edits) };
    }

    async on_quickFix(document, request)
    {
        const position = document.positionAt(request.offset);
        const actions = await vscode.commands.executeCommand("vscode.executeCodeActionProvider",
            document.uri, new vscode.Range(position, position));
        const usable = (actions || []).filter((action) => action.edit || action.command);
        if (!usable.length)
        {
            vscode.window.setStatusBarMessage("Thunder: no quick fixes here", 3000);
            return null;
        }
        const chosen = usable.length === 1 && request.preferred
            ? usable[0]
            : await vscode.window.showQuickPick(usable.map((action) => ({ label: action.title, action })), { title: "Quick Fix" })
                .then((pick) => pick && pick.action);
        if (!chosen)
        {
            return null;
        }
        if (chosen.edit)
        {
            await vscode.workspace.applyEdit(chosen.edit);
        }
        if (chosen.command)
        {
            const command = typeof chosen.command === "string" ? { command: chosen.command } : chosen.command;
            await vscode.commands.executeCommand(command.command, ...(command.arguments || []));
        }
        return { applied: true };
    }

    async on_symbols(document)
    {
        const symbols = await vscode.commands.executeCommand("vscode.executeDocumentSymbolProvider", document.uri);
        const flat = [];
        const visit = (list, container) =>
        {
            for (const symbol of (list || []))
            {
                const range = symbol.selectionRange || symbol.range || (symbol.location && symbol.location.range);
                flat.push({ label: symbol.name, description: container, offset: document.offsetAt(range.start) });
                visit(symbol.children, symbol.name);
            }
        };
        visit(symbols, "");
        if (!flat.length)
        {
            return null;
        }
        const chosen = await vscode.window.showQuickPick(flat, { title: "Go to Symbol in Editor", matchOnDescription: true });
        if (chosen)
        {
            await this.reveal(document.uri, chosen.offset);
        }
        return { count: flat.length };
    }

    /** Semantic tokens as ranges by type: {type: [[start, end], ...]}. */
    async on_semanticTokens(document)
    {
        const legend = await vscode.commands.executeCommand("vscode.provideDocumentSemanticTokensLegend", document.uri);
        const tokens = await vscode.commands.executeCommand("vscode.provideDocumentSemanticTokens", document.uri);
        if (!legend || !tokens || !tokens.data)
        {
            return null;
        }
        const data = tokens.data;
        const byType = {};
        let line = 0;
        let character = 0;
        for (let i = 0; i + 4 < data.length; i += 5)
        {
            line += data[i];
            character = data[i] === 0 ? character + data[i + 1] : data[i + 1];
            const type = legend.tokenTypes[data[i + 3]];
            const modifiers = legend.tokenModifiers.filter((name, bit) => (data[i + 4] >> bit) & 1);
            const start = document.offsetAt(new vscode.Position(line, character));
            const name = modifiers.includes("readonly") && type === "variable" ? "constant" : type;
            (byType[name] = byType[name] || []).push([start, start + data[i + 2]]);
        }
        return byType;
    }

    async revealLocation(location)
    {
        await this.reveal(location.uri, null, location.range.start);
    }
}

module.exports = { COMPLETION_KINDS, LanguageBridge, expandSnippet, wordStartBefore };
