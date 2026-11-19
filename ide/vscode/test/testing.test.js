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
    test("a function marked @test is a test, named by its description or its own name, in either language", () =>
    {
        const english = "import \"lib/maths\"\n\n@test(\"adds up\")\nfunc adds()\n    expect.equal(2, 2)\n\n"
            + "@test   # a comment\nfunc second_one()\n    pass\n\nfunc helper()\n    pass\n";
        assert.deepStrictEqual(discoverTests(english), [{ name: "adds up", suite: "", line: 2 }, { name: "second one", suite: "", line: 6 }]);
        const pashto = "# ژبه: ps\n@آزموينه(«جمع سمه ده»)\nدنده جمع()\n    تمه.برابر(۲، ۲)\n@ازموینه\nدنده دویمه_ازموینه()\n    تېر\n";
        assert.deepStrictEqual(discoverTests(pashto), [{ name: "جمع سمه ده", suite: "", line: 1 }, { name: "دویمه ازموینه", suite: "", line: 4 }]);
    });

    test("a marked struct or module is a suite: its public methods without parameters are its tests", () =>
    {
        const program = "@test(\"a tally\")\nstruct TallyTests\n    _t: int\n    TallyTests()\n        _t = 1\n"
            + "    func starts()\n        pass\n    func _helper()\n        pass\n    func with_args(n: int)\n        pass\n"
            + "    func answer(): int\n        return 1\n    func close()\n        pass\n    @test(\"twice\")\n    func doubles()\n        pass\n"
            + "@test\nmodule numbers\n    func positive()\n        pass\n"
            + "module plain\n    @test\n    func only_this()\n        pass\n    func not_this()\n        pass\n"
            + "@test\nfunc outside()\n    pass\n";
        assert.deepStrictEqual(discoverTests(program), [
            { name: "starts", suite: "a tally", line: 5 },
            { name: "twice", suite: "a tally", line: 15 },
            { name: "positive", suite: "numbers", line: 20 },
            { name: "only this", suite: "plain", line: 23 },
            { name: "outside", suite: "", line: 28 },
        ]);
        const pashto = "# ژبه: ps\n@آزموینه\nټولګی شمېرې\n    دنده جمع()\n        تېر\n";
        assert.deepStrictEqual(discoverTests(pashto), [{ name: "جمع", suite: "شمېرې", line: 3 }]);
    });

    test("a file without @test has no tests, and mocks, mixins and interfaces hold none", () =>
    {
        assert.deepStrictEqual(discoverTests("func test()\n    pass\ntest()\n"), []);
        assert.deepStrictEqual(discoverTests("@mock\nstruct Fake: Clock\n    func hour(): int\n        return 1\n    func reset()\n        pass\n"
            + "interface Clock\n    func reset()\n"), []);
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
        "@test",
        "func adds()",
        "    expect.equal(2 + 2, 4)",
        "@test",
        "func wrong()",
        "    expect.equal(\"a\", \"b\")",
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
        assert.deepStrictEqual(names, [["adds", 0], ["wrong", 3]]);
        assert.ok(tests.hasTests(file));
        // Taking the annotations away takes the tests away.
        tests.refreshText(document.uri, PROGRAM.replace(/@test\n/g, ""));
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
        assert.strictEqual(await tests.runTestAt(vscode.Uri.file(file), 2), null, "no test is declared on line 2");
        const run = await tests.runTestAt(vscode.Uri.file(file), 3); // @test before func wrong()
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
            "@آزموینه",
            "دنده جمع()",
            "    تمه.برابر(۲ + ۲، ۴)",
            "@آزموینه",
            "دنده ناسمه()",
            "    تمه.برابر(\"الف\"، \"ب\")",
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
            "@test",
            "module numbers",
            "    func adds()",
            "        expect.equal(2 + 2, 4)",
            "    func wrong()",
            "        expect.equal(1, 2)",
            "@test",
            "module text",
            "    func joins()",
            "        expect.equal(\"a\" + \"b\", \"ab\")",
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
