// Laying a line out in the direction of the program's language.
//
// The authority is the Qt IDE's implementation (ide/thunder_ide/core/direction.py); a Python
// test compares the two over every line of every Pashto sample. These tests pin the behaviour
// that matters, in a form a reader can check by eye.

"use strict";

const { assert, suite, test } = require("./harness");
const direction = require("../src/direction");

const { FSI, LRM, PDI } = direction;

/** The display string with the invisible marks spelled out, so a failure can be read. */
function visible(text)
{
    return text.replace(new RegExp(FSI, "g"), "[FSI]")
        .replace(new RegExp(PDI, "g"), "[PDI]")
        .replace(new RegExp(LRM, "g"), "[LRM]");
}

suite("direction: which way a character reads", () =>
{
    test("letters are classified by their script, not by their block", () =>
    {
        assert.ok(direction.isStrongLeftToRight("a"));
        assert.ok(direction.isStrongLeftToRight("Z"));
        assert.ok(direction.isStrongRightToLeft("ی"));
        assert.ok(direction.isStrongRightToLeft("א"));
        // The traps: Arabic punctuation and the multiplication sign are neither.
        assert.ok(!direction.isStrongRightToLeft("،"), "the Arabic comma is a separator");
        assert.ok(!direction.isStrongLeftToRight("،"));
        assert.ok(!direction.isStrongLeftToRight("×"), "× is a symbol, not a Latin letter");
        assert.ok(!direction.isStrongRightToLeft("×"));
        assert.ok(!direction.isStrongRightToLeft("۵"), "a Pashto digit is a number, not a letter");
        assert.ok(!direction.isStrongLeftToRight("5"));
    });

    test("the direction of a token is its first strong letter", () =>
    {
        assert.ok(direction.isRightToLeftText("نوم"));
        assert.ok(direction.isRightToLeftText("\"متن\""), "the quote is neutral, the letters are not");
        assert.ok(!direction.isRightToLeftText("name"));
        assert.ok(!direction.isRightToLeftText("۱۲۳"), "digits alone have no direction");
        assert.ok(!direction.isRightToLeftText(""));
        assert.ok(direction.isRightToLeftText("# یادښت"));
    });

    test("a token with no letter of its own is weak", () =>
    {
        assert.ok(direction.isWeak("123"));
        assert.ok(direction.isWeak("-۴۲"));
        assert.ok(direction.isWeak("\"، \""), "an Arabic comma in a string is still weak");
        assert.ok(!direction.isWeak("name"));
        assert.ok(!direction.isWeak("نوم"));
    });

    test("a line is known to hold right-to-left text", () =>
    {
        assert.ok(direction.hasRightToLeftText("var نوم = 1"));
        assert.ok(!direction.hasRightToLeftText("var name = 1"));
        assert.ok(!direction.hasRightToLeftText("۱۲۳"), "Pashto digits are read left to right");
        assert.ok(direction.hasRightToLeftText("٠١٢"), "Arabic-Indic digits are read right to left");
    });
});

suite("direction: laying out a line", () =>
{
    test("an English line in an English program is left alone", () =>
    {
        const layout = direction.analyseLine("var total = 1", false);
        assert.strictEqual(layout.display, "var total = 1");
        assert.deepStrictEqual(layout.tokens, []);
        assert.strictEqual(layout.offsets.length, "var total = 1".length + 1);
    });

    test("every token of a Pashto line is isolated", () =>
    {
        const layout = direction.analyseLine("متغیر نوم = \"ناصر\"", true);
        assert.strictEqual(visible(layout.display),
            "[FSI]متغیر[PDI] [FSI]نوم[PDI] = [FSI]\"ناصر\"[PDI]");
        assert.deepStrictEqual(layout.tokens.map((token) => [token.start, token.end]),
            [[0, 5], [6, 9], [12, 18]]);
        assert.ok(layout.tokens.every((token) => !token.opposite), "all three read right to left");
    });

    test("a Latin name in a Pashto program keeps its own direction", () =>
    {
        const layout = direction.analyseLine("متغیر name = 1", true);
        assert.strictEqual(visible(layout.display), "[FSI]متغیر[PDI] [FSI]name[PDI] = [FSI][LRM]1[LRM][PDI]");
        assert.deepStrictEqual(layout.tokens.map((token) => token.opposite), [false, true, true]);
    });

    test("a negative number keeps its sign on the left", () =>
    {
        // The sign is merged into the number so the pair stays one unit...
        const layout = direction.analyseLine("متغیر ش = -۳", true);
        assert.strictEqual(visible(layout.display), "[FSI]متغیر[PDI] [FSI]ش[PDI] = [FSI][LRM]-۳[LRM][PDI]");
        // ... but a subtraction is not a sign.
        const subtraction = direction.analyseLine("متغیر ش = الف-۳", true);
        assert.ok(visible(subtraction.display).includes("[FSI]الف[PDI]-[FSI][LRM]۳[LRM][PDI]"),
            visible(subtraction.display));
    });

    test("a negative number inside a comment or a string keeps its sign too", () =>
    {
        const layout = direction.analyseLine("# درجه -۵ ده", true);
        assert.strictEqual(visible(layout.display), "[FSI]# درجه [LRM]-۵[LRM] ده[PDI]");
    });

    test("a decimal number stays one unit", () =>
    {
        const layout = direction.analyseLine("متغیر ع = ۹٫۸۱", true);
        assert.strictEqual(visible(layout.display), "[FSI]متغیر[PDI] [FSI]ع[PDI] = [FSI][LRM]۹٫۸۱[LRM][PDI]");
        assert.deepStrictEqual(layout.tokens[2], { start: 10, end: 14, opposite: true });
    });

    test("display positions map back to the text", () =>
    {
        const text = "متغیر نوم = ۱";
        const layout = direction.analyseLine(text, true);
        for (let index = 0; index <= text.length; ++index)
        {
            assert.strictEqual(direction.toLogical(layout, layout.offsets[index]), index, `index ${index}`);
        }
        assert.strictEqual(layout.display[layout.offsets[0]], "م");
        assert.strictEqual(layout.display[layout.offsets[6]], "ن");
    });

    test("an empty line and a line of spaces are safe", () =>
    {
        assert.strictEqual(direction.analyseLine("", true).display, "");
        assert.strictEqual(direction.analyseLine("   ", true).display, "   ");
        assert.deepStrictEqual(direction.analyseLine("", true).offsets, [0]);
    });

    test("a Pashto name in an English program is isolated the other way round", () =>
    {
        const layout = direction.analyseLine("var نوم = 1", false);
        assert.strictEqual(visible(layout.display), "[FSI]var[PDI] [FSI]نوم[PDI] = [FSI][LRM]1[LRM][PDI]");
        assert.deepStrictEqual(layout.tokens.map((token) => token.opposite), [false, true, false]);
    });
});
