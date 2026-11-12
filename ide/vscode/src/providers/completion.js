// Code completion and signature help.
//
// What is offered depends on what the caret is in:
//
//   after `import`          the built-in modules, and the files of the project in quotes
//   after a `.`            the fields of the struct the expression has, or a tuple's .0 and .1
//   after `:`              the types
//   anywhere else          the keywords, the built-ins the file may use, the names in scope,
//                          the functions and the structs of the program and of its modules
//
// The names come from the compiler (`thunderc --language-info` and the report of the file),
// so Pashto programs are completed with Pashto words without a second word list existing.

"use strict";

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const { completionWords } = require("../language");
const { modelOf, prefixOf } = require("./support");

const KIND = {
    keyword: vscode.CompletionItemKind.Keyword,
    type: vscode.CompletionItemKind.TypeParameter,
    literal: vscode.CompletionItemKind.Constant,
    builtin: vscode.CompletionItemKind.Function,
    module: vscode.CompletionItemKind.Module,
};

class CompletionProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideCompletionItems(document, position, cancellation)
    {
        const language = await this.services.languageFor(document);
        const prefix = prefixOf(document, position);

        const afterImport = /(?:^|\s)(?:import|راوړه)\s+(?:(?:native|بهرنی)\s+)?(["«“][^"»”]*)?$/u.exec(prefix);
        if (afterImport)
        {
            return this.importCompletions(document, position, language, afterImport[1] || "");
        }

        const model = await modelOf(this.services, document, cancellation);
        const good = this.services.analyser.goodModelFor(document.uri.fsPath) || model;
        const member = /([\p{L}\p{M}_][\p{L}\p{M}\p{N}_.]*)\.$/u.exec(prefix);
        if (member)
        {
            const fields = this.memberCompletions(good, position, member[1]);
            if (fields)
            {
                return fields;
            }
        }

        const items = [];
        const imported = good ? good.importedModules(language.info) : new Set();
        for (const word of completionWords(language, {
            imported,
            moduleBuiltins: this.services.configuration(document.uri).get("completion.moduleBuiltins"),
        }))
        {
            items.push(this.wordItem(word, language));
        }
        if (good)
        {
            for (const item of this.programCompletions(good, position))
            {
                items.push(item);
            }
        }
        return new vscode.CompletionList(items, false);
    }

    wordItem(word, language)
    {
        const item = new vscode.CompletionItem(word.label, KIND[word.kind] || vscode.CompletionItemKind.Text);
        item.detail = word.detail;
        item.sortText = word.sortText;
        if (word.documentation)
        {
            item.documentation = new vscode.MarkdownString(word.documentation);
        }
        if (word.kind === "builtin")
        {
            // Insert the brackets and put the caret between them, with the parameters as
            // placeholders so Tab walks them.
            const parameters = (word.parameters || []).filter((parameter) => !parameter.startsWith("..."));
            if (parameters.length)
            {
                const names = parameters.map((parameter, index) =>
                    `\${${index + 1}:${parameter.split(":")[0].trim()}}`);
                item.insertText = new vscode.SnippetString(`${word.label}(${names.join(language.comma + " ")})`);
            }
            else
            {
                item.insertText = new vscode.SnippetString(`${word.label}($0)`);
            }
            if (word.module)
            {
                item.detail = `${item.detail}   (module ${word.module})`;
            }
        }
        return item;
    }

    /** The names the program itself declares: variables in scope, functions, structs. */
    programCompletions(model, position)
    {
        const items = [];
        for (const symbol of model.visibleSymbols({ line: position.line, character: position.character }))
        {
            const item = new vscode.CompletionItem(symbol.name, symbol.const
                ? vscode.CompletionItemKind.Constant
                : (symbol.kind === "parameter" ? vscode.CompletionItemKind.Variable : vscode.CompletionItemKind.Variable));
            item.detail = `${symbol.kind}: ${symbol.type}`;
            item.sortText = "0" + symbol.name;
            items.push(item);
        }
        for (const declaration of model.allFunctions())
        {
            const item = new vscode.CompletionItem(declaration.name, vscode.CompletionItemKind.Function);
            item.detail = model.signatureOf(declaration);
            item.sortText = "0" + declaration.name;
            const parameters = declaration.children.filter((child) => child.kind === "Param");
            item.insertText = new vscode.SnippetString(parameters.length
                ? `${declaration.name}(${parameters.map((parameter, index) => `\${${index + 1}:${parameter.name}}`).join(", ")})`
                : `${declaration.name}()`);
            items.push(item);
        }
        for (const declaration of model.allStructs())
        {
            const item = new vscode.CompletionItem(declaration.name, vscode.CompletionItemKind.Struct);
            const fields = declaration.children.filter((child) => child.kind === "Field");
            item.detail = `struct ${declaration.name}(${fields.map((field) => `${field.name}: ${field.type}`).join(", ")})`;
            item.sortText = "0" + declaration.name;
            items.push(item);
        }
        return items;
    }

    /** The fields of the struct an expression has, after a `.`. */
    memberCompletions(model, position, expression)
    {
        if (!model)
        {
            return null;
        }
        const type = this.typeOfExpression(model, position, expression);
        if (!type)
        {
            return null;
        }
        if (/^\(/.test(type))
        {
            // A tuple: its parts are reached with .0, .1, ...
            const parts = type.replace(/^\(|\)$/g, "").split(",").map((part) => part.trim());
            return new vscode.CompletionList(parts.map((part, index) =>
            {
                const item = new vscode.CompletionItem(String(index), vscode.CompletionItemKind.Field);
                item.detail = part;
                return item;
            }), true);
        }
        const struct = model.allStructs().find((candidate) => candidate.name === type);
        if (!struct)
        {
            return null;
        }
        return new vscode.CompletionList(struct.children
            .filter((child) => child.kind === "Field")
            .map((field) =>
            {
                const item = new vscode.CompletionItem(field.name, vscode.CompletionItemKind.Field);
                item.detail = field.type;
                item.documentation = new vscode.MarkdownString(`field of \`${struct.name}\``);
                return item;
            }), true);
    }

    /**
     * The type of the expression written before a dot.
     *
     * `r` is looked up among the names in scope; `r.corner` then follows the field in the
     * struct, so `r.corner.` offers Point's fields. (The text being typed usually does not
     * parse, so this walks the names rather than the syntax tree.)
     */
    typeOfExpression(model, position, expression)
    {
        const parts = expression.split(".");
        const first = parts.shift();
        const where = { line: position.line, character: position.character };
        const symbol = model.visibleSymbols(where).find((entry) => entry.name === first);
        let type = symbol ? symbol.type : null;
        if (!type)
        {
            const global = model.allSymbols().find((entry) => entry.name === first);
            type = global ? global.type : null;
        }
        for (const part of parts)
        {
            const struct = type ? model.allStructs().find((candidate) => candidate.name === type) : null;
            const field = struct ? struct.children.find((child) => child.name === part) : null;
            type = field ? field.type : null;
        }
        return type;
    }

    /** After `import`: the built-in modules, and the .th files of the project. */
    async importCompletions(document, position, language, quoted)
    {
        const items = [];
        if (!quoted)
        {
            for (const module of language.modules)
            {
                const item = new vscode.CompletionItem(module.name, vscode.CompletionItemKind.Module);
                item.detail = `built-in module ${module.canonical_name}`;
                items.push(item);
            }
            const keyword = language.keywordNamed("native") || language.keywords.find((word) => word.kind === "KwNative");
            if (keyword)
            {
                const item = new vscode.CompletionItem(keyword.text, vscode.CompletionItemKind.Keyword);
                item.detail = "a library written in C++";
                items.push(item);
            }
        }
        for (const module of this.moduleFiles(document))
        {
            const item = new vscode.CompletionItem(module, vscode.CompletionItemKind.File);
            item.detail = "module";
            item.insertText = quoted ? module : `"${module}"`;
            items.push(item);
        }
        return new vscode.CompletionList(items, false);
    }

    /** The modules a file may import, written as `import` would name them. */
    moduleFiles(document)
    {
        const directory = path.dirname(document.uri.fsPath);
        const roots = [directory].concat(this.services.modulePathsFor(document.uri));
        const names = new Set();
        for (const root of roots)
        {
            for (const file of listThunderFiles(root, 2))
            {
                if (path.resolve(file) === path.resolve(document.uri.fsPath))
                {
                    continue;
                }
                const relative = path.relative(root, file).replace(/\\/g, "/").replace(/\.th$/, "");
                if (!relative.startsWith(".."))
                {
                    names.add(relative);
                }
            }
        }
        return [...names].sort();
    }
}

function listThunderFiles(root, depth)
{
    const files = [];
    let entries = [];
    try
    {
        entries = fs.readdirSync(root, { withFileTypes: true });
    }
    catch (error)
    {
        return files;
    }
    for (const entry of entries)
    {
        const full = path.join(root, entry.name);
        if (entry.isDirectory())
        {
            if (depth > 0 && !entry.name.startsWith(".") && entry.name !== "build")
            {
                files.push(...listThunderFiles(full, depth - 1));
            }
        }
        else if (entry.isFile() && entry.name.endsWith(".th"))
        {
            files.push(full);
        }
    }
    return files;
}

class SignatureHelpProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideSignatureHelp(document, position, cancellation)
    {
        const language = await this.services.languageFor(document);
        const model = this.services.analyser.goodModelFor(document.uri.fsPath)
            || await modelOf(this.services, document, cancellation);
        if (!model)
        {
            return null;
        }
        const where = { line: position.line, character: position.character };
        const call = model.callAt(where) || this.callFromText(document, position, language, model);
        if (!call)
        {
            return null;
        }
        const builtin = language.builtinNamed(call.name);
        let label = "";
        let parameters = [];
        let documentation = "";
        if (builtin)
        {
            label = language.signatureOf(builtin);
            parameters = language.parametersOf(builtin);
            documentation = builtin.description || "";
        }
        else
        {
            const declaration = model.allFunctions().find((candidate) => candidate.name === call.name)
                || model.allStructs().find((candidate) => candidate.name === call.name);
            if (!declaration)
            {
                return null;
            }
            const members = declaration.children.filter(
                (child) => child.kind === "Param" || child.kind === "Field");
            parameters = members.map((member) => `${member.name}: ${member.type}`);
            label = declaration.kind === "Struct"
                ? `${declaration.name}(${parameters.join(", ")})`
                : model.signatureOf(declaration);
            documentation = declaration.kind === "Struct"
                ? `Builds a ${declaration.name}. Arguments may also be named: ${declaration.name}(`
                    + `${members.map((member) => `${member.name}: ...`).join(", ")})`
                : "";
        }
        const help = new vscode.SignatureHelp();
        const signature = new vscode.SignatureInformation(label, new vscode.MarkdownString(documentation));
        signature.parameters = parameters.map((parameter) => new vscode.ParameterInformation(parameter));
        help.signatures = [signature];
        help.activeSignature = 0;
        const variadic = parameters.some((parameter) => parameter.includes("..."));
        help.activeParameter = variadic
            ? Math.min(call.argument, parameters.length - 1)
            : Math.max(0, Math.min(call.argument, Math.max(0, parameters.length - 1)));
        return help;
    }

    /**
     * The call around the caret when the file does not parse (`println(` while typing).
     *
     * Counts the brackets backwards from the caret to find the name of the call it is in, and
     * the commas at that level to find which argument it is.
     */
    callFromText(document, position, language)
    {
        const text = document.getText(new vscode.Range(new vscode.Position(Math.max(0, position.line - 20), 0), position));
        let depth = 0;
        let argument = 0;
        for (let i = text.length - 1; i >= 0; --i)
        {
            const character = text[i];
            if (character === ")" || character === "]" || character === "}")
            {
                ++depth;
            }
            else if (character === "(" || character === "[" || character === "{")
            {
                if (depth === 0)
                {
                    if (character !== "(")
                    {
                        return null;
                    }
                    const before = text.slice(0, i);
                    const name = /([\p{L}\p{M}_][\p{L}\p{M}\p{N}_]*(?:\s+[\p{L}\p{M}_][\p{L}\p{M}\p{N}_]*)*)\s*$/u
                        .exec(before);
                    if (!name)
                    {
                        return null;
                    }
                    return { name: longestReservedName(name[1], language), argument };
                }
                --depth;
            }
            else if ((character === "," || character === "،") && depth === 0)
            {
                ++argument;
            }
            else if (character === "\n" && depth === 0)
            {
                return null;
            }
        }
        return null;
    }
}

/** `println` out of `var x = println`, or the whole of `کرښه چاپ` when that is a built-in. */
function longestReservedName(text, language)
{
    const words = text.trim().split(/\s+/);
    for (let count = Math.min(words.length, 4); count >= 1; --count)
    {
        const candidate = words.slice(words.length - count).join(" ");
        if (language.builtinNamed(candidate))
        {
            return candidate;
        }
    }
    return words[words.length - 1];
}

module.exports = { CompletionProvider, SignatureHelpProvider, listThunderFiles, longestReservedName };
