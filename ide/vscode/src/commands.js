// What the Thunder commands do.
//
// Building and running go through VS Code tasks, so the real command line is shown in a real
// terminal: the program's output arrives as it is written and `input()` reads what the user
// types, exactly as it would from a shell. Everything else - compiling to fill the Problems
// panel, translating, packaging, setting a project up - is done directly, and every command
// that starts a tool writes it to the build log first.

"use strict";

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const { PROJECT_FILE, outputFor, packageFor, projectTemplate } = require("./project");
const { runCommand, workDirectory } = require("./toolchain");
const { showArtefact } = require("./artefacts");
const { TASK_TYPE } = require("./tasks");
const { documentsIn } = require("./views");
const { listThunderFiles } = require("./providers/completion");

/** Registers every command and returns the disposables. */
function registerCommands(context, services)
{
    const register = (name, callback) =>
    {
        context.subscriptions.push(vscode.commands.registerCommand(name, callback));
    };

    register("thunder.compile", () => compile(services));
    register("thunder.run", (resource) => runTask(services, "run", resource));
    register("thunder.runWithArguments", () => runWithArguments(services));
    register("thunder.debug", (resource) => debug(services, resource));
    register("thunder.stop", () => stop());
    register("thunder.package", (resource) => packageApplication(services, resource));
    register("thunder.showRightToLeft", () => showRightToLeft(services));
    register("thunder.saveSvg", (resource) => saveSvg(services, resource));
    register("thunder.translate", () => translate(services));
    register("thunder.showBuildLog", () => services.showBuildLog());
    register("thunder.showArtefact", (stage, file) => showArtefact(stage, file || services.activeProgram()));
    register("thunder.showPipeline", () => vscode.commands.executeCommand("thunderPipeline.focus"));
    register("thunder.newProject", () => newProject(services));
    register("thunder.selectToolchain", () => selectToolchain(services));
    register("thunder.buildToolchain", () => buildToolchain(services));
    register("thunder.openReference", () => openReference(services));
    register("thunder.openSample", () => openSample(services));
    register("thunder.setEntryPoint", (resource) => setEntryPoint(services, resource));
    register("thunder.refreshViews", () => services.refreshViews());
    // One command per artefact. The command names read better than the compiler's stage names
    // ("--emit asm" is "Show Bytecode"), so the mapping is written out.
    const artefactCommands = {
        "thunder.showTokens": "tokens",
        "thunder.showAst": "ast",
        "thunder.showSymbols": "symbols",
        "thunder.showBytecode": "asm",
        "thunder.showHex": "hex",
    };
    for (const [name, stage] of Object.entries(artefactCommands))
    {
        register(name, () => showArtefact(stage, services.activeProgram()));
    }
}

// ---------------------------------------------------------------------- compiling and running

async function compile(services)
{
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== "thunder")
    {
        vscode.window.showInformationMessage("Open a Thunder program first.");
        return null;
    }
    const toolchain = services.toolchain();
    if (!toolchain)
    {
        await services.reportMissingToolchain();
        return null;
    }
    await editor.document.save();
    const file = editor.document.uri.fsPath;
    const project = services.projectForFile(file);
    const output = outputFor(file, project);
    fs.mkdirSync(path.dirname(output), { recursive: true });
    const outcome = await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Window, title: `Compiling ${path.basename(file)}...` },
        () => toolchain.compile(file, {
            language: services.languageOfText(editor.document.getText(), file),
            modulePaths: services.modulePathsFor(editor.document.uri),
            output,
            cwd: path.dirname(file),
        }));
    const model = await services.diagnostics.check(editor.document);
    services.refreshArtefacts();
    if (outcome.report.success)
    {
        vscode.window.setStatusBarMessage(
            `Thunder: ${path.basename(file)} compiled to ${path.basename(output)}`, 5000);
    }
    else
    {
        const errors = outcome.report.error_count || 0;
        const choice = await vscode.window.showErrorMessage(
            `${path.basename(file)}: ${errors} error(s).`, "Show Problems");
        if (choice)
        {
            vscode.commands.executeCommand("workbench.actions.view.problems");
        }
    }
    return model;
}

/** Runs one of the Thunder tasks on a file. */
async function runTask(services, kind, resource)
{
    const file = services.programFor(resource);
    if (!file)
    {
        vscode.window.showInformationMessage("Open a Thunder program, or set an entry point in thunder.json.");
        return;
    }
    if (!services.toolchain())
    {
        await services.reportMissingToolchain();
        return;
    }
    await saveIfOpen(file);
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
    const task = await services.tasks.createTask({ type: TASK_TYPE, task: kind, file }, folder);
    if (!task)
    {
        return;
    }
    await vscode.tasks.executeTask(task);
}

async function runWithArguments(services)
{
    const file = services.programFor();
    if (!file)
    {
        vscode.window.showInformationMessage("Open a Thunder program first.");
        return;
    }
    const previous = (services.projectForFile(file) || {}).arguments || [];
    const answer = await vscode.window.showInputBox({
        title: `Arguments for ${path.basename(file)}`,
        prompt: "Passed to the program; args() in module os returns them. Separate them with spaces.",
        value: previous.join(" "),
    });
    if (answer === undefined)
    {
        return;
    }
    await saveIfOpen(file);
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
    const task = await services.tasks.createTask(
        { type: TASK_TYPE, task: "run", file, arguments: splitArguments(answer) }, folder);
    if (task)
    {
        await vscode.tasks.executeTask(task);
    }
}

/** Splits a command line the way a shell would, respecting quotes. */
function splitArguments(text)
{
    const result = [];
    const pattern = /"([^"]*)"|'([^']*)'|(\S+)/g;
    let match = pattern.exec(text || "");
    while (match)
    {
        result.push(match[1] !== undefined ? match[1] : (match[2] !== undefined ? match[2] : match[3]));
        match = pattern.exec(text || "");
    }
    return result;
}

function stop()
{
    for (const execution of vscode.tasks.taskExecutions)
    {
        if (execution.task.definition && execution.task.definition.type === TASK_TYPE)
        {
            execution.terminate();
        }
    }
    return vscode.commands.executeCommand("workbench.action.debug.stop");
}

async function debug(services, resource)
{
    const file = services.programFor(resource);
    if (!file)
    {
        vscode.window.showInformationMessage("Open a Thunder program first.");
        return;
    }
    await saveIfOpen(file);
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
    await vscode.debug.startDebugging(folder, {
        type: "thunder",
        request: "launch",
        name: `Debug ${path.basename(file)}`,
        program: file,
        stopOnEntry: true,
    });
}

async function packageApplication(services, resource)
{
    const file = services.programFor(resource);
    const toolchain = services.toolchain();
    if (!file || !toolchain)
    {
        await services.reportMissingToolchain();
        return;
    }
    await saveIfOpen(file);
    const project = services.projectForFile(file);
    const configuration = services.configuration(vscode.Uri.file(file));
    const suggested = packageFor(file, project, configuration.get("packageOutputDirectory"));
    const target = await vscode.window.showSaveDialog({
        title: "Package as an application",
        defaultUri: vscode.Uri.file(suggested),
        saveLabel: "Package",
    });
    if (!target)
    {
        return;
    }
    const outcome = await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Notification, title: `Packaging ${path.basename(file)}...` },
        () => toolchain.package(file, target.fsPath, {
            language: services.languageOfText(readFile(file), file),
            modulePaths: services.modulePathsFor(vscode.Uri.file(file)),
            display: configuration.get("display"),
            cwd: path.dirname(file),
        }));
    if (outcome.ok)
    {
        const choice = await vscode.window.showInformationMessage(
            `Wrote ${path.basename(outcome.output)}.`, "Show in Folder", "Show Build Log");
        if (choice === "Show in Folder")
        {
            vscode.commands.executeCommand("revealFileInOS", vscode.Uri.file(outcome.output));
        }
        else if (choice === "Show Build Log")
        {
            services.showBuildLog();
        }
    }
    else
    {
        vscode.window.showErrorMessage(`Packaging failed: ${outcome.message}`);
        services.showBuildLog();
    }
}

/**
 * Shows the program the way it is meant to be read.
 *
 * For a Pashto program that means right to left, token by token, as the Thunder IDE shows it;
 * VS Code's own editor cannot be made to do this (see the extension's README).
 */
async function showRightToLeft(services)
{
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== "thunder")
    {
        vscode.window.showInformationMessage("Open a Thunder program first.");
        return;
    }
    await services.rightToLeftView.show(editor.document, { reveal: true, preserveFocus: true });
}

/**
 * Runs a graphics program and saves the picture it drew (`thundervm --svg`).
 *
 * VS Code has no canvas of its own, so this is how a drawing is seen without a display
 * plug-in: the program runs to its end and the final picture is written as an SVG, which VS
 * Code opens in its own image preview.
 */
async function saveSvg(services, resource)
{
    const file = services.programFor(resource);
    const toolchain = services.toolchain();
    if (!file || !toolchain)
    {
        await services.reportMissingToolchain();
        return;
    }
    await saveIfOpen(file);
    const target = await vscode.window.showSaveDialog({
        title: "Save the picture",
        defaultUri: vscode.Uri.file(path.join(path.dirname(file), `${path.basename(file, path.extname(file))}.svg`)),
        filters: { "SVG image": ["svg"] },
        saveLabel: "Run and Save",
    });
    if (!target)
    {
        return;
    }
    const bytecode = path.join(workDirectory("svg"), `${path.basename(file, path.extname(file))}.thc`);
    const compiled = await toolchain.compile(file, {
        language: services.languageOfText(readFile(file), file),
        modulePaths: services.modulePathsFor(vscode.Uri.file(file)),
        output: bytecode,
        cwd: path.dirname(file),
    });
    if (!compiled.report.success)
    {
        vscode.window.showErrorMessage(
            `${path.basename(file)}: ${compiled.report.error_count || 0} error(s); nothing was drawn.`);
        return;
    }
    const configuration = services.configuration(vscode.Uri.file(file));
    const command = runCommand(toolchain.thundervm, {
        bytecode,
        svg: target.fsPath,
        maxSteps: configuration.get("maxSteps"),
        nativePaths: services.nativePathsFor(vscode.Uri.file(file)),
        arguments: (services.projectForFile(file) || {}).arguments || [],
    });
    const result = await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Notification, title: `Running ${path.basename(file)}...` },
        () => toolchain.execute(command, { cwd: path.dirname(file), timeout: 300000 }));
    if (result.code !== 0)
    {
        vscode.window.showErrorMessage(`The program stopped with code ${result.code}: ${result.stderr.trim()}`);
        services.showBuildLog();
        return;
    }
    const open = await vscode.window.showInformationMessage(
        `Wrote ${path.basename(target.fsPath)}.`, "Open It");
    if (open)
    {
        await vscode.commands.executeCommand("vscode.open", target);
    }
}

// ---------------------------------------------------------------------- translating

async function translate(services)
{
    const editor = vscode.window.activeTextEditor;
    const toolchain = services.toolchain();
    if (!editor || editor.document.languageId !== "thunder" || !toolchain)
    {
        await services.reportMissingToolchain();
        return;
    }
    const current = services.languageOfText(editor.document.getText(), editor.document.uri.fsPath);
    const languages = await services.languageList();
    const others = languages.filter((language) => language.code !== current);
    if (!others.length)
    {
        return;
    }
    const target = others.length === 1
        ? others[0]
        : await vscode.window.showQuickPick(
            others.map((language) => ({ label: language.native_name, description: language.code, language })),
            { title: "Translate the program into" }).then((picked) => (picked ? picked.language : null));
    if (!target)
    {
        return;
    }
    await editor.document.save();
    const outcome = await toolchain.translate(editor.document.uri.fsPath, {
        language: current,
        to: target.code,
        modulePaths: services.modulePathsFor(editor.document.uri),
        cwd: path.dirname(editor.document.uri.fsPath),
    });
    if (!outcome.ok)
    {
        vscode.window.showErrorMessage(`Translation failed: ${outcome.error}`);
        return;
    }
    const translated = await vscode.workspace.openTextDocument({ content: outcome.text, language: "thunder" });
    await vscode.window.showTextDocument(translated, { viewColumn: vscode.ViewColumn.Beside, preview: false });
    const choice = await vscode.window.showInformationMessage(
        `Translated into ${target.native_name}. The program itself has not been changed.`,
        "Replace the Original", "Keep Both");
    if (choice === "Replace the Original")
    {
        const edit = new vscode.WorkspaceEdit();
        const whole = new vscode.Range(
            editor.document.positionAt(0),
            editor.document.positionAt(editor.document.getText().length));
        edit.replace(editor.document.uri, whole, outcome.text);
        await vscode.workspace.applyEdit(edit);
        await editor.document.save();
    }
}

// ---------------------------------------------------------------------- the project

async function newProject(services)
{
    const folders = vscode.workspace.workspaceFolders || [];
    let target = null;
    if (folders.length === 1)
    {
        target = folders[0].uri;
    }
    else if (folders.length > 1)
    {
        const picked = await vscode.window.showQuickPick(
            folders.map((folder) => ({ label: folder.name, description: folder.uri.fsPath, folder })),
            { title: "Which folder should hold the project?" });
        target = picked ? picked.folder.uri : null;
    }
    else
    {
        const picked = await vscode.window.showOpenDialog({
            canSelectFolders: true, canSelectFiles: false, title: "Choose a folder for the Thunder project",
        });
        target = picked && picked.length ? picked[0] : null;
    }
    if (!target)
    {
        return;
    }
    const name = await vscode.window.showInputBox({
        title: "Project name",
        value: path.basename(target.fsPath),
        validateInput: (value) => (value.trim() ? null : "The project needs a name."),
    });
    if (!name)
    {
        return;
    }
    const languages = await services.languageList();
    const picked = await vscode.window.showQuickPick(
        languages.map((language) => ({
            label: language.native_name,
            description: language.code === "en" ? "English keywords" : "right-to-left, Pashto keywords",
            language,
        })),
        { title: "Which language are the programs written in?" });
    if (!picked)
    {
        return;
    }
    await writeProject(services, target.fsPath, name.trim(), picked.language.code);
    services.refreshViews();
    const main = vscode.Uri.file(path.join(target.fsPath, "main.th"));
    const document = await vscode.workspace.openTextDocument(main);
    await vscode.window.showTextDocument(document);
    vscode.window.showInformationMessage(
        `Thunder project "${name}" is ready. Press F5 to debug it, Ctrl+F5 to run it.`);
}

/** Writes a project: thunder.json, a program, a module and the VS Code task and launch files. */
async function writeProject(services, folder, name, language)
{
    const templates = services.templates(language);
    const write = (relative, contents) =>
    {
        const file = path.join(folder, relative);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        if (!fs.existsSync(file))
        {
            fs.writeFileSync(file, contents, "utf8");
        }
    };
    write(PROJECT_FILE, JSON.stringify(projectTemplate(name, language, "main.th"), null, 2) + "\n");
    write("main.th", templates.main);
    write(path.join("lib", templates.moduleName + ".th"), templates.module);
    write(path.join(".vscode", "tasks.json"), JSON.stringify({
        version: "2.0.0",
        tasks: [
            { type: "thunder", task: "build", problemMatcher: ["$thunderc"], group: { kind: "build", isDefault: true },
                label: "Thunder: build" },
            { type: "thunder", task: "run", problemMatcher: ["$thunderc"], label: "Thunder: run" },
            { type: "thunder", task: "package", problemMatcher: ["$thunderc"], label: "Thunder: package" },
        ],
    }, null, 2) + "\n");
    write(path.join(".vscode", "launch.json"), JSON.stringify({
        version: "0.2.0",
        configurations: [
            {
                type: "thunder",
                request: "launch",
                name: "Thunder: debug the project",
                program: "${workspaceFolder}/main.th",
                stopOnEntry: true,
            },
            {
                type: "thunder",
                request: "launch",
                name: "Thunder: debug the current file",
                program: "${file}",
                stopOnEntry: true,
            },
        ],
    }, null, 2) + "\n");
    write(path.join(".vscode", "thunder-project.schema.json"), services.projectSchema());
}

async function setEntryPoint(services, resource)
{
    const file = services.programFor(resource);
    if (!file)
    {
        return;
    }
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
    if (!folder)
    {
        vscode.window.showInformationMessage("Open the folder as a workspace first, so the project can be saved.");
        return;
    }
    const projectFile = path.join(folder.uri.fsPath, PROJECT_FILE);
    let data = {};
    if (fs.existsSync(projectFile))
    {
        try
        {
            data = JSON.parse(fs.readFileSync(projectFile, "utf8"));
        }
        catch (error)
        {
            vscode.window.showErrorMessage(`${PROJECT_FILE} is not valid JSON: ${error.message}`);
            return;
        }
    }
    data.entry = path.relative(folder.uri.fsPath, file).replace(/\\/g, "/");
    fs.writeFileSync(projectFile, JSON.stringify(data, null, 2) + "\n", "utf8");
    services.refreshViews();
    vscode.window.setStatusBarMessage(`Thunder: ${data.entry} is now the entry point`, 4000);
}

async function selectToolchain(services)
{
    const picked = await vscode.window.showOpenDialog({
        canSelectFolders: true,
        canSelectFiles: false,
        title: "The directory containing thunderc and thundervm (usually build/release/bin)",
    });
    if (!picked || !picked.length)
    {
        return;
    }
    const directory = picked[0].fsPath;
    const found = services.probeToolchain(directory);
    if (!found)
    {
        vscode.window.showErrorMessage(`thunderc and thundervm are not in ${directory}.`);
        return;
    }
    await vscode.workspace.getConfiguration("thunder").update(
        "toolchainPath", directory, vscode.ConfigurationTarget.Global);
    services.reload();
    vscode.window.showInformationMessage(`Thunder toolchain: ${directory}`);
}

async function buildToolchain(services)
{
    const root = services.sdkRoot() || (vscode.workspace.workspaceFolders || [])[0];
    const directory = typeof root === "string" ? root : (root ? root.uri.fsPath : null);
    if (!directory || !fs.existsSync(path.join(directory, "CMakeLists.txt")))
    {
        vscode.window.showErrorMessage(
            "The Thunder sources were not found. Open the Thunder repository as a folder, then run this again.");
        return;
    }
    const terminal = vscode.window.createTerminal({ name: "Thunder build", cwd: directory });
    terminal.show();
    terminal.sendText("cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release");
    terminal.sendText("cmake --build build/release --config Release");
}

// ---------------------------------------------------------------------- the SDK

async function openReference(services)
{
    const root = services.sdkRoot();
    if (!root)
    {
        vscode.window.showInformationMessage(
            "The reference lives in the Thunder repository's docs/ directory, which was not found.");
        return;
    }
    const language = await services.interfaceLanguage();
    const directory = language === "en" ? path.join(root, "docs") : path.join(root, "docs", language);
    const documents = documentsIn(fs.existsSync(directory) ? directory : path.join(root, "docs"));
    const picked = await vscode.window.showQuickPick(
        documents.map((file) => ({ label: path.basename(file, ".md"), description: file, file })),
        { title: "SDK reference" });
    if (picked)
    {
        await vscode.commands.executeCommand("markdown.showPreview", vscode.Uri.file(picked.file));
    }
}

async function openSample(services)
{
    const root = services.sdkRoot();
    if (!root)
    {
        vscode.window.showInformationMessage("The samples live in the Thunder repository's samples/ directory.");
        return;
    }
    const language = await services.interfaceLanguage();
    const directory = path.join(root, "samples", fs.existsSync(path.join(root, "samples", language)) ? language : "en");
    const samples = listThunderFiles(directory, 0).sort();
    const picked = await vscode.window.showQuickPick(
        samples.map((file) => ({ label: path.basename(file), description: path.relative(root, file), file })),
        { title: "Sample programs (in teaching order)" });
    if (!picked)
    {
        return;
    }
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(picked.file));
    await vscode.window.showTextDocument(document);
}

// ---------------------------------------------------------------------- helpers

async function saveIfOpen(file)
{
    const document = vscode.workspace.textDocuments.find(
        (candidate) => path.resolve(candidate.uri.fsPath) === path.resolve(file));
    if (document && document.isDirty)
    {
        await document.save();
    }
}

function readFile(file)
{
    try
    {
        return fs.readFileSync(file, "utf8");
    }
    catch (error)
    {
        return "";
    }
}

module.exports = { registerCommands, splitArguments };
