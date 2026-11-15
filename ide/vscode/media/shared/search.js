// Searching Thunder text, with an optional loose mode for Pashto.
//
// The Arabic script spells several Pashto letters in more than one way: a yeh may be written
// ی, ي, ى, ۍ, ې or ئ depending on the keyboard, the writer and the position in the word, and
// the same goes for gaf (ګ/گ), kaf (ک/ك), heh and alef. Searching for one spelling and missing
// the others makes find useless for Pashto, so the find bars of the right-to-left editor and
// the Thunder Output view offer a *loose* mode: every letter matches every variant of itself,
// and the diacritics (harakat) and the tatweel are ignored.
//
// It is loaded both by Node (`require`, for the tests) and by the webviews (a <script> tag),
// so it uses no modules: in the browser it hangs itself on `globalThis.ThunderSearch`.

"use strict";

(function factory(root)
{
    // Each string is one letter with every spelling of it; the first is the representative.
    // The yeh row is the user-facing rule: searching any of ی ي ى ۍ ې ئ finds all of them.
    const LETTER_CLASSES = [
        "یيىۍېئ",   // the yeh family
        "کك",       // kaf and the Arabic-keyboard kaf
        "ګگ",       // the Pashto gaf and the Farsi gaf
        "اأإآٱ",    // alef with its hamza and madda forms
        "هھہةۀە",   // heh in its regional forms, and teh marbuta
        "وؤ",       // waw, with and without hamza
    ];

    // Ignored in loose mode: the Arabic diacritics (fathah, dammah, kasrah, shaddah, sukun,
    // tanwin, the superscript alef and the Quranic marks) and the tatweel (ـ, a joining filler).
    const IGNORED = /[\u0610-\u061A\u0640\u064B-\u065F\u0670\u06D6-\u06DC\u06DF-\u06E4\u06E7\u06E8\u06EA-\u06ED]/;

    const CLASS_OF = new Map();
    for (const letters of LETTER_CLASSES)
    {
        for (const letter of letters)
        {
            CLASS_OF.set(letter, letters[0]);
        }
    }

    /** A character as it is compared in loose mode: its class's representative, lower-cased. */
    function looseCharacter(character)
    {
        return CLASS_OF.get(character) || character.toLowerCase();
    }

    /**
     * A text as it is compared, with the way back: {folded, map}, where map[i] is the offset in
     * `text` of folded[i]. In loose mode the ignored marks disappear, so folded may be shorter.
     */
    function fold(text, loose)
    {
        const source = String(text);
        let folded = "";
        const map = [];
        for (let index = 0; index < source.length; ++index)
        {
            const character = source[index];
            if (loose && IGNORED.test(character))
            {
                continue;
            }
            folded += loose ? looseCharacter(character) : character.toLowerCase();
            map.push(index);
        }
        return { folded, map };
    }

    /**
     * Every match of `query` in `text`, as [{start, end}] offsets of `text`, first to last.
     * The search never minds case; with `loose` it also never minds the spelling of a Pashto
     * letter or the diacritics. Matches do not overlap. An empty query has no matches.
     */
    function findMatches(text, query, loose)
    {
        const haystack = fold(text, loose);
        const needle = fold(query, loose).folded;
        if (!needle)
        {
            return [];
        }
        const matches = [];
        let at = haystack.folded.indexOf(needle);
        while (at >= 0)
        {
            const last = at + needle.length - 1;
            matches.push({ start: haystack.map[at], end: haystack.map[last] + 1 });
            at = haystack.folded.indexOf(needle, at + needle.length);
        }
        return matches;
    }

    const search = { LETTER_CLASSES, findMatches, fold, looseCharacter };
    if (typeof module !== "undefined" && module.exports)
    {
        module.exports = search;
    }
    else
    {
        root.ThunderSearch = search;
    }
}(typeof globalThis !== "undefined" ? globalThis : this));
