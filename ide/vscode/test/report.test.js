// The model of a program, built from what the real compiler reports. These tests run thunderc,
// so they are skipped when the toolchain has not been built.

"use strict";

const fs = require("fs");
const path = require("path");

const { assert, before, requireToolchain, suite, temporaryDirectory, test } = require("./harness");
const { Model } = require("../src/report");
const { Toolchain } = require("../src/toolchain");

let toolchain = null;
let directory = null;

/** Compiles a program and returns its model (with the models of the modules it imports). */
async function compile(name, text, options)
{
    const settings = options || {};
    const file = path.join(directory, name);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text, "utf8");
    const outcome = await toolchain.compile(file, {
        language: settings.language || "en",
        noOutput: !settings.withBytecode,
        output: settings.withBytecode ? file.replace(/\.th$/, ".thc") : undefined,
        cwd: directory,
    });
    const modules = new Map();
    for (const module of (outcome.report.modules || []))
    {
        const resolved = path.isAbsolute(module.path) ? module.path : path.resolve(directory, module.path);
        const moduleOutcome = await toolchain.compile(resolved, { language: settings.language || "en", noOutput: true });
        modules.set(resolved, new Model(moduleOutcome.report, fs.readFileSync(resolved, "utf8"), { file: resolved }));
    }
    return new Model(outcome.report, text, { file, modules });
}

const STRUCTS = [
    "struct Point",
    "    x: int",
    "    y: int",
    "",
    "func distance_squared(a: Point, b: Point): int",
    "    var dx = a.x - b.x",
    "    var dy = a.y - b.y",
    "    return dx * dx + dy * dy",
    "",
    "const origin = Point(0, 0)",
    "var far = Point(3, 4)",
    "println(distance_squared(origin, far))",
    "",
].join("\n");

suite("report: the model of a program", () =>
{
    before(async () =>
    {
        const bin = requireToolchain();
        toolchain = new Toolchain(bin);
        directory = temporaryDirectory("report");
    });

    test("a program that compiles has no diagnostics and every phase ran", async () =>
    {
        const model = await compile("good.th", STRUCTS);
        assert.ok(model.succeeded);
        assert.deepStrictEqual(model.diagnostics(), []);
        assert.strictEqual(model.phases.length, 4);
        assert.ok(model.phases.every((phase) => phase.ran && phase.succeeded));
    });

    test("a diagnostic lands on the text it is about", async () =>
    {
        const text = ["var count: int = 0", "count = \"ten\"", ""].join("\n");
        const model = await compile("bad.th", text);
        const diagnostics = model.diagnostics();
        assert.ok(diagnostics.length >= 1);
        const first = diagnostics[0];
        assert.strictEqual(first.severity, "error");
        assert.strictEqual(first.code, "S005");
        assert.strictEqual(first.phase, "semantic");
        assert.strictEqual(first.range.start.line, 1);
        // The range covers "ten" with its quotes.
        const line = text.split("\n")[1];
        assert.strictEqual(line.slice(first.range.start.character, first.range.end.character), "\"ten\"");
        assert.strictEqual(path.basename(first.file), "bad.th");
    });

    test("the declarations are the outline of the file", async () =>
    {
        const model = await compile("outline.th", STRUCTS);
        const top = model.declarations().filter((declaration) => !declaration.container);
        assert.deepStrictEqual(top.map((declaration) => [declaration.kind, declaration.name]), [
            ["Struct", "Point"],
            ["Function", "distance_squared"],
            ["Const", "origin"],
            ["Var", "far"],
        ]);
        const point = top[0];
        assert.deepStrictEqual(point.children.map((field) => [field.name, field.type]), [["x", "int"], ["y", "int"]]);
        assert.strictEqual(model.signatureOf(top[1]), "distance_squared(a: Point, b: Point): int");
        // The selection range is the name itself, so renaming starts there.
        const line = STRUCTS.split("\n")[0];
        assert.strictEqual(line.slice(point.selectionRange.start.character, point.selectionRange.end.character),
            "Point");
    });

    test("a name is traced to where it was declared", async () =>
    {
        const model = await compile("definitions.th", STRUCTS);
        // "a.x" on line 6 (0-based 5): the parameter a...
        const parameter = model.definitionAt({ line: 5, character: 13 });
        assert.ok(parameter, "the parameter was not found");
        assert.strictEqual(parameter.name, "a");
        assert.strictEqual(parameter.range.start.line, 4);
        // ... and the field x of Point.
        const field = model.definitionAt({ line: 5, character: 15 });
        assert.ok(field, "the field was not found");
        assert.strictEqual(field.name, "x");
        assert.strictEqual(field.range.start.line, 1);
        // The call on the last line leads to the function.
        const call = model.definitionAt({ line: 11, character: 10 });
        assert.ok(call);
        assert.strictEqual(call.name, "distance_squared");
        assert.strictEqual(call.range.start.line, 4);
        // A keyword is not a name.
        assert.strictEqual(model.definitionAt({ line: 0, character: 1 }), null);
    });

    test("a name declared in a module is found in that module's file", async () =>
    {
        fs.mkdirSync(path.join(directory, "lib"), { recursive: true });
        fs.writeFileSync(path.join(directory, "lib", "shapes.th"),
            ["struct Rectangle", "    width: int", "    height: int", "",
                "func area(r: Rectangle): int", "    return r.width * r.height", ""].join("\n"), "utf8");
        const model = await compile("uses_module.th",
            ["import \"lib/shapes\"", "var room = Rectangle(4, 3)", "println(area(room))", ""].join("\n"));
        assert.ok(model.succeeded, JSON.stringify(model.diagnostics()));
        assert.deepStrictEqual(model.importedFiles.map((module) => module.name), ["lib/shapes.th"]);
        const struct = model.allStructs().find((candidate) => candidate.name === "Rectangle");
        assert.ok(struct, "the struct of the module was not found");
        assert.deepStrictEqual(struct.children.map((field) => field.name), ["width", "height"]);
        const definition = model.definitionAt({ line: 2, character: 9 });
        assert.ok(definition);
        assert.strictEqual(path.basename(definition.file), "shapes.th");
        assert.strictEqual(definition.name, "area");
    });

    test("a diagnostic inside a module belongs to the module's file", async () =>
    {
        fs.mkdirSync(path.join(directory, "lib"), { recursive: true });
        fs.writeFileSync(path.join(directory, "lib", "broken.th"), "var wrong: int = \"text\"\n", "utf8");
        const model = await compile("imports_broken.th", "import \"lib/broken\"\n");
        const diagnostics = model.diagnostics(() => "var wrong: int = \"text\"\n");
        assert.ok(diagnostics.length >= 1);
        assert.strictEqual(path.basename(diagnostics[0].file), "broken.th");
    });

    test("only the names already declared are visible", async () =>
    {
        const text = ["var first = 1", "var second = 2", "func f(n: int): int", "    var inner = n", "    return inner",
            ""].join("\n");
        const model = await compile("scopes.th", text);
        const atSecond = model.visibleSymbols({ line: 1, character: 12 }).map((symbol) => symbol.name);
        assert.ok(atSecond.includes("first"));
        assert.ok(!atSecond.includes("second"), "a name is not visible above its own declaration");
        const inside = model.visibleSymbols({ line: 4, character: 15 }).map((symbol) => symbol.name);
        assert.ok(inside.includes("n"), "the parameter is visible inside the function");
        assert.ok(inside.includes("inner"));
        assert.ok(inside.includes("first"));
        assert.ok(!model.visibleSymbols({ line: 0, character: 5 }).some((symbol) => symbol.name === "inner"));
    });

    test("the call around a position knows which argument it is", async () =>
    {
        const text = ["func add(a: int, b: int): int", "    return a + b", "", "println(add(1, add(2, 3)))", ""]
            .join("\n");
        const model = await compile("calls.th", text);
        const line = 3;
        // "1" is the first argument of the inner add.
        const first = model.callAt({ line, character: text.split("\n")[line].indexOf("1") });
        assert.deepStrictEqual([first.name, first.argument], ["add", 0]);
        // "2" is the first argument of the nested add, not the second of anything.
        const nested = model.callAt({ line, character: text.split("\n")[line].indexOf("2") });
        assert.deepStrictEqual([nested.name, nested.argument], ["add", 0]);
        const third = model.callAt({ line, character: text.split("\n")[line].indexOf("3") });
        assert.deepStrictEqual([third.name, third.argument], ["add", 1]);
    });

    test("the lines a breakpoint can stop on are the lines with instructions", async () =>
    {
        const text = ["// a comment", "var x = 1", "", "func unused_function(n: int): int", "    return n",
            "println(x)", ""].join("\n");
        const model = await compile("lines.th", text);
        const lines = model.executableLines().get(0);
        assert.ok(lines.has(2), "the assignment has code");
        assert.ok(lines.has(6), "the call has code");
        assert.ok(!lines.has(1), "a comment has no code");
        assert.ok(!lines.has(3), "a blank line has no code");
    });

    test("the modules a program imports are known by their canonical name", async () =>
    {
        const model = await compile("imports.th", "import os\nprintln(platform())\n");
        const info = await toolchain.languageInfo("en");
        assert.deepStrictEqual([...model.importedModules(info)], ["os"]);
    });

    test("a Pashto program is modelled exactly like an English one", async () =>
    {
        const text = [
            "// ژبه: ps",
            "جوړښت ټکی",
            "    س: عدد",
            "    ص: عدد",
            "",
            "دنده ټولټال(الف: ټکی): عدد",
            "    راګرځه الف.س + الف.ص",
            "",
            "متغیر ټ = ټکی(۳، ۴)",
            "کرښه چاپ(ټولټال(ټ))",
            "",
        ].join("\n");
        const model = await compile("pashto.th", text, { language: "ps" });
        assert.ok(model.succeeded, JSON.stringify(model.diagnostics()));
        assert.strictEqual(model.languageCode, "ps");
        assert.ok(model.rightToLeft);
        const names = model.declarations().filter((declaration) => !declaration.container)
            .map((declaration) => declaration.name);
        assert.deepStrictEqual(names, ["ټکی", "ټولټال", "ټ"]);
        const info = await toolchain.languageInfo("ps");
        assert.ok(model.tokens().some((token) => token.lexeme === "کرښه چاپ" || token.category === "identifier"));
        // The field of a Pashto struct is found through the Pashto name.
        const returnLine = text.split("\n")[6];
        const definition = model.definitionAt({ line: 6, character: returnLine.indexOf(".س") + 1 });
        assert.ok(definition, "the field was not found");
        assert.strictEqual(definition.name, "س");
        assert.strictEqual(definition.range.start.line, 2);
        assert.ok(info.keywords.length > 0);
    });
});
