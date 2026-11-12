// The compiler's report, read as a model of the program.
//
// `thunderc --json` describes everything each phase produced: the token stream with its spans,
// the syntax tree with the type of every expression, the scopes with their symbols (and which
// file each came from), the bytecode, and the diagnostics. The extension derives all of its
// language intelligence from that report, so what the editor says and what the compiler does
// can never disagree.
//
// Nothing here uses the VS Code API: ranges are plain {start:{line,character},end:{...}}
// objects with 0-based lines. See test/report.test.js.

"use strict";

const path = require("path");

const { rangeContains, spanToRange, splitLines } = require("./util");

/** AST nodes that declare something a reader would look for in an outline. */
const DECLARATION_KINDS = new Set(["Struct", "Function", "NativeFunction", "Var", "Const", "Field", "Param"]);

class Model
{
    /**
     * @param report the parsed `thunderc --json` output
     * @param text   the source the report was produced from
     * @param options {file: the program's path, modules: Map<path, Model> of imported files}
     */
    constructor(report, text, options)
    {
        const settings = options || {};
        this.report = report || {};
        this.text = text || "";
        this.lines = splitLines(this.text);
        this.file = settings.file || this.report.file || "";
        this.modules = settings.modules || new Map();
        this._symbolsFlat = null;
        this._declarations = null;
        this._declarationNodes = null;
    }

    get languageCode()
    {
        return (this.report.language || {}).code || "en";
    }

    get succeeded()
    {
        return Boolean(this.report.success);
    }

    get rightToLeft()
    {
        return Boolean((this.report.language || {}).right_to_left);
    }

    /** The files this program imports: [{file: index, name, path}]. */
    get importedFiles()
    {
        return (this.report.modules || []).map((module) => ({
            index: module.file,
            name: module.name,
            path: module.path,
        }));
    }

    /** The absolute path of a file index used by symbols and diagnostics (0 = this program). */
    pathOfFileIndex(index)
    {
        if (!index)
        {
            return this.file;
        }
        const module = (this.report.modules || []).find((entry) => entry.file === index);
        if (!module)
        {
            return this.file;
        }
        return path.isAbsolute(module.path) ? module.path : path.resolve(path.dirname(this.file), module.path);
    }

    /** The built-in modules the program imports, by their canonical name ("os", "graphics"). */
    importedModules(languageInfo)
    {
        const names = new Set();
        const translation = new Map();
        for (const module of ((languageInfo || {}).modules || []))
        {
            translation.set(module.name, module.canonical_name);
            translation.set(module.canonical_name, module.canonical_name);
        }
        for (const node of this.topLevel())
        {
            if (node.kind === "Import" && !/^["«“]/.test(node.label))
            {
                names.add(translation.get(node.label) || node.label);
            }
        }
        return names;
    }

    // ------------------------------------------------------------------ diagnostics

    /**
     * Every problem the compiler reported, as ranges in the file it belongs to.
     *
     * A program that imports modules gets diagnostics from several files; each one carries the
     * file it is in, so the caller can show it in the right editor. `sourceOf(file)` returns
     * the text of an imported file when it is known, so the range can be measured in it.
     */
    diagnostics(sourceOf)
    {
        const result = [];
        for (const diagnostic of (this.report.diagnostics || []))
        {
            const file = this._fileOfDiagnostic(diagnostic);
            const lines = file === this.file || !file
                ? this.lines
                : splitLines((sourceOf ? sourceOf(file) : "") || "");
            result.push({
                range: spanToRange(diagnostic, lines.length ? lines : [""]),
                severity: diagnostic.severity === "warning" ? "warning" : "error",
                code: diagnostic.code,
                phase: diagnostic.phase,
                message: diagnostic.message,
                file: file || this.file,
                text: diagnostic.text,
            });
        }
        if (this.report.internal_error)
        {
            result.push({
                range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
                severity: "error",
                code: "ICE",
                phase: "internal",
                message: `internal compiler error: ${this.report.internal_error}`,
                file: this.file,
                text: this.report.internal_error,
            });
        }
        return result;
    }

    _fileOfDiagnostic(diagnostic)
    {
        const name = diagnostic.file_name || "";
        if (!name)
        {
            return this.file;
        }
        if (path.basename(name) === path.basename(this.file))
        {
            return this.file;
        }
        const module = (this.report.modules || []).find(
            (entry) => entry.path === name || entry.name === name || path.basename(entry.path) === path.basename(name));
        if (module)
        {
            return this.pathOfFileIndex(module.file);
        }
        return path.isAbsolute(name) ? name : path.resolve(path.dirname(this.file), name);
    }

    // ------------------------------------------------------------------ tokens

    /** The token stream, with a plain range on each token. */
    tokens()
    {
        return (this.report.tokens || []).map((token) => ({
            kind: token.kind,
            category: token.category,
            lexeme: token.lexeme,
            range: spanToRange(token, this.lines),
        }));
    }

    /** The token at a position, or null (identifiers, keywords, numbers - never whitespace). */
    tokenAt(position)
    {
        for (const token of this.tokens())
        {
            if (rangeContains(token.range, position))
            {
                return token;
            }
        }
        return null;
    }

    // ------------------------------------------------------------------ the syntax tree

    get ast()
    {
        return this.report.ast || null;
    }

    topLevel()
    {
        return this.ast ? (this.ast.children || []) : [];
    }

    /** Every node of the tree, parents before children. */
    walk(visit, node)
    {
        const start = node || this.ast;
        if (!start)
        {
            return;
        }
        visit(start);
        for (const child of (start.children || []))
        {
            this.walk(visit, child);
        }
    }

    /** The chain of nodes containing a position, outermost first. */
    nodePath(position)
    {
        const chain = [];
        let node = this.ast;
        while (node)
        {
            chain.push(node);
            const child = (node.children || []).find((candidate) => rangeContains(this.rangeOf(candidate), position));
            node = child || null;
        }
        return chain;
    }

    /** The innermost node containing a position. */
    nodeAt(position)
    {
        const chain = this.nodePath(position);
        return chain.length ? chain[chain.length - 1] : null;
    }

    rangeOf(node)
    {
        return spanToRange(node, this.lines);
    }

    // ------------------------------------------------------------------ declarations

    /**
     * Everything the program declares: structs with their fields, functions with their
     * parameters, and the variables and constants of the file, each with its range.
     */
    declarations()
    {
        if (this._declarations)
        {
            return this._declarations;
        }
        const result = [];
        const collect = (node, container) =>
        {
            if (!DECLARATION_KINDS.has(node.kind))
            {
                return;
            }
            const declaration = {
                name: node.label,
                kind: node.kind,
                type: node.type || "",
                range: this.rangeOf(node),
                selectionRange: this.nameRangeOf(node),
                container: container || "",
                file: this.file,
                children: [],
            };
            result.push(declaration);
            return declaration;
        };
        for (const node of this.topLevel())
        {
            const declaration = collect(node, "");
            if (!declaration)
            {
                continue;
            }
            for (const child of (node.children || []))
            {
                if (child.kind === "Field" || child.kind === "Param")
                {
                    const member = collect(child, node.label);
                    if (member)
                    {
                        declaration.children.push(member);
                    }
                }
            }
        }
        this._declarations = result;
        return result;
    }

    /** The range of the *name* in a declaration, for "go to definition" and renaming. */
    nameRangeOf(node)
    {
        const range = this.rangeOf(node);
        if (!node.label)
        {
            return range;
        }
        const line = this.lines[range.start.line] || "";
        const index = line.indexOf(node.label, range.start.character);
        if (index < 0)
        {
            return range;
        }
        return {
            start: { line: range.start.line, character: index },
            end: { line: range.start.line, character: index + node.label.length },
        };
    }

    structs()
    {
        return this.declarations().filter((declaration) => declaration.kind === "Struct");
    }

    /** The structs of this file and of every module it imports. */
    allStructs()
    {
        const result = this.structs().slice();
        for (const module of this.modules.values())
        {
            for (const declaration of module.structs())
            {
                if (!result.some((existing) => existing.name === declaration.name))
                {
                    result.push(declaration);
                }
            }
        }
        return result;
    }

    functions()
    {
        return this.declarations().filter(
            (declaration) => declaration.kind === "Function" || declaration.kind === "NativeFunction");
    }

    allFunctions()
    {
        const result = this.functions().slice();
        for (const module of this.modules.values())
        {
            for (const declaration of module.functions())
            {
                if (!result.some((existing) => existing.name === declaration.name))
                {
                    result.push(declaration);
                }
            }
        }
        return result;
    }

    /** A function's signature as it would be written: `area(r: Rectangle): int`. */
    signatureOf(declaration)
    {
        const parameters = declaration.children
            .filter((child) => child.kind === "Param")
            .map((child) => `${child.name}: ${child.type}`);
        const result = declaration.type ? `: ${declaration.type}` : "";
        return `${declaration.name}(${parameters.join(", ")})${result}`;
    }

    // ------------------------------------------------------------------ scopes and symbols

    get scopes()
    {
        return (this.report.symbols || {}).scopes || null;
    }

    /** The scopes containing a position, outermost (global) first. */
    scopeChainAt(position)
    {
        const chain = [];
        let scope = this.scopes;
        while (scope)
        {
            chain.push(scope);
            const child = (scope.children || []).find((candidate) =>
                !candidate.file && rangeContains(spanToRange(candidate, this.lines), position));
            scope = child || null;
        }
        return chain;
    }

    /**
     * The variables, constants and parameters visible at a position, innermost first.
     *
     * A symbol declared later in the same scope is left out: Thunder declares names as the
     * program is read, so a name is not yet known above its `var`.
     */
    visibleSymbols(position)
    {
        const result = [];
        const chain = this.scopeChainAt(position);
        for (let i = chain.length - 1; i >= 0; --i)
        {
            for (const symbol of (chain[i].symbols || []))
            {
                if (symbol.file)
                {
                    continue; // declared in an imported module; offered through that module's model
                }
                const range = spanToRange(symbol, this.lines);
                if (range.start.line > position.line
                    || (range.start.line === position.line && range.start.character > position.character))
                {
                    continue;
                }
                if (this.insideOwnDeclaration(symbol.name, range, position))
                {
                    continue; // `var x = x` is an error: the name is not known inside its own declaration
                }
                if (!result.some((existing) => existing.name === symbol.name))
                {
                    result.push({
                        name: symbol.name,
                        kind: symbol.kind,
                        type: symbol.type,
                        const: Boolean(symbol.const),
                        used: Boolean(symbol.used),
                        range,
                        file: this.file,
                    });
                }
            }
        }
        return result;
    }

    /**
     * True when a position is inside the very statement that declares a name.
     *
     * Thunder declares a name as the program is read, so `var total = total + 1` is an error
     * rather than a use of the new variable: inside its own `var` the name does not exist yet.
     */
    insideOwnDeclaration(name, nameRange, position)
    {
        for (const node of this.declarationNodes())
        {
            if (node.label !== name)
            {
                continue;
            }
            const selection = this.nameRangeOf(node);
            if (selection.start.line !== nameRange.start.line
                || selection.start.character !== nameRange.start.character)
            {
                continue;
            }
            if (rangeContains(this.rangeOf(node), position))
            {
                return true;
            }
        }
        return false;
    }

    /** Every `var`, `const` and parameter in the whole tree, not only at the top level. */
    declarationNodes()
    {
        if (!this._declarationNodes)
        {
            const nodes = [];
            this.walk((node) =>
            {
                if (node.kind === "Var" || node.kind === "Const" || node.kind === "Param"
                    || node.kind === "ForEach")
                {
                    nodes.push(node);
                }
            });
            this._declarationNodes = nodes;
        }
        return this._declarationNodes;
    }

    /** Every symbol in the program, with the file it was declared in. */
    allSymbols()
    {
        if (this._symbolsFlat)
        {
            return this._symbolsFlat;
        }
        const result = [];
        const visit = (scope, container) =>
        {
            for (const symbol of (scope.symbols || []))
            {
                result.push({
                    name: symbol.name,
                    kind: symbol.kind,
                    type: symbol.type,
                    const: Boolean(symbol.const),
                    used: Boolean(symbol.used),
                    fileIndex: symbol.file || 0,
                    file: this.pathOfFileIndex(symbol.file || 0),
                    container,
                    span: symbol,
                });
            }
            for (const child of (scope.children || []))
            {
                visit(child, child.name || container);
            }
        };
        if (this.scopes)
        {
            visit(this.scopes, "");
        }
        this._symbolsFlat = result;
        return result;
    }

    /**
     * Where the name at a position is declared.
     *
     * Looks through the scopes the position is in, then the file's own declarations, then those
     * of the modules it imports. Returns {file, range, name, detail} or null.
     */
    definitionAt(position)
    {
        const token = this.tokenAt(position);
        if (!token || token.category !== "identifier")
        {
            return null;
        }
        const node = this.nodeAt(position);
        // A field access (`r.width`): the definition is the field in the struct of the target.
        if (node && node.kind === "Field" && node.label === token.lexeme)
        {
            const target = (node.children || [])[0];
            const struct = target ? this.allStructs().find((candidate) => candidate.name === target.type) : null;
            const field = struct ? struct.children.find((child) => child.name === token.lexeme) : null;
            if (field)
            {
                return { file: struct.file, range: field.selectionRange, name: field.name, detail: field.type };
            }
        }
        const local = this.visibleSymbols(position).find((symbol) => symbol.name === token.lexeme);
        if (local)
        {
            return { file: local.file, range: local.range, name: local.name, detail: local.type };
        }
        const declaration = this.allDeclarationsNamed(token.lexeme)[0];
        if (declaration)
        {
            return {
                file: declaration.file,
                range: declaration.selectionRange,
                name: declaration.name,
                detail: declaration.kind === "Struct" ? "struct" : this.signatureOf(declaration),
            };
        }
        // A name declared in a module (a global variable or a function) - the symbol table
        // knows the file and the line.
        const foreign = this.allSymbols().find((symbol) => symbol.name === token.lexeme && symbol.fileIndex);
        if (foreign)
        {
            const module = this.modules.get(foreign.file);
            const lines = module ? module.lines : [""];
            return {
                file: foreign.file,
                range: spanToRange(foreign.span, lines),
                name: foreign.name,
                detail: foreign.type,
            };
        }
        return null;
    }

    /** Declarations with a name, in this file first, then in imported modules. */
    allDeclarationsNamed(name)
    {
        const matches = this.declarations().filter(
            (declaration) => declaration.name === name && declaration.kind !== "Param" && declaration.kind !== "Field");
        for (const module of this.modules.values())
        {
            for (const declaration of module.declarations())
            {
                if (declaration.name === name && declaration.kind !== "Param" && declaration.kind !== "Field")
                {
                    matches.push(declaration);
                }
            }
        }
        return matches;
    }

    // ------------------------------------------------------------------ calls

    /**
     * The call the position is inside, for signature help: {name, argument, range}.
     *
     * `argument` is the 0-based index of the argument the caret is in. The syntax tree gives
     * each argument the role "arg N", so the index comes from the compiler, not from counting
     * commas (which would be wrong inside nested calls).
     */
    callAt(position)
    {
        const chain = this.nodePath(position);
        for (let i = chain.length - 1; i >= 0; --i)
        {
            if (chain[i].kind !== "Call")
            {
                continue;
            }
            const call = chain[i];
            let argument = 0;
            const inner = chain[i + 1];
            if (inner)
            {
                const match = /^arg (\d+)$/.exec(inner.role || "");
                if (match)
                {
                    argument = Number(match[1]) - 1;
                }
            }
            else
            {
                // The caret is in the brackets but not on an argument yet: count the ones before it.
                const before = (call.children || []).filter((child) =>
                {
                    const range = this.rangeOf(child);
                    return range.end.line < position.line
                        || (range.end.line === position.line && range.end.character <= position.character);
                });
                argument = before.length;
            }
            return { name: call.label, argument, range: this.rangeOf(call) };
        }
        return null;
    }

    // ------------------------------------------------------------------ bytecode

    /** The bytecode listing, if the program compiled. */
    get bytecodeListing()
    {
        return (this.report.bytecode || {}).listing || "";
    }

    /**
     * The source lines that have instructions, per file index: a breakpoint can only stop
     * where the code generator produced something.
     */
    executableLines()
    {
        const byFile = new Map();
        for (const func of ((this.report.bytecode || {}).functions || []))
        {
            for (const instruction of (func.instructions || []))
            {
                const file = instruction.file || 0;
                if (!byFile.has(file))
                {
                    byFile.set(file, new Set());
                }
                byFile.get(file).add(instruction.line);
            }
        }
        return byFile;
    }

    get phases()
    {
        return this.report.phases || [];
    }
}

module.exports = { Model };
