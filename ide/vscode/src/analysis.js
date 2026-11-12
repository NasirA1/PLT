// Keeping a compiled picture of every open Thunder file.
//
// Every language feature - diagnostics, completion, hovers, go to definition, the outline,
// semantic highlighting - is answered from `thunderc --json`. This module runs the compiler,
// caches the report per file and version, and compiles the modules a program imports so that a
// name from another file can be found too.
//
// A file that has unsaved changes is written to a scratch copy with the same name and compiled
// with `-I <its real directory>`, so `import "lib/x"` still resolves - the same trick the Qt
// IDE uses (ide/thunder_ide/core/toolchain.py).

"use strict";

const fs = require("fs");
const path = require("path");

const { Model } = require("./report");
const { workDirectory } = require("./toolchain");

class Analyser
{
    /**
     * @param options {toolchain: () => Toolchain|null, settings: () => ({language, modulePaths}),
     *                 log: (text) => void}
     */
    constructor(options)
    {
        const settings = options || {};
        this.getToolchain = settings.toolchain || (() => null);
        this.getSettings = settings.settings || (() => ({}));
        this.log = settings.log || (() => {});
        this.cache = new Map();        // file -> {version, model, text}
        this.good = new Map();         // file -> the last model that compiled without errors
        this.pending = new Map();      // file -> Promise
        this.listeners = [];
        this.work = settings.workDirectory || null;
    }

    onDidAnalyse(listener)
    {
        this.listeners.push(listener);
        return { dispose: () => { this.listeners = this.listeners.filter((entry) => entry !== listener); } };
    }

    _announce(file, model)
    {
        for (const listener of this.listeners)
        {
            listener(file, model);
        }
    }

    /** The newest model of a file, without compiling anything. */
    modelFor(file)
    {
        const entry = this.cache.get(path.resolve(file));
        return entry ? entry.model : null;
    }

    /**
     * The last model of a file that compiled without errors.
     *
     * While a line is being typed (`r.`, `println(`) the program does not parse, so the current
     * report has no syntax tree. Completion and signature help fall back on the last good one:
     * the declarations in it are still the program's declarations.
     */
    goodModelFor(file)
    {
        return this.good.get(path.resolve(file)) || this.modelFor(file);
    }

    invalidate(file)
    {
        this.cache.delete(path.resolve(file));
    }

    clear()
    {
        this.cache.clear();
        this.good.clear();
    }

    /**
     * Compiles a file and returns its model (or null when the toolchain is missing).
     *
     * `version` identifies the contents: when it has not changed since the last compile, the
     * cached model is returned, so twenty completion requests cost one compile.
     */
    async analyse(file, text, options)
    {
        const settings = options || {};
        const key = path.resolve(file);
        const cached = this.cache.get(key);
        if (cached && cached.version === settings.version && !settings.force)
        {
            return cached.model;
        }
        if (this.pending.has(key) && !settings.force)
        {
            return this.pending.get(key);
        }
        const promise = this._compile(key, text, settings).finally(() => this.pending.delete(key));
        this.pending.set(key, promise);
        return promise;
    }

    async _compile(file, text, settings)
    {
        const toolchain = this.getToolchain();
        if (!toolchain)
        {
            return null;
        }
        const configuration = this.getSettings(file) || {};
        const source = this._sourceFor(file, text, settings);
        let outcome = null;
        try
        {
            outcome = await toolchain.compile(source.path, {
                language: configuration.language,
                modulePaths: source.modulePaths,
                noOutput: true,
                cwd: path.dirname(file),
                cancellation: settings.cancellation,
            });
        }
        catch (error)
        {
            this.log(`analysis of ${path.basename(file)} failed: ${error.message}`);
            return this.modelFor(file);
        }
        const modules = settings.withModules === false
            ? new Map()
            : await this._analyseModules(outcome.report, file, settings);
        const model = new Model(outcome.report, text, { file, modules });
        this.cache.set(file, { version: settings.version, model, text });
        if (model.succeeded && model.ast)
        {
            this.good.set(file, model);
        }
        this._announce(file, model);
        return model;
    }

    /**
     * The file handed to the compiler: the file itself when the editor's text is what is on
     * disk, otherwise a copy in a scratch directory under the same name.
     */
    _sourceFor(file, text, settings)
    {
        const configuration = this.getSettings(file) || {};
        const modulePaths = (configuration.modulePaths || []).slice();
        if (!settings.dirty)
        {
            return { path: file, modulePaths };
        }
        const directory = this.work || workDirectory("analysis");
        const copy = path.join(directory, path.basename(file));
        fs.writeFileSync(copy, text, "utf8");
        // The real directory first: that is where "import" looks from the file's point of view.
        modulePaths.unshift(path.dirname(file));
        return { path: copy, modulePaths };
    }

    /** Compiles the files a program imports, so their structs and functions can be looked up. */
    async _analyseModules(report, file, settings)
    {
        const modules = new Map();
        for (const module of (report.modules || []))
        {
            const resolved = path.isAbsolute(module.path)
                ? module.path
                : path.resolve(path.dirname(file), module.path);
            if (modules.has(resolved) || resolved === file)
            {
                continue;
            }
            let text = "";
            try
            {
                text = fs.readFileSync(resolved, "utf8");
            }
            catch (error)
            {
                continue; // a module that cannot be read is already a compile error
            }
            const model = await this.analyse(resolved, text, {
                version: `module:${text.length}:${fileTime(resolved)}`,
                dirty: false,
                withModules: false,
                cancellation: settings.cancellation,
            });
            if (model)
            {
                modules.set(resolved, model);
            }
        }
        return modules;
    }
}

function fileTime(file)
{
    try
    {
        return fs.statSync(file).mtimeMs;
    }
    catch (error)
    {
        return 0;
    }
}

module.exports = { Analyser };
