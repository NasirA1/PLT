// "Under the bonnet": what each phase of the compiler produced, shown as a document.
//
// The Qt IDE gives every artefact a tab; in VS Code they are read-only documents with the
// scheme `thunder-artefact`, filled by the compiler's own printers (`thunderc --emit tokens`,
// `--emit ast`, `--emit symbols`, `--emit asm`, `--emit hex`). Opening one beside the program
// is the same view a student gets in the IDE, and the text is exactly what the command line
// prints - there is no second renderer to disagree with it.

"use strict";

const path = require("path");
const vscode = require("vscode");

const SCHEME = "thunder-artefact";

const STAGES = {
    tokens: { emit: "tokens", title: "tokens", description: "the token stream (lexical analysis)" },
    ast: { emit: "ast", title: "syntax tree", description: "the abstract syntax tree (syntax analysis)" },
    symbols: { emit: "symbols", title: "symbols", description: "scopes and symbols (semantic analysis)" },
    asm: { emit: "asm", title: "bytecode", description: "the bytecode listing (code generation)" },
    hex: { emit: "hex", title: "hex dump", description: "the compiled file, byte by byte" },
};

/** The document that shows one phase's output for one program. */
function artefactUri(stage, file)
{
    const name = path.basename(file, path.extname(file));
    return vscode.Uri.parse(`${SCHEME}:/${stage}/${name}.${stage}.txt`)
        .with({ query: `file=${encodeURIComponent(file)}` });
}

class ArtefactProvider
{
    constructor(services)
    {
        this.services = services;
        this.emitter = new vscode.EventEmitter();
        this.onDidChange = this.emitter.event;
    }

    /** Asks every open artefact document to reload (after a file was saved or compiled). */
    refresh()
    {
        for (const document of vscode.workspace.textDocuments)
        {
            if (document.uri.scheme === SCHEME)
            {
                this.emitter.fire(document.uri);
            }
        }
    }

    async provideTextDocumentContent(uri, cancellation)
    {
        const stage = uri.path.split("/")[1];
        const parameters = new URLSearchParams(uri.query);
        const file = parameters.get("file") || "";
        const definition = STAGES[stage];
        if (!definition || !file)
        {
            return `Unknown artefact "${stage}".`;
        }
        const toolchain = this.services.toolchain();
        if (!toolchain)
        {
            return this.services.missingToolchainMessage();
        }
        const source = await this.services.sourceFileFor(file);
        const command = [toolchain.thunderc, "--lang", await this.services.languageCodeFor(file)];
        for (const directory of source.modulePaths)
        {
            command.push("--module-path", directory);
        }
        command.push("--emit", definition.emit, "--no-output", source.path);
        const result = await toolchain.execute(command, { cwd: path.dirname(file), timeout: 60000, logErrors: false });
        const header = [
            `// ${definition.description}`,
            `// ${path.basename(file)} - regenerated every time this document is opened or the file is saved`,
            "",
        ].join("\n");
        const body = result.stdout || "";
        const problems = result.stderr ? `\n// the compiler also said:\n${result.stderr}` : "";
        return header + body + problems;
    }
}

/** Opens one artefact beside the program. */
async function showArtefact(stage, file)
{
    const uri = artefactUri(stage, file);
    const document = await vscode.workspace.openTextDocument(uri);
    await vscode.window.showTextDocument(document, { preview: true, viewColumn: vscode.ViewColumn.Beside });
    return document;
}

module.exports = { ArtefactProvider, SCHEME, STAGES, artefactUri, showArtefact };
