// How a line of Thunder is ordered on screen in a right-to-left program.
//
// This is the same algorithm as the Qt IDE's (ide/thunder_ide/core/direction.py), ported so
// that the extension's right-to-left view lays a Pashto program out exactly as the IDE does.
// `tests/python/test_vscode_extension.py` runs both over every line of every Pashto sample and
// compares the result character for character, so the two cannot drift apart.
//
// The Unicode Bidirectional Algorithm is made for prose. Applied to code it merges a run of
// Latin names, operators and string literals into one left-to-right phrase, so the Pashto
// statement `متغیر name = "Nasir"` comes out as `"name = "Nasir متغیر` - the assignment reading
// backwards with a quote stranded at the far end. Thunder programs are therefore read **token
// by token in the direction of the program's language**: each token keeps the direction of its
// own script, and everything between tokens (operators, brackets, commas) follows the language.
// Wrapping every token in a FIRST STRONG ISOLATE ... POP DIRECTIONAL ISOLATE pair makes it an
// opaque unit for the algorithm, and the text engine does the rest.

"use strict";

const FSI = "⁨"; // first strong isolate: the token takes the direction of its first letter
const PDI = "⁩"; // pop directional isolate
const LRM = "‎"; // left-to-right mark: keeps a negative number's sign left of its digits
const MARKS = [FSI, PDI, LRM];

// The units a line is read in: comments, string literals (either quote style, possibly
// unterminated), decimal numbers and words (names, keywords and numbers in any script).
//
// A decimal number comes before the general word so that 9.81 and ۹٫۸۱ stay *one* unit: the
// decimal point is not a word character, so without this the halves would be two tokens with
// the point between them, and a right-to-left line would order them as ۸۱٫۹.
const TOKEN = new RegExp([
    "#[^\\n]*",
    "'''[\\s\\S]*?(?:'''|$)",
    "\"(?:\\\\.|[^\"\\\\\\n])*\"?",
    "«(?:\\\\.|[^»\\\\\\n])*»?",
    "[“”„](?:\\\\.|[^“”„\\\\\\n])*[“”„]?",
    "[\\p{Nd}]+[.\\u066b][\\p{Nd}]+",
    "[\\p{L}\\p{M}\\p{Nd}_]+",
].join("|"), "gu");

// Which characters the Unicode Bidirectional Algorithm reads right to left.
//
// Python's unicodedata knows every character's bidi class; JavaScript's regular expressions
// know scripts and categories but not bidi classes, so the two classes this algorithm needs
// are written out here. They were derived from unicodedata, Unicode 16.0.0 (see the test
// "the direction tables still match Python's unicodedata", which derives them again and
// compares), and they matter: the Arabic comma ، is a *separator*, not a letter, and × is a
// neutral symbol, not a Latin letter - reading either as strong would order a line wrongly.
//
// Strong left-to-right is then "a letter that is not one of these": every letter is L, R or AL,
// and a Thunder program contains no other L character.

const RIGHT_TO_LEFT = new RegExp("[" +
    "\u{5BE}\u{5C0}\u{5C3}\u{5C6}\u{5D0}-\u{5EA}\u{5EF}-\u{5F4}\u{608}\u{60B}\u{60D}\u{61B}-\u{64A}" +
    "\u{66D}-\u{66F}\u{671}-\u{6D5}\u{6E5}-\u{6E6}\u{6EE}-\u{6EF}\u{6FA}-\u{70D}\u{70F}-\u{710}" +
    "\u{712}-\u{72F}\u{74D}-\u{7A5}\u{7B1}\u{7C0}-\u{7EA}\u{7F4}-\u{7F5}\u{7FA}\u{7FE}-\u{815}\u{81A}\u{824}" +
    "\u{828}\u{830}-\u{83E}\u{840}-\u{858}\u{85E}\u{860}-\u{86A}\u{870}-\u{88E}\u{8A0}-\u{8C9}\u{200F}" +
    "\u{FB1D}\u{FB1F}-\u{FB28}\u{FB2A}-\u{FB36}\u{FB38}-\u{FB3C}\u{FB3E}\u{FB40}-\u{FB41}\u{FB43}-\u{FB44}" +
    "\u{FB46}-\u{FBC2}\u{FBD3}-\u{FD3D}\u{FD50}-\u{FD8F}\u{FD92}-\u{FDC7}\u{FDF0}-\u{FDFC}\u{FE70}-\u{FE74}" +
    "\u{FE76}-\u{FEFC}\u{10800}-\u{10805}\u{10808}\u{1080A}-\u{10835}\u{10837}-\u{10838}\u{1083C}" +
    "\u{1083F}-\u{10855}\u{10857}-\u{1089E}\u{108A7}-\u{108AF}\u{108E0}-\u{108F2}\u{108F4}-\u{108F5}" +
    "\u{108FB}-\u{1091B}\u{10920}-\u{10939}\u{1093F}\u{10980}-\u{109B7}\u{109BC}-\u{109CF}\u{109D2}-\u{10A00}" +
    "\u{10A10}-\u{10A13}\u{10A15}-\u{10A17}\u{10A19}-\u{10A35}\u{10A40}-\u{10A48}\u{10A50}-\u{10A58}" +
    "\u{10A60}-\u{10A9F}\u{10AC0}-\u{10AE4}\u{10AEB}-\u{10AF6}\u{10B00}-\u{10B35}\u{10B40}-\u{10B55}" +
    "\u{10B58}-\u{10B72}\u{10B78}-\u{10B91}\u{10B99}-\u{10B9C}\u{10BA9}-\u{10BAF}\u{10C00}-\u{10C48}" +
    "\u{10C80}-\u{10CB2}\u{10CC0}-\u{10CF2}\u{10CFA}-\u{10D23}\u{10D4A}-\u{10D65}\u{10D6F}-\u{10D85}" +
    "\u{10D8E}-\u{10D8F}\u{10E80}-\u{10EA9}\u{10EAD}\u{10EB0}-\u{10EB1}\u{10EC2}-\u{10EC4}\u{10F00}-\u{10F27}" +
    "\u{10F30}-\u{10F45}\u{10F51}-\u{10F59}\u{10F70}-\u{10F81}\u{10F86}-\u{10F89}\u{10FB0}-\u{10FCB}" +
    "\u{10FE0}-\u{10FF6}\u{1E800}-\u{1E8C4}\u{1E8C7}-\u{1E8CF}\u{1E900}-\u{1E943}\u{1E94B}\u{1E950}-\u{1E959}" +
    "\u{1E95E}-\u{1E95F}\u{1EC71}-\u{1ECB4}\u{1ED01}-\u{1ED3D}\u{1EE00}-\u{1EE03}\u{1EE05}-\u{1EE1F}" +
    "\u{1EE21}-\u{1EE22}\u{1EE24}\u{1EE27}\u{1EE29}-\u{1EE32}\u{1EE34}-\u{1EE37}\u{1EE39}\u{1EE3B}\u{1EE42}" +
    "\u{1EE47}\u{1EE49}\u{1EE4B}\u{1EE4D}-\u{1EE4F}\u{1EE51}-\u{1EE52}\u{1EE54}\u{1EE57}\u{1EE59}\u{1EE5B}" +
    "\u{1EE5D}\u{1EE5F}\u{1EE61}-\u{1EE62}\u{1EE64}\u{1EE67}-\u{1EE6A}\u{1EE6C}-\u{1EE72}\u{1EE74}-\u{1EE77}" +
    "\u{1EE79}-\u{1EE7C}\u{1EE7E}\u{1EE80}-\u{1EE89}\u{1EE8B}-\u{1EE9B}\u{1EEA1}-\u{1EEA3}\u{1EEA5}-\u{1EEA9}" +
    "\u{1EEAB}-\u{1EEBB}"
+ "]", "u");

const ARABIC_NUMBER = new RegExp("[" +
    "\u{600}-\u{605}\u{660}-\u{669}\u{66B}-\u{66C}\u{6DD}\u{890}-\u{891}\u{8E2}\u{10D30}-\u{10D39}" +
    "\u{10D40}-\u{10D49}\u{10E60}-\u{10E7E}"
+ "]", "u");

const LETTER = /\p{L}/u;

/** True if a character is strongly right-to-left (bidi class R or AL). */
function isStrongRightToLeft(character)
{
    return RIGHT_TO_LEFT.test(character);
}

/** True if a character is strongly left-to-right (bidi class L: a letter of an LTR script). */
function isStrongLeftToRight(character)
{
    return LETTER.test(character) && !RIGHT_TO_LEFT.test(character);
}

/** True if the first character with a direction reads right to left (what FSI decides). */
function isRightToLeftText(text)
{
    for (const character of String(text))
    {
        if (isStrongRightToLeft(character))
        {
            return true;
        }
        if (isStrongLeftToRight(character))
        {
            return false;
        }
    }
    return false;
}

/** True if the text holds anything the algorithm reads right to left (R, AL or AN). */
function hasRightToLeftText(text)
{
    for (const character of String(text))
    {
        if (isStrongRightToLeft(character) || ARABIC_NUMBER.test(character))
        {
            return true;
        }
    }
    return false;
}

/** True if the text has no strong direction of its own (a number, say). */
function isWeak(text)
{
    for (const character of String(text))
    {
        if (isStrongLeftToRight(character) || isStrongRightToLeft(character))
        {
            return false;
        }
    }
    return true;
}

const DIGITS = "0123456789٠١٢٣٤٥٦٧٨٩۰۱۲۳۴۵۶۷۸۹";
const DECIMAL_POINTS = ".٫";
const NUMBER_PART = DIGITS + DECIMAL_POINTS;
const BEFORE_SIGN = "=+-*/%<>(,[{:،"; // after one of these a '-' is a sign, not a subtraction

function isDigit(character)
{
    return DIGITS.includes(character);
}

/**
 * Extends number tokens to include a unary minus sign.
 *
 * Negative numbers are written sign-first in every script (an exception to the right-to-left
 * reading), so `-۳` must stay one unit; a lone `-` between tokens would follow the paragraph
 * and end up to the right of the digits.
 */
function withSigns(text, tokens)
{
    return tokens.map(([start, end]) =>
    {
        if (start > 0 && text[start - 1] === "-" && isDigit(text[start]))
        {
            const before = text.slice(0, start - 1).replace(/\s+$/u, "");
            if (!before || BEFORE_SIGN.includes(before[before.length - 1]))
            {
                return [start - 1, end];
            }
        }
        return [start, end];
    });
}

/**
 * Lays out one logical line of code for display in a program of the given direction.
 *
 * Returns {text, rightToLeft, display, offsets, tokens}: `display` is what should be drawn
 * (the line with isolate marks around every token), `offsets[i]` is the display index of
 * logical index i, and `tokens` are the isolated ranges.
 */
function analyseLine(text, rightToLeft)
{
    const line = String(text);
    const matches = [];
    TOKEN.lastIndex = 0;
    let match = TOKEN.exec(line);
    while (match)
    {
        matches.push([match.index, match.index + match[0].length]);
        if (match[0].length === 0)
        {
            ++TOKEN.lastIndex;
        }
        match = TOKEN.exec(line);
    }
    const tokens = withSigns(line, matches);

    if (!rightToLeft && !hasRightToLeftText(line))
    {
        // Pure left-to-right text needs no help: the display is the text itself.
        const offsets = [];
        for (let i = 0; i <= line.length; ++i)
        {
            offsets.push(i);
        }
        return { text: line, rightToLeft, display: line, offsets, tokens: [] };
    }

    const display = [];
    const offsets = [];
    const isolated = [];

    // Copies one token, keeping a negative number inside a comment or a string sign-first:
    // there the algorithm runs free (they are prose), so "-۳" would come out as "۳-".
    const copyToken = (start, end) =>
    {
        let marked = false;
        for (let index = start; index < end; ++index)
        {
            const character = line[index];
            if (marked && !NUMBER_PART.includes(character))
            {
                display.push(LRM);
                marked = false;
            }
            if (character === "-" && index > start && index + 1 < end && isDigit(line[index + 1])
                && !isDigit(line[index - 1]) && !marked)
            {
                display.push(LRM);
                marked = true;
            }
            offsets.push(display.length);
            display.push(character);
        }
        if (marked)
        {
            display.push(LRM);
        }
    };

    let position = 0;
    for (const [start, end] of tokens)
    {
        for (let index = position; index < start; ++index)
        {
            offsets.push(display.length);
            display.push(line[index]);
        }
        // A token with no letter of its own (a number, possibly with its sign) gets
        // left-to-right marks inside its isolate: after a Pashto word the algorithm turns the
        // digits into Arabic numbers and puts a "-" to their right. The marks sit inside the
        // isolate, so nothing leaks out of the token.
        const weak = isWeak(line.slice(start, end));
        display.push(FSI);
        if (weak)
        {
            display.push(LRM);
        }
        copyToken(start, end);
        if (weak)
        {
            display.push(LRM);
        }
        display.push(PDI);
        isolated.push({
            start,
            end,
            opposite: isRightToLeftText(line.slice(start, end)) !== rightToLeft,
        });
        position = end;
    }
    for (let index = position; index < line.length; ++index)
    {
        offsets.push(display.length);
        display.push(line[index]);
    }
    offsets.push(display.length);
    return { text: line, rightToLeft, display: display.join(""), offsets, tokens: isolated };
}

/** The logical index of a display index (the marks count as nothing). */
function toLogical(layout, displayIndex)
{
    const index = Math.max(0, Math.min(displayIndex, layout.display.length));
    let marks = 0;
    for (let i = 0; i < index; ++i)
    {
        if (MARKS.includes(layout.display[i]))
        {
            ++marks;
        }
    }
    return index - marks;
}

/** The Unicode version the tables above were derived from. */
const UNICODE_VERSION = "16.0.0";

const direction = {
    UNICODE_VERSION,
    ARABIC_NUMBER,
    FSI,
    LRM,
    MARKS,
    PDI,
    analyseLine,
    hasRightToLeftText,
    isRightToLeftText,
    isStrongLeftToRight,
    isStrongRightToLeft,
    isWeak,
    toLogical,
    withSigns,
};

// The same file runs in the extension (Node) and in the right-to-left editor's webview, so
// both lay a line out by exactly the same rules.
if (typeof module !== "undefined" && module.exports)
{
    module.exports = direction;
}
else
{
    globalThis.ThunderDirection = direction;
}
