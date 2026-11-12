// Moving around a program: go to definition, find all references, rename, the outline of a
// file, the symbols of the whole workspace and the inlay hints that show inferred types.
//
// All of it comes from the compiler's symbol table and syntax tree, so a name that a module
// declares is found in that module's file, and a name that only looks like one (a word inside
// a string or a comment) is not found at all.

"use strict";

const path = require("path");
const vscode = require("vscode");

const { modelOf, toRange } = require("./support");

const SYMBOL_KIND = {
    Struct: vscode.SymbolKind.Struct,
    Function: vscode.SymbolKind.Function,
    NativeFunction: vscode.SymbolKind.Function,
    Var: vscode.SymbolKind.Variable,
    Const: vscode.SymbolKind.Constant,
    Field: vscode.SymbolKind.Field,
    Param: vscode.SymbolKind.Variable,
};

class DefinitionProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideDefinition(document, position, cancellation)
    {
        const model = await modelOf(this.services, document, cancellation)
            || this.services.analyser.goodModelFor(document.uri.fsPath);
        if (!model)
        {
            return null;
        }
        const definition = model.definitionAt({ line: position.line, character: position.character });
        if (!definition)
        {
            return null;
        }
        return new vscode.Location(vscode.Uri.file(definition.file), toRange(definition.range));
    }
}

class ReferenceProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideReferences(document, position, context, cancellation)
    {
        const model = await modelOf(this.services, document, cancellation);
        if (!model)
        {
            return [];
        }
        const token = model.tokenAt({ line: position.line, character: position.character });
        if (!token || token.category !== "identifier")
        {
            return [];
        }
        const locations = [];
        for (const other of model.tokens())
        {
            if (other.category === "identifier" && other.lexeme === token.lexeme)
            {
                locations.push(new vscode.Location(document.uri, toRange(other.range)));
            }
        }
        if (context && context.includeDeclaration === false)
        {
            const definition = model.definitionAt({ line: position.line, character: position.character });
            if (definition)
            {
                return locations.filter((location) => !location.range.isEqual(toRange(definition.range)));
            }
        }
        return locations;
    }
}

class RenameProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async prepareRename(document, position, cancellation)
    {
        const model = await modelOf(this.services, document, cancellation);
        const token = model ? model.tokenAt({ line: position.line, character: position.character }) : null;
        if (!token || token.category !== "identifier")
        {
            throw new Error("Only a name the program declares can be renamed.");
        }
        return { range: toRange(token.range), placeholder: token.lexeme };
    }

    async provideRenameEdits(document, position, newName, cancellation)
    {
        const model = await modelOf(this.services, document, cancellation);
        if (!model)
        {
            return null;
        }
        const token = model.tokenAt({ line: position.line, character: position.character });
        if (!token || token.category !== "identifier")
        {
            return null;
        }
        const edit = new vscode.WorkspaceEdit();
        for (const other of model.tokens())
        {
            if (other.category === "identifier" && other.lexeme === token.lexeme)
            {
                edit.replace(document.uri, toRange(other.range), newName);
            }
        }
        return edit;
    }
}

class DocumentSymbolProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideDocumentSymbols(document, cancellation)
    {
        const model = await modelOf(this.services, document, cancellation)
            || this.services.analyser.goodModelFor(document.uri.fsPath);
        if (!model)
        {
            return [];
        }
        const symbols = [];
        for (const declaration of model.declarations())
        {
            if (declaration.container)
            {
                continue; // a field or parameter: listed under its struct or function
            }
            const symbol = new vscode.DocumentSymbol(
                declaration.name || "<program>",
                detailOf(model, declaration),
                SYMBOL_KIND[declaration.kind] || vscode.SymbolKind.Variable,
                toRange(declaration.range),
                toRange(declaration.selectionRange));
            for (const child of declaration.children)
            {
                symbol.children.push(new vscode.DocumentSymbol(
                    child.name,
                    child.type,
                    SYMBOL_KIND[child.kind] || vscode.SymbolKind.Field,
                    toRange(child.range),
                    toRange(child.selectionRange)));
            }
            symbols.push(symbol);
        }
        return symbols;
    }
}

function detailOf(model, declaration)
{
    if (declaration.kind === "Function" || declaration.kind === "NativeFunction")
    {
        return model.signatureOf(declaration).slice(declaration.name.length);
    }
    return declaration.type || "";
}

/** The names of every Thunder file in the workspace, for Ctrl+T. */
class WorkspaceSymbolProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideWorkspaceSymbols(query)
    {
        const symbols = [];
        const wanted = String(query || "").toLowerCase();
        for (const [file, entry] of this.services.analyser.cache)
        {
            const model = entry.model;
            if (!model)
            {
                continue;
            }
            for (const declaration of model.declarations())
            {
                if (declaration.container || (wanted && !declaration.name.toLowerCase().includes(wanted)))
                {
                    continue;
                }
                symbols.push(new vscode.SymbolInformation(
                    declaration.name,
                    SYMBOL_KIND[declaration.kind] || vscode.SymbolKind.Variable,
                    path.basename(file),
                    new vscode.Location(vscode.Uri.file(file), toRange(declaration.selectionRange))));
            }
        }
        return symbols;
    }
}

/**
 * The type of a variable the program did not write down.
 *
 * `var total = 0` is an `int` because the compiler worked that out; the hint shows it the way
 * `var total: int = 0` would have been written. A variable *with* a written type gets no hint.
 */
class InlayHintsProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideInlayHints(document, range, cancellation)
    {
        const model = await modelOf(this.services, document, cancellation);
        if (!model)
        {
            return [];
        }
        const hints = [];
        const tokens = model.tokens();
        for (const declaration of model.declarations())
        {
            if (declaration.kind !== "Var" && declaration.kind !== "Const")
            {
                continue;
            }
            const line = declaration.selectionRange.start.line;
            if (line < range.start.line || line > range.end.line)
            {
                continue;
            }
            if (!declaration.type)
            {
                continue;
            }
            // Written as `var x: int = ...`? Then the colon follows the name immediately.
            const after = tokens.find((token) =>
                token.range.start.line === line
                && token.range.start.character >= declaration.selectionRange.end.character);
            if (after && after.kind === "Colon")
            {
                continue;
            }
            const hint = new vscode.InlayHint(
                new vscode.Position(line, declaration.selectionRange.end.character),
                `: ${declaration.type}`,
                vscode.InlayHintKind.Type);
            hint.paddingLeft = false;
            hints.push(hint);
        }
        return hints;
    }
}

module.exports = {
    DefinitionProvider,
    DocumentSymbolProvider,
    InlayHintsProvider,
    ReferenceProvider,
    RenameProvider,
    WorkspaceSymbolProvider,
};
