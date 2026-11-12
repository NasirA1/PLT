// Text work that does not need an editor: tidying the layout of a program.
//
// This is the same rule as the Qt IDE's "Tidy layout" (ide/thunder_ide/core/edits.py): tabs
// become spaces, trailing whitespace goes, runs of blank lines are cut to one, and the file
// ends with a single line break. The *amount* of indentation is never changed - in Thunder the
// indentation is the block structure, so re-indenting would be rewriting the program. That is
// why the extension offers no "format document" that reflows code: there is nothing to reflow
// that would not change the meaning.
//
// tests/python/test_vscode_extension.py runs this over every sample and compares the result
// with the Python implementation's, so the two cannot drift apart.

"use strict";

const INDENT_WIDTH = 4;

/** Tabs to the next multiple of four, exactly as the lexer measures them. */
function expandTabs(line)
{
    let out = "";
    let width = 0;
    for (const character of line)
    {
        if (character === "\t")
        {
            const spaces = INDENT_WIDTH - (width % INDENT_WIDTH);
            out += " ".repeat(spaces);
            width += spaces;
        }
        else
        {
            out += character;
            width += 1;
        }
    }
    return out;
}

/** Removes the whitespace at the end of a line (the line breaks are already gone). */
function trimEnd(line)
{
    return line.replace(/\s+$/u, "");
}

/** The tidied text of a program. */
function tidy(text)
{
    const out = [];
    let blanks = 0;
    for (const line of String(text).replace(/\r\n/g, "\n").replace(/\r/g, "\n").split("\n"))
    {
        const expanded = trimEnd(expandTabs(line));
        if (!expanded)
        {
            ++blanks;
            if (blanks > 1)
            {
                continue;
            }
        }
        else
        {
            blanks = 0;
        }
        out.push(expanded);
    }
    while (out.length && !out[out.length - 1])
    {
        out.pop();
    }
    return out.length ? out.join("\n") + "\n" : "";
}

/** The indentation of a line, measured in columns (a tab counts to the next multiple of four). */
function indentWidth(line)
{
    let width = 0;
    for (const character of line)
    {
        if (character === " ")
        {
            width += 1;
        }
        else if (character === "\t")
        {
            width += INDENT_WIDTH - (width % INDENT_WIDTH);
        }
        else
        {
            break;
        }
    }
    return width;
}

module.exports = { INDENT_WIDTH, expandTabs, indentWidth, tidy, trimEnd };
