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
            // A member of a module (sqrt in math.sqrt) is coloured by its own word, after the dot -
            // unless the word is a keyword, which it then stays (random.int: int is a type word).
            const word = builtin.member || builtin.name;
            if (!words.has(word))
            {
                words.set(word, { category: "builtin", kind: builtin.canonical_name, text: builtin.name });
            }
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
     * The compiler reports each built-in's signature in the program's language - Pashto ones
     * are Pashto through and through (`ریاضي.جذر(س: اعشاري): اعشاري`) - and the English one as
     * `canonical_signature`. An older toolchain reports the English signature only; then the
     * function's name is translated here and localiseTypes puts the type words into the
     * language.
     */
    signatureOf(builtin)
    {
        if (!builtin)
        {
            return "";
        }
        const signature = builtin.signature || `${builtin.canonical_name}()`;
        const named = builtin.name !== builtin.canonical_name && signature.startsWith(builtin.canonical_name)
            ? builtin.name + signature.slice(builtin.canonical_name.length)
            : signature;
        return this.localiseTypes(named);
    }

    /**
     * A signature as the language writes it: the compiler records built-in signatures in English
     * (`file_exists(path: string): bool`), so the type words, the library's enum types and the
     * comma are put into this language (`فایل شته(path: متن): منطقي`,
     * `سیسټم.عامل سیسټم(): پلیټفارم`). Parameter names have no translation and stay.
     */
    localiseTypes(signature)
    {
        const canonical = {
            KwInt: "int", KwFloat: "float", KwBool: "bool", KwString: "string", KwByte: "byte", KwFunc: "func",
        };
        const words = new Map();
        for (const keyword of this.keywords)
        {
            if (canonical[keyword.kind] && keyword.text !== canonical[keyword.kind])
            {
                words.set(canonical[keyword.kind], keyword.text);
            }
        }
        for (const enumeration of (this.info.enums || []))
        {
            if (enumeration.canonical_name && enumeration.name && enumeration.name !== enumeration.canonical_name)
            {
                words.set(enumeration.canonical_name, enumeration.name);
            }
        }
        if (!words.size)
        {
            return signature;
        }
        // One pass, longest words first, so a replacement is never replaced again.
        const alternatives = [...words.keys()].sort((a, b) => b.length - a.length);
        const pattern = new RegExp(`\\b(${alternatives.join("|")})\\b`, "g");
        const comma = this.comma;
        return signature.replace(pattern, (word) => words.get(word)).replace(/,/g, comma);
    }

    /** The spelling of a keyword in this language (`var` -> `متغیر`), by its token kind. */
    keywordText(kind, fallback)
    {
        const keyword = this.keywords.find((entry) => entry.kind === kind);
        return keyword ? keyword.text : fallback;
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
            detail: describeCategory(keyword.category, language),
            documentation: otherSpellings(keyword, language),
            sortText: "2" + keyword.text,
            keyword,
        });
    }
    for (const module of language.modules)
    {
        items.push({
            label: module.name,
            kind: COMPLETION_MODULE,
            detail: language.code === "en" ? phrase(language, "module", { name: module.canonical_name }) : phrase(language, "module"),
            documentation: "",
            sortText: "3" + module.name,
            module,
        });
    }
    for (const builtin of language.builtins)
    {
        // A module's members are offered after its name and a dot (math. -> sqrt, pi, ...).
        if (builtin.module)
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
            builtin,
        });
    }
    return items;
}

/**
 * The members of a built-in module, for completion after "math." or "ریاضي.": its functions
 * (with their parameters) and its enums. `name` may be the module's name in the program's
 * language or in English. Null when the name is no built-in module.
 */
function moduleMemberWords(language, name)
{
    const module = language.modules.find((entry) => entry.name === name || entry.canonical_name === name);
    if (!module)
    {
        return null;
    }
    const items = [];
    for (const builtin of language.builtins)
    {
        if (builtin.module !== module.canonical_name)
        {
            continue;
        }
        items.push({
            label: builtin.member || builtin.name,
            kind: COMPLETION_BUILTIN,
            detail: language.signatureOf(builtin),
            documentation: builtin.description || "",
            module: "",
            sortText: "1" + (builtin.member || builtin.name),
            parameters: language.parametersOf(builtin),
            builtin,
        });
    }
    for (const enumeration of (language.info.enums || []))
    {
        if (enumeration.module === module.canonical_name)
        {
            items.push({ label: enumeration.name, kind: COMPLETION_TYPE, detail: language.keywordText("KwEnum", phrase(language, "enum")),
                documentation: enumeration.description || "", sortText: "2" + enumeration.name, enumeration });
        }
    }
    return items;
}

// The words the editor writes around a program's names (completion details, hovers), in the
// program's language: a Pashto program's list and tooltips carry no English.
const PHRASES = {
    en: {
        type: "type", literal: "literal", operator: "operator", keyword: "keyword",
        also: "also written {list}", module: "module {name}", builtinModule: "built-in module {name}",
        inModule: "(module {name})", fieldOf: "field of `{name}`", memberOf: "member {n} of `{name}`",
        of: "of `{name}`", native: "a library written in C++", moduleFile: "module",
        global: "global", local: "local", parameter: "parameter", capture: "captured", enum: "enum",
    },
    ps: {
        type: "ډول", literal: "لفظي ارزښت", operator: "عملګر", keyword: "کلیدي کلمه",
        also: "دا هم لیکل کېږي: {list}", module: "ټولګی", builtinModule: "دننی ټولګی",
        inModule: "(ټولګی {name})", fieldOf: "د `{name}` برخه", memberOf: "د `{name}` غړی، ځای {n}",
        of: "د `{name}`", native: "بهرنی کتابتون", moduleFile: "ټولګی",
        global: "ټولنیز متغیر", local: "ځایی متغیر", parameter: "پارامټر", capture: "نیول شوی متغیر", enum: "ګڼنه",
    },
};

/** A phrase in the language's words, with {name}-style values filled in (digits in its own). */
function phrase(language, key, values)
{
    const table = PHRASES[language && language.code] || PHRASES.en;
    let text = table[key] !== undefined ? table[key] : (PHRASES.en[key] !== undefined ? PHRASES.en[key] : key);
    for (const [name, value] of Object.entries(values || {}))
    {
        text = text.split(`{${name}}`).join(String(value));
    }
    return text;
}

/** A whole number written in the language's digits (۳ in Pashto). */
function localDigits(language, number)
{
    const digits = language && language.info && language.info.digits;
    return Array.isArray(digits) && digits.length === 10
        ? String(number).replace(/[0-9]/g, (digit) => digits[Number(digit)])
        : String(number);
}

function describeCategory(category, language)
{
    switch (category)
    {
    case "type":
        return phrase(language, "type");
    case "literal":
        return phrase(language, "literal");
    case "operator":
        return phrase(language, "operator");
    default:
        return phrase(language, "keyword");
    }
}

function otherSpellings(keyword, language)
{
    const spellings = (keyword.spellings || []).filter((spelling) => spelling !== keyword.text);
    return spellings.length ? phrase(language, "also", { list: spellings.join(`${language ? language.comma : ","} `) }) : "";
}

/**
 * What a completion word is called in the other languages: [{code, name, language}] - a
 * built-in or an enum by its canonical name, a module by its canonical name, a keyword by its
 * token kind. (A name the program declares is the same in every language: none.)
 */
function translationsOfWord(word, language, others)
{
    if (word.builtin)
    {
        return translationsOfBuiltin(word.builtin, language, others);
    }
    if (word.keyword)
    {
        return translationsOfKeyword(word.keyword, language, others);
    }
    const result = [];
    for (const [code, other] of (others || new Map()))
    {
        if (code === language.code)
        {
            continue;
        }
        const match = word.module
            ? other.modules.find((entry) => entry.canonical_name === word.module.canonical_name)
            : (word.enumeration ? (other.info.enums || []).find((entry) => entry.canonical_name === word.enumeration.canonical_name) : null);
        const own = word.module || word.enumeration;
        if (match && own && match.name !== own.name)
        {
            result.push({ code, name: match.name, language: other.nativeName });
        }
    }
    return result;
}

/** The lines that end a completion item's documentation: "English: `println`". */
function translationLines(translations)
{
    return (translations || []).map((translation) => `${translation.language}: \`${translation.name}\``).join("  \n");
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
            body: describeCategory(keyword.category, language),
            spellings: (keyword.spellings || []).filter((spelling) => spelling !== keyword.text),
            translations: translationsOfKeyword(keyword, language, others),
        };
    }
    const module = language.modules.find((entry) => entry.name === word || entry.canonical_name === word);
    if (module)
    {
        return { kind: "module", title: module.name,
            body: language.code === "en" ? phrase(language, "builtinModule", { name: module.canonical_name }) : phrase(language, "builtinModule"),
            translations: translationsOfWord({ module }, language, others) };
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
    LanguageData,
    completionWords,
    moduleMemberWords,
    describeWord,
    localDigits,
    phrase,
    translationLines,
    translationsOfWord,
    splitParameters,
};
