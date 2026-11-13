// The right-to-left editor: the part that lives in the browser.
//
// VS Code's own editor cannot lay a line out right to left (see the README), so this one is a
// `contenteditable` in a webview, where the direction is ours. The browser is never allowed to
// change the text itself: every `beforeinput` is turned into an edit of the model (model.js),
// the changed lines are drawn again, and the caret is put back where the model says. That keeps
// the text exact - no stray markup, no direction marks - and makes the editor's behaviour the
// model's, which is tested in Node.
//
// It talks to the extension (src/rtleditor.js) in messages: it sends every change of the text,
// and receives the text when another editor changed it, the compiler's problems, and the words
// it may complete.

"use strict";

(function editor()
{
    const model = globalThis.ThunderEditorModel;
    const vscode = acquireVsCodeApi();

    const code = document.getElementById("code");
    const problems = document.getElementById("problems");
    const popup = document.getElementById("completion");

    const state = {
        text: "",
        rightToLeft: true,
        words: {},            // reserved word -> kind, for colouring
        blockWords: { opening: [], leaving: [] },
        candidates: [],       // what may be completed: [{label, kind, detail}]
        diagnostics: [],      // [{line, message, code, severity}]
        lines: [],            // the text of each line as last drawn
        history: null,
        completion: null,     // {start, items, index} while the list is open
        composing: false,
    };

    // ------------------------------------------------------------------ drawing

    /** Draws the lines that changed (all of them when `everything`). */
    function draw(everything)
    {
        const lines = state.text.split("\n");
        const errors = new Map();
        for (const diagnostic of state.diagnostics)
        {
            if (!errors.has(diagnostic.line))
            {
                errors.set(diagnostic.line, diagnostic);
            }
        }
        while (code.children.length > lines.length)
        {
            code.lastElementChild.remove();
        }
        for (let index = 0; index < lines.length; ++index)
        {
            let element = code.children[index];
            if (!element)
            {
                element = document.createElement("div");
                element.className = "line";
                code.appendChild(element);
                everything = true;
            }
            if (everything || state.lines[index] !== lines[index])
            {
                element.innerHTML = lines[index]
                    ? model.renderLine(lines[index], state.rightToLeft, state.words)
                    : "<br>";
            }
            const error = errors.get(index);
            element.classList.toggle("error", Boolean(error));
            element.title = error ? `${error.code}: ${error.message}` : "";
        }
        state.lines = lines;
    }

    function drawProblems()
    {
        problems.replaceChildren();
        problems.hidden = state.diagnostics.length === 0;
        for (const diagnostic of state.diagnostics)
        {
            const row = document.createElement("div");
            row.className = `problem ${diagnostic.severity}`;
            row.textContent = `${diagnostic.line + 1}: ${diagnostic.code} ${diagnostic.message}`;
            row.dir = "auto";
            row.addEventListener("click", () =>
            {
                select(model.offsetOf(state.text, diagnostic.line, 0));
                code.focus();
            });
            problems.appendChild(row);
        }
    }

    // ------------------------------------------------------------------ positions <-> DOM

    /** The offset in the text of a DOM position inside the editor. */
    function offsetOfDom(node, offset)
    {
        let line = node.nodeType === Node.ELEMENT_NODE ? node : node.parentElement;
        while (line && line.parentElement !== code)
        {
            line = line.parentElement;
        }
        if (!line)
        {
            return node === code ? model.offsetOf(state.text, offset, 0) : 0;
        }
        const index = Array.prototype.indexOf.call(code.children, line);
        let character = 0;
        const walker = document.createTreeWalker(line, NodeFilter.SHOW_TEXT);
        let current = walker.nextNode();
        while (current)
        {
            if (current === node)
            {
                character += offset;
                break;
            }
            if (node.nodeType === Node.ELEMENT_NODE && node.childNodes[offset]
                && (node.childNodes[offset] === current || node.childNodes[offset].contains(current)))
            {
                break;
            }
            character += current.nodeValue.length;
            current = walker.nextNode();
        }
        return model.offsetOf(state.text, index, character);
    }

    /** The DOM position of an offset in the text. */
    function domOfOffset(offset)
    {
        const position = model.positionOf(state.text, offset);
        const line = code.children[position.line];
        if (!line)
        {
            return { node: code, offset: code.childNodes.length };
        }
        let remaining = position.character;
        const walker = document.createTreeWalker(line, NodeFilter.SHOW_TEXT);
        let current = walker.nextNode();
        let last = null;
        while (current)
        {
            if (remaining <= current.nodeValue.length)
            {
                return { node: current, offset: remaining };
            }
            remaining -= current.nodeValue.length;
            last = current;
            current = walker.nextNode();
        }
        return last ? { node: last, offset: last.nodeValue.length } : { node: line, offset: 0 };
    }

    /** The selection as offsets: {start, end} with start <= end. */
    function selection()
    {
        const current = window.getSelection();
        if (!current || current.rangeCount === 0 || !code.contains(current.anchorNode))
        {
            return { start: state.text.length, end: state.text.length };
        }
        const anchor = offsetOfDom(current.anchorNode, current.anchorOffset);
        const focus = offsetOfDom(current.focusNode, current.focusOffset);
        return { start: Math.min(anchor, focus), end: Math.max(anchor, focus) };
    }

    function select(start, end)
    {
        const from = domOfOffset(start);
        const to = domOfOffset(end === undefined ? start : end);
        const range = document.createRange();
        range.setStart(from.node, from.offset);
        range.setEnd(to.node, to.offset);
        const current = window.getSelection();
        current.removeAllRanges();
        current.addRange(range);
        const element = (from.node.nodeType === Node.ELEMENT_NODE ? from.node : from.node.parentElement);
        if (element && element.scrollIntoView)
        {
            element.scrollIntoView({ block: "nearest" });
        }
    }

    // ------------------------------------------------------------------ editing

    /** Applies a new text, draws it, places the caret, tells the extension and remembers it. */
    function commit(text, start, end, kind)
    {
        state.text = text;
        draw(false);
        select(start, end);
        state.history.record(text, start, kind, Date.now());
        vscode.postMessage({ type: "edit", text });
        vscode.setState({ text });
    }

    function insert(inserted, kind)
    {
        const { start, end } = selection();
        const caret = start + inserted.length;
        commit(model.applyEdit(state.text, start, end, inserted), caret, caret, kind);
    }

    function deleteRange(from, to)
    {
        commit(model.applyEdit(state.text, from, to, ""), from, from, "delete");
    }

    code.addEventListener("beforeinput", (event) =>
    {
        if (state.composing || event.inputType === "insertCompositionText")
        {
            return; // an input method is composing: the browser draws it, compositionend reads it
        }
        event.preventDefault();
        const { start, end } = selection();
        switch (event.inputType)
        {
        case "insertText":
        case "insertReplacementText":
        {
            const data = event.data !== null && event.data !== undefined
                ? event.data
                : (event.dataTransfer ? event.dataTransfer.getData("text/plain") : "");
            insert(data.replace(/\r\n?/g, "\n"), "type");
            updateCompletion(false);
            return;
        }
        case "insertLineBreak":
        case "insertParagraph":
        {
            const result = model.newline(state.text, start, end, state.blockWords);
            commit(result.text, result.caret, result.caret, "newline");
            closeCompletion();
            return;
        }
        case "insertFromPaste":
        case "insertFromDrop":
        case "insertFromYank":
        {
            const data = event.dataTransfer ? event.dataTransfer.getData("text/plain") : (event.data || "");
            insert(data.replace(/\r\n?/g, "\n").replace(/\t/g, model.INDENT), "paste");
            return;
        }
        case "deleteContentBackward":
        {
            const result = model.backspace(state.text, start, end);
            commit(result.text, result.caret, result.caret, "delete");
            updateCompletion(false);
            return;
        }
        case "deleteContentForward":
            if (start !== end)
            {
                deleteRange(start, end);
            }
            else if (start < state.text.length)
            {
                deleteRange(start, start + 1);
            }
            return;
        case "deleteWordBackward":
        {
            const from = start !== end ? start : model.wordBefore(state.text, start).start;
            deleteRange(from === start && start > 0 ? start - 1 : from, end);
            return;
        }
        case "deleteWordForward":
        {
            const match = /^\s*[^\s]*/.exec(state.text.slice(end));
            deleteRange(start, end + (start === end && match ? match[0].length : 0));
            return;
        }
        case "deleteSoftLineBackward":
        case "deleteHardLineBackward":
            deleteRange(start === end ? model.lineBounds(state.text, start).start : start, end);
            return;
        case "deleteByCut":
        case "deleteByDrag":
        case "deleteContent":
            if (start !== end)
            {
                deleteRange(start, end);
            }
            return;
        case "historyUndo":
            undo();
            return;
        case "historyRedo":
            redo();
            return;
        default:
            return; // formatting and the like: a program has no bold
        }
    });

    code.addEventListener("compositionstart", () => { state.composing = true; });
    code.addEventListener("compositionend", () =>
    {
        state.composing = false;
        // Read back what the input method put in, line by line.
        const text = Array.from(code.children).map((line) => line.textContent).join("\n");
        const caret = selection();
        commit(text, caret.start, caret.end, "type");
    });

    function undo()
    {
        const previous = state.history.undo();
        if (previous)
        {
            state.text = previous.text;
            draw(false);
            select(previous.caret);
            vscode.postMessage({ type: "edit", text: previous.text });
        }
    }

    function redo()
    {
        const next = state.history.redo();
        if (next)
        {
            state.text = next.text;
            draw(false);
            select(next.caret);
            vscode.postMessage({ type: "edit", text: next.text });
        }
    }

    code.addEventListener("keydown", (event) =>
    {
        if (state.completion && handleCompletionKey(event))
        {
            return;
        }
        const control = event.ctrlKey || event.metaKey;
        if (event.key === "Tab")
        {
            event.preventDefault();
            const { start, end } = selection();
            const result = event.shiftKey ? model.outdent(state.text, start, end) : model.indent(state.text, start, end);
            commit(result.text, result.start, result.end, "indent");
            return;
        }
        if (control && !event.altKey && (event.key === "z" || event.key === "Z"))
        {
            event.preventDefault();
            if (event.shiftKey)
            {
                redo();
            }
            else
            {
                undo();
            }
            return;
        }
        if (control && (event.key === "y" || event.key === "Y"))
        {
            event.preventDefault();
            redo();
            return;
        }
        if (control && event.key === " ")
        {
            event.preventDefault();
            updateCompletion(true);
        }
    });

    // Ctrl+S, Ctrl+P and the rest belong to VS Code: the webview passes them on by itself.

    // ------------------------------------------------------------------ completion

    function updateCompletion(explicit)
    {
        const { start, end } = selection();
        if (start !== end)
        {
            closeCompletion();
            return;
        }
        const word = model.wordBefore(state.text, start);
        if (!word.prefix && !explicit)
        {
            closeCompletion();
            return;
        }
        const items = explicit && !word.prefix
            ? state.candidates.slice(0, 12)
            : model.completions(word.prefix, state.candidates, 12);
        if (!items.length)
        {
            closeCompletion();
            return;
        }
        state.completion = { start: word.start, items, index: 0 };
        drawCompletion();
    }

    function drawCompletion()
    {
        const completion = state.completion;
        popup.replaceChildren();
        completion.items.forEach((item, index) =>
        {
            const row = document.createElement("div");
            row.className = `item${index === completion.index ? " selected" : ""}`;
            row.dir = "auto";
            const label = document.createElement("span");
            label.className = `k-${item.kind === "name" || item.kind === "function" || item.kind === "struct" ? "name" : item.kind}`;
            label.textContent = item.label;
            row.appendChild(label);
            if (item.detail)
            {
                const detail = document.createElement("span");
                detail.className = "detail";
                detail.textContent = item.detail;
                row.appendChild(detail);
            }
            row.addEventListener("mousedown", (event) =>
            {
                event.preventDefault();
                completion.index = index;
                accept();
            });
            popup.appendChild(row);
        });
        const range = window.getSelection().rangeCount ? window.getSelection().getRangeAt(0) : null;
        const rect = range ? range.getBoundingClientRect() : code.getBoundingClientRect();
        popup.style.top = `${rect.bottom + window.scrollY + 2}px`;
        const width = 320;
        const left = state.rightToLeft ? rect.right - width : rect.left;
        popup.style.left = `${Math.max(4, Math.min(left, window.innerWidth - width - 4))}px`;
        popup.hidden = false;
    }

    function closeCompletion()
    {
        state.completion = null;
        popup.hidden = true;
    }

    function accept()
    {
        const completion = state.completion;
        const item = completion.items[completion.index];
        const { end } = selection();
        closeCompletion();
        const caret = completion.start + item.label.length;
        commit(model.applyEdit(state.text, completion.start, end, item.label), caret, caret, "complete");
    }

    /** Arrow keys, Enter, Tab and Escape while the list is open. True if the key was used. */
    function handleCompletionKey(event)
    {
        const completion = state.completion;
        switch (event.key)
        {
        case "ArrowDown":
            completion.index = (completion.index + 1) % completion.items.length;
            break;
        case "ArrowUp":
            completion.index = (completion.index - 1 + completion.items.length) % completion.items.length;
            break;
        case "Enter":
        case "Tab":
            event.preventDefault();
            accept();
            return true;
        case "Escape":
            closeCompletion();
            event.preventDefault();
            return true;
        default:
            return false;
        }
        event.preventDefault();
        drawCompletion();
        return true;
    }

    code.addEventListener("blur", () => setTimeout(closeCompletion, 150));

    // ------------------------------------------------------------------ the extension

    window.addEventListener("message", (event) =>
    {
        const message = event.data || {};
        switch (message.type)
        {
        case "init":
            state.rightToLeft = Boolean(message.rightToLeft);
            state.words = message.words || {};
            state.blockWords = message.blockWords || state.blockWords;
            state.candidates = message.candidates || [];
            state.diagnostics = message.diagnostics || [];
            state.text = message.text || "";
            state.history = new model.History(state.text, 0);
            document.documentElement.dir = state.rightToLeft ? "rtl" : "ltr";
            document.documentElement.lang = message.language || "";
            code.dir = state.rightToLeft ? "rtl" : "ltr";
            draw(true);
            drawProblems();
            code.focus();
            select(0);
            vscode.postMessage({ type: "ready" });
            break;
        case "update":
            // Another editor (the text editor, Undo in the Edit menu, a formatter) changed it.
            if (typeof message.text === "string" && message.text !== state.text)
            {
                const caret = selection();
                state.text = message.text;
                state.history.reset(message.text, caret.start);
                draw(false);
                const clamp = (offset) => Math.min(offset, state.text.length);
                if (document.activeElement === code)
                {
                    select(clamp(caret.start), clamp(caret.end));
                }
            }
            break;
        case "diagnostics":
            state.diagnostics = message.diagnostics || [];
            draw(false);
            drawProblems();
            break;
        case "candidates":
            state.candidates = message.candidates || [];
            break;
        case "reveal":
            select(model.offsetOf(state.text, message.line || 0, message.character || 0));
            code.focus();
            break;
        default:
            break;
        }
    });

    // Tell the extension the page is ready for its first message.
    vscode.postMessage({ type: "loaded" });

    // For the tests: the editor's state, read from outside.
    globalThis.thunderEditor = { state, selection, select, draw };
}());
