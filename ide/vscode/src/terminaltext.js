// Test transcripts for VS Code's Test Results panel.
//
// The panel is a terminal (xterm.js), and a terminal has no bidirectional text: it lays every
// character out left to right in the order it arrives, so a Pashto line comes out backwards
// and starts at the left edge. The Thunder IDE's Output panel shows the same transcript right
// to left and right-aligned; here the line is therefore put into *visual* order before it is
// written - the order the Unicode Bidirectional Algorithm would give it in a right-to-left
// paragraph - and moved against the right edge with cursor movements (a jump to the last
// column, then back by the line's width), so it is aligned whatever the panel's width.
//
// The transcript's marks are coloured as the Thunder IDE colours them: ✓ green, ✗ red, and a
// summary line ("3 passed, 0 failed", "۳ تېرې شوې، ۰ ناکامې") green when nothing failed and
// red otherwise.

"use strict";

const direction = require("./direction");

const GREEN = "\u001b[32m";
const RED = "\u001b[31m";
const BOLD = "\u001b[1m";
const RESET = "\u001b[0m";

const MIRRORED = { "(": ")", ")": "(", "[": "]", "]": "[", "{": "}", "}": "{", "<": ">", ">": "<", "«": "»", "»": "«" };
const NUMBER_SEPARATORS = ".,:/٫٬";
const ZERO_WIDTH = /[\p{M}\u200B-\u200F\u202A-\u202E\u2066-\u2069]/u;

function isNumber(character)
{
    return /[0-9٠-٩۰-۹]/u.test(character);
}

/**
 * The visual order of one line in a right-to-left paragraph: right-to-left letters, and the
 * spaces and punctuation between them, read from the right; runs of Latin letters and of
 * digits keep their own left-to-right order; brackets and quotes in the right-to-left parts
 * are mirrored. (The parts of the Bidirectional Algorithm a transcript needs: no embeddings.)
 */
function visualOrder(line)
{
    const characters = Array.from(line);
    // Levels: 1 for right to left (the paragraph's own), 2 for left to right.
    const kinds = characters.map((character) =>
    {
        if (direction.isStrongRightToLeft(character))
        {
            return "R";
        }
        if (isNumber(character))
        {
            return "N";
        }
        if (/\p{L}/u.test(character))
        {
            return "L";
        }
        if (/\p{M}/u.test(character))
        {
            return "M";
        }
        return "";
    });
    // A combining mark takes its letter's kind; a separator inside a number is part of it.
    for (let i = 0; i < kinds.length; ++i)
    {
        if (kinds[i] === "M")
        {
            kinds[i] = i > 0 ? kinds[i - 1] : "R";
        }
        else if (!kinds[i] && NUMBER_SEPARATORS.includes(characters[i]) && kinds[i - 1] === "N" && kinds[i + 1] === "N")
        {
            kinds[i] = "N";
        }
    }
    // A number after Latin text belongs to it (rule W7): "ab 12" is one left-to-right run.
    let last = "R";
    for (let i = 0; i < kinds.length; ++i)
    {
        if (kinds[i] === "L" || kinds[i] === "R")
        {
            last = kinds[i];
        }
        else if (kinds[i] === "N" && last === "L")
        {
            kinds[i] = "L";
        }
    }
    const strongBefore = (index) =>
    {
        for (let i = index - 1; i >= 0; --i)
        {
            if (kinds[i])
            {
                return kinds[i] === "L" ? "L" : "R"; // numbers count as right to left for neutrals
            }
        }
        return "R";
    };
    const strongAfter = (index) =>
    {
        for (let i = index + 1; i < kinds.length; ++i)
        {
            if (kinds[i])
            {
                return kinds[i] === "L" ? "L" : "R";
            }
        }
        return "R";
    };
    const levels = kinds.map((kind, index) =>
    {
        if (kind === "R")
        {
            return 1;
        }
        if (kind === "L" || kind === "N")
        {
            return 2;
        }
        return strongBefore(index) === "L" && strongAfter(index) === "L" ? 2 : 1;
    });
    // Reverse every left-to-right run, then the whole line: the runs read left to right again.
    const ordered = [];
    for (let i = 0; i < characters.length;)
    {
        if (levels[i] === 2)
        {
            let end = i;
            while (end < characters.length && levels[end] === 2)
            {
                ++end;
            }
            ordered.push(...characters.slice(i, end).reverse());
            i = end;
        }
        else
        {
            ordered.push(MIRRORED[characters[i]] || characters[i]);
            ++i;
        }
    }
    return ordered.reverse().join("");
}

/** The number of terminal columns a line takes (marks and joiners take none). */
function columns(text)
{
    let count = 0;
    for (const character of text)
    {
        if (!ZERO_WIDTH.test(character))
        {
            ++count;
        }
    }
    return count;
}

const SUMMARY = /^(?:.*?: )?([0-9۰-۹٠-٩]+) (?:passed|تېرې شوې)[,،] ([0-9۰-۹٠-٩]+) (?:failed|ناکامې)$/u;

/** The colour of a transcript line: its marks, or the whole of a summary line. */
function coloured(line, logical)
{
    const summary = SUMMARY.exec(logical.trim());
    if (summary)
    {
        const failed = /^[0۰٠]+$/u.test(summary[2]) ? GREEN : RED;
        return `${BOLD}${failed}${line}${RESET}`;
    }
    return line.replace(/✓/g, `${BOLD}${GREEN}✓${RESET}`).replace(/✗/g, `${BOLD}${RED}✗${RESET}`);
}

/**
 * A transcript as the Test Results panel should get it: CRLF line ends, the marks coloured,
 * and - for a right-to-left language - every line in visual order against the right edge.
 */
function forTestResults(text, rightToLeft)
{
    const lines = String(text).replace(/\r\n/g, "\n").split("\n");
    return lines.map((line) =>
    {
        if (!line || !rightToLeft)
        {
            return line ? coloured(line, line) : line;
        }
        const visual = direction.hasRightToLeftText(line) ? visualOrder(line) : line;
        const width = columns(visual);
        // To the last column, then back to where the line must start to end there.
        const align = width > 1 ? `\u001b[999C\u001b[${width - 1}D` : "\u001b[999C";
        return align + coloured(visual, line);
    }).join("\r\n");
}

module.exports = { forTestResults, visualOrder, columns };
