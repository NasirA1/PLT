// The Thunder extension for VS Code.
//
// Everything the editor knows about Thunder comes from the toolchain itself: `thunderc --json`
// for the diagnostics, the syntax tree, the symbols and the token stream, `thunderc
// --language-info` for the keywords and built-ins of each natural language, and `thundervm
// --trace-json` for the debugger. This file wires those into VS Code and nothing more.

"use strict";

const path = require("path");
const vscode = require("vscode");

const { ArtefactProvider, SCHEME } = require("./artefacts");
const { CodeActionProvider, FormattingProvider } = require("./providers/edits");
const { CompletionProvider, SignatureHelpProvider } = require("./providers/completion");
const { DebugSession } = require("./debug/adapter");
const { DiagnosticsManager } = require("./diagnostics");
const { DocumentHighlightProvider, LEGEND, SemanticTokensProvider } = require("./providers/highlight");
const { HoverProvider } = require("./providers/hover");
const { PipelineView, ProjectView, SamplesView } = require("./views");
const { Services } = require("./services");
const { TASK_TYPE, TaskProvider } = require("./tasks");
const { prepareDebugSession } = require("./debug/launch");
const { registerCommands } = require("./commands");
const navigation = require("./providers/navigation");

const SELECTOR = { language: "thunder" };

function activate(context)
{
    const services = new Services(context);
    context.subscriptions.push(services);

    const diagnostics = new DiagnosticsManager(services);
    services.diagnostics = diagnostics;
    context.subscriptions.push(diagnostics);

    const tasks = new TaskProvider(services);
    services.tasks = tasks;
    context.subscriptions.push(vscode.tasks.registerTaskProvider(TASK_TYPE, tasks));

    // ---------------------------------------------------------------- the side bar
    const projectView = new ProjectView(services);
    const pipelineView = new PipelineView(services);
    const samplesView = new SamplesView(services);
    context.subscriptions.push(
        vscode.window.registerTreeDataProvider("thunderProject", projectView),
        vscode.window.registerTreeDataProvider("thunderPipeline", pipelineView),
        vscode.window.registerTreeDataProvider("thunderSamples", samplesView));

    const artefacts = new ArtefactProvider(services);
    context.subscriptions.push(vscode.workspace.registerTextDocumentContentProvider(SCHEME, artefacts));

    services.refreshViews = () =>
    {
        services.forgetProjects();
        services.reload();
        projectView.refresh();
        pipelineView.refresh();
        samplesView.refresh();
    };
    services.refreshArtefacts = () => artefacts.refresh();

    // ---------------------------------------------------------------- language features
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(
            SELECTOR, new CompletionProvider(services), ".", "\""),
        vscode.languages.registerSignatureHelpProvider(
            SELECTOR, new SignatureHelpProvider(services), "(", ",", "،"),
        vscode.languages.registerHoverProvider(SELECTOR, new HoverProvider(services)),
        vscode.languages.registerDefinitionProvider(SELECTOR, new navigation.DefinitionProvider(services)),
        vscode.languages.registerReferenceProvider(SELECTOR, new navigation.ReferenceProvider(services)),
        vscode.languages.registerRenameProvider(SELECTOR, new navigation.RenameProvider(services)),
        vscode.languages.registerDocumentSymbolProvider(SELECTOR, new navigation.DocumentSymbolProvider(services)),
        vscode.languages.registerWorkspaceSymbolProvider(new navigation.WorkspaceSymbolProvider(services)),
        vscode.languages.registerInlayHintsProvider(SELECTOR, new navigation.InlayHintsProvider(services)),
        vscode.languages.registerDocumentSemanticTokensProvider(
            SELECTOR, new SemanticTokensProvider(services), LEGEND),
        vscode.languages.registerDocumentHighlightProvider(SELECTOR, new DocumentHighlightProvider(services)),
        vscode.languages.registerDocumentFormattingEditProvider(SELECTOR, new FormattingProvider(services)),
        vscode.languages.registerCodeActionsProvider(SELECTOR, new CodeActionProvider(services),
            { providedCodeActionKinds: CodeActionProvider.kinds }));

    // ---------------------------------------------------------------- the debugger
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider("thunder", new DebugConfigurationProvider(services)),
        vscode.debug.registerDebugAdapterDescriptorFactory("thunder", new DebugAdapterFactory(services)));

    registerCommands(context, services);

    // ---------------------------------------------------------------- keeping up to date
    const check = (document) =>
    {
        if (document.languageId !== "thunder")
        {
            return;
        }
        diagnostics.schedule(document);
    };
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(check),
        vscode.workspace.onDidChangeTextDocument((event) => check(event.document)),
        vscode.workspace.onDidSaveTextDocument((document) =>
        {
            check(document);
            artefacts.refresh();
            if (path.basename(document.uri.fsPath) === "thunder.json")
            {
                services.refreshViews();
            }
        }),
        vscode.workspace.onDidCloseTextDocument((document) =>
        {
            if (document.languageId === "thunder")
            {
                diagnostics.forget(document.uri);
                services.analyser.invalidate(document.uri.fsPath);
            }
        }),
        vscode.workspace.onDidChangeWorkspaceFolders(() => services.refreshViews()),
        vscode.workspace.onDidChangeConfiguration((event) =>
        {
            if (event.affectsConfiguration("thunder"))
            {
                services.refreshViews();
                for (const document of vscode.workspace.textDocuments)
                {
                    check(document);
                }
            }
        }),
        vscode.window.onDidChangeActiveTextEditor((editor) =>
        {
            if (editor && editor.document.languageId === "thunder")
            {
                pipelineView.show(editor.document.uri.fsPath,
                    services.analyser.modelFor(editor.document.uri.fsPath));
                check(editor.document);
            }
        }),
        services.analyser.onDidAnalyse((file, model) =>
        {
            const editor = vscode.window.activeTextEditor;
            if (editor && path.resolve(editor.document.uri.fsPath) === path.resolve(file))
            {
                pipelineView.show(file, model);
            }
        }));

    for (const document of vscode.workspace.textDocuments)
    {
        check(document);
    }
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.languageId === "thunder")
    {
        pipelineView.show(editor.document.uri.fsPath, null);
    }

    return {
        // Exposed so that other extensions (and the tests) can reach the same model.
        services,
        analyse: (document) => services.analyse(document),
    };
}

/** Fills in a launch configuration that was started with none (F5 in a Thunder file). */
class DebugConfigurationProvider
{
    constructor(services)
    {
        this.services = services;
    }

    resolveDebugConfiguration(folder, configuration)
    {
        if (!configuration.type && !configuration.request && !configuration.name)
        {
            const file = this.services.programFor();
            if (!file)
            {
                return vscode.window.showInformationMessage("Open a Thunder program to debug.").then(() => undefined);
            }
            return {
                type: "thunder",
                request: "launch",
                name: `Debug ${path.basename(file)}`,
                program: file,
                stopOnEntry: true,
            };
        }
        if (!configuration.program)
        {
            configuration.program = this.services.programFor();
        }
        return configuration;
    }
}

/** Runs the debug adapter inside the extension: no second process, no extra dependency. */
class DebugAdapterFactory
{
    constructor(services)
    {
        this.services = services;
    }

    createDebugAdapterDescriptor(session)
    {
        return new vscode.DebugAdapterInlineImplementation(new InlineAdapter(this.services, session));
    }
}

class InlineAdapter
{
    constructor(services, session)
    {
        this.emitter = new vscode.EventEmitter();
        this.onDidSendMessage = this.emitter.event;
        this.session = new DebugSession(
            (message) => this.emitter.fire(message),
            {
                prepare: (configuration) => prepareDebugSession(services, configuration, session),
            });
    }

    handleMessage(message)
    {
        this.session.handleMessage(message);
    }

    dispose()
    {
        this.emitter.dispose();
    }
}

function deactivate()
{
}

module.exports = { activate, deactivate };
