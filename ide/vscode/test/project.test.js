"use strict";

const path = require("path");

const { absolute, assert, suite, test } = require("./harness");
const project = require("../src/project");

const FOLDER = absolute("work", "game");

suite("project: thunder.json", () =>
{
    test("a folder without a project file still works", () =>
    {
        const { project: parsed, error } = project.parseProject(null, FOLDER);
        assert.strictEqual(error, null);
        assert.strictEqual(parsed.entry, "");
        assert.strictEqual(parsed.language, "auto");
        assert.deepStrictEqual(parsed.modulePaths, []);
        assert.strictEqual(parsed.file, "");
    });

    test("relative paths are resolved against the folder", () =>
    {
        const { project: parsed } = project.parseProject(JSON.stringify({
            name: "game",
            entry: "src/main.th",
            modulePaths: ["lib", "../shared"],
            nativePaths: ["build"],
            output: "build/out.thc",
            package: "dist/game",
        }), FOLDER);
        assert.strictEqual(parsed.entry, path.join(FOLDER, "src", "main.th"));
        assert.strictEqual(parsed.modulePaths[0], path.join(FOLDER, "lib"));
        assert.strictEqual(parsed.modulePaths[1], path.resolve(FOLDER, "..", "shared"));
        assert.strictEqual(parsed.nativePaths[0], path.join(FOLDER, "build"));
        assert.strictEqual(parsed.output, path.join(FOLDER, "build", "out.thc"));
        assert.strictEqual(parsed.package, path.join(FOLDER, "dist", "game"));
    });

    test("comments are allowed, as in VS Code's own JSON files", () =>
    {
        const text = "{\n  // the program to run\n  \"entry\": \"main.th\" /* here */\n}\n";
        const { project: parsed, error } = project.parseProject(text, FOLDER);
        assert.strictEqual(error, null);
        assert.strictEqual(parsed.entry, path.join(FOLDER, "main.th"));
    });

    test("a // inside a string is not a comment", () =>
    {
        const { project: parsed } = project.parseProject('{"name": "http://example.com"}', FOLDER);
        assert.strictEqual(parsed.name, "http://example.com");
    });

    test("a broken project file is reported, not thrown", () =>
    {
        const { project: parsed, error } = project.parseProject("{ not json", FOLDER);
        assert.ok(error && error.includes("thunder.json"));
        assert.strictEqual(parsed.entry, ""); // the defaults, so the editor stays usable
        assert.ok(project.parseProject("[1, 2]", FOLDER).error.includes("object"));
    });

    test("unknown keys are kept", () =>
    {
        const { project: parsed } = project.parseProject('{"futureOption": 42}', FOLDER);
        assert.strictEqual(parsed.futureOption, 42);
    });
});

suite("project: which program a command works on", () =>
{
    test("the active editor wins", () =>
    {
        const { project: parsed } = project.parseProject('{"entry": "main.th"}', FOLDER);
        const active = absolute("work", "game", "other.th");
        assert.strictEqual(project.programFor(active, parsed, []), active);
    });

    test("then the entry point, then main.th, then the only file", () =>
    {
        const { project: parsed } = project.parseProject('{"entry": "src/main.th"}', FOLDER);
        assert.strictEqual(project.programFor(null, parsed, []), path.join(FOLDER, "src", "main.th"));

        const { project: plain } = project.parseProject(null, FOLDER);
        const files = [path.join(FOLDER, "lib", "x.th"), path.join(FOLDER, "main.th")];
        assert.strictEqual(project.programFor(null, plain, files), path.join(FOLDER, "main.th"));
        assert.strictEqual(project.programFor(null, plain, [path.join(FOLDER, "only.th")]),
            path.join(FOLDER, "only.th"));
        assert.strictEqual(project.programFor(null, plain, files.slice(0, 1)), path.join(FOLDER, "lib", "x.th"));
        assert.strictEqual(project.programFor(null, plain,
            [path.join(FOLDER, "a.th"), path.join(FOLDER, "b.th")]), null);
    });
});

suite("project: where things are written", () =>
{
    test("the bytecode goes beside the source, or where the project says", () =>
    {
        const { project: plain } = project.parseProject(null, FOLDER);
        assert.strictEqual(project.outputFor(path.join(FOLDER, "main.th"), plain), path.join(FOLDER, "main.thc"));

        const { project: directory } = project.parseProject('{"output": "build"}', FOLDER);
        assert.strictEqual(project.outputFor(path.join(FOLDER, "main.th"), directory),
            path.join(FOLDER, "build", "main.thc"));

        const { project: named } = project.parseProject('{"output": "build/game.thc"}', FOLDER);
        assert.strictEqual(project.outputFor(path.join(FOLDER, "main.th"), named),
            path.join(FOLDER, "build", "game.thc"));
    });

    test("a packaged application is named after the program", () =>
    {
        const { project: plain } = project.parseProject(null, FOLDER);
        assert.strictEqual(project.packageFor(path.join(FOLDER, "snake.th"), plain, ""),
            path.join(FOLDER, "snake"));
        assert.strictEqual(project.packageFor(path.join(FOLDER, "snake.th"), plain, "dist"),
            path.join(FOLDER, "dist", "snake"));
        const { project: fixed } = project.parseProject('{"package": "out/game"}', FOLDER);
        assert.strictEqual(project.packageFor(path.join(FOLDER, "snake.th"), fixed, "dist"),
            path.join(FOLDER, "out", "game"));
    });

    test("the module search starts with the program's own directory, without repeats", () =>
    {
        const { project: parsed } = project.parseProject('{"modulePaths": ["lib"]}', FOLDER);
        const paths = project.modulePathsFor(path.join(FOLDER, "src", "main.th"), parsed, ["lib", "extra"]);
        assert.strictEqual(paths[0], path.join(FOLDER, "src"));
        assert.ok(paths.includes(path.join(FOLDER, "lib")));
        assert.ok(paths.includes(path.join(FOLDER, "extra")));
        assert.strictEqual(new Set(paths).size, paths.length);
    });

    test("a new project names its entry point and its module directory", () =>
    {
        const template = project.projectTemplate("game", "ps");
        assert.strictEqual(template.entry, "main.th");
        assert.strictEqual(template.language, "ps");
        assert.deepStrictEqual(template.modulePaths, ["lib"]);
    });
});
