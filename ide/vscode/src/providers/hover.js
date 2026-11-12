// What the editor says when the pointer rests on something.
//
// For a built-in: its signature, what it does and what it is called in the other language.
// For a keyword: what kind of word it is, its other accepted spellings and its counterpart in
// the other language - the one thing a reader of a bilingual program wants most.
// For a name the program declares: its kind and type, from the compiler's symbol table.
// For any expression: the type the semantic analyser gave it.

"use strict";

const path = require("path");
const vscode = require("vscode");

const { describeWord } = require("../language");
const { modelOf, reservedWordAt } = require("./support");

class HoverProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async provideHover(document, position, cancellation)
    {
        const language = await this.services.languageFor(document);
        const found = reservedWordAt(document, position, language);
        if (!found)
        {
            return null;
        }
        const lines = [];
        const description = describeWord(language, found.word, await this.services.allLanguages());
        if (description)
        {
            lines.push(...describeMarkdown(description));
        }

        const model = await modelOf(this.services, document, cancellation);
        if (model && !description)
        {
            const where = { line: position.line, character: position.character };
            const symbol = model.visibleSymbols(where).find((entry) => entry.name === found.word);
            if (symbol)
            {
                lines.push(`\`\`\`thunder\n${symbol.const ? "const" : "var"} ${symbol.name}: ${symbol.type}\n\`\`\``);
                lines.push(`${symbol.kind}${symbol.used ? "" : " (never read)"}`);
            }
            else
            {
                const declaration = model.allDeclarationsNamed(found.word)[0];
                if (declaration && declaration.kind === "Struct")
                {
                    const fields = declaration.children.map((field) => `    ${field.name}: ${field.type}`);
                    lines.push(`\`\`\`thunder\nstruct ${declaration.name}\n${fields.join("\n")}\n\`\`\``);
                    if (declaration.file && path.resolve(declaration.file) !== path.resolve(document.uri.fsPath))
                    {
                        lines.push(`from \`${path.basename(declaration.file)}\``);
                    }
                }
                else if (declaration)
                {
                    lines.push(`\`\`\`thunder\n${model.signatureOf(declaration)}\n\`\`\``);
                    if (declaration.kind === "NativeFunction")
                    {
                        lines.push("a function written in C++ (`import native`)");
                    }
                    if (declaration.file && path.resolve(declaration.file) !== path.resolve(document.uri.fsPath))
                    {
                        lines.push(`from \`${path.basename(declaration.file)}\``);
                    }
                }
            }
            // The type the semantic analyser worked out for the expression under the pointer.
            const node = model.nodeAt(where);
            if (node && node.type && !lines.some((line) => line.includes(node.type)))
            {
                lines.push(`type: \`${node.type}\``);
            }
        }
        if (!lines.length)
        {
            return null;
        }
        const markdown = new vscode.MarkdownString(lines.join("\n\n"));
        markdown.isTrusted = true;
        return new vscode.Hover(markdown, found.range);
    }
}

function describeMarkdown(description)
{
    const lines = [];
    if (description.kind === "builtin")
    {
        lines.push(`\`\`\`thunder\n${description.title}\n\`\`\``);
        if (description.body)
        {
            lines.push(description.body);
        }
        if (description.module)
        {
            lines.push(`Needs \`import ${description.module}\`.`);
        }
    }
    else if (description.kind === "keyword")
    {
        lines.push(`**${description.title}** — ${description.body}`);
        if ((description.spellings || []).length)
        {
            lines.push(`also written ${description.spellings.map((word) => `\`${word}\``).join(", ")}`);
        }
    }
    else
    {
        lines.push(`**${description.title}** — ${description.body}`);
    }
    for (const translation of (description.translations || []))
    {
        lines.push(`${translation.language}: \`${translation.name}\``);
    }
    return lines;
}

module.exports = { HoverProvider };
