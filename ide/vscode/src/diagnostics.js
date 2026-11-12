// Showing the compiler's problems in the editor.
//
// Every error and warning comes from `thunderc` itself, with its stable code (L002, P010,
// S005, ...) and the phase that found it, so the Problems panel reads exactly like the
// compiler's own output. A program that imports modules gets diagnostics in several files;
// each is published against the file it belongs to, so clicking it opens the right one.
//
// Names the program declares but never reads are shown as hints (greyed out, not errors):
// the compiler records a `used` flag for every symbol, and that is worth seeing.

"use strict";

const path = require("path");
const vscode = require("vscode");

const { toRange } = require("./providers/support");

class DiagnosticsManager
{
    constructor(services)
    {
        this.services = services;
        this.collection = vscode.languages.createDiagnosticCollection("thunder");
        this.timers = new Map();
        this.published = new Map(); // the document that last published diagnostics for a file
    }

    dispose()
    {
        for (const timer of this.timers.values())
        {
            clearTimeout(timer);
        }
        this.collection.dispose();
    }

    /** Checks a document after a moment of quiet, so that typing does not start a compile a key. */
    schedule(document)
    {
        if (document.languageId !== "thunder")
        {
            return;
        }
        const when = this.services.configuration(document.uri).get("diagnostics.run");
        if (when === "off")
        {
            this.collection.delete(document.uri);
            return;
        }
        if (when === "onSave" && document.isDirty)
        {
            return;
        }
        const key = document.uri.toString();
        clearTimeout(this.timers.get(key));
        const delay = Number(this.services.configuration(document.uri).get("diagnostics.delay")) || 0;
        this.timers.set(key, setTimeout(() => this.check(document), delay));
    }

    /** Compiles a document now and publishes what the compiler said. */
    async check(document)
    {
        if (document.isClosed || document.languageId !== "thunder")
        {
            return null;
        }
        const model = await this.services.analyse(document);
        if (!model)
        {
            return null;
        }
        this.publish(document, model);
        return model;
    }

    publish(document, model)
    {
        const byFile = new Map();
        const own = document.uri.fsPath;
        byFile.set(path.resolve(own), []);
        const sourceOf = (file) =>
        {
            const open = vscode.workspace.textDocuments.find(
                (candidate) => path.resolve(candidate.uri.fsPath) === path.resolve(file));
            if (open)
            {
                return open.getText();
            }
            const cached = this.services.analyser.modelFor(file);
            return cached ? cached.text : "";
        };
        for (const diagnostic of model.diagnostics(sourceOf))
        {
            const file = path.resolve(diagnostic.file || own);
            if (!byFile.has(file))
            {
                byFile.set(file, []);
            }
            const item = new vscode.Diagnostic(
                toRange(diagnostic.range),
                diagnostic.message,
                diagnostic.severity === "warning"
                    ? vscode.DiagnosticSeverity.Warning
                    : vscode.DiagnosticSeverity.Error);
            item.source = `thunderc (${diagnostic.phase})`;
            item.code = diagnostic.code;
            if (file !== path.resolve(own))
            {
                item.relatedInformation = [new vscode.DiagnosticRelatedInformation(
                    new vscode.Location(document.uri, new vscode.Range(0, 0, 0, 0)),
                    `while compiling ${path.basename(own)}`)];
            }
            byFile.get(file).push(item);
        }

        if (this.services.configuration(document.uri).get("diagnostics.unusedNames") !== false)
        {
            for (const hint of unusedNameHints(model))
            {
                const file = path.resolve(hint.file || own);
                if (!byFile.has(file))
                {
                    byFile.set(file, []);
                }
                const item = new vscode.Diagnostic(toRange(hint.range), hint.message, vscode.DiagnosticSeverity.Hint);
                item.source = "thunder";
                item.tags = [vscode.DiagnosticTag.Unnecessary];
                byFile.get(file).push(item);
            }
        }

        // Forget the files this document reported about last time but does not any more.
        const previous = this.published.get(path.resolve(own)) || [];
        for (const file of previous)
        {
            if (!byFile.has(file))
            {
                this.collection.delete(vscode.Uri.file(file));
            }
        }
        this.published.set(path.resolve(own), [...byFile.keys()]);
        for (const [file, items] of byFile)
        {
            this.collection.set(vscode.Uri.file(file), items);
        }
    }

    forget(uri)
    {
        this.collection.delete(uri);
    }
}

/** The variables, constants and parameters the program never reads. */
function unusedNameHints(model)
{
    const hints = [];
    for (const symbol of model.allSymbols())
    {
        if (symbol.used || symbol.fileIndex)
        {
            continue;
        }
        const range = model.rangeOf(symbol.span);
        hints.push({
            file: symbol.file,
            range,
            message: symbol.kind === "parameter"
                ? `parameter '${symbol.name}' is never read`
                : `'${symbol.name}' is never read`,
        });
    }
    return hints;
}

module.exports = { DiagnosticsManager, unusedNameHints };
