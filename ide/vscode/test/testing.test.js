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
    test("a program that imports testing declares a test per test(...) call, in either language", () =>
    {
        const english = "import \"testing\"\n\nfunc adds()\n    expect_equal(2, 2)\n\ntest(\"adds up\", adds)\ntest( \"second\" , adds)\nfinish_tests()\n";
        assert.deepStrictEqual(discoverTests(english), [{ name: "adds up", line: 5 }, { name: "second", line: 6 }]);
        const pashto = "# ژبه: ps\nراوړه \"testing\"\nدنده جمع()\n    برابر_تمه(۲، ۲)\nازموینه(«جمع سمه ده»، جمع)\nازموینه(“دویمه”، جمع)\nازموینې_پای_کړه()\n";
        assert.deepStrictEqual(discoverTests(pashto), [{ name: "جمع سمه ده", line: 4 }, { name: "دویمه", line: 5 }]);
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
        "    expect_equal(2 + 2, 4)",
        "func wrong()",
        "    expect_equal(\"a\", \"b\")",
        "test(\"adds\", adds)",
        "test(\"wrong\", wrong)",
        "finish_tests()",
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
        const broken = PROGRAM.replace("expect_equal(2 + 2, 4)", "expect_equal(2 + , 4)");
        fs.writeFileSync(file, broken, "utf8");
        tests.refreshText(vscode.Uri.file(file), broken);
        const run = await tests.runFileCommand(vscode.Uri.file(file));
        assert.strictEqual(run.of("errored").length, 2, "both tests are errored, with the compiler's words");
        assert.ok(run.of("errored")[0].message.message.includes("error"));
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
