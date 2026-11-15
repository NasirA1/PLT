// The Thunder extension for VS Code.
//
// Everything the editor knows about Thunder comes from the toolchain itself: `thunderc --json`
// for the diagnostics, the syntax tree, the symbols and the token stream, `thunderc
// --language-info` for the keywords and built-ins of each natural language, and `thundervm
// --debug` for the debugger. This file wires those into VS Code and nothing more.

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
const { CanvasPanel } = require("./canvasview");
const { ThunderTests } = require("./testing");
const { PipelineView, ProjectView, SamplesView } = require("./views");
const { ProgramOutputView, VIEW_ID: OUTPUT_VIEW } = require("./outputview");
const { RightToLeftEditorProvider, VIEW_TYPE: RIGHT_TO_LEFT_EDITOR, isRightToLeftProgram, reopenWith } = require("./rtleditor");
const { Services } = require("./services");
const { TASK_TYPE, TaskProvider } = require("./tasks");
const { prepareDebugSession } = require("./debug/launch");
const { registerCommands } = require("./commands");
const navigation = require("./providers/navigation");
const { sameFile } = require("./util");

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

    // ---------------------------------------------------------------- right-to-left programs
    // A Pashto program is edited in an editor of its own, laid out right to left (rtleditor.js);
    // it is a second view of the same document, so the text editor stays one click away.
    const rightToLeft = new RightToLeftEditorProvider(services);
    services.rightToLeftEditor = rightToLeft;
    services.keepTextEditor = new Set(); // files the user moved back to the text editor
    // No enableFindWidget: the page has a find bar of its own (Ctrl+F), which searches the text
    // rather than the rendered page and offers loose Pashto letters (ی finds ي, ى, ۍ, ې, ئ).
    context.subscriptions.push(vscode.window.registerCustomEditorProvider(RIGHT_TO_LEFT_EDITOR, rightToLeft, {
        webviewOptions: { retainContextWhenHidden: true },
        supportsMultipleEditorsPerDocument: true,
    }));

    // The language of the program in front of the user, in the status bar: it says which
    // keyword set the editor is using, and for a Pashto program opens the right-to-left editor.
    const languageStatus = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    languageStatus.command = "thunder.openRightToLeftEditor";
    context.subscriptions.push(languageStatus);
    services.showLanguageStatus = async (document) =>
    {
        if (!document || document.languageId !== "thunder")
        {
            languageStatus.hide();
            await vscode.commands.executeCommand("setContext", "thunder.rightToLeft", false);
            await vscode.commands.executeCommand("setContext", "thunder.hasTests", false);
            return null;
        }
        const code = services.languageOfText(document.getText(), document.uri.fsPath);
        const data = await services.languageData(code);
        languageStatus.text = `$(globe) ${data.nativeName}`;
        languageStatus.tooltip = data.rightToLeft
            ? `This program is written in ${data.nativeName}. Click to edit it right to left.`
            : `This program is written in ${data.nativeName} (its keywords and built-ins).`;
        languageStatus.show();
        await vscode.commands.executeCommand("setContext", "thunder.rightToLeft", data.rightToLeft);
        // The Run Tests button, on both editors, for a program that declares tests.
        await vscode.commands.executeCommand("setContext", "thunder.hasTests",
            Boolean(services.tests) && services.tests.refreshText(document.uri, document.getText()).length > 0);
        return data;
    };

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
    // Panel > Thunder Output: the program's output in its own direction (the Debug Console's
    // alignment cannot be changed by an extension).
    const outputView = new ProgramOutputView(services);
    services.outputView = outputView;
    // The Thunder Canvas: a graphics program's picture while it is debugged, opened by its first frame.
    services.canvasPanel = new CanvasPanel(services);
    context.subscriptions.push(vscode.window.registerWebviewViewProvider(OUTPUT_VIEW, outputView,
        { webviewOptions: { retainContextWhenHidden: true } }));
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider("thunder", new DebugConfigurationProvider(services)),
        vscode.debug.registerDebugAdapterDescriptorFactory("thunder", new DebugAdapterFactory(services)));
    followDebugger(context, services, rightToLeft);

    // ---------------------------------------------------------------- tests
    // A program that imports "testing" is a test file: its tests appear in the Test Explorer.
    services.tests = new ThunderTests(services);
    context.subscriptions.push(services.tests);

    registerCommands(context, services);

    // "thunder.running": a program is running in the terminal (the run task), so the Stop button
    // and Shift+F5 are offered - while debugging, VS Code's own inDebugMode says so.
    if (vscode.tasks.onDidStartTaskProcess && vscode.tasks.onDidEndTaskProcess)
    {
        const isThunderTask = (event) => event && event.execution && event.execution.task
            && event.execution.task.definition && event.execution.task.definition.type === TASK_TYPE;
        context.subscriptions.push(
            vscode.tasks.onDidStartTaskProcess((event) =>
            {
                if (isThunderTask(event))
                {
                    vscode.commands.executeCommand("setContext", "thunder.running", true);
                }
            }),
            vscode.tasks.onDidEndTaskProcess((event) =>
            {
                if (isThunderTask(event))
                {
                    vscode.commands.executeCommand("setContext", "thunder.running", false);
                }
            }));
    }

    // ---------------------------------------------------------------- keeping up to date
    const followEditor = async (editor) =>
    {
        const document = editor ? editor.document : null;
        const language = await services.showLanguageStatus(document);
        if (!document || document.languageId !== "thunder")
        {
            return;
        }
        pipelineView.show(document.uri.fsPath, services.analyser.modelFor(document.uri.fsPath));
        check(document);
        // A right-to-left program is edited right to left: its text editor gives way to the
        // right-to-left editor, unless the user has moved it back to the text editor.
        if (services.configuration(document.uri).get("rightToLeft.editor") !== "off"
            && !services.keepTextEditor.has(String(document.uri))
            && language && language.rightToLeft && await isRightToLeftProgram(services, document))
        {
            await reopenWith(document.uri, RIGHT_TO_LEFT_EDITOR);
        }
    };

    const check = (document) =>
    {
        if (document.languageId !== "thunder")
        {
            return;
        }
        diagnostics.schedule(document);
    };

    // Typing the language directive ("# ژبه: ps" on the first line) into a new file makes it a
    // Pashto program there and then: the language status follows, and the file moves to the
    // right-to-left editor - the way an already-Pashto file does when it is opened. Only the
    // first line can change the language, so only its edits are watched, after a short quiet.
    let directiveTimer = null;
    const followDirective = (event) =>
    {
        const document = event.document;
        const editor = vscode.window.activeTextEditor;
        if (document.languageId !== "thunder" || !editor || editor.document !== document)
        {
            return;
        }
        if (!event.contentChanges.some((change) => change.range && change.range.start.line === 0))
        {
            return;
        }
        clearTimeout(directiveTimer);
        directiveTimer = setTimeout(() => followEditor(editor), 400);
    };

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(check),
        vscode.workspace.onDidChangeTextDocument((event) =>
        {
            check(event.document);
            followDirective(event);
        }),
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
        vscode.window.onDidChangeActiveTextEditor(followEditor),
        services.analyser.onDidAnalyse((file, model) =>
        {
            const editor = vscode.window.activeTextEditor;
            if (editor && sameFile(editor.document.uri.fsPath, file))
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
    followEditor(editor);

    return {
        // Exposed so that other extensions (and the tests) can reach the same model.
        services,
        analyse: (document) => services.analyse(document),
    };
}

/**
 * What the debugger tells the editor beyond the protocol VS Code already understands.
 *
 * The adapter sends a custom event with the stopped program's position (VS Code draws its own
 * marker in the text editor, but nothing inside the right-to-left editor, so that editor draws
 * it from this), one when the program runs on, and one when the program asks for input - a
 * question the user answers in an input box or in the Debug Console. When the user picks another
 * frame in the Call Stack, that frame's line is shown as the focused one.
 */
function followDebugger(context, services, rightToLeft)
{
    const isThunder = (session) => session && session.type === "thunder";
    context.subscriptions.push(
        rightToLeft,
        vscode.debug.onDidReceiveDebugSessionCustomEvent(async (event) =>
        {
            if (!isThunder(event.session))
            {
                return;
            }
            if (event.event === "thunder/frame")
            {
                services.canvasPanel.showFrame(event.session, (event.body || {}).record);
            }
            else if (event.event === "thunder/position")
            {
                rightToLeft.showExecution(event.body);
            }
            else if (event.event === "thunder/continued")
            {
                rightToLeft.showExecution(null);
            }
            else if (event.event === "thunder/input")
            {
                if (services.outputView && services.outputView.session === event.session && services.outputView.askForInput())
                {
                    return; // the Thunder Output view's input line takes it
                }
                const body = event.body || {}; // the adapter words it in the program's language
                const text = await vscode.window.showInputBox({
                    title: body.title || "The program asks for input",
                    prompt: body.prompt || `${path.basename(body.program || "The program")} is waiting for a line of input`,
                    placeHolder: body.placeholder || "Type the line and press Enter (Escape: the end of the input)",
                    ignoreFocusOut: true,
                });
                await event.session.customRequest("evaluate", {
                    expression: text === undefined ? "" : text,
                    context: "repl",
                });
            }
        }),
        vscode.debug.onDidTerminateDebugSession((session) =>
        {
            if (isThunder(session))
            {
                rightToLeft.showExecution(null);
                services.canvasPanel.end(session);
            }
        }));
    if (vscode.debug.onDidChangeActiveStackItem)
    {
        context.subscriptions.push(vscode.debug.onDidChangeActiveStackItem(async (item) =>
        {
            if (!item || !isThunder(item.session) || item.frameId === undefined)
            {
                return;
            }
            const trace = await item.session.customRequest("stackTrace", { threadId: item.threadId }).catch(() => null);
            const frame = trace && trace.stackFrames ? trace.stackFrames.find((candidate) => candidate.id === item.frameId) : null;
            const file = frame ? frame.thunderFile || (frame.source && frame.source.path) : null;
            if (file)
            {
                rightToLeft.showExecution({
                    file,
                    line: frame.line,
                    column: frame.column,
                    focused: frame.id !== 1, // the innermost frame is the stopped line itself
                });
            }
        }));
    }
}

/** Fills in a launch configuration that was started with none (F5 in a Thunder file). */
class DebugConfigurationProvider
{
    constructor(services)
    {
        this.services = services;
    }

    async resolveDebugConfiguration(folder, configuration)
    {
        if (!configuration.type && !configuration.request && !configuration.name)
        {
            const file = this.services.programFor();
            if (!file)
            {
                return vscode.window.showInformationMessage("Open a Thunder program to debug.").then(() => undefined);
            }
            configuration = {
                type: "thunder",
                request: "launch",
                name: `Debug ${path.basename(file)}`,
                program: file,
                stopOnEntry: true,
                noDebug: configuration.noDebug,
            };
        }
        if (!configuration.program)
        {
            configuration.program = this.services.programFor();
        }
        // "Run Without Debugging" (Ctrl+F5) sets noDebug: the program is run as the run task
        // runs it - compiled and executed in the terminal, where it reads and writes directly.
        if (configuration.noDebug && configuration.program)
        {
            await vscode.commands.executeCommand("thunder.run", vscode.Uri.file(configuration.program));
            return null;
        }
        // When the Thunder Output view will show this session, VS Code must not open the Debug
        // Console over it - which it does, by default, at the very first session of the window
        // ("internalConsoleOptions": "openOnFirstSessionStart"), covering the view just revealed.
        if (configuration.program && configuration.internalConsoleOptions === undefined
            && this.services.outputView && await this.services.outputView.takes(configuration.program))
        {
            configuration.internalConsoleOptions = "neverOpen";
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

/** The program's output events, also shown in the Thunder Output view. */
function copyToOutputView(output, message)
{
    if (!output || message.type !== "event")
    {
        return;
    }
    if (message.event === "output" && message.body)
    {
        output.append(message.body.output, message.body.category);
    }
    else if (message.event === "terminated")
    {
        output.end();
    }
}

class InlineAdapter
{
    constructor(services, session)
    {
        this.emitter = new vscode.EventEmitter();
        this.onDidSendMessage = this.emitter.event;
        const output = services.outputView;
        this.session = new DebugSession(
            (message) =>
            {
                copyToOutputView(output, message);
                this.emitter.fire(message);
            },
            {
                prepare: async (configuration) =>
                {
                    if (output)
                    {
                        await output.begin(session, configuration.program);
                    }
                    return prepareDebugSession(services, configuration, session);
                },
                log: (text) => services.write(text),
                hidesSource: (file) => services.rightToLeftEditor.showsFile(file),
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
