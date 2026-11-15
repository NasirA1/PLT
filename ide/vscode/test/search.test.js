// Loose Pashto search (media/shared/search.js): the find bars of the right-to-left editor and
// the Thunder Output view match every spelling of a letter when the loose toggle is on.

"use strict";

const { assert, suite, test } = require("./harness");

const search = require("../media/shared/search");

suite("search: loose Pashto letters", () =>
{
    test("every spelling of yeh finds every other", () =>
    {
        // The rule as the user states it: searching ى must match ى, ي, ۍ, ې, ئ (and ی).
        const text = "ى ي ۍ ې ئ ی";
        for (const query of ["ى", "ي", "ۍ", "ې", "ئ", "ی"])
        {
            const matches = search.findMatches(text, query, true);
            assert.strictEqual(matches.length, 6, `${query} must find all six spellings`);
        }
        assert.deepStrictEqual(search.findMatches(text, "ې", false),
            [{ start: 6, end: 7 }], "exact mode finds only the spelling itself");
    });

    test("gaf, kaf, alef, heh and waw have their variants too", () =>
    {
        assert.strictEqual(search.findMatches("ګل گل", "گ", true).length, 2);
        assert.strictEqual(search.findMatches("کور كور", "ک", true).length, 2);
        assert.strictEqual(search.findMatches("اآأإ", "ا", true).length, 4);
        assert.strictEqual(search.findMatches("ه ۀ ھ", "ه", true).length, 3);
        assert.strictEqual(search.findMatches("و ؤ", "و", true).length, 2);
        assert.strictEqual(search.findMatches("ګل گل", "گ", false).length, 1, "exact mode keeps them apart");
    });

    test("diacritics and the tatweel are ignored, and the match spans them in the original", () =>
    {
        // وَلي carries a fathah; searching ولي must find it, and the match must cover all four
        // characters of the original text so the highlight is not cut short.
        assert.deepStrictEqual(search.findMatches("وَلي", "ولي", true), [{ start: 0, end: 4 }]);
        // A tatweel-stretched word (کـور) matches its plain spelling.
        assert.deepStrictEqual(search.findMatches("کـور", "كور", true), [{ start: 0, end: 4 }]);
        // Exact mode leaves the marks alone.
        assert.deepStrictEqual(search.findMatches("وَلي", "ولي", false), []);
    });

    test("the search never minds case, loose or not", () =>
    {
        assert.deepStrictEqual(search.findMatches("Hello WORLD", "world", false), [{ start: 6, end: 11 }]);
        assert.deepStrictEqual(search.findMatches("Hello WORLD", "HELLO", true), [{ start: 0, end: 5 }]);
    });

    test("matches come in order, never overlap, and an empty query has none", () =>
    {
        assert.deepStrictEqual(search.findMatches("aaaa", "aa", false),
            [{ start: 0, end: 2 }, { start: 2, end: 4 }]);
        assert.deepStrictEqual(search.findMatches("anything", "", true), []);
        assert.deepStrictEqual(search.findMatches("", "a", true), []);
    });

    test("a match on a later line keeps the right offsets", () =>
    {
        const text = "چاپ(«سلام»)\nمتغير ن = ۱";
        const matches = search.findMatches(text, "متغیر", true);
        assert.strictEqual(matches.length, 1);
        assert.strictEqual(text.slice(matches[0].start, matches[0].end), "متغير");
    });
});
