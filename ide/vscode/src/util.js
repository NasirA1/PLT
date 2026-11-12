// Small helpers shared by the whole extension. Nothing here touches the VS Code API, so it
// can be unit-tested on its own (see test/util.test.js).

"use strict";

const path = require("path");

const EXE_SUFFIX = process.platform === "win32" ? ".exe" : "";

// ---------------------------------------------------------------------- positions
//
// thunderc counts lines and columns from 1 and measures a column in *code points* (the lexer
// advances one code point at a time). VS Code counts from 0 and measures a character in UTF-16
// code units. The two agree for every character Thunder programs normally contain - Pashto
// letters included, they are all in the basic plane - but not for an emoji inside a string or a
// comment, which is one code point and two UTF-16 units. The conversion below is therefore done
// against the real line, not by assuming they are the same.

/** The UTF-16 offset (0-based) of a 1-based code-point column in `line`. */
function columnToOffset(line, column)
{
    const wanted = Math.max(0, column - 1);
    let codePoints = 0;
    let offset = 0;
    while (offset < line.length && codePoints < wanted)
    {
        offset += surrogatePairAt(line, offset) ? 2 : 1;
        ++codePoints;
    }
    // A column beyond the end of the line (the lexer's "one past the last character") stays there.
    return offset + Math.max(0, wanted - codePoints);
}

/** The 1-based code-point column of a UTF-16 offset in `line` (the inverse of columnToOffset). */
function offsetToColumn(line, offset)
{
    let codePoints = 0;
    let index = 0;
    while (index < offset && index < line.length)
    {
        index += surrogatePairAt(line, index) ? 2 : 1;
        ++codePoints;
    }
    return codePoints + 1 + Math.max(0, offset - index);
}

function surrogatePairAt(text, index)
{
    const code = text.charCodeAt(index);
    if (code < 0xd800 || code > 0xdbff || index + 1 >= text.length)
    {
        return false;
    }
    const next = text.charCodeAt(index + 1);
    return next >= 0xdc00 && next <= 0xdfff;
}

/**
 * A compiler span ({line, column, end_line, end_column}, all 1-based) as a plain range
 * ({start: {line, character}, end: {line, character}}, all 0-based) in `lines`.
 */
function spanToRange(span, lines)
{
    const startLine = clamp((span.line || 1) - 1, 0, Math.max(0, lines.length - 1));
    const endLineNumber = span.end_line ? span.end_line - 1 : startLine;
    const endLine = clamp(endLineNumber, startLine, Math.max(0, lines.length - 1));
    const start = columnToOffset(lines[startLine] || "", span.column || 1);
    let end = columnToOffset(lines[endLine] || "", span.end_column || (span.column || 1) + 1);
    if (endLine === startLine && end <= start)
    {
        // An empty span (the end of the file, or a diagnostic without a width) still marks
        // one character, so that the squiggle is visible.
        end = Math.min(Math.max(start + 1, end), Math.max((lines[endLine] || "").length, start + 1));
    }
    return { start: { line: startLine, character: start }, end: { line: endLine, character: end } };
}

function clamp(value, low, high)
{
    return Math.min(Math.max(value, low), high);
}

/** True if the 0-based position is inside the plain range (end exclusive). */
function rangeContains(range, position)
{
    if (position.line < range.start.line || position.line > range.end.line)
    {
        return false;
    }
    if (position.line === range.start.line && position.character < range.start.character)
    {
        return false;
    }
    if (position.line === range.end.line && position.character >= range.end.character)
    {
        return false;
    }
    return true;
}

/** Splits text into lines the way an editor does (\r\n, \r and \n all end a line). */
function splitLines(text)
{
    return String(text).split(/\r\n|\r|\n/);
}

// ---------------------------------------------------------------------- the language directive

// "// lang: ps" (or the Pashto "// ژبه: ps") on the first line names the language of a program.
const DIRECTIVE = /^\s*\/\/\s*(?:lang|ژبه)\s*:\s*([A-Za-z_-]+)\s*$/;

/** The language code a program declares on its first line, or null. */
function directiveLanguage(text)
{
    const first = splitLines(text)[0] || "";
    const match = DIRECTIVE.exec(first);
    return match ? match[1].toLowerCase() : null;
}

/**
 * The language a file is written in: its own directive wins, then the configured language,
 * then English.
 */
function languageOf(text, configured)
{
    const declared = directiveLanguage(text);
    if (declared)
    {
        return declared;
    }
    if (configured && configured !== "auto")
    {
        return configured;
    }
    return "en";
}

// ---------------------------------------------------------------------- commands

/** A command line as it could be typed in a shell (for the build log). */
function formatCommand(command)
{
    return command.map((argument) => (argument === "" || /[\s"]/.test(argument) ? JSON.stringify(argument) : argument))
        .join(" ");
}

/** The bytecode file belonging to a source file. */
function bytecodePath(sourcePath)
{
    const parsed = path.parse(sourcePath);
    return path.join(parsed.dir, parsed.name + ".thc");
}

/** Folds the Arabic letter shapes a Pashto keyboard may produce onto the Pashto ones.
 *
 * The compiler does the same (fold_letters in language_pack.cpp): ك (U+0643) and ک look
 * almost identical but are different characters, and so do گ/ګ and ي/ى/ی. ي is a letter of
 * its own in Pashto, but the language packs accept it where ی is meant, so a word typed with
 * an Arabic keyboard is still recognised.
 */
function foldLetters(text)
{
    return String(text).replace(/ك/g, "ک").replace(/گ/g, "ګ")
        .replace(/[يى]/g, "ی");
}

module.exports = {
    EXE_SUFFIX,
    bytecodePath,
    clamp,
    columnToOffset,
    directiveLanguage,
    foldLetters,
    formatCommand,
    languageOf,
    offsetToColumn,
    rangeContains,
    spanToRange,
    splitLines,
};
