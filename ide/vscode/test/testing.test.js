// Thunder tests in the Test Explorer (src/testing.js): finding the tests of a program, reading
// the testing module's transcript, and - with the toolchain - running a real test file.

"use strict";

const fs = require("fs");
const os = require("os");
const path = require("path");

const { EXTENSION, assert, before, suite, test, toolchainDirectory, removeOnExit } = require("./harness");
const vscode = require("./fake-vscode");

const { ThunderTests, discoverTests, parseTranscript } = require("../src/testing");

suite("tests: finding them and reading the transcript", () =>
{
    test("a program that imports testing declares a test per test.run(...) call, in either language", () =>
    {
        const english = "import \"testing\"\n\nfunc adds()\n    expect.equal(2, 2)\n\ntest.run(\"adds up\", adds)\ntest.run( \"second\" , adds)\ntest.finish()\n";
        assert.deepStrictEqual(discoverTests(english), [{ name: "adds up", suite: "", line: 5 }, { name: "second", suite: "", line: 6 }]);
        const pashto = "# ژبه: ps\nراوړه \"testing\"\nدنده جمع()\n    تمه.برابر(۲، ۲)\nازموینه.وچلوه(«جمع سمه ده»، جمع)\nازموینه.وچلوه(“دویمه”، جمع)\nازموینه.پای_ته_ورسوه()\n";
        assert.deepStrictEqual(discoverTests(pashto), [{ name: "جمع سمه ده", suite: "", line: 4 }, { name: "دویمه", suite: "", line: 5 }]);
    });

    test("a test whose function belongs to a module belongs to that module's suite", () =>
    {
        const program = "import \"testing\"\nmodule arithmetic\n    func adds()\n        expect.equal(2, 2)\n"
            + "test.run(\"adds up\", arithmetic.adds)\ntest.run(\"flat\", main)\n";
        assert.deepStrictEqual(discoverTests(program),
            [{ name: "adds up", suite: "arithmetic", line: 4 }, { name: "flat", suite: "", line: 5 }]);
        const pashto = "# ژبه: ps\nراوړه \"testing\"\nازموینه.وچلوه(\"جمع\"، شمېرې.جمع)\n";
        assert.deepStrictEqual(discoverTests(pashto), [{ name: "جمع", suite: "شمېرې", line: 2 }]);
    });

    test("a program without the import has no tests, whatever it calls test", () =>
    {
        assert.deepStrictEqual(discoverTests("func test(name: string, f: func())\n    f()\ntest(\"mine\", main)\n"), []);
    });

    test("the transcript gives every pass and every failed expectation, by test", () =>
    {
        const { passed, failed } = parseTranscript("✓ adds\n✗ wrong: expected b, got a\n✗ wrong: and more\n✓ د لړۍ تر ټولو لوی\n2 passed, 1 failed\n");
        assert.deepStrictEqual([...passed], ["adds", "د لړۍ تر ټولو لوی"]);
        assert.deepStrictEqual([...failed.entries()], [["wrong", ["expected b, got a", "and more"]]]);
    });

    test("a suite's heading puts its indented tests under it, and only a known suite is a heading", () =>
    {
        const transcript = "flat line\n✓ top\nnumbers\n  ✓ adds\n  ✗ wrong: no\nprinted by a test\n  ✓ later\n"
            + "numbers: 2 passed, 1 failed\n3 passed, 1 failed\n";
        const { passed, failed } = parseTranscript(transcript, new Set(["numbers"]));
        assert.deepStrictEqual([...passed], ["top", "numbers/adds", "numbers/later"]);
        assert.deepStrictEqual([...failed.keys()], ["numbers/wrong"]);
    });
});

suite("tests: the Test Explorer", () =>
{
    let directory = null;
    let services = null;

    before(() =>
    {
        vscode._reset();
        directory = fs.mkdtempSync(path.join(os.tmpdir(), "thunder-vscode-tests-"));
        vscode._setFolders([directory]);
        if (toolchainDirectory())
        {
            vscode._state.configuration["thunder.toolchainPath"] = toolchainDirectory();
        }
        const { Services } = require("../src/services");
        services = new Services({ extensionPath: EXTENSION });
        removeOnExit(directory);
        return { directory, services };
    });

    const PROGRAM = [
        "import \"testing\"",
        "func adds()",
        "    expect.equal(2 + 2, 4)",
        "func wrong()",
        "    expect.equal(\"a\", \"b\")",
        "test.run(\"adds\", adds)",
        "test.run(\"wrong\", wrong)",
        "test.finish()",
        "",
    ].join("\n");

    test("an open test file becomes a file item with one child per test, placed on its line", () =>
    {
        const tests = new ThunderTests(services);
        const file = path.join(directory, "suite.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        const document = vscode._openDocument(file, PROGRAM);
        tests.refreshDocument(document);
        const item = tests.controller.items.get(file);
        assert.ok(item, "the file is a test item");
        const names = [];
        item.children.forEach((child) => names.push([child.label, child.range.start.line]));
        assert.deepStrictEqual(names, [["adds", 5], ["wrong", 6]]);
        assert.ok(tests.hasTests(file));
        // Taking the import away takes the tests away.
        tests.refreshText(document.uri, PROGRAM.replace("import \"testing\"", ""));
        assert.ok(!tests.controller.items.get(file));
        tests.dispose();
    });

    test("the workspace is searched for test files when the explorer asks", () =>
    {
        const tests = new ThunderTests(services);
        fs.writeFileSync(path.join(directory, "found.th"), PROGRAM, "utf8");
        fs.writeFileSync(path.join(directory, "plain.th"), "println(1)\n", "utf8");
        tests.controller.resolveHandler(undefined);
        const files = tests.files().map((item) => item.label).sort();
        assert.ok(files.includes("found.th"));
        assert.ok(!files.includes("plain.th"));
        tests.dispose();
    });

    test("running a file reports each test from the module's transcript", async function runs()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const tests = new ThunderTests(services);
        const file = path.join(directory, "run.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        tests.refreshText(vscode.Uri.file(file), PROGRAM);
        const run = await tests.runFileCommand(vscode.Uri.file(file));
        assert.ok(run.ended);
        assert.deepStrictEqual(run.of("passed").map((result) => result.item.label), ["adds"]);
        assert.deepStrictEqual(run.of("failed").map((result) => [result.item.label, result.message[0].message]),
            [["wrong", "expected b, got a"]]);
        assert.ok(run.output.includes("1 passed, 1 failed"), run.output);
        // One test alone: only it runs, through the program's arguments.
        const file_item = tests.controller.items.get(file);
        const single = await tests.run(new vscode.TestRunRequest([file_item.children.get(`${file}::wrong`)]));
        assert.deepStrictEqual(single.of("started").map((result) => result.item.label), ["wrong"]);
        assert.strictEqual(single.of("failed").length, 1);
        assert.ok(!single.output.includes("✓ adds"), "the other test did not run");
        tests.dispose();
    });

    test("a program that does not compile reports the error against its tests", async function compileError()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const tests = new ThunderTests(services);
        const file = path.join(directory, "broken.th");
        const broken = PROGRAM.replace("expect.equal(2 + 2, 4)", "expect.equal(2 + , 4)");
        fs.writeFileSync(file, broken, "utf8");
        tests.refreshText(vscode.Uri.file(file), broken);
        const run = await tests.runFileCommand(vscode.Uri.file(file));
        assert.strictEqual(run.of("errored").length, 2, "both tests are errored, with the compiler's words");
        assert.ok(run.of("errored")[0].message.message.includes("error"));
        tests.dispose();
    });

    test("the play button's line runs exactly that test", async function playButton()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const tests = new ThunderTests(services);
        const file = path.join(directory, "line.th");
        fs.writeFileSync(file, PROGRAM, "utf8");
        tests.refreshText(vscode.Uri.file(file), PROGRAM);
        assert.strictEqual(await tests.runTestAt(vscode.Uri.file(file), 4), null, "no test is declared on line 4");
        const run = await tests.runTestAt(vscode.Uri.file(file), 6); // test("wrong", wrong)
        assert.ok(run && run.ended);
        assert.deepStrictEqual(run.of("started").map((result) => result.item.label), ["wrong"]);
        assert.strictEqual(run.of("failed").length, 1);
        tests.dispose();
    });

    test("a Pashto test file runs exactly like an English one", async function pashtoRun()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const tests = new ThunderTests(services);
        const file = path.join(directory, "pashto.th");
        const program = [
            "# ژبه: ps",
            "راوړه \"testing\"",
            "دنده جمع()",
            "    تمه.برابر(۲ + ۲، ۴)",
            "دنده ناسمه()",
            "    تمه.برابر(\"الف\"، \"ب\")",
            "ازموینه.وچلوه(\"جمع\"، جمع)",
            "ازموینه.وچلوه(\"ناسمه\"، ناسمه)",
            "ازموینه.پای_ته_ورسوه()",
            "",
        ].join("\n");
        fs.writeFileSync(file, program, "utf8");
        tests.refreshText(vscode.Uri.file(file), program);
        const run = await tests.runFileCommand(vscode.Uri.file(file));
        assert.deepStrictEqual(run.of("passed").map((result) => result.item.label), ["جمع"]);
        assert.deepStrictEqual(run.of("failed").map((result) => result.item.label), ["ناسمه"]);
        assert.ok(run.of("failed")[0].message[0].message.includes("تمه ب وه"));
        // A single Pashto test, chosen by its name through the program's arguments.
        const item = tests.controller.items.get(file);
        const single = await tests.run(new vscode.TestRunRequest([item.children.get(`${file}::جمع`)]));
        assert.deepStrictEqual(single.of("passed").map((result) => result.item.label), ["جمع"]);
        assert.ok(!single.output.includes("ناسمه"), "the other test did not run");
        tests.dispose();
    });

    test("modules are suites: a suite item runs its tests, and one test of a suite runs alone", async function suites()
    {
        if (!toolchainDirectory())
        {
            return;
        }
        const tests = new ThunderTests(services);
        const file = path.join(directory, "suites.th");
        const program = [
            "import \"testing\"",
            "module numbers",
            "    func adds()",
            "        expect.equal(2 + 2, 4)",
            "    func wrong()",
            "        expect.equal(1, 2)",
            "module text",
            "    func joins()",
            "        expect.equal(\"a\" + \"b\", \"ab\")",
            "test.run(\"adds\", numbers.adds)",
            "test.run(\"wrong\", numbers.wrong)",
            "test.run(\"joins\", text.joins)",
            "test.finish()",
            "",
        ].join("\n");
        fs.writeFileSync(file, program, "utf8");
        tests.refreshText(vscode.Uri.file(file), program);
        const item = tests.controller.items.get(file);
        const numbers = item.children.get(`${file}::numbers/`);
        assert.ok(numbers, "the module is a suite item under the file");
        const labels = [];
        numbers.children.forEach((child) => labels.push(child.label));
        assert.deepStrictEqual(labels, ["adds", "wrong"]);
        const whole = await tests.run(new vscode.TestRunRequest([numbers]));
        assert.deepStrictEqual(whole.of("passed").map((result) => result.item.label), ["adds"]);
        assert.deepStrictEqual(whole.of("failed").map((result) => result.item.label), ["wrong"]);
        assert.ok(!whole.output.includes("joins"), "the other suite did not run");
        const one = await tests.run(new vscode.TestRunRequest([numbers.children.get(`${file}::numbers/adds`)]));
        assert.deepStrictEqual(one.of("passed").map((result) => result.item.label), ["adds"]);
        assert.ok(!one.output.includes("wrong"), "only the chosen test ran");
        tests.dispose();
    });

    test("a file without tests says so instead of running nothing", async () =>
    {
        const tests = new ThunderTests(services);
        vscode._state.messages = [];
        assert.strictEqual(await tests.runFileCommand(vscode.Uri.file(path.join(directory, "none.th"))), null);
        assert.ok(vscode._state.messages.some((entry) => entry.message.includes("no tests")));
        tests.dispose();
    });
});
