// The Thunder side bar: the project, what the compiler did to the current file, and the way
// into the SDK's samples and reference.

"use strict";

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const { STAGES } = require("./artefacts");
const { listThunderFiles } = require("./providers/completion");

class TreeNode extends vscode.TreeItem
{
    constructor(label, collapsible, options)
    {
        super(label, collapsible);
        Object.assign(this, options || {});
    }
}

/**
 * The project view: every workspace folder with its entry point, its programs, the modules it
 * imports and the libraries it uses, plus the toolchain the extension found.
 */
class ProjectView
{
    constructor(services)
    {
        this.services = services;
        this.emitter = new vscode.EventEmitter();
        this.onDidChangeTreeData = this.emitter.event;
    }

    refresh()
    {
        this.emitter.fire();
    }

    getTreeItem(element)
    {
        return element;
    }

    async getChildren(element)
    {
        if (!element)
        {
            return this.roots();
        }
        if (element.folder && element.kind === "folder")
        {
            return this.folderChildren(element.folder);
        }
        if (element.kind === "programs")
        {
            return element.files.map((file) => this.programNode(file, element.entry));
        }
        return [];
    }

    roots()
    {
        const nodes = [];
        const toolchain = this.services.toolchain();
        const status = new TreeNode(
            toolchain ? "Toolchain: ready" : "Toolchain: not found",
            vscode.TreeItemCollapsibleState.None,
            {
                description: toolchain ? toolchain.directory : "click to choose the directory",
                iconPath: new vscode.ThemeIcon(toolchain ? "tools" : "warning"),
                command: {
                    command: toolchain ? "thunder.showBuildLog" : "thunder.selectToolchain",
                    title: "Thunder toolchain",
                },
                contextValue: "thunderToolchain",
            });
        nodes.push(status);
        for (const folder of (vscode.workspace.workspaceFolders || []))
        {
            const project = this.services.projectFor(folder.uri);
            nodes.push(new TreeNode(folder.name, vscode.TreeItemCollapsibleState.Expanded, {
                description: project.file ? "thunder.json" : "folder",
                iconPath: new vscode.ThemeIcon(project.file ? "project" : "folder"),
                folder,
                kind: "folder",
                contextValue: "thunderFolder",
                resourceUri: folder.uri,
            }));
        }
        return nodes;
    }

    folderChildren(folder)
    {
        const project = this.services.projectFor(folder.uri);
        const files = listThunderFiles(folder.uri.fsPath, 3).sort();
        const nodes = [];
        if (project.name)
        {
            nodes.push(new TreeNode(project.name, vscode.TreeItemCollapsibleState.None, {
                description: "name",
                iconPath: new vscode.ThemeIcon("tag"),
            }));
        }
        nodes.push(new TreeNode(
            project.entry ? path.relative(folder.uri.fsPath, project.entry) : "(not set)",
            vscode.TreeItemCollapsibleState.None,
            {
                description: "entry point",
                iconPath: new vscode.ThemeIcon("star-full"),
                command: project.entry
                    ? { command: "vscode.open", title: "Open", arguments: [vscode.Uri.file(project.entry)] }
                    : undefined,
            }));
        nodes.push(new TreeNode(languageName(project.language), vscode.TreeItemCollapsibleState.None, {
            description: "language",
            iconPath: new vscode.ThemeIcon("globe"),
        }));
        if (project.modulePaths.length)
        {
            nodes.push(new TreeNode(
                project.modulePaths.map((directory) => path.relative(folder.uri.fsPath, directory)).join(", "),
                vscode.TreeItemCollapsibleState.None,
                { description: "module paths", iconPath: new vscode.ThemeIcon("library") }));
        }
        nodes.push(new TreeNode(`Programs (${files.length})`, vscode.TreeItemCollapsibleState.Expanded, {
            kind: "programs",
            files,
            entry: project.entry,
            iconPath: new vscode.ThemeIcon("file-code"),
        }));
        return nodes;
    }

    programNode(file, entry)
    {
        const isEntry = entry && path.resolve(entry) === path.resolve(file);
        return new TreeNode(path.basename(file), vscode.TreeItemCollapsibleState.None, {
            description: isEntry ? "entry point" : path.dirname(vscode.workspace.asRelativePath(file)),
            resourceUri: vscode.Uri.file(file),
            iconPath: new vscode.ThemeIcon(isEntry ? "star-full" : "file-code"),
            contextValue: "thunderProgram",
            command: { command: "vscode.open", title: "Open", arguments: [vscode.Uri.file(file)] },
        });
    }
}

function languageName(code)
{
    if (code === "ps")
    {
        return "Pashto (پښتو)";
    }
    if (code === "en")
    {
        return "English";
    }
    return "from each file's directive";
}

/**
 * The pipeline view: the phases of the compiler for the file in front of the user, with what
 * each produced and how long it took, and a way into every artefact.
 */
class PipelineView
{
    constructor(services)
    {
        this.services = services;
        this.emitter = new vscode.EventEmitter();
        this.onDidChangeTreeData = this.emitter.event;
        this.file = null;
        this.model = null;
    }

    show(file, model)
    {
        this.file = file;
        this.model = model;
        this.emitter.fire();
    }

    refresh()
    {
        this.emitter.fire();
    }

    getTreeItem(element)
    {
        return element;
    }

    getChildren(element)
    {
        if (element)
        {
            return [];
        }
        if (!this.file)
        {
            return [new TreeNode("Open a Thunder program to see what the compiler does with it.",
                vscode.TreeItemCollapsibleState.None, { iconPath: new vscode.ThemeIcon("info") })];
        }
        const nodes = [new TreeNode(path.basename(this.file), vscode.TreeItemCollapsibleState.None, {
            description: this.model && this.model.succeeded ? "compiled" : "has problems",
            iconPath: new vscode.ThemeIcon(this.model && this.model.succeeded ? "pass" : "error"),
            resourceUri: vscode.Uri.file(this.file),
        })];
        for (const phase of (this.model ? this.model.phases : []))
        {
            nodes.push(new TreeNode(capitalise(phase.name), vscode.TreeItemCollapsibleState.None, {
                description: phase.ran
                    ? `${(phase.microseconds / 1000).toFixed(2)} ms${phase.succeeded ? "" : " - failed"}`
                    : "not reached",
                iconPath: new vscode.ThemeIcon(phase.ran ? (phase.succeeded ? "check" : "error") : "circle-outline"),
            }));
        }
        if (this.model)
        {
            nodes.push(new TreeNode(`${this.model.report.error_count || 0} error(s), `
                + `${this.model.report.warning_count || 0} warning(s)`,
            vscode.TreeItemCollapsibleState.None, { iconPath: new vscode.ThemeIcon("list-flat") }));
        }
        for (const [stage, definition] of Object.entries(STAGES))
        {
            nodes.push(new TreeNode(capitalise(definition.title), vscode.TreeItemCollapsibleState.None, {
                description: definition.description,
                iconPath: new vscode.ThemeIcon("output"),
                command: {
                    command: "thunder.showArtefact",
                    title: "Show",
                    arguments: [stage, this.file],
                },
            }));
        }
        return nodes;
    }
}

function capitalise(text)
{
    return text ? text[0].toUpperCase() + text.slice(1) : text;
}

/** The samples and the reference documents that come with the toolchain. */
class SamplesView
{
    constructor(services)
    {
        this.services = services;
        this.emitter = new vscode.EventEmitter();
        this.onDidChangeTreeData = this.emitter.event;
    }

    refresh()
    {
        this.emitter.fire();
    }

    getTreeItem(element)
    {
        return element;
    }

    getChildren(element)
    {
        const root = this.services.sdkRoot();
        if (!root)
        {
            return [new TreeNode("The Thunder SDK was not found beside the toolchain.",
                vscode.TreeItemCollapsibleState.None, { iconPath: new vscode.ThemeIcon("info") })];
        }
        if (!element)
        {
            const nodes = [];
            for (const language of ["en", "ps"])
            {
                if (fs.existsSync(path.join(root, "samples", language)))
                {
                    nodes.push(new TreeNode(`Samples (${languageName(language)})`,
                        vscode.TreeItemCollapsibleState.Collapsed,
                        { kind: "samples", directory: path.join(root, "samples", language),
                            iconPath: new vscode.ThemeIcon("book") }));
                }
            }
            if (fs.existsSync(path.join(root, "docs")))
            {
                nodes.push(new TreeNode("SDK reference", vscode.TreeItemCollapsibleState.Collapsed,
                    { kind: "docs", directory: path.join(root, "docs"), iconPath: new vscode.ThemeIcon("library") }));
            }
            return nodes;
        }
        if (element.kind === "samples")
        {
            return listThunderFiles(element.directory, 0).sort().map((file) =>
                new TreeNode(path.basename(file), vscode.TreeItemCollapsibleState.None, {
                    resourceUri: vscode.Uri.file(file),
                    iconPath: new vscode.ThemeIcon("file-code"),
                    command: { command: "vscode.open", title: "Open", arguments: [vscode.Uri.file(file)] },
                }));
        }
        if (element.kind === "docs")
        {
            return documentsIn(element.directory).map((file) =>
                new TreeNode(path.basename(file), vscode.TreeItemCollapsibleState.None, {
                    resourceUri: vscode.Uri.file(file),
                    iconPath: new vscode.ThemeIcon("book"),
                    command: {
                        command: "markdown.showPreview",
                        title: "Open",
                        arguments: [vscode.Uri.file(file)],
                    },
                }));
        }
        return [];
    }
}

function documentsIn(directory)
{
    try
    {
        return fs.readdirSync(directory)
            .filter((name) => name.endsWith(".md"))
            .map((name) => path.join(directory, name))
            .sort();
    }
    catch (error)
    {
        return [];
    }
}

module.exports = { PipelineView, ProjectView, SamplesView, TreeNode, documentsIn };
