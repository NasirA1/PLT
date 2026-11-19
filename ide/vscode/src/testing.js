// Thunder tests in VS Code's Test Explorer.
//
// A test file is a Thunder file with declarations marked `@test` (Pashto `@آزموینه`): a function
// without parameters is one test; a struct or a module marked `@test` is a suite whose public
// methods (or functions) without parameters are its tests; `@test` on a single method of a struct
// or function of a module makes that one a test of that suite. An optional description names the
// test: `@test("adds up")`. This file finds them - in the open documents and in the workspace's
// .th files - the way the compiler does, and shows them as test items (file -> suite -> test),
// so the Test Explorer, the gutter buttons and the commands can run one test, a suite, a file
// or everything.
//
// Running means what the testing module means by it: compile the file, run it with the chosen
// tests as program arguments ("suite/name" for a test of a suite, the suite's name for a whole
// suite; the harness runs only those), and read the transcript it prints - a suite's name on a
// line of its own, then "✓ name" for a pass, "✗ name: message" for each failed expectation,
// in English and Pashto alike. A compile error or a runtime error is reported against the
// tests that did not get their line.

"use strict";

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const { compileCommand, runCommand, workDirectory } = require("./toolchain");
const { listThunderFiles } = require("./providers/completion");
const { sameFile, splitLines } = require("./util");

// The words the compiler reads, in both languages, with the letters a keyboard may vary folded
// (ي/ى -> ی, ك -> ک, گ -> ګ) as the compiler folds them.
function fold(word)
{
    return word.replace(/[يى]/g, "ی").replace(/ك/g, "ک").replace(/گ/g, "ګ");
}
const TEST_WORDS = new Set(["test", "آزموینه", "ازموینه"]);
const MOCK_WORDS = new Set(["mock", "ساختګی"]);
const CLOSE_WORDS = new Set(["close", "وتړه"]);
// `@test`, `@test("description")`, `@آزموینه(«تشریح»)`, alone on a line (a comment may follow).
const ANNOTATION = /^(\s*)@([^\s(#]+)\s*(?:\(\s*(?:"([^"]*)"|«([^»]*)»|[“„]([^”“]*)[”“])\s*\))?\s*(?:#.*)?$/;
// The declarations an annotation can mark, and the methods inside a struct.
const FUNCTION = /^(\s*)(?:func|دنده)\s+([^\s(<]+)\s*(<[^>]*>)?\s*\(([^)]*)\)\s*(:)?/;
const SUITE = /^(\s*)(struct|جوړښت|module|ټولګی|ټولګى|ټولگی|ټولگى)\s+([^\s:<]+)/;
const OTHER_BLOCK = /^(\s*)(?:mixin|ګډون|گډون|interface|برسېر|برسیر|enum|ګڼنه|گڼنه)\s/;
// The harness's transcript: a suite's heading, a pass, or one failed expectation of a test.
const PASS_LINE = /^✓ (.*)$/;
const FAIL_LINE = /^✗ (.*?): (.*)$/;
const SUITE_LINE = /^(\S[^:✓✗]*)$/;

function indentOf(line)
{
    return line.length - line.trimStart().length;
}

/** "largest_of_a_list" -> "largest of a list": a test's name when its annotation gives none. */
function displayName(declared)
{
    const name = declared.replace(/_/g, " ").trim();
    return name || declared;
}

/**
 * The tests a file declares: [{name, suite, line}] in the order they run (0-based lines; suite ""
 * for a test outside any suite), or [] if it is not a test file.
 */
function discoverTests(text)
{
    const lines = splitLines(text);
    const tests = [];
    let pending = []; // the annotations read since the last declaration: {word, description, line}
    let suite = null; // {name, marked, indent}
    for (let index = 0; index < lines.length; ++index)
    {
        const line = lines[index];
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith("#"))
        {
            continue;
        }
        const indent = indentOf(line);
        if (suite && indent <= suite.indent)
        {
            suite = null; // the suite's block has ended
        }
        const annotation = ANNOTATION.exec(line);
        if (annotation)
        {
            const description = annotation[3] !== undefined ? annotation[3]
                : (annotation[4] !== undefined ? annotation[4] : annotation[5]);
            pending.push({ word: fold(annotation[2]), description, line: index });
            continue;
        }
        const marks = pending;
        pending = [];
        const test = marks.find((mark) => TEST_WORDS.has(mark.word));
        const mock = marks.some((mark) => MOCK_WORDS.has(mark.word));
        const declaredSuite = SUITE.exec(line);
        if (declaredSuite)
        {
            // A struct or module opens a suite when it is marked, or when one of its members is.
            const name = test && test.description !== undefined ? test.description : declaredSuite[3];
            suite = mock ? null : { name, marked: Boolean(test), indent };
            continue;
        }
        if (OTHER_BLOCK.test(line))
        {
            suite = { name: "", marked: false, indent, inert: true }; // its members are never tests
            continue;
        }
        const func = FUNCTION.exec(line);
        if (!func)
        {
            continue;
        }
        const [, , name, generic, parameters, returns] = func;
        const inSuite = suite && indent > suite.indent && !suite.inert;
        if (suite && suite.inert && indent > suite.indent)
        {
            continue;
        }
        const shaped = !generic && !parameters.trim() && !returns;
        const implied = !test && inSuite && suite.marked && shaped && !name.startsWith("_") && !CLOSE_WORDS.has(fold(name));
        if ((!test || !shaped) && !implied)
        {
            continue;
        }
        tests.push({
            name: test && test.description !== undefined ? test.description : displayName(name),
            suite: inSuite ? suite.name : "",
            line: test ? test.line : index,
        });
    }
    return tests;
}

/** The argument that runs one test: "suite/name", or the name of a test outside any suite. */
function testKey(suite, name)
{
    return suite ? `${suite}/${name}` : name;
}

/**
 * What the module printed, by test key (testKey): {passed: Set, failed: Map(key -> [messages])}.
 * A line that is neither a pass nor a failure and is not indented is a suite's heading: the
 * tests after it (indented) belong to it. With `suites` (a Set of the names the file declares),
 * only those count as headings, so a line the program itself prints is never mistaken for one.
 */
function parseTranscript(stdout, suites)
{
    const passed = new Set();
    const failed = new Map();
    let suite = "";
    for (const line of splitLines(stdout))
    {
        const indented = /^\s/.test(line);
        const text = line.trim();
        const pass = PASS_LINE.exec(text);
        if (pass)
        {
            passed.add(testKey(indented ? suite : "", pass[1]));
            continue;
        }
        const fail = FAIL_LINE.exec(text);
        if (fail)
        {
            const key = testKey(indented ? suite : "", fail[1]);
            if (!failed.has(key))
            {
                failed.set(key, []);
            }
            failed.get(key).push(fail[2]);
            continue;
        }
        if (!indented && SUITE_LINE.test(line) && (!suites || suites.has(text)))
        {
            suite = text;
        }
    }
    return { passed, failed };
}

class ThunderTests
{
    /**
     * @param services the extension's services (toolchain, languages, projects)
     */
    constructor(services)
    {
        this.services = services;
        this.controller = vscode.tests.createTestController("thunderTests", "Thunder Tests");
        this.controller.resolveHandler = (item) => (item ? undefined : this.discoverWorkspace());
        this.controller.createRunProfile("Run", vscode.TestRunProfileKind.Run,
            (request, token) => this.run(request, token), true);
        this.controller.createRunProfile("Debug", vscode.TestRunProfileKind.Debug,
            (request) => this.debug(request), false);
        this.timers = new Map(); // file -> the timer of a pending refresh
        this.keys = new Map();   // test item id -> the program argument that runs it
        this.disposables = [
            this.controller,
            vscode.workspace.onDidOpenTextDocument((document) => this.refreshDocument(document)),
            vscode.workspace.onDidChangeTextDocument((event) => this.refreshDocument(event.document, 300)),
            vscode.workspace.onDidChangeWorkspaceFolders(() => this.discoverWorkspace()),
        ];
        for (const document of vscode.workspace.textDocuments)
        {
            this.refreshDocument(document);
        }
    }

    dispose()
    {
        for (const disposable of this.disposables)
        {
            disposable.dispose();
        }
        for (const timer of this.timers.values())
        {
            clearTimeout(timer);
        }
    }

    // ------------------------------------------------------------------ discovery

    /** Every test file of every workspace folder (a few levels deep, as the project view looks). */
    discoverWorkspace()
    {
        for (const folder of (vscode.workspace.workspaceFolders || []))
        {
            for (const file of listThunderFiles(folder.uri.fsPath, 4))
            {
                const open = vscode.workspace.textDocuments.find((document) => sameFile(document.uri.fsPath, file));
                let text = "";
                try
                {
                    text = open ? open.getText() : fs.readFileSync(file, "utf8");
                }
                catch (error)
                {
                    continue;
                }
                this.refreshText(vscode.Uri.file(file), text);
            }
        }
    }

    /** A document opened or changed: its tests follow, after a short quiet when typing. */
    refreshDocument(document, delay)
    {
        if (!document || document.languageId !== "thunder" || document.uri.scheme !== "file")
        {
            return;
        }
        const key = document.uri.fsPath;
        clearTimeout(this.timers.get(key));
        const apply = () =>
        {
            this.timers.delete(key);
            this.refreshText(document.uri, document.getText());
        };
        if (delay)
        {
            this.timers.set(key, setTimeout(apply, delay));
        }
        else
        {
            apply();
        }
    }

    /** The test items of one file, made to match its text; a file without tests has none. */
    refreshText(uri, text)
    {
        const tests = discoverTests(text);
        const id = uri.fsPath;
        if (!tests.length)
        {
            this.controller.items.delete(id);
            return [];
        }
        let file = this.controller.items.get(id);
        if (!file)
        {
            file = this.controller.createTestItem(id, path.basename(uri.fsPath), uri);
            this.controller.items.add(file);
        }
        // Tests outside any module hang from the file; the others from their suite's item.
        const children = [];
        const suites = new Map();
        for (const test of tests)
        {
            const item = this.controller.createTestItem(`${id}::${testKey(test.suite, test.name)}`, test.name, uri);
            item.range = new vscode.Range(test.line, 0, test.line, 0);
            this.keys.set(item.id, testKey(test.suite, test.name));
            if (!test.suite)
            {
                children.push(item);
                continue;
            }
            if (!suites.has(test.suite))
            {
                const suite = this.controller.createTestItem(`${id}::${test.suite}/`, test.suite, uri);
                suite.range = item.range;
                this.keys.set(suite.id, test.suite);
                suites.set(test.suite, { suite, tests: [] });
                children.push(suite);
            }
            suites.get(test.suite).tests.push(item);
        }
        for (const { suite, tests: members } of suites.values())
        {
            suite.children.replace(members);
        }
        file.children.replace(children);
        return tests;
    }

    /** The program argument that runs an item: "suite/name", a suite's name, or a test's name. */
    keyOf(item)
    {
        return this.keys.get(item.id) || item.label;
    }

    /** The tests (leaves) under a file or a suite item. */
    leavesOf(item)
    {
        const leaves = [];
        item.children.forEach((child) =>
        {
            let size = 0;
            child.children.forEach(() => { size += 1; });
            if (size)
            {
                leaves.push(...this.leavesOf(child));
            }
            else
            {
                leaves.push(child);
            }
        });
        return leaves;
    }

    /** The file item a test or suite item belongs to. */
    fileOf(item)
    {
        return this.files().find((file) =>
        {
            let found = false;
            file.children.forEach((child) =>
            {
                found = found || child.id === item.id || Boolean(child.children.get(item.id));
            });
            return found;
        });
    }

    /** The file items of the controller, as an array. */
    files()
    {
        const files = [];
        this.controller.items.forEach((item) => files.push(item));
        return files;
    }

    /** True when a file has tests - for the buttons that run the file in front. */
    hasTests(file)
    {
        return Boolean(file) && Boolean(this.controller.items.get(file));
    }

    // ------------------------------------------------------------------ running

    /**
     * What a request asks for, by file: Map(file item -> [test items] or null for the whole
     * file). No `include` means everything the controller knows.
     */
    selection(request)
    {
        const chosen = new Map();
        const include = request && request.include && request.include.length ? request.include : this.files();
        for (const item of include)
        {
            if (this.controller.items.get(item.id))
            {
                chosen.set(item, null); // a whole file
            }
            else
            {
                const file = this.fileOf(item); // a suite or a single test
                if (file && chosen.get(file) !== null)
                {
                    chosen.set(file, (chosen.get(file) || []).concat(item));
                }
            }
        }
        for (const excluded of ((request && request.exclude) || []))
        {
            chosen.delete(excluded);
        }
        return chosen;
    }

    async run(request, token)
    {
        const run = this.controller.createTestRun(request);
        const toolchain = this.services.toolchain();
        try
        {
            for (const [file, tests] of this.selection(request))
            {
                if (token && token.isCancellationRequested)
                {
                    break;
                }
                // A chosen suite runs all its tests: the program is given the suite's name.
                const items = tests ? tests.flatMap((item) => (this.leavesOf(item).length ? this.leavesOf(item) : [item]))
                    : this.leavesOf(file);
                for (const item of items)
                {
                    run.started(item);
                }
                if (!toolchain)
                {
                    for (const item of items)
                    {
                        run.errored(item, new vscode.TestMessage("The Thunder toolchain was not found (thunder.toolchainPath)."));
                    }
                    continue;
                }
                const outcome = await this.runFile(toolchain, file.uri.fsPath, tests ? tests.map((item) => this.keyOf(item)) : null);
                run.appendOutput(outcome.output.replace(/\r?\n/g, "\r\n"));
                const suites = new Set(this.leavesOf(file).map((item) => this.keyOf(item))
                    .filter((key) => key.includes("/")).map((key) => key.slice(0, key.indexOf("/"))));
                const transcript = parseTranscript(outcome.stdout, suites);
                for (const item of items)
                {
                    const key = this.keyOf(item);
                    if (transcript.failed.has(key))
                    {
                        run.failed(item, transcript.failed.get(key).map((text) => new vscode.TestMessage(text)));
                    }
                    else if (transcript.passed.has(key))
                    {
                        run.passed(item);
                    }
                    else if (outcome.error)
                    {
                        run.errored(item, new vscode.TestMessage(outcome.error));
                    }
                    else
                    {
                        run.skipped(item); // the program ended before it reached this test
                    }
                }
            }
        }
        finally
        {
            run.end();
        }
        return run;
    }

    childrenOf(file)
    {
        const children = [];
        file.children.forEach((item) => children.push(item));
        return children;
    }

    /**
     * Compiles a test file and runs it with the chosen test names as its arguments (all tests
     * when `names` is null). Returns {stdout, output, error}: the transcript, everything the
     * tools printed, and what went wrong when the program could not run to its end.
     */
    async runFile(toolchain, file, names)
    {
        const source = await this.services.sourceFileFor(file); // the unsaved text too
        const language = await this.services.languageCodeFor(file);
        const output = path.join(workDirectory("tests"), `${path.basename(file, path.extname(file))}.thc`);
        const cwd = path.dirname(file);
        const compiled = await toolchain.execute(compileCommand(toolchain.thunderc, {
            source: source.path, language, modulePaths: source.modulePaths, output, quiet: true,
        }), { cwd, timeout: 60000 });
        if (compiled.code !== 0)
        {
            return { stdout: "", output: compiled.stdout + compiled.stderr, error: compiled.stderr.trim() || "the program did not compile" };
        }
        const ran = await toolchain.execute(runCommand(toolchain.thundervm, {
            bytecode: output,
            arguments: names || [],
            nativePaths: this.services.nativePathsFor(vscode.Uri.file(file)),
        }), { cwd, timeout: 120000, logErrors: false });
        // Exit status 1 is the module's own verdict (a test failed): not an error of the run.
        const error = ran.code !== 0 && ran.code !== 1 ? (ran.stderr.trim() || `the program ended with exit code ${ran.code}`) : "";
        return { stdout: ran.stdout, output: ran.stdout + ran.stderr, error };
    }

    /** The Debug profile: the chosen tests of the first file, under the debugger. */
    async debug(request)
    {
        const [first] = this.selection(request);
        if (!first)
        {
            return false;
        }
        const [file, tests] = first;
        const folder = vscode.workspace.getWorkspaceFolder(file.uri);
        return vscode.debug.startDebugging(folder, {
            type: "thunder",
            request: "launch",
            name: `Debug tests: ${file.label}`,
            program: file.uri.fsPath,
            arguments: tests ? tests.map((item) => this.keyOf(item)) : [],
            stopOnEntry: false,
        });
    }

    /** Runs the test declared on a line of a file (the right-to-left editor's play buttons). */
    async runTestAt(uri, line)
    {
        const file = this.controller.items.get(uri.fsPath);
        if (!file)
        {
            return null;
        }
        let found = null;
        for (const item of this.leavesOf(file))
        {
            if (item.range && item.range.start.line === line)
            {
                found = item;
            }
        }
        return found ? this.run(new vscode.TestRunRequest([found])) : null;
    }

    /** Runs every test of one file (the title-bar button and the context menus). */
    async runFileCommand(uri)
    {
        const file = uri ? this.controller.items.get(uri.fsPath) : null;
        if (!file)
        {
            vscode.window.showInformationMessage("This file has no tests: mark a function, a struct or a module with @test.");
            return null;
        }
        return this.run(new vscode.TestRunRequest([file]));
    }

    /** Runs every test file of the workspace. */
    async runAll()
    {
        this.discoverWorkspace();
        if (!this.files().length)
        {
            vscode.window.showInformationMessage("No test files were found: a test file marks its tests with @test.");
            return null;
        }
        return this.run(new vscode.TestRunRequest());
    }
}

module.exports = { ThunderTests, discoverTests, parseTranscript, testKey, displayName };
