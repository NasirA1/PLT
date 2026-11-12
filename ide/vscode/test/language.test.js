// What the extension makes of `thunderc --language-info`. The bundled copies in data/ are
// used, so these tests run without the toolchain; a separate Python test checks that those
// copies still match the compiler.

"use strict";

const fs = require("fs");
const path = require("path");

const { EXTENSION, assert, before, suite, test } = require("./harness");
const language = require("../src/language");

function load(code)
{
    return new language.LanguageData(
        JSON.parse(fs.readFileSync(path.join(EXTENSION, "data", `language-info.${code}.json`), "utf8")));
}

suite("language: keywords, built-ins and signatures", () =>
{
    let english = null;
    let pashto = null;

    before(() =>
    {
        english = load("en");
        pashto = load("ps");
    });

    test("the English pack has the keywords and built-ins of the language", () =>
    {
        assert.strictEqual(english.code, "en");
        assert.ok(!english.rightToLeft);
        assert.ok(english.keywords.some((keyword) => keyword.text === "func"));
        assert.ok(english.builtinNamed("println"));
        assert.ok(english.builtinNamed("len"));
        assert.strictEqual(english.comma, ",");
    });

    test("the Pashto pack is right to left and writes its own comma", () =>
    {
        assert.strictEqual(pashto.code, "ps");
        assert.ok(pashto.rightToLeft);
        assert.strictEqual(pashto.comma, "،");
        assert.ok(pashto.keywordNamed("متغیر"), "متغیر is a keyword");
        assert.ok(pashto.keywordNamed("ودریږه"), "an alternative spelling is accepted too");
    });

    test("a built-in's signature is shown under its own name", () =>
    {
        const println = pashto.builtinNamed("println");
        assert.strictEqual(println.name, "کرښه چاپ");
        assert.ok(pashto.signatureOf(println).startsWith("کرښه چاپ("));
        assert.strictEqual(english.signatureOf(english.builtinNamed("println")), "println(value, ...)");
    });

    test("the parameters of a signature split at the top level only", () =>
    {
        assert.deepStrictEqual(language.splitParameters("len(x: string | T[] | tuple | map): int"),
            ["x: string | T[] | tuple | map"]);
        assert.deepStrictEqual(language.splitParameters("f(a: {string: int}, b: (int, string)): bool"),
            ["a: {string: int}", "b: (int, string)"]);
        assert.deepStrictEqual(language.splitParameters("pi(): float"), []);
        assert.deepStrictEqual(language.splitParameters("no brackets"), []);
    });

    test("module built-ins are offered only once the module is imported", () =>
    {
        const withoutImport = language.completionWords(english, { imported: new Set() });
        assert.ok(!withoutImport.some((word) => word.label === "platform"), "os is not imported yet");
        assert.ok(withoutImport.some((word) => word.label === "println"));

        const withImport = language.completionWords(english, { imported: new Set(["os"]) });
        assert.ok(withImport.some((word) => word.label === "platform"));
        assert.ok(!withImport.some((word) => word.label === "circle"), "graphics is still not imported");

        const always = language.completionWords(english, { imported: new Set(), moduleBuiltins: "always" });
        assert.ok(always.some((word) => word.label === "circle"));
    });

    test("completion offers keywords, types, literals and the modules", () =>
    {
        const words = language.completionWords(pashto, { imported: new Set() });
        const kinds = new Map(words.map((word) => [word.label, word.kind]));
        assert.strictEqual(kinds.get("متغیر"), "keyword");
        assert.strictEqual(kinds.get("عدد"), "type");
        assert.strictEqual(kinds.get("رښتیا"), "literal");
        assert.strictEqual(kinds.get("ګرافیک"), "module");
        assert.strictEqual(kinds.get("کرښه چاپ"), "builtin");
        // The built-ins of the language come before its keywords in the list.
        const builtin = words.find((word) => word.label === "کرښه چاپ");
        const keyword = words.find((word) => word.label === "متغیر");
        assert.ok(builtin.sortText < keyword.sortText);
    });

    test("every spelling of every reserved word is known", () =>
    {
        const words = pashto.reservedWords();
        assert.strictEqual(words.get("متغیر").category, "keyword");
        assert.strictEqual(words.get("عدد").category, "type");
        assert.strictEqual(words.get("او").category, "operator");
        assert.strictEqual(words.get("کرښه چاپ").category, "builtin");
        assert.strictEqual(words.get("دروغ").category, "literal", "the old spelling is still accepted");
    });

    test("a word is described with its counterpart in the other language", () =>
    {
        const others = new Map([["en", english], ["ps", pashto]]);
        const builtin = language.describeWord(english, "println", others);
        assert.strictEqual(builtin.kind, "builtin");
        assert.ok(builtin.body.length > 0, "the compiler's own description is shown");
        assert.deepStrictEqual(builtin.translations.map((entry) => entry.name), ["کرښه چاپ"]);

        const keyword = language.describeWord(pashto, "که", others);
        assert.strictEqual(keyword.kind, "keyword");
        assert.deepStrictEqual(keyword.translations.map((entry) => entry.name), ["if"]);

        const module = language.describeWord(english, "graphics", others);
        assert.strictEqual(module.kind, "module");

        assert.strictEqual(language.describeWord(english, "not_a_word", others), null);
    });

    test("an alternative spelling is described as the word it means", () =>
    {
        const keyword = language.describeWord(pashto, "ودریږه", new Map());
        assert.strictEqual(keyword.title, "ودرېږه");
        assert.ok(keyword.spellings.includes("ودریږه"));
    });

    test("the instruction set is there for the bytecode views", () =>
    {
        assert.ok(english.opcodes.length > 40);
        const halt = english.opcodeNamed("HALT");
        assert.ok(halt && halt.description);
    });
});
