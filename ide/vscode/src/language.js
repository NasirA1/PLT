// What the compiler knows about the language: keywords, built-in functions, modules and the
// instruction set, as `thunderc --language-info` reports them for one natural language.
//
// The extension asks the toolchain itself, so a new keyword or built-in appears in completion
// and hovers without a line of editor code changing. A copy of the answer is bundled in
// data/language-info.<code>.json and used until the toolchain has been built, so a fresh
// checkout still highlights and completes.
//
// Pure: no VS Code API, no file system (the caller hands in the parsed JSON).

"use strict";

/** The kinds of word a position can be completed with. */
const COMPLETION_KEYWORD = "keyword";
const COMPLETION_TYPE = "type";
const COMPLETION_LITERAL = "literal";
const COMPLETION_BUILTIN = "builtin";
const COMPLETION_MODULE = "module";

class LanguageData
{
    constructor(info)
    {
        this.info = info || {};
        this._builtinsByName = null;
    }

    get code()
    {
        return this.info.code || "en";
    }

    get nativeName()
    {
        return this.info.native_name || this.info.name || this.code;
    }

    get rightToLeft()
    {
        return Boolean(this.info.right_to_left);
    }

    get keywords()
    {
        return this.info.keywords || [];
    }

    get builtins()
    {
        return this.info.builtins || [];
    }

    get modules()
    {
        return this.info.modules || [];
    }

    get opcodes()
    {
        return this.info.opcodes || [];
    }

    get languages()
    {
        return this.info.languages || [];
    }

    /** The comma of this language ("," in English, "،" in Pashto). */
    get comma()
    {
        const mark = (this.info.punctuation || []).find((entry) => entry.means === ",");
        return mark ? mark.text : ",";
    }

    /** Every spelling of every reserved word, mapped to what it is. */
    reservedWords()
    {
        const words = new Map();
        for (const keyword of this.keywords)
        {
            const category = ["keyword", "type", "literal", "operator"].includes(keyword.category)
                ? keyword.category
                : "keyword";
            for (const spelling of (keyword.spellings || [keyword.text]))
            {
                words.set(spelling, { category, kind: keyword.kind, text: keyword.text });
            }
        }
        for (const builtin of this.builtins)
        {
            words.set(builtin.name, { category: "builtin", kind: builtin.canonical_name, text: builtin.name });
        }
        return words;
    }

    builtinNamed(name)
    {
        if (!this._builtinsByName)
        {
            this._builtinsByName = new Map();
            for (const builtin of this.builtins)
            {
                this._builtinsByName.set(builtin.name, builtin);
                this._builtinsByName.set(builtin.canonical_name, builtin);
            }
        }
        return this._builtinsByName.get(name) || null;
    }

    keywordNamed(name)
    {
        for (const keyword of this.keywords)
        {
            if ((keyword.spellings || [keyword.text]).includes(name))
            {
                return keyword;
            }
        }
        return null;
    }

    opcodeNamed(mnemonic)
    {
        return this.opcodes.find((opcode) => opcode.mnemonic === mnemonic) || null;
    }

    /**
     * A built-in's signature under its name in this language.
     *
     * The compiler reports the canonical signature (`println(value, ...)`) in every language,
     * because the types in it are the language's own words; only the function's name is
     * translated, so `کرښه چاپ` shows as `کرښه چاپ(value, ...)`.
     */
    signatureOf(builtin)
    {
        if (!builtin)
        {
            return "";
        }
        const signature = builtin.signature || `${builtin.canonical_name}()`;
        if (builtin.name === builtin.canonical_name || !signature.startsWith(builtin.canonical_name))
        {
            return signature;
        }
        return builtin.name + signature.slice(builtin.canonical_name.length);
    }

    /** The parameters of a built-in, for signature help. */
    parametersOf(builtin)
    {
        return splitParameters(this.signatureOf(builtin));
    }
}

/**
 * The parameter list inside a signature, split at the top level only.
 *
 * `map(xs: T[], f: func(T): U): U[]` has two parameters, not three: the comma inside
 * `func(T): U` belongs to the function type, and the same goes for `{string: int}` and
 * `(int, string)`.
 */
function splitParameters(signature)
{
    const open = signature.indexOf("(");
    if (open < 0)
    {
        return [];
    }
    let depth = 0;
    let end = -1;
    for (let i = open; i < signature.length; ++i)
    {
        const character = signature[i];
        if ("([{".includes(character))
        {
            ++depth;
        }
        else if (")]}".includes(character))
        {
            --depth;
            if (depth === 0)
            {
                end = i;
                break;
            }
        }
    }
    if (end < 0)
    {
        return [];
    }
    const inside = signature.slice(open + 1, end);
    if (!inside.trim())
    {
        return [];
    }
    const parameters = [];
    let current = "";
    depth = 0;
    for (const character of inside)
    {
        if ("([{".includes(character))
        {
            ++depth;
        }
        else if (")]}".includes(character))
        {
            --depth;
        }
        if ((character === "," || character === "،") && depth === 0)
        {
            parameters.push(current.trim());
            current = "";
            continue;
        }
        current += character;
    }
    if (current.trim())
    {
        parameters.push(current.trim());
    }
    return parameters;
}

/**
 * The words that can be completed at a place in a program.
 *
 * `options`:
 *   imported      Set of the canonical names of the built-in modules the file imports
 *   moduleBuiltins "whenImported" (default) or "always"
 *
 * Returns plain objects {label, kind, detail, documentation, sortText, insertText}; the
 * provider turns them into VS Code completion items.
 */
function completionWords(language, options)
{
    const settings = options || {};
    const imported = settings.imported || new Set();
    const always = settings.moduleBuiltins === "always";
    const items = [];
    for (const keyword of language.keywords)
    {
        const category = keyword.category === "type"
            ? COMPLETION_TYPE
            : (keyword.category === "literal" ? COMPLETION_LITERAL : COMPLETION_KEYWORD);
        items.push({
            label: keyword.text,
            kind: category,
            detail: describeCategory(keyword.category),
            documentation: otherSpellings(keyword),
            sortText: "2" + keyword.text,
        });
    }
    for (const module of language.modules)
    {
        items.push({
            label: module.name,
            kind: COMPLETION_MODULE,
            detail: `module ${module.canonical_name}`,
            documentation: "",
            sortText: "3" + module.name,
        });
    }
    for (const builtin of language.builtins)
    {
        if (builtin.module && !always && !imported.has(builtin.module))
        {
            continue;
        }
        const parameters = language.parametersOf(builtin);
        items.push({
            label: builtin.name,
            kind: COMPLETION_BUILTIN,
            detail: language.signatureOf(builtin),
            documentation: builtin.description || "",
            module: builtin.module || "",
            sortText: (builtin.module ? "4" : "1") + builtin.name,
            parameters,
        });
    }
    return items;
}

function describeCategory(category)
{
    switch (category)
    {
    case "type":
        return "type";
    case "literal":
        return "literal";
    case "operator":
        return "operator";
    default:
        return "keyword";
    }
}

function otherSpellings(keyword)
{
    const spellings = (keyword.spellings || []).filter((spelling) => spelling !== keyword.text);
    return spellings.length ? `also written ${spellings.join(", ")}` : "";
}

/**
 * What to show when the pointer rests on a word: a built-in's signature and description, or a
 * keyword with its category, its other spellings and what it is called in the other languages.
 *
 * `others` maps a language code to its LanguageData, so an English program can show the Pashto
 * word and the other way round - the one thing a reader of a bilingual language most wants.
 */
function describeWord(language, word, others)
{
    const builtin = language.builtinNamed(word);
    if (builtin)
    {
        const translations = translationsOfBuiltin(builtin, language, others);
        return {
            kind: "builtin",
            title: language.signatureOf(builtin),
            body: builtin.description || "",
            module: builtin.module || "",
            translations,
        };
    }
    const keyword = language.keywordNamed(word);
    if (keyword)
    {
        return {
            kind: "keyword",
            title: keyword.text,
            body: describeCategory(keyword.category),
            spellings: (keyword.spellings || []).filter((spelling) => spelling !== keyword.text),
            translations: translationsOfKeyword(keyword, language, others),
        };
    }
    const module = language.modules.find((entry) => entry.name === word || entry.canonical_name === word);
    if (module)
    {
        return { kind: "module", title: module.name, body: `built-in module ${module.canonical_name}`, translations: [] };
    }
    return null;
}

function translationsOfBuiltin(builtin, language, others)
{
    const result = [];
    for (const [code, other] of (others || new Map()))
    {
        if (code === language.code)
        {
            continue;
        }
        const match = other.builtins.find((entry) => entry.canonical_name === builtin.canonical_name);
        if (match && match.name !== builtin.name)
        {
            result.push({ code, name: match.name, language: other.nativeName });
        }
    }
    return result;
}

function translationsOfKeyword(keyword, language, others)
{
    const result = [];
    for (const [code, other] of (others || new Map()))
    {
        if (code === language.code)
        {
            continue;
        }
        const match = other.keywords.find((entry) => entry.kind === keyword.kind);
        if (match && match.text !== keyword.text)
        {
            result.push({ code, name: match.text, language: other.nativeName });
        }
    }
    return result;
}

module.exports = {
    COMPLETION_BUILTIN,
    COMPLETION_KEYWORD,
    COMPLETION_LITERAL,
    COMPLETION_MODULE,
    COMPLETION_TYPE,
    LanguageData,
    completionWords,
    describeWord,
    splitParameters,
};
