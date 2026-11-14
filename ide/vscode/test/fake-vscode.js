// A stand-in for the `vscode` module, so the extension can be loaded and exercised outside
// VS Code.
//
// It is deliberately simple: the classes hold what was given to them and the namespaces record
// what was registered, which is exactly what the tests need to check. Anything the extension
// does not use is missing on purpose - a missing member fails loudly rather than pretending.

"use strict";

const path = require("path");

class Position
{
    constructor(line, character)
    {
        this.line = line;
        this.character = character;
    }

    isBefore(other)
    {
        return this.line < other.line || (this.line === other.line && this.character < other.character);
    }

    translate(lines, characters)
    {
        return new Position(this.line + (lines || 0), this.character + (characters || 0));
    }
}

class Range
{
    constructor(startOrLine, startCharacterOrEnd, endLine, endCharacter)
    {
        if (startOrLine instanceof Position)
        {
            this.start = startOrLine;
            this.end = startCharacterOrEnd;
        }
        else
        {
            this.start = new Position(startOrLine, startCharacterOrEnd);
            this.end = new Position(endLine, endCharacter);
        }
    }

    isEqual(other)
    {
        return other && this.start.line === other.start.line && this.start.character === other.start.character
            && this.end.line === other.end.line && this.end.character === other.end.character;
    }

    contains(position)
    {
        return !position.isBefore(this.start) && position.isBefore(this.end);
    }
}

class Uri
{
    constructor(scheme, fsPath, query)
    {
        this.scheme = scheme;
        this.fsPath = fsPath;
        this.path = fsPath.replace(/\\/g, "/");
        this.query = query || "";
    }

    static file(file)
    {
        return new Uri("file", file);
    }

    static parse(text)
    {
        const match = /^([a-z-]+):(.*)$/i.exec(text);
        return match ? new Uri(match[1], match[2]) : new Uri("file", text);
    }

    with(changes)
    {
        return new Uri(changes.scheme || this.scheme, changes.path || this.fsPath,
            changes.query === undefined ? this.query : changes.query);
    }

    toString()
    {
        return `${this.scheme}:${this.path}${this.query ? "?" + this.query : ""}`;
    }
}

class Location
{
    constructor(uri, range)
    {
        this.uri = uri;
        this.range = range;
    }
}

class EventEmitter
{
    constructor()
    {
        this.listeners = [];
        this.event = (listener) =>
        {
            this.listeners.push(listener);
            return { dispose: () => { this.listeners = this.listeners.filter((entry) => entry !== listener); } };
        };
    }

    fire(value)
    {
        for (const listener of this.listeners.slice())
        {
            listener(value);
        }
    }

    dispose()
    {
        this.listeners = [];
    }
}

class Diagnostic
{
    constructor(range, message, severity)
    {
        this.range = range;
        this.message = message;
        this.severity = severity;
    }
}

class DiagnosticRelatedInformation
{
    constructor(location, message)
    {
        this.location = location;
        this.message = message;
    }
}

class CompletionItem
{
    constructor(label, kind)
    {
        this.label = label;
        this.kind = kind;
    }
}

class CompletionList
{
    constructor(items, incomplete)
    {
        this.items = items || [];
        this.isIncomplete = Boolean(incomplete);
    }
}

class SnippetString
{
    constructor(value)
    {
        this.value = value;
    }
}

class MarkdownString
{
    constructor(value)
    {
        this.value = value || "";
    }

    appendMarkdown(text)
    {
        this.value += text;
        return this;
    }
}

class Hover
{
    constructor(contents, range)
    {
        this.contents = Array.isArray(contents) ? contents : [contents];
        this.range = range;
    }
}

class SignatureHelp
{
    constructor()
    {
        this.signatures = [];
        this.activeSignature = 0;
        this.activeParameter = 0;
    }
}

class SignatureInformation
{
    constructor(label, documentation)
    {
        this.label = label;
        this.documentation = documentation;
        this.parameters = [];
    }
}

class ParameterInformation
{
    constructor(label, documentation)
    {
        this.label = label;
        this.documentation = documentation;
    }
}

class DocumentSymbol
{
    constructor(name, detail, kind, range, selectionRange)
    {
        this.name = name;
        this.detail = detail;
        this.kind = kind;
        this.range = range;
        this.selectionRange = selectionRange;
        this.children = [];
    }
}

class SymbolInformation
{
    constructor(name, kind, containerName, location)
    {
        this.name = name;
        this.kind = kind;
        this.containerName = containerName;
        this.location = location;
    }
}

class InlayHint
{
    constructor(position, label, kind)
    {
        this.position = position;
        this.label = label;
        this.kind = kind;
    }
}

class DocumentHighlight
{
    constructor(range, kind)
    {
        this.range = range;
        this.kind = kind;
    }
}

class TextEdit
{
    constructor(range, newText)
    {
        this.range = range;
        this.newText = newText;
    }

    static replace(range, newText)
    {
        return new TextEdit(range, newText);
    }

    static insert(position, newText)
    {
        return new TextEdit(new Range(position, position), newText);
    }
}

class WorkspaceEdit
{
    constructor()
    {
        this.edits = [];
    }

    replace(uri, range, newText)
    {
        this.edits.push({ kind: "replace", uri, range, newText });
    }

    insert(uri, position, newText)
    {
        this.edits.push({ kind: "insert", uri, position, newText });
    }
}

class CodeAction
{
    constructor(title, kind)
    {
        this.title = title;
        this.kind = kind;
    }
}

class ThemeIcon
{
    constructor(id)
    {
        this.id = id;
    }
}

class TreeItem
{
    constructor(label, collapsibleState)
    {
        this.label = label;
        this.collapsibleState = collapsibleState;
    }
}

class SemanticTokensLegend
{
    constructor(types, modifiers)
    {
        this.tokenTypes = types;
        this.tokenModifiers = modifiers;
    }
}

class SemanticTokensBuilder
{
    constructor(legend)
    {
        this.legend = legend;
        this.pushed = [];
    }

    push(line, character, length, type, modifiers)
    {
        this.pushed.push({ line, character, length, type, modifiers });
    }

    build()
    {
        return { pushed: this.pushed };
    }
}

class ShellExecution
{
    constructor(commandLine, args)
    {
        this.commandLine = commandLine;
        this.command = commandLine;
        this.args = args || [];
    }
}

class Task
{
    constructor(definition, scope, name, source, execution, problemMatchers)
    {
        this.definition = definition;
        this.scope = scope;
        this.name = name;
        this.source = source;
        this.execution = execution;
        this.problemMatchers = problemMatchers;
        this.presentationOptions = {};
    }
}

class Selection extends Range
{
    constructor(anchor, active)
    {
        super(anchor, active);
        this.anchor = anchor;
        this.active = active;
    }
}

/** A webview panel: it keeps the HTML it was given, which is what the tests read. */
class WebviewPanel
{
    constructor(viewType, title, showOptions, options)
    {
        this.viewType = viewType;
        this.title = title;
        this.showOptions = showOptions;
        this.options = options;
        this.visible = true;
        this.disposed = false;
        this.revealed = [];
        this.messages = [];
        this._onDidDispose = new EventEmitter();
        this._onDidReceiveMessage = new EventEmitter();
        this.onDidDispose = this._onDidDispose.event;
        this.webview = {
            html: "",
            options: options || {},
            cspSource: "vscode-webview:",
            onDidReceiveMessage: this._onDidReceiveMessage.event,
            postMessage: (message) => { this.messages.push(message); return Promise.resolve(true); },
            asWebviewUri: (uri) => uri,
        };
    }

    /** Test helper: pretend the webview sent a message. */
    receive(message)
    {
        this._onDidReceiveMessage.fire(message);
    }

    reveal(column, preserveFocus)
    {
        this.revealed.push({ column, preserveFocus });
        this.visible = true;
    }

    dispose()
    {
        this.disposed = true;
        this.visible = false;
        this._onDidDispose.fire();
    }
}

class DebugAdapterInlineImplementation
{
    constructor(implementation)
    {
        this.implementation = implementation;
    }
}

/** A document good enough for the providers: created from text. */
function makeDocument(file, text, options)
{
    const settings = options || {};
    const lines = String(text).split("\n");
    return {
        uri: Uri.file(file),
        fileName: file,
        languageId: settings.languageId || "thunder",
        version: settings.version || 1,
        isDirty: Boolean(settings.dirty),
        isClosed: false,
        lineCount: lines.length,
        getText: (range) =>
        {
            if (!range)
            {
                return text;
            }
            const out = [];
            for (let line = range.start.line; line <= range.end.line && line < lines.length; ++line)
            {
                const from = line === range.start.line ? range.start.character : 0;
                const to = line === range.end.line ? range.end.character : lines[line].length;
                out.push(lines[line].slice(from, to));
            }
            return out.join("\n");
        },
        lineAt: (line) => ({ text: lines[typeof line === "number" ? line : line.line] || "", lineNumber: line }),
        positionAt: (offset) =>
        {
            let remaining = offset;
            for (let line = 0; line < lines.length; ++line)
            {
                if (remaining <= lines[line].length)
                {
                    return new Position(line, remaining);
                }
                remaining -= lines[line].length + 1;
            }
            return new Position(lines.length - 1, (lines[lines.length - 1] || "").length);
        },
        offsetAt: (position) =>
        {
            let offset = 0;
            for (let line = 0; line < position.line && line < lines.length; ++line)
            {
                offset += lines[line].length + 1;
            }
            return offset + position.character;
        },
        save: async () => true,
    };
}

/** Tab groups with one group, whose active tab a test can set, and which records closed tabs. */
function makeTabGroups()
{
    const group = { activeTab: null, tabs: [] };
    return {
        activeTabGroup: group,
        all: [group],
        closed: [],
        close(tab)
        {
            this.closed.push(tab);
            group.tabs = group.tabs.filter((entry) => entry !== tab);
            if (group.activeTab === tab)
            {
                group.activeTab = null;
            }
            return Promise.resolve(true);
        },
    };
}

const state = {
    configuration: {},
    documents: [],
    activeEditor: null,
    registered: {
        commands: [],
        providers: [],
        views: [],
        tasks: [],
        debug: [],
        customEditors: [],
    },
    messages: [],
    channels: [],
    webviews: [],
    statusBarItems: [],
    workspaceFolders: [],
    executed: [],
    tabGroups: makeTabGroups(),
};

function settingValue(section, key)
{
    const full = `${section}.${key}`;
    if (Object.prototype.hasOwnProperty.call(state.configuration, full))
    {
        return state.configuration[full];
    }
    return undefined;
}

const vscode = {
    // ------------------------------------------------------------------ types
    CodeAction,
    CodeActionKind: { QuickFix: "quickfix", Refactor: "refactor" },
    CompletionItem,
    CompletionItemKind: {
        Text: 0, Method: 1, Function: 2, Constructor: 3, Field: 4, Variable: 5, Class: 6, Struct: 21,
        Module: 8, Property: 9, Unit: 10, Value: 11, Enum: 12, Keyword: 13, Snippet: 14, File: 16,
        Constant: 20, TypeParameter: 24,
    },
    CompletionList,
    ConfigurationTarget: { Global: 1, Workspace: 2, WorkspaceFolder: 3 },
    DebugAdapterInlineImplementation,
    Diagnostic,
    DiagnosticRelatedInformation,
    DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
    DiagnosticTag: { Unnecessary: 1, Deprecated: 2 },
    DocumentHighlight,
    DocumentHighlightKind: { Text: 0, Read: 1, Write: 2 },
    DocumentSymbol,
    EventEmitter,
    Hover,
    InlayHint,
    InlayHintKind: { Type: 1, Parameter: 2 },
    Location,
    MarkdownString,
    ParameterInformation,
    Position,
    ProgressLocation: { Notification: 15, Window: 10 },
    Range,
    SemanticTokensBuilder,
    SemanticTokensLegend,
    Selection,
    ShellExecution,
    ShellQuoting: { Escape: 1, Strong: 2, Weak: 3 },
    SignatureHelp,
    SignatureInformation,
    SnippetString,
    SymbolInformation,
    SymbolKind: { Struct: 22, Function: 11, Variable: 12, Constant: 13, Field: 7, Module: 1 },
    Task,
    TaskGroup: { Build: "build", Test: "test" },
    TaskPanelKind: { Dedicated: 2, Shared: 1 },
    TaskRevealKind: { Always: 1, Silent: 2 },
    TaskScope: { Workspace: 1 },
    TextEdit,
    StatusBarAlignment: { Left: 1, Right: 2 },
    TextEditorRevealType: { Default: 0, InCenter: 1, InCenterIfOutsideViewport: 2, AtTop: 3 },
    ThemeIcon,
    TreeItem,
    TreeItemCollapsibleState: { None: 0, Collapsed: 1, Expanded: 2 },
    Uri,
    ViewColumn: { Beside: -2, Active: -1, One: 1 },
    WorkspaceEdit,

    // ------------------------------------------------------------------ namespaces
    commands: {
        registerCommand(name, callback)
        {
            state.registered.commands.push(name);
            vscode._commands.set(name, callback);
            return { dispose() {} };
        },
        executeCommand(name, ...args)
        {
            state.executed.push({ name, args });
            const callback = vscode._commands.get(name);
            return Promise.resolve(callback ? callback(...args) : undefined);
        },
        getCommands()
        {
            return Promise.resolve([...vscode._commands.keys()]);
        },
    },
    _commands: new Map(),

    languages: {
        createDiagnosticCollection(name)
        {
            const entries = new Map();
            return {
                name,
                entries,
                set: (uri, items) => entries.set(String(uri), items),
                delete: (uri) => entries.delete(String(uri)),
                dispose: () => entries.clear(),
            };
        },
        registerCompletionItemProvider: register("completion"),
        registerSignatureHelpProvider: register("signature"),
        registerHoverProvider: register("hover"),
        registerDefinitionProvider: register("definition"),
        registerReferenceProvider: register("reference"),
        registerRenameProvider: register("rename"),
        registerDocumentSymbolProvider: register("documentSymbol"),
        registerWorkspaceSymbolProvider: register("workspaceSymbol"),
        registerInlayHintsProvider: register("inlayHints"),
        registerDocumentSemanticTokensProvider: register("semanticTokens"),
        registerDocumentHighlightProvider: register("documentHighlight"),
        registerDocumentFormattingEditProvider: register("formatting"),
        registerCodeActionsProvider: register("codeActions"),
    },

    workspace: {
        get workspaceFolders()
        {
            return state.workspaceFolders;
        },
        get textDocuments()
        {
            return state.documents;
        },
        getConfiguration(section, resource)
        {
            return {
                get(key, fallback)
                {
                    const value = settingValue(section, key);
                    return value === undefined ? fallback : value;
                },
                update(key, value)
                {
                    state.configuration[`${section}.${key}`] = value;
                    return Promise.resolve();
                },
                resource,
            };
        },
        getWorkspaceFolder(uri)
        {
            return state.workspaceFolders.find(
                (folder) => String(uri.fsPath).startsWith(folder.uri.fsPath)) || undefined;
        },
        asRelativePath(file)
        {
            const folder = state.workspaceFolders[0];
            const full = typeof file === "string" ? file : file.fsPath;
            return folder ? path.relative(folder.uri.fsPath, full) : full;
        },
        openTextDocument(options)
        {
            if (options && options.content !== undefined)
            {
                return Promise.resolve(makeDocument("untitled", options.content));
            }
            const uri = options instanceof Uri ? options : Uri.file(String(options));
            const existing = state.documents.find((document) => String(document.uri) === String(uri));
            return Promise.resolve(existing || makeDocument(uri.fsPath, ""));
        },
        registerTextDocumentContentProvider(scheme, provider)
        {
            state.registered.providers.push({ kind: "content", scheme, provider });
            return { dispose() {} };
        },
        onDidOpenTextDocument: listener("openDocument"),
        onDidChangeTextDocument: listener("changeDocument"),
        onDidSaveTextDocument: listener("saveDocument"),
        onDidCloseTextDocument: listener("closeDocument"),
        onDidChangeWorkspaceFolders: listener("changeFolders"),
        onDidChangeConfiguration: listener("changeConfiguration"),
        applyEdit()
        {
            return Promise.resolve(true);
        },
    },

    window: {
        get activeTextEditor()
        {
            return state.activeEditor;
        },
        get visibleTextEditors()
        {
            return state.activeEditor ? [state.activeEditor] : [];
        },
        createOutputChannel(name)
        {
            const channel = {
                name,
                lines: [],
                appendLine(text) { this.lines.push(text); },
                show() {},
                dispose() {},
            };
            state.channels.push(channel);
            return channel;
        },
        registerCustomEditorProvider(viewType, provider, options)
        {
            state.registered.customEditors.push({ viewType, provider, options });
            return { dispose() {} };
        },
        get tabGroups()
        {
            return state.tabGroups;
        },
        createWebviewPanel(viewType, title, showOptions, options)
        {
            const panel = new WebviewPanel(viewType, title, showOptions, options);
            state.webviews.push(panel);
            return panel;
        },
        createStatusBarItem(alignment, priority)
        {
            const item = {
                alignment,
                priority,
                text: "",
                tooltip: "",
                command: undefined,
                visible: false,
                show() { this.visible = true; },
                hide() { this.visible = false; },
                dispose() { this.visible = false; },
            };
            state.statusBarItems.push(item);
            return item;
        },
        createTerminal(options)
        {
            return { options, sent: [], show() {}, sendText(text) { this.sent.push(text); }, dispose() {} };
        },
        registerTreeDataProvider(id, provider)
        {
            state.registered.views.push({ id, provider });
            return { dispose() {} };
        },
        showInformationMessage(message)
        {
            state.messages.push({ kind: "information", message });
            return Promise.resolve(undefined);
        },
        showWarningMessage(message)
        {
            state.messages.push({ kind: "warning", message });
            return Promise.resolve(undefined);
        },
        showErrorMessage(message)
        {
            state.messages.push({ kind: "error", message });
            return Promise.resolve(undefined);
        },
        showQuickPick(items)
        {
            return Promise.resolve(Array.isArray(items) ? items[0] : undefined);
        },
        showInputBox(options)
        {
            return Promise.resolve((options || {}).value || "");
        },
        showOpenDialog()
        {
            return Promise.resolve(undefined);
        },
        showSaveDialog()
        {
            return Promise.resolve(undefined);
        },
        showTextDocument(document)
        {
            state.activeEditor = { document };
            return Promise.resolve(state.activeEditor);
        },
        setStatusBarMessage()
        {
            return { dispose() {} };
        },
        withProgress(options, task)
        {
            return task({ report() {} }, { isCancellationRequested: false });
        },
        onDidChangeActiveTextEditor: listener("changeEditor"),
        onDidChangeTextEditorSelection: listener("changeSelection"),
    },

    tasks: {
        registerTaskProvider(type, provider)
        {
            state.registered.tasks.push({ type, provider });
            return { dispose() {} };
        },
        executeTask(task)
        {
            state.registered.tasks.push({ executed: task });
            return Promise.resolve({ task, terminate() {} });
        },
        get taskExecutions()
        {
            return [];
        },
    },

    debug: {
        registerDebugConfigurationProvider(type, provider)
        {
            state.registered.debug.push({ kind: "configuration", type, provider });
            return { dispose() {} };
        },
        registerDebugAdapterDescriptorFactory(type, factory)
        {
            state.registered.debug.push({ kind: "factory", type, factory });
            return { dispose() {} };
        },
        startDebugging(folder, configuration)
        {
            state.registered.debug.push({ kind: "started", configuration });
            return Promise.resolve(true);
        },
    },

    env: { openExternal() { return Promise.resolve(true); }, shell: "/bin/bash" },

    // ------------------------------------------------------------------ test helpers
    _state: state,
    _reset()
    {
        state.configuration = {};
        state.documents = [];
        state.activeEditor = null;
        state.registered = { commands: [], providers: [], views: [], tasks: [], debug: [], customEditors: [] };
        state.messages = [];
        state.channels = [];
        state.webviews = [];
        state.statusBarItems = [];
        state.workspaceFolders = [];
        state.executed = [];
        state.tabGroups = makeTabGroups();
        vscode._commands = new Map();
        vscode._listeners = {};
    },
    _listeners: {},
    _document: makeDocument,
    _openDocument(file, text, options)
    {
        const document = makeDocument(file, text, options);
        state.documents.push(document);
        state.activeEditor = {
            document,
            selection: new Selection(new Position(0, 0), new Position(0, 0)),
            revealRange() {},
        };
        return document;
    },
    /** Test helper: the panel the extension last opened. */
    _lastWebview()
    {
        return state.webviews.length ? state.webviews[state.webviews.length - 1] : null;
    },
    /** Test helper: fires the listeners the extension registered for an event. */
    async _fire(name, value)
    {
        for (const listener of (vscode._listeners[name] || []))
        {
            await listener(value);
        }
    },
    _setFolders(folders)
    {
        state.workspaceFolders = folders.map((folder, index) => ({
            uri: Uri.file(folder),
            name: path.basename(folder),
            index,
        }));
    },
};

function register(kind)
{
    return function registerProvider(...args)
    {
        state.registered.providers.push({ kind, args });
        return { dispose() {} };
    };
}

function listener(name)
{
    return function on(callback)
    {
        vscode._listeners[name] = (vscode._listeners[name] || []).concat([callback]);
        return { dispose() {} };
    };
}

module.exports = vscode;
