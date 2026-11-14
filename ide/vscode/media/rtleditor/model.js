// The right-to-left editor's model: everything it does to text, with no DOM in sight.
//
// The editor (editor.js) turns key presses into calls on this module and draws what it returns;
// keeping the decisions here means they are tested in Node (test/rtleditor.test.js) and the
// browser part only has to be tested for what is genuinely about the browser.
//
// It is loaded both by Node (`require`) and by the webview (a <script> tag), so it uses no
// modules: in the browser it hangs itself on `globalThis.ThunderEditorModel`, and it finds the
// direction algorithm (src/direction.js, the Qt IDE's own rules) on `globalThis.ThunderDirection`.

"use strict";

(function factory(root)
{
    const direction = (typeof module !== "undefined" && module.exports)
        ? require("../../src/direction")
        : root.ThunderDirection;

    const INDENT = "    ";

    // ------------------------------------------------------------------ text and positions

    /** The text with one edit applied: [start, end) replaced by `inserted`. */
    function applyEdit(text, start, end, inserted)
    {
        const from = Math.max(0, Math.min(start, end, text.length));
        const to = Math.min(text.length, Math.max(start, end));
        return text.slice(0, from) + inserted + text.slice(to);
    }

    /**
     * The smallest edit that turns one text into the other: {start, end, text}.
     *
     * The extension applies this to the document rather than replacing it whole, so VS Code's
     * undo, the dirty marker and every other editor of the same file see a real edit.
     */
    function difference(before, after)
    {
        if (before === after)
        {
            return null;
        }
        let start = 0;
        const shortest = Math.min(before.length, after.length);
        while (start < shortest && before.charCodeAt(start) === after.charCodeAt(start))
        {
            ++start;
        }
        let endBefore = before.length;
        let endAfter = after.length;
        while (endBefore > start && endAfter > start
            && before.charCodeAt(endBefore - 1) === after.charCodeAt(endAfter - 1))
        {
            --endBefore;
            --endAfter;
        }
        // Never split a surrogate pair.
        if (start > 0 && isHighSurrogate(before.charCodeAt(start - 1)))
        {
            --start;
        }
        return { start, end: endBefore, text: after.slice(start, endAfter) };
    }

    function isHighSurrogate(code)
    {
        return code >= 0xd800 && code <= 0xdbff;
    }

    /** {line, character} of an offset, both from 0. */
    function positionOf(text, offset)
    {
        let line = 0;
        let lineStart = 0;
        const end = Math.max(0, Math.min(offset, text.length));
        for (let index = 0; index < end; ++index)
        {
            if (text[index] === "\n")
            {
                ++line;
                lineStart = index + 1;
            }
        }
        return { line, character: end - lineStart };
    }

    /** The offset of {line, character} (clamped to the text). */
    function offsetOf(text, line, character)
    {
        let offset = 0;
        for (let current = 0; current < line; ++current)
        {
            const next = text.indexOf("\n", offset);
            if (next < 0)
            {
                return text.length;
            }
            offset = next + 1;
        }
        const lineEnd = text.indexOf("\n", offset);
        const length = (lineEnd < 0 ? text.length : lineEnd) - offset;
        return offset + Math.max(0, Math.min(character, length));
    }

    function lineBounds(text, offset)
    {
        const start = text.lastIndexOf("\n", Math.max(0, offset - 1)) + 1;
        const newline = text.indexOf("\n", offset);
        return { start: offset === 0 ? 0 : start, end: newline < 0 ? text.length : newline };
    }

    // ------------------------------------------------------------------ editing with indentation

    /**
     * What Enter does: a line break, keeping the indentation; one level more after a line that
     * opens a block (`if`, `که`, `دنده` ...) and one less after `return`, `break` and the like.
     * Returns {text, caret}. `words` is {opening: [...], leaving: [...]}.
     */
    function newline(text, start, end, words)
    {
        const bounds = lineBounds(text, start);
        const before = text.slice(bounds.start, start);
        const indent = /^[ \t]*/.exec(before)[0];
        const first = firstWord(before, words);
        let next = indent;
        if (first && (words.opening || []).includes(first) && before.trim())
        {
            next = indent + INDENT;
        }
        else if (first && (words.leaving || []).includes(first))
        {
            next = indent.slice(0, Math.max(0, indent.length - INDENT.length));
        }
        const inserted = "\n" + next;
        return { text: applyEdit(text, start, end, inserted), caret: Math.min(start, end) + inserted.length };
    }

    /** The first word of a line, joined with the words after it when they form a reserved word. */
    function firstWord(line, words)
    {
        const match = /^[ \t]*([\p{L}\p{M}_][\p{L}\p{M}\p{N}_]*)/u.exec(line);
        if (!match)
        {
            return "";
        }
        const known = (words.opening || []).concat(words.leaving || []);
        // A compound such as "بهرنی دنده" (native func) is matched whole.
        for (const candidate of known)
        {
            if (candidate.includes(" ") && line.trimStart().startsWith(candidate))
            {
                return candidate;
            }
        }
        return foldLetters(match[1]);
    }

    /** Tab: four spaces at the caret, or one more level on every selected line. */
    function indent(text, start, end)
    {
        if (start === end || !text.slice(Math.min(start, end), Math.max(start, end)).includes("\n"))
        {
            const from = Math.min(start, end);
            return { text: applyEdit(text, from, Math.max(start, end), INDENT), start: from + 4, end: from + 4 };
        }
        return shiftLines(text, start, end, 1);
    }

    /** Shift+Tab: one level less on every line the selection touches. */
    function outdent(text, start, end)
    {
        return shiftLines(text, start, end, -1);
    }

    function shiftLines(text, start, end, direction)
    {
        const from = lineBounds(text, Math.min(start, end)).start;
        const to = lineBounds(text, Math.max(start, end)).end;
        const lines = text.slice(from, to).split("\n");
        let removedBeforeStart = 0;
        let delta = 0;
        const changed = lines.map((line, index) =>
        {
            if (direction > 0)
            {
                if (!line.trim())
                {
                    return line;
                }
                delta += INDENT.length;
                return INDENT + line;
            }
            const leading = /^ {0,4}/.exec(line)[0].length;
            if (index === 0)
            {
                removedBeforeStart = leading;
            }
            delta -= leading;
            return line.slice(leading);
        });
        const replaced = changed.join("\n");
        const firstShift = direction > 0 ? (lines[0].trim() ? INDENT.length : 0) : -removedBeforeStart;
        return {
            text: text.slice(0, from) + replaced + text.slice(to),
            start: Math.max(from, Math.min(start, end) + firstShift),
            end: Math.max(from, Math.max(start, end) + delta),
        };
    }

    /** Backspace in the leading spaces takes away a whole level, as the Qt IDE does. */
    function backspace(text, start, end)
    {
        if (start !== end)
        {
            const from = Math.min(start, end);
            return { text: applyEdit(text, from, Math.max(start, end), ""), caret: from };
        }
        if (start === 0)
        {
            return { text, caret: 0 };
        }
        const bounds = lineBounds(text, start);
        const before = text.slice(bounds.start, start);
        if (before.length > 0 && /^ +$/.test(before))
        {
            const remove = before.length % INDENT.length || INDENT.length;
            return { text: applyEdit(text, start - remove, start, ""), caret: start - remove };
        }
        const width = isLowSurrogate(text.charCodeAt(start - 1)) && start > 1 ? 2 : 1;
        return { text: applyEdit(text, start - width, start, ""), caret: start - width };
    }

    function isLowSurrogate(code)
    {
        return code >= 0xdc00 && code <= 0xdfff;
    }

    // ------------------------------------------------------------------ undo

    /**
     * Undo and redo for the editor itself.
     *
     * The editor redraws its lines after every key, which leaves the browser's own undo history
     * meaningless, so it keeps snapshots: one per pause in typing (a burst of letters undoes as
     * one step, as in every editor) and one per edit of any other kind.
     */
    class History
    {
        constructor(text, caret)
        {
            this.undoStack = [];
            this.redoStack = [];
            this.current = { text, caret: caret || 0 };
            this.lastKind = "";
            this.lastTime = 0;
        }

        /** Records a change; `kind` is "type" for letters, which coalesce, or anything else. */
        record(text, caret, kind, time)
        {
            if (text === this.current.text)
            {
                this.current = { text, caret };
                return;
            }
            const coalesce = kind === "type" && this.lastKind === "type" && time - this.lastTime < 1000
                && this.undoStack.length > 0;
            if (!coalesce)
            {
                this.undoStack.push(this.current);
                if (this.undoStack.length > 500)
                {
                    this.undoStack.shift();
                }
            }
            this.current = { text, caret };
            this.redoStack = [];
            this.lastKind = kind;
            this.lastTime = time;
        }

        /** The text changed outside this editor (another editor, a formatter): start afresh. */
        reset(text, caret)
        {
            if (text !== this.current.text)
            {
                this.undoStack.push(this.current);
                this.current = { text, caret: caret || 0 };
                this.redoStack = [];
            }
            this.lastKind = "";
        }

        undo()
        {
            if (!this.undoStack.length)
            {
                return null;
            }
            this.redoStack.push(this.current);
            this.current = this.undoStack.pop();
            this.lastKind = "";
            return this.current;
        }

        redo()
        {
            if (!this.redoStack.length)
            {
                return null;
            }
            this.undoStack.push(this.current);
            this.current = this.redoStack.pop();
            this.lastKind = "";
            return this.current;
        }
    }

    // ------------------------------------------------------------------ colouring

    // Comments, strings (every quote style a Thunder program may use), numbers and words: the
    // same units as the Qt IDE's highlighter (core/highlight.py).
    const LEXEME = /(#[^\n]*)|("(?:\\.|[^"\\\n])*"?|«(?:\\.|[^»\\\n])*»?|[“”„](?:\\.|[^“”„\\\n])*[“”„]?)|(\b0[xX][0-9a-fA-F]+|\b0[bB][01]+|[\p{Nd}]+(?:[.٫][\p{Nd}]+)?)|([\p{L}\p{M}_][\p{L}\p{M}\p{N}_]*)/gu;

    /** Folds the letters a Pashto keyboard may produce in two forms, as the compiler does. */
    function foldLetters(text)
    {
        return String(text).replace(/ك/g, "ک").replace(/گ/g, "ګ").replace(/[يى]/g, "ی");
    }

    /**
     * The kind of every character of a line, for colouring: "comment", "string", "number",
     * "keyword", "type", "literal", "builtin", "name" or "" (operators, spaces).
     *
     * `words` maps a reserved word (folded) to its kind. A built-in may be several words
     * (`کرښه چاپ`), so a run of words is matched longest first.
     */
    function classify(line, words)
    {
        const kinds = new Array(line.length).fill("");
        const pieces = [];
        LEXEME.lastIndex = 0;
        let match = LEXEME.exec(line);
        while (match)
        {
            const start = match.index;
            const end = start + match[0].length;
            if (match[1] !== undefined)
            {
                kinds.fill("comment", start, end);
            }
            else if (match[2] !== undefined)
            {
                kinds.fill("string", start, end);
            }
            else if (match[3] !== undefined)
            {
                kinds.fill("number", start, end);
            }
            else
            {
                pieces.push({ start, end, text: match[0] });
            }
            match = LEXEME.exec(line);
        }
        // Words: the longest run of up to four that is a reserved word.
        for (let index = 0; index < pieces.length;)
        {
            let matched = 0;
            for (let length = Math.min(4, pieces.length - index); length >= 1 && !matched; --length)
            {
                const run = pieces.slice(index, index + length);
                const gapsAreSpaces = run.every((piece, at) =>
                    at === 0 || /^[ \t]+$/.test(line.slice(run[at - 1].end, piece.start)));
                if (!gapsAreSpaces)
                {
                    continue;
                }
                const phrase = foldLetters(run.map((piece) => piece.text).join(" "));
                const kind = words[phrase];
                if (kind)
                {
                    kinds.fill(kind, run[0].start, run[run.length - 1].end);
                    matched = length;
                }
            }
            if (!matched)
            {
                kinds.fill("name", pieces[index].start, pieces[index].end);
                matched = 1;
            }
            index += matched;
        }
        return kinds;
    }

    function escapeHtml(text)
    {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    /**
     * One line as HTML, laid out in the program's direction.
     *
     * Each token the direction algorithm isolates becomes an isolated span - `dir="auto"` reads
     * it in the direction of its own first letter (the FSI of the Qt IDE), and a token with no
     * letter of its own, a number, is kept left to right so its sign stays on the left (the
     * IDE's LRM). The text of the spans is exactly the text of the line - no marks are added -
     * so a position in the line is a position in the DOM.
     */
    function renderLine(line, rightToLeft, words)
    {
        const kinds = classify(line, words);
        const layout = direction.analyseLine(line, rightToLeft);
        const pieces = [];
        let position = 0;
        const plain = (from, to) =>
        {
            if (to > from)
            {
                pieces.push(colour(line, kinds, from, to));
            }
        };
        for (const token of layout.tokens)
        {
            plain(position, token.start);
            const text = line.slice(token.start, token.end);
            const dir = direction.isWeak(text) ? "ltr" : "auto";
            pieces.push(`<span class="tok" dir="${dir}">${colour(line, kinds, token.start, token.end)}</span>`);
            position = token.end;
        }
        plain(position, line.length);
        return pieces.join("");
    }

    /** The characters [from, to) of a line, in spans by kind. */
    function colour(line, kinds, from, to)
    {
        let html = "";
        let index = from;
        while (index < to)
        {
            const kind = kinds[index];
            let end = index;
            while (end < to && kinds[end] === kind)
            {
                ++end;
            }
            const text = escapeHtml(line.slice(index, end));
            html += kind ? `<span class="k-${kind}">${text}</span>` : text;
            index = end;
        }
        return html;
    }

    // ------------------------------------------------------------------ completion

    /** The word being typed before the caret: {start, prefix}. */
    function wordBefore(text, caret)
    {
        const before = text.slice(lineBounds(text, caret).start, caret);
        const match = /([\p{L}\p{M}_][\p{L}\p{M}\p{N}_]*)$/u.exec(before);
        if (!match)
        {
            return { start: caret, prefix: "" };
        }
        return { start: caret - match[1].length, prefix: match[1] };
    }

    /**
     * The completions for a prefix, best first: words that start with it, the program's own
     * names before the language's, then shorter before longer. Letters are folded, so a word
     * typed on an Arabic keyboard finds its Pashto spelling.
     */
    function completions(prefix, candidates, limit)
    {
        const wanted = foldLetters(prefix);
        if (!wanted)
        {
            return [];
        }
        const seen = new Set();
        const found = [];
        for (const candidate of candidates)
        {
            const label = candidate.label;
            if (seen.has(label) || label === prefix)
            {
                continue;
            }
            if (foldLetters(label).startsWith(wanted))
            {
                seen.add(label);
                found.push(candidate);
            }
        }
        const rank = { name: 0, function: 0, struct: 0, keyword: 1, type: 1, literal: 1, builtin: 2, module: 3 };
        found.sort((a, b) => (rank[a.kind] ?? 2) - (rank[b.kind] ?? 2)
            || a.label.length - b.label.length || a.label.localeCompare(b.label));
        return found.slice(0, limit || 12);
    }

    const model = {
        History,
        INDENT,
        applyEdit,
        backspace,
        classify,
        completions,
        difference,
        foldLetters,
        indent,
        lineBounds,
        newline,
        offsetOf,
        outdent,
        positionOf,
        renderLine,
        wordBefore,
    };
    if (typeof module !== "undefined" && module.exports)
    {
        module.exports = model;
    }
    else
    {
        root.ThunderEditorModel = model;
    }
}(typeof globalThis !== "undefined" ? globalThis : this));
