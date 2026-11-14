"use strict";

const { assert, suite, test } = require("./harness");
const util = require("../src/util");

suite("util: positions and language", () =>
{
    test("a column counts code points, a character counts UTF-16 units", () =>
    {
        // Pashto letters are one of each, so the two agree.
        assert.strictEqual(util.columnToOffset("متغیر نوم", 7), 6);
        assert.strictEqual(util.offsetToColumn("متغیر نوم", 6), 7);
        // An emoji in a string is one code point and two UTF-16 units: column 3 is offset 4.
        const line = "\"\u{1F600}\u{1F600}x\"";
        assert.strictEqual(util.columnToOffset(line, 4), 5);
        assert.strictEqual(util.offsetToColumn(line, 5), 4);
        // Round trips for every column of a mixed line.
        const mixed = "var نوم = \"a\u{1F600}b\"";
        for (let column = 1; column <= 15; ++column)
        {
            assert.strictEqual(util.offsetToColumn(mixed, util.columnToOffset(mixed, column)), column,
                `column ${column}`);
        }
    });

    test("a column past the end of the line stays past the end", () =>
    {
        assert.strictEqual(util.columnToOffset("ab", 5), 4);
        assert.strictEqual(util.columnToOffset("", 1), 0);
    });

    test("a span becomes a range with 0-based lines", () =>
    {
        const lines = ["var x = 1", "println(x)"];
        const range = util.spanToRange({ line: 2, column: 1, end_line: 2, end_column: 8 }, lines);
        assert.deepStrictEqual(range, {
            start: { line: 1, character: 0 },
            end: { line: 1, character: 7 },
        });
    });

    test("an empty span still marks one character", () =>
    {
        const range = util.spanToRange({ line: 1, column: 3, end_line: 1, end_column: 3 }, ["var x = 1"]);
        assert.strictEqual(range.end.character, 3);
        assert.strictEqual(range.start.character, 2);
    });

    test("a span beyond the file is clamped to it", () =>
    {
        const range = util.spanToRange({ line: 99, column: 1, end_line: 99, end_column: 2 }, ["only"]);
        assert.strictEqual(range.start.line, 0);
        assert.strictEqual(range.end.line, 0);
    });

    test("rangeContains excludes the end", () =>
    {
        const range = { start: { line: 1, character: 2 }, end: { line: 1, character: 5 } };
        assert.ok(util.rangeContains(range, { line: 1, character: 2 }));
        assert.ok(util.rangeContains(range, { line: 1, character: 4 }));
        assert.ok(!util.rangeContains(range, { line: 1, character: 5 }));
        assert.ok(!util.rangeContains(range, { line: 0, character: 3 }));
    });

    test("the language comes from the first line, then from the setting", () =>
    {
        assert.strictEqual(util.directiveLanguage("# lang: ps\nvar x = 1"), "ps");
        assert.strictEqual(util.directiveLanguage("# ژبه: ps\n"), "ps");
        assert.strictEqual(util.directiveLanguage("// lang: ps\nvar x = 1"), "ps",
            "a directive written before '#' replaced '//' is still recognised");
        assert.strictEqual(util.directiveLanguage("var x = 1\n# lang: ps"), null);
        assert.strictEqual(util.languageOf("# lang: ps\n", "en"), "ps");
        assert.strictEqual(util.languageOf("var x = 1", "ps"), "ps");
        assert.strictEqual(util.languageOf("var x = 1", "auto"), "en");
        assert.strictEqual(util.languageOf("var x = 1", undefined), "en");
    });

    test("letters are folded the way the compiler folds them", () =>
    {
        // Arabic kaf (U+0643) against Pashto keheh (U+06A9), and ي/ى against ی.
        assert.strictEqual(util.foldLetters("كرښه چاپ"), util.foldLetters("کرښه چاپ"));
        assert.strictEqual(util.foldLetters("متغير"), "متغیر");
        assert.strictEqual(util.foldLetters("گ"), "ګ");
    });

    test("a command line can be pasted into a shell", () =>
    {
        assert.strictEqual(util.formatCommand(["thunderc", "-o", "a b.thc", "x.th"]),
            "thunderc -o \"a b.thc\" x.th");
    });

    test("the bytecode of a program sits beside it", () =>
    {
        assert.ok(util.bytecodePath("/tmp/x/main.th").endsWith("main.thc"));
        assert.ok(util.bytecodePath("main.th").endsWith("main.thc"));
    });

    test("splitLines understands every line ending", () =>
    {
        assert.deepStrictEqual(util.splitLines("a\r\nb\rc\nd"), ["a", "b", "c", "d"]);
    });
});
