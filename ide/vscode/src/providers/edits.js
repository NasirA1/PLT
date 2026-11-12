// Formatting ("tidy layout") and the quick fixes offered for the compiler's own hints.

"use strict";

const vscode = require("vscode");

const { tidy } = require("../edits");

class FormattingProvider
{
    provideDocumentFormattingEdits(document)
    {
        const text = document.getText();
        const tidied = tidy(text);
        if (tidied === text)
        {
            return [];
        }
        const whole = new vscode.Range(
            document.positionAt(0),
            document.positionAt(text.length));
        return [vscode.TextEdit.replace(whole, tidied)];
    }
}

/**
 * Quick fixes.
 *
 * The compiler already says what it thinks the programmer meant - "unexpected character ';';
 * did you mean ','?" - so the fix is to do what it suggests. The other fix is for a name that
 * is not declared but is a function of a built-in module: the file is missing its `import`.
 */
class CodeActionProvider
{
    constructor(services)
    {
        this.services = services;
    }

    static get kinds()
    {
        return [vscode.CodeActionKind.QuickFix];
    }

    async provideCodeActions(document, range, context)
    {
        const language = await this.services.languageFor(document);
        const actions = [];
        for (const diagnostic of (context.diagnostics || []))
        {
            const suggestion = /did you mean '([^']+)'/.exec(diagnostic.message);
            if (suggestion)
            {
                const action = new vscode.CodeAction(`Replace with '${suggestion[1]}'`, vscode.CodeActionKind.QuickFix);
                action.edit = new vscode.WorkspaceEdit();
                action.edit.replace(document.uri, diagnostic.range, suggestion[1]);
                action.diagnostics = [diagnostic];
                action.isPreferred = true;
                actions.push(action);
            }
            const undeclared = /'([^']+)' is not declared/.exec(diagnostic.message)
                || /«([^»]+)» (?:نه|نده)/.exec(diagnostic.message);
            if (undeclared)
            {
                const builtin = language.builtinNamed(undeclared[1]);
                if (builtin && builtin.module)
                {
                    const module = language.modules.find((entry) => entry.canonical_name === builtin.module);
                    const name = module ? module.name : builtin.module;
                    const keyword = language.keywords.find((entry) => entry.kind === "KwImport");
                    const statement = `${keyword ? keyword.text : "import"} ${name}`;
                    const action = new vscode.CodeAction(`Add "${statement}"`, vscode.CodeActionKind.QuickFix);
                    action.edit = new vscode.WorkspaceEdit();
                    action.edit.insert(document.uri, importPosition(document), statement + "\n");
                    action.diagnostics = [diagnostic];
                    action.isPreferred = true;
                    actions.push(action);
                }
            }
        }
        return actions;
    }
}

/**
 * Where a new `import` goes: after the comments at the top of the file (a program's first
 * line may be the `// lang:` directive, which must stay first) and after the imports already
 * there.
 */
function importPosition(document)
{
    let line = 0;
    for (; line < document.lineCount; ++line)
    {
        const text = document.lineAt(line).text.trim();
        if (text === "" || text.startsWith("//") || /^(import|راوړه)\b/u.test(text))
        {
            continue;
        }
        break;
    }
    // Put it directly after the last import or comment, not after the blank lines that follow.
    let last = line;
    while (last > 0 && document.lineAt(last - 1).text.trim() === "")
    {
        --last;
    }
    return new vscode.Position(last, 0);
}

module.exports = { CodeActionProvider, FormattingProvider, importPosition };
