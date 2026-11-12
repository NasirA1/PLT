"use strict";

const { assert, suite, test } = require("./harness");
const edits = require("../src/edits");

suite("edits: tidy layout", () =>
{
    test("tabs become spaces, as the lexer measures them", () =>
    {
        assert.strictEqual(edits.expandTabs("\tx"), "    x");
        assert.strictEqual(edits.expandTabs("ab\tx"), "ab  x");
        assert.strictEqual(edits.expandTabs("abcd\tx"), "abcd    x");
        assert.strictEqual(edits.expandTabs("\t\t"), "        ");
    });

    test("trailing whitespace goes and the file ends with one line break", () =>
    {
        assert.strictEqual(edits.tidy("var x = 1   \n"), "var x = 1\n");
        assert.strictEqual(edits.tidy("var x = 1"), "var x = 1\n");
        assert.strictEqual(edits.tidy("var x = 1\n\n\n"), "var x = 1\n");
    });

    test("runs of blank lines are cut to one", () =>
    {
        assert.strictEqual(edits.tidy("a\n\n\n\nb\n"), "a\n\nb\n");
        assert.strictEqual(edits.tidy("a\n\nb\n"), "a\n\nb\n");
    });

    test("the amount of indentation is never changed", () =>
    {
        // In Thunder the indentation is the block structure: re-indenting would be rewriting.
        const program = "func f()\n      println(1)\n";
        assert.strictEqual(edits.tidy(program), program);
    });

    test("every line ending becomes a line feed", () =>
    {
        assert.strictEqual(edits.tidy("a\r\nb\rc"), "a\nb\nc\n");
    });

    test("an empty file stays empty", () =>
    {
        assert.strictEqual(edits.tidy(""), "");
        assert.strictEqual(edits.tidy("\n\n\n"), "");
    });

    test("indentation is measured in columns", () =>
    {
        assert.strictEqual(edits.indentWidth("    x"), 4);
        assert.strictEqual(edits.indentWidth("\tx"), 4);
        assert.strictEqual(edits.indentWidth("  \tx"), 4);
        assert.strictEqual(edits.indentWidth("x"), 0);
    });
});
