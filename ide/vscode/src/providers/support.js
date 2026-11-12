// Helpers shared by the language providers: turning the plain ranges of the model into VS Code
// ranges, finding the word under the pointer (which in Pashto may be two words, as in
// `کرښه چاپ`), and getting the model of a document.

"use strict";

const vscode = require("vscode");

const { foldLetters } = require("../util");

/** A plain {line, character} as a VS Code position. */
function toPosition(position)
{
    return new vscode.Position(position.line, position.character);
}

/** A plain range as a VS Code range. */
function toRange(range)
{
    return new vscode.Range(toPosition(range.start), toPosition(range.end));
}

/** The compiled model of a document, compiling it first if the text has changed. */
async function modelOf(services, document, cancellation)
{
    return services.analyser.analyse(document.uri.fsPath, document.getText(), {
        version: `${document.version}:${document.isDirty}`,
        dirty: document.isDirty || document.uri.scheme !== "file",
        cancellation,
    });
}

const WORD = /[\p{L}\p{M}_][\p{L}\p{M}\p{N}_]*/u;

/**
 * The reserved word at a position, which may be written as several words.
 *
 * Pashto built-ins such as `کرښه چاپ` ("println") and `د موږک افقي ځای` are spelled as two to
 * four words. The language pack knows them as one name, so the editor must too: the words
 * around the pointer are joined, longest first, until one of them is a reserved word. Letters
 * are folded first (ك→ک, ي→ی), exactly as the compiler folds them, so a word typed on an
 * Arabic keyboard is recognised as well.
 */
function reservedWordAt(document, position, language)
{
    const words = language ? language.reservedWords() : new Map();
    const folded = new Map();
    for (const [word, meaning] of words)
    {
        folded.set(foldLetters(word), meaning);
    }
    const line = document.lineAt(position.line).text;
    const pieces = [];
    const pattern = new RegExp(WORD.source, "gu");
    let match = pattern.exec(line);
    while (match)
    {
        pieces.push({ text: match[0], start: match.index, end: match.index + match[0].length });
        match = pattern.exec(line);
    }
    const index = pieces.findIndex(
        (piece) => piece.start <= position.character && position.character <= piece.end);
    if (index < 0)
    {
        return null;
    }
    // Up to four words, starting at or before the word under the pointer, longest first.
    for (let length = 4; length >= 1; --length)
    {
        for (let first = Math.max(0, index - length + 1); first <= index; ++first)
        {
            const last = first + length - 1;
            if (last >= pieces.length || last < index)
            {
                continue;
            }
            const text = pieces.slice(first, last + 1).map((piece) => piece.text).join(" ");
            const meaning = words.get(text) || folded.get(foldLetters(text));
            if (meaning)
            {
                return {
                    word: text,
                    meaning,
                    range: new vscode.Range(
                        new vscode.Position(position.line, pieces[first].start),
                        new vscode.Position(position.line, pieces[last].end)),
                };
            }
        }
    }
    const piece = pieces[index];
    return {
        word: piece.text,
        meaning: null,
        range: new vscode.Range(
            new vscode.Position(position.line, piece.start),
            new vscode.Position(position.line, piece.end)),
    };
}

/** The text before the caret on its line (used to decide what to complete). */
function prefixOf(document, position)
{
    return document.lineAt(position.line).text.slice(0, position.character);
}

module.exports = { WORD, modelOf, prefixOf, reservedWordAt, toPosition, toRange };
