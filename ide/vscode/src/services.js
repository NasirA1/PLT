// What every part of the extension needs: the toolchain, the project of a folder, the
// language of a file, the settings and the build log.
//
// Keeping this in one place means a provider never has to work out where thunderc is or which
// language a file is in, and the tests have one object to stand in for.

"use strict";

const fs = require("fs");
const path = require("path");
const vscode = require("vscode");

const project = require("./project");
const { Analyser } = require("./analysis");
const { LanguageData } = require("./language");
const { Toolchain, findToolchain, toolPath, workDirectory } = require("./toolchain");
const { EXE_SUFFIX, languageOf } = require("./util");

class Services
{
    constructor(context)
    {
        this.context = context;
        this.log = vscode.window.createOutputChannel("Thunder");
        this.toolchainInstance = null;
        this.toolchainSearched = [];
        this.projects = new Map();
        this.languages = new Map();      // code -> LanguageData
        this.reportedMissing = false;
        this.analyser = new Analyser({
            toolchain: () => this.toolchain(),
            settings: (file) => this.analysisSettings(file),
            log: (text) => this.write(text),
        });
        this.reload();
    }

    dispose()
    {
        this.log.dispose();
    }

    // ------------------------------------------------------------------ settings and logging

    configuration(uri)
    {
        return vscode.workspace.getConfiguration("thunder", uri || null);
    }

    /** One setting, for the folder a file belongs to. */
    setting(file, key, fallback)
    {
        return this.configuration(file ? vscode.Uri.file(file) : null).get(key, fallback);
    }

    write(text)
    {
        this.log.appendLine(text);
    }

    showBuildLog()
    {
        this.log.show(true);
    }

    // ------------------------------------------------------------------ the toolchain

    /** Finds the tools again (after a setting changed or a build finished). */
    reload()
    {
        const folders = (vscode.workspace.workspaceFolders || []).map((folder) => folder.uri.fsPath);
        const found = findToolchain({
            configured: this.configuration().get("toolchainPath") || "",
            environment: process.env.THUNDER_BIN || "",
            folders,
        });
        this.toolchainSearched = found.searched;
        const directory = found.directory;
        if (!directory)
        {
            this.toolchainInstance = null;
            this.write("thunderc and thundervm were not found. Searched:");
            for (const candidate of found.searched)
            {
                this.write(`  ${candidate}`);
            }
            return null;
        }
        if (!this.toolchainInstance || this.toolchainInstance.directory !== directory)
        {
            this.toolchainInstance = new Toolchain(directory, (text) => this.write(text));
            this.languages.clear();
            this.analyser.clear();
            this.write(`Thunder toolchain: ${directory}`);
        }
        return this.toolchainInstance;
    }

    toolchain()
    {
        return this.toolchainInstance;
    }

    /** True if thunderc and thundervm are both in a directory. */
    probeToolchain(directory)
    {
        return fs.existsSync(toolPath(directory, "thunderc")) && fs.existsSync(toolPath(directory, "thundervm"));
    }

    missingToolchainMessage()
    {
        return [
            "The Thunder toolchain was not found.",
            "",
            "Build it with ./run.sh (Linux) or .\\run.ps1 (Windows) in the Thunder repository, or",
            "  cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release",
            "  cmake --build build/release --config Release",
            "",
            "and then set \"thunder.toolchainPath\" (or the THUNDER_BIN environment variable) to the",
            `directory holding thunderc${EXE_SUFFIX} and thundervm${EXE_SUFFIX}.`,
            "",
            "Searched:",
            ...this.toolchainSearched.map((candidate) => `  ${candidate}`),
        ].join("\n");
    }

    async reportMissingToolchain()
    {
        this.write(this.missingToolchainMessage());
        const choice = await vscode.window.showErrorMessage(
            "The Thunder toolchain (thunderc, thundervm) was not found.",
            "Choose the Directory...", "Build It", "Show Details");
        if (choice === "Choose the Directory...")
        {
            await vscode.commands.executeCommand("thunder.selectToolchain");
        }
        else if (choice === "Build It")
        {
            await vscode.commands.executeCommand("thunder.buildToolchain");
        }
        else if (choice === "Show Details")
        {
            this.showBuildLog();
        }
    }

    /** The directory of the Thunder SDK (the repository with samples/ and docs/), or null. */
    sdkRoot()
    {
        const candidates = [];
        const toolchain = this.toolchain();
        if (toolchain)
        {
            // build/<kind>/bin -> the repository root is three levels up.
            candidates.push(path.resolve(toolchain.directory, "..", "..", ".."));
            candidates.push(path.resolve(toolchain.directory, "..", ".."));
            candidates.push(path.resolve(toolchain.directory, ".."));
        }
        for (const folder of (vscode.workspace.workspaceFolders || []))
        {
            candidates.push(folder.uri.fsPath);
            candidates.push(path.resolve(folder.uri.fsPath, ".."));
        }
        for (const candidate of candidates)
        {
            if (fs.existsSync(path.join(candidate, "samples")) && fs.existsSync(path.join(candidate, "docs")))
            {
                return candidate;
            }
        }
        return null;
    }

    // ------------------------------------------------------------------ languages

    /** The language data of a code, from the toolchain or from the bundled copy. */
    async languageData(code)
    {
        const wanted = code || "en";
        if (this.languages.has(wanted))
        {
            return this.languages.get(wanted);
        }
        let info = null;
        const toolchain = this.toolchain();
        if (toolchain)
        {
            try
            {
                info = await toolchain.languageInfo(wanted);
            }
            catch (error)
            {
                this.write(`language information for "${wanted}" could not be read: ${error.message}`);
            }
        }
        if (!info)
        {
            info = this.bundledLanguageInfo(wanted);
        }
        const data = new LanguageData(info || { code: wanted });
        this.languages.set(wanted, data);
        return data;
    }

    bundledLanguageInfo(code)
    {
        const file = path.join(this.context.extensionPath, "data", `language-info.${code}.json`);
        try
        {
            return JSON.parse(fs.readFileSync(file, "utf8"));
        }
        catch (error)
        {
            return null;
        }
    }

    /** The language data for the language a document is written in. */
    async languageFor(document)
    {
        return this.languageData(this.languageOfText(document.getText(), document.uri.fsPath));
    }

    /** Every language the toolchain knows, as {code, name, native_name}. */
    async languageList()
    {
        const english = await this.languageData("en");
        const languages = english.languages;
        return languages.length ? languages : [{ code: "en", name: "English", native_name: "English" }];
    }

    /** All language data, by code (used to show a word's counterpart in the other language). */
    async allLanguages()
    {
        const map = new Map();
        for (const language of await this.languageList())
        {
            map.set(language.code, await this.languageData(language.code));
        }
        return map;
    }

    /** The language of a program: its own directive, then the project's, then the setting. */
    languageOfText(text, file)
    {
        const uri = file ? vscode.Uri.file(file) : null;
        const folderProject = file ? this.projectForFile(file) : null;
        const configured = (folderProject && folderProject.language && folderProject.language !== "auto")
            ? folderProject.language
            : this.configuration(uri).get("language");
        return languageOf(text, configured);
    }

    async languageCodeFor(file)
    {
        const open = vscode.workspace.textDocuments.find(
            (candidate) => path.resolve(candidate.uri.fsPath) === path.resolve(file));
        const text = open ? open.getText() : readFirstLines(file);
        return this.languageOfText(text, file);
    }

    /** The language the reference and the samples are offered in. */
    async interfaceLanguage()
    {
        const configured = this.configuration().get("language");
        if (configured && configured !== "auto")
        {
            return configured;
        }
        const editor = vscode.window.activeTextEditor;
        if (editor && editor.document.languageId === "thunder")
        {
            return this.languageOfText(editor.document.getText(), editor.document.uri.fsPath);
        }
        return "en";
    }

    // ------------------------------------------------------------------ projects

    /** The project of a workspace folder (read from thunder.json, cached until it changes). */
    projectFor(folderUri)
    {
        const folder = folderUri.fsPath;
        if (this.projects.has(folder))
        {
            return this.projects.get(folder);
        }
        const file = path.join(folder, project.PROJECT_FILE);
        let text = null;
        try
        {
            text = fs.readFileSync(file, "utf8");
        }
        catch (error)
        {
            text = null;
        }
        const parsed = project.parseProject(text, folder);
        if (parsed.error)
        {
            this.write(`${file}: ${parsed.error}`);
            vscode.window.showWarningMessage(`Thunder: ${parsed.error}`);
        }
        this.projects.set(folder, parsed.project);
        return parsed.project;
    }

    projectForFile(file)
    {
        const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
        return folder ? this.projectFor(folder.uri) : project.parseProject(null, path.dirname(file)).project;
    }

    forgetProjects()
    {
        this.projects.clear();
    }

    modulePathsFor(uri)
    {
        const file = uri.fsPath;
        return project.modulePathsFor(file, this.projectForFile(file),
            this.configuration(uri).get("modulePaths") || []);
    }

    nativePathsFor(uri)
    {
        const file = uri.fsPath;
        const folderProject = this.projectForFile(file);
        const configured = (this.configuration(uri).get("nativePaths") || []).map((directory) =>
            (path.isAbsolute(directory) ? directory : path.resolve(folderProject.folder || path.dirname(file), directory)));
        return (folderProject.nativePaths || []).concat(configured);
    }

    modulePathsForFile(file)
    {
        return this.modulePathsFor(vscode.Uri.file(file));
    }

    nativePathsForFile(file)
    {
        return this.nativePathsFor(vscode.Uri.file(file));
    }

    analysisSettings(file)
    {
        const uri = vscode.Uri.file(file);
        const open = vscode.workspace.textDocuments.find(
            (candidate) => path.resolve(candidate.uri.fsPath) === path.resolve(file));
        return {
            language: this.languageOfText(open ? open.getText() : readFirstLines(file), file),
            modulePaths: this.modulePathsFor(uri),
        };
    }

    /** Compiles a document and returns its model. */
    analyse(document)
    {
        return this.analyser.analyse(document.uri.fsPath, document.getText(), {
            version: `${document.version}:${document.isDirty}`,
            dirty: document.isDirty || document.uri.scheme !== "file",
        });
    }

    /** The file to compile for a document, and the module paths that go with it. */
    async sourceFileFor(file)
    {
        const open = vscode.workspace.textDocuments.find(
            (candidate) => path.resolve(candidate.uri.fsPath) === path.resolve(file));
        const modulePaths = this.modulePathsFor(vscode.Uri.file(file));
        if (!open || !open.isDirty)
        {
            return { path: file, modulePaths };
        }
        const directory = workDirectory("artefacts");
        const copy = path.join(directory, path.basename(file));
        fs.writeFileSync(copy, open.getText(), "utf8");
        return { path: copy, modulePaths: [path.dirname(file)].concat(modulePaths) };
    }

    // ------------------------------------------------------------------ the active program

    /** The Thunder file in front of the user, if there is one. */
    activeProgram()
    {
        const editor = vscode.window.activeTextEditor;
        if (editor && editor.document.languageId === "thunder" && editor.document.uri.scheme === "file")
        {
            return editor.document.uri.fsPath;
        }
        for (const document of vscode.window.visibleTextEditors)
        {
            if (document.document.languageId === "thunder" && document.document.uri.scheme === "file")
            {
                return document.document.uri.fsPath;
            }
        }
        return null;
    }

    /** The program a command works on: an explicit one, the active editor, or the entry point. */
    programFor(resource)
    {
        if (resource && resource.fsPath && resource.fsPath.endsWith(".th"))
        {
            return resource.fsPath;
        }
        if (resource && resource.resourceUri && String(resource.resourceUri.fsPath).endsWith(".th"))
        {
            return resource.resourceUri.fsPath;
        }
        const active = this.activeProgram();
        if (active)
        {
            return active;
        }
        const folder = (vscode.workspace.workspaceFolders || [])[0];
        return folder ? (this.projectFor(folder.uri).entry || null) : null;
    }

    resolveVariables(value, folder)
    {
        const root = folder ? folder.uri.fsPath : ((vscode.workspace.workspaceFolders || [])[0] || {}).uri;
        const base = typeof root === "string" ? root : (root ? root.fsPath : "");
        return String(value)
            .replace(/\$\{workspaceFolder\}/g, base)
            .replace(/\$\{file\}/g, this.activeProgram() || "")
            .replace(/\$\{fileBasename\}/g, path.basename(this.activeProgram() || ""));
    }

    // ------------------------------------------------------------------ templates

    templates(language)
    {
        const directory = path.join(this.context.extensionPath, "templates", language === "ps" ? "ps" : "en");
        return {
            main: fs.readFileSync(path.join(directory, "main.th"), "utf8"),
            module: fs.readFileSync(path.join(directory, "lib", "greeting.th"), "utf8"),
            moduleName: "greeting",
        };
    }

    projectSchema()
    {
        return fs.readFileSync(
            path.join(this.context.extensionPath, "schemas", "thunder-project.schema.json"), "utf8");
    }
}

function readFirstLines(file)
{
    try
    {
        return fs.readFileSync(file, "utf8").split("\n").slice(0, 3).join("\n");
    }
    catch (error)
    {
        return "";
    }
}

module.exports = { Services };
