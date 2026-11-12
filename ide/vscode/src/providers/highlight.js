// Semantic highlighting, and highlighting every use of the name under the caret.
//
// The TextMate grammar colours a file the moment it is opened (and keeps working when the
// toolchain has not been built). Semantic tokens then refine it with what only the compiler
// knows: which name is a function, which is a parameter, which is a field, which is a built-in
// and which constant may not be assigned to. Because the tokens come from the lexer itself,
// Pashto programs are highlighted exactly as English ones are - including built-ins written as
// several words, which no regular expression in a grammar gets reliably right.

"use strict";

const vscode = require("vscode");

const { modelOf, toRange } = require("./support");

const TOKEN_TYPES = [
    "keyword", "type", "function", "variable", "parameter", "property", "struct",
    "string", "number", "operator", "namespace", "builtinFunction", "moduleFunction",
];
const TOKEN_MODIFIERS = ["declaration", "readonly", "defaultLibrary"];

const LEGEND = new vscode.SemanticTokensLegend(TOKEN_TYPES, TOKEN_MODIFIERS);

class SemanticTokensProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideDocumentSemanticTokens(document, cancellation)
    {
        const model = await modelOf(this.services, document, cancellation);
        if (!model)
        {
            return null;
        }
        const language = await this.services.languageFor(document);
        const builder = new vscode.SemanticTokensBuilder(LEGEND);
        const roles = this.rolesOf(model);
        for (const token of model.tokens())
        {
            const classified = classify(token, roles, language, model);
            if (!classified)
            {
                continue;
            }
            // A semantic token may not span lines; Thunder's tokens never do except for a
            // string that was left open, which the lexer has already reported.
            if (token.range.start.line !== token.range.end.line)
            {
                continue;
            }
            builder.push(
                token.range.start.line,
                token.range.start.character,
                token.range.end.character - token.range.start.character,
                TOKEN_TYPES.indexOf(classified.type),
                modifierBits(classified.modifiers));
        }
        return builder.build();
    }

    /**
     * What each name in the file is, by position: the syntax tree says which identifier is a
     * function being declared, a parameter, a field or a struct, and the symbol table says
     * which names are constants.
     */
    rolesOf(model)
    {
        const roles = new Map();
        const mark = (range, type, modifiers) =>
        {
            roles.set(`${range.start.line}:${range.start.character}`, { type, modifiers: modifiers || [] });
        };
        for (const declaration of model.declarations())
        {
            const range = declaration.selectionRange;
            switch (declaration.kind)
            {
            case "Struct":
                mark(range, "struct", ["declaration"]);
                break;
            case "Function":
                mark(range, "function", ["declaration"]);
                break;
            case "NativeFunction":
                mark(range, "function", ["declaration", "defaultLibrary"]);
                break;
            case "Param":
                mark(range, "parameter", ["declaration"]);
                break;
            case "Field":
                mark(range, "property", ["declaration"]);
                break;
            case "Const":
                mark(range, "variable", ["declaration", "readonly"]);
                break;
            default:
                mark(range, "variable", ["declaration"]);
                break;
            }
        }
        const names = new Set(model.allFunctions().map((declaration) => declaration.name));
        const structs = new Set(model.allStructs().map((declaration) => declaration.name));
        const constants = new Set(model.allSymbols().filter((symbol) => symbol.const).map((symbol) => symbol.name));
        const parameters = new Set(model.allSymbols()
            .filter((symbol) => symbol.kind === "parameter").map((symbol) => symbol.name));
        model.walk((node) =>
        {
            if (node.kind === "Call" && names.has(node.label))
            {
                mark(model.nameRangeOf(node), "function", []);
            }
            else if (node.kind === "Call" && structs.has(node.label))
            {
                mark(model.nameRangeOf(node), "struct", []);
            }
            else if (node.kind === "Field" && node.label)
            {
                mark(model.nameRangeOf(node), "property", []);
            }
        });
        this.byName = { names, structs, constants, parameters };
        return roles;
    }
}

function classify(token, roles, language, model)
{
    switch (token.category)
    {
    case "keyword":
        return { type: "keyword", modifiers: [] };
    case "type":
        return { type: "type", modifiers: ["defaultLibrary"] };
    case "literal":
        if (token.kind === "StringLiteral")
        {
            return { type: "string", modifiers: [] };
        }
        if (token.kind === "IntLiteral" || token.kind === "FloatLiteral")
        {
            return { type: "number", modifiers: [] };
        }
        return { type: "keyword", modifiers: ["readonly"] };
    case "operator":
        return { type: "operator", modifiers: [] };
    case "identifier":
        return classifyName(token, roles, language, model);
    default:
        return null;
    }
}

function classifyName(token, roles, language, model)
{
    const role = roles.get(`${token.range.start.line}:${token.range.start.character}`);
    if (role)
    {
        return role;
    }
    const builtin = language ? language.builtinNamed(token.lexeme) : null;
    if (builtin)
    {
        return {
            type: builtin.module ? "moduleFunction" : "builtinFunction",
            modifiers: ["defaultLibrary"],
        };
    }
    const symbol = model.allSymbols().find((entry) => entry.name === token.lexeme);
    if (symbol)
    {
        const modifiers = symbol.const ? ["readonly"] : [];
        return { type: symbol.kind === "parameter" ? "parameter" : "variable", modifiers };
    }
    if (model.allStructs().some((declaration) => declaration.name === token.lexeme))
    {
        return { type: "struct", modifiers: [] };
    }
    if (model.allFunctions().some((declaration) => declaration.name === token.lexeme))
    {
        return { type: "function", modifiers: [] };
    }
    return { type: "variable", modifiers: [] };
}

function modifierBits(modifiers)
{
    let bits = 0;
    for (const modifier of (modifiers || []))
    {
        const index = TOKEN_MODIFIERS.indexOf(modifier);
        if (index >= 0)
        {
            bits |= 1 << index;
        }
    }
    return bits;
}

/**
 * Every use of the name the caret is on, marked in the editor.
 *
 * Only *names* count: the compiler's token stream is used, so the word inside a string or a
 * comment is not marked and `x` is not found inside `max` (the same rule as the Qt IDE's
 * `edits.occurrences`).
 */
class DocumentHighlightProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideDocumentHighlights(document, position, cancellation)
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
        const definition = model.definitionAt({ line: position.line, character: position.character });
        return model.tokens()
            .filter((other) => other.category === "identifier" && other.lexeme === token.lexeme)
            .map((other) =>
            {
                const isDefinition = definition
                    && other.range.start.line === definition.range.start.line
                    && other.range.start.character === definition.range.start.character;
                return new vscode.DocumentHighlight(
                    toRange(other.range),
                    isDefinition ? vscode.DocumentHighlightKind.Write : vscode.DocumentHighlightKind.Read);
            });
    }
}

module.exports = { DocumentHighlightProvider, LEGEND, SemanticTokensProvider, TOKEN_MODIFIERS, TOKEN_TYPES, classify };
