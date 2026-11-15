// The right-to-left editor: the part that lives in the browser.
//
// VS Code's own editor cannot lay a line out right to left (see the README), so this one is a
// `contenteditable` in a webview, where the direction is ours. The browser is never allowed to
// change the text itself: every `beforeinput` is turned into an edit of the model (model.js),
// the changed lines are drawn again, and the caret is put back where the model says. That keeps
// the text exact - no stray markup, no direction marks - and makes the editor's behaviour the
// model's, which is tested in Node.
//
// It talks to the extension (src/rtleditor.js) in messages: it sends every change of the text
// and every click in the gutter (a breakpoint), and receives the text when another editor
// changed it, the compiler's problems, the breakpoints and - while a program is debugged - the
// line it is stopped on. Every language feature (completion, signature help, hovers,
// definitions, references, rename, formatting, quick fixes, symbols, occurrences, semantic
// colours) is a request to the extension, answered by the same providers as VS Code's own
// editor (src/rtlservices.js): the page only draws the answers.

"use strict";

(function editor()
{
    const model = globalThis.ThunderEditorModel;
    const search = globalThis.ThunderSearch;
    const vscode = acquireVsCodeApi();

    const code = document.getElementById("code");
    const problems = document.getElementById("problems");
    const popup = document.getElementById("completion");
    const tooltip = document.getElementById("tooltip");
    const signature = document.getElementById("signature");
    const findBar = document.getElementById("find-bar");
    const findInput = document.getElementById("find-input");
    const findCount = document.getElementById("find-count");
    const findLoose = document.getElementById("find-loose");
    const findExpand = document.getElementById("find-expand");
    const replaceRow = document.getElementById("replace-row");
    const replaceInput = document.getElementById("replace-input");

    const saved = (vscode.getState && vscode.getState()) || {};

    const state = {
        text: "",
        rightToLeft: true,
        words: {},            // reserved word -> kind, for colouring
        blockWords: { opening: [], leaving: [] },
        occurrences: [],      // [{start, end, kind}] of the name at the caret
        semantic: {},         // token type -> [[start, end], ...] from the semantic tokens provider
        diagnostics: [],      // [{line, message, code, severity}]
        breakpoints: [],      // 0-based lines with a breakpoint
        execution: null,      // {line, column, historic, focused} while a debugged program is stopped
        hover: null,          // {id, element, expression} for the value being asked about
        lines: [],            // the text of each line as last drawn
        history: null,
        completion: null,     // {wordStart, items, shown, index} while the list is open
        composing: false,
        find: { matches: [], index: -1, loose: Boolean(saved.findLoose) },
        zoom: Math.max(-8, Math.min(12, Math.round(saved.zoom) || 0)),
        tests: [],            // [{name, line}]: the file's tests, a play button on each line
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
            element.classList.toggle("breakpoint", state.breakpoints.includes(index));
            element.classList.toggle("test", state.tests.some((candidate) => candidate.line === index));
            const execution = state.execution;
            const current = Boolean(execution) && execution.line === index;
            element.classList.toggle("current", current && !execution.focused);
            element.classList.toggle("focused-frame", current && execution.focused);
            element.classList.toggle("historic", current && execution.historic);
        }
        state.lines = lines;
        paintDecorations();
    }

    // ------------------------------------------------------------------ decorations
    //
    // Occurrences of the name at the caret, the semantic colours and the problems' squiggles do
    // not change the text, so they are painted over it with the CSS Custom Highlight API: ranges
    // of the document, styled by ::highlight() rules in editor.css. They are painted again after
    // every redraw (a redrawn line has new text nodes).

    const SEMANTIC = {
        function: "fn", method: "fn", builtinFunction: "builtin", moduleFunction: "builtin", parameter: "parameter",
        property: "field", struct: "type", class: "type", type: "type", constant: "constant", enumMember: "constant",
    };

    function domRange(start, end)
    {
        if (start < 0 || end > state.text.length || end < start)
        {
            return null;
        }
        const from = domOfOffset(start);
        const to = domOfOffset(end);
        const range = document.createRange();
        try
        {
            range.setStart(from.node, from.offset);
            range.setEnd(to.node, to.offset);
        }
        catch (error)
        {
            return null;
        }
        return range;
    }

    function paint(name, spans)
    {
        if (typeof CSS === "undefined" || !CSS.highlights || typeof Highlight !== "function")
        {
            return;
        }
        const highlight = new Highlight();
        for (const [start, end] of spans)
        {
            const range = domRange(start, end);
            if (range)
            {
                highlight.add(range);
            }
        }
        CSS.highlights.set(name, highlight);
    }

    function paintDecorations()
    {
        paint("thunder-read", state.occurrences.filter((o) => o.kind !== "write").map((o) => [o.start, o.end]));
        paint("thunder-write", state.occurrences.filter((o) => o.kind === "write").map((o) => [o.start, o.end]));
        const byClass = {};
        for (const [type, spans] of Object.entries(state.semantic))
        {
            const name = SEMANTIC[type];
            if (name)
            {
                byClass[name] = (byClass[name] || []).concat(spans);
            }
        }
        for (const name of new Set(Object.values(SEMANTIC)))
        {
            paint(`thunder-${name}`, byClass[name] || []);
        }
        const squiggle = (severity) => state.diagnostics
            .filter((d) => (severity === "warning") === (d.severity === "warning"))
            .map((d) =>
            {
                const start = model.offsetOf(state.text, d.line, d.character);
                const end = d.endLine !== undefined ? model.offsetOf(state.text, d.endLine, d.endCharacter) : start + 1;
                return [start, Math.max(end, Math.min(start + 1, state.text.length))];
            });
        paint("thunder-error", squiggle("error"));
        paint("thunder-warning", squiggle("warning"));
        // The find bar's matches: painted here too, so a redrawn line keeps them.
        const current = state.find.index >= 0 ? state.find.matches[state.find.index] : null;
        paint("thunder-find", state.find.matches
            .filter((match) => match !== current)
            .map((match) => [match.start, match.end]));
        paint("thunder-find-current", current ? [[current.start, current.end]] : []);
    }

    /** Brings the line the program is stopped on into view, without moving the caret. */
    /*
     * Brings the line the program is stopped on into view - only when it is out of view, as VS
     * Code's editor does: stepping through a loop must not move the page at every step.
     */
    function revealExecution()
    {
        const execution = state.execution;
        const element = execution ? code.children[execution.line] : null;
        if (!element || !element.scrollIntoView)
        {
            return;
        }
        const box = element.getBoundingClientRect();
        const visible = box.top >= 0 && box.bottom <= window.innerHeight;
        if (!visible)
        {
            element.scrollIntoView({ block: "center" });
        }
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
        refreshFind();
        draw(false);
        select(start, end);
        state.history.record(text, start, kind, Date.now());
        vscode.postMessage({ type: "edit", text });
        saveState({ text });
    }

    function saveState(extra)
    {
        if (vscode.setState)
        {
            vscode.setState(Object.assign({ text: state.text, findLoose: state.find.loose, zoom: state.zoom }, extra || {}));
        }
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
            afterTyping(data);
            return;
        }
        case "insertLineBreak":
        case "insertParagraph":
        {
            const result = model.newline(state.text, start, end, state.blockWords);
            commit(result.text, result.caret, result.caret, "newline");
            closeCompletion();
            hideSignature();
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
            refilterCompletion();
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
            refreshFind();
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
            refreshFind();
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
        if (event.key === "Escape" && !signature.hidden)
        {
            event.preventDefault();
            hideSignature();
            return;
        }
        const shortcut = shortcutFor(event);
        if (shortcut)
        {
            event.preventDefault();
            event.stopPropagation(); // the editor's own command, not VS Code's text editor one
            shortcut();
        }
    });

    /*
     * The shortcuts of VS Code's editor, done for this one: completion, parameter hints, go to
     * definition, references, rename, format, quick fix, symbols and line comments.
     */
    function shortcutFor(event)
    {
        const control = event.ctrlKey || event.metaKey;
        const key = event.key.length === 1 ? event.key.toLowerCase() : event.key;
        const plain = !control && !event.shiftKey && !event.altKey;
        if (control && !event.shiftKey && (key === " " || event.code === "Space"))
        {
            return () => openCompletion();
        }
        if (control && event.shiftKey && (key === " " || event.code === "Space"))
        {
            return () => showSignature(true);
        }
        if (plain && key === "F12")
        {
            return () => ask("definition");
        }
        if (!control && event.shiftKey && !event.altKey && key === "F12")
        {
            return () => ask("references");
        }
        if (plain && key === "F2")
        {
            return () => ask("rename");
        }
        if (!control && event.shiftKey && event.altKey && (key === "f" || event.code === "KeyF"))
        {
            return () => ask("format");
        }
        if (control && !event.shiftKey && key === ".")
        {
            return () => ask("quickFix");
        }
        if (control && !event.shiftKey && (key === "/" || event.code === "Slash"))
        {
            return toggleComment;
        }
        if (control && event.shiftKey && (key === "o" || event.code === "KeyO"))
        {
            return () => ask("symbols");
        }
        return null;
    }

    function toggleComment()
    {
        const { start, end } = selection();
        const result = model.toggleComment(state.text, start, end);
        if (result.text !== state.text)
        {
            commit(result.text, result.start, result.end, "comment");
        }
    }

    // A right click outside the selection puts the caret there first, as VS Code's editor does,
    // so a command chosen from the context menu (VS Code's own, with the extension's items:
    // package.json "webview/context") works on the word that was clicked.
    code.addEventListener("contextmenu", (event) =>
    {
        const offset = offsetAtPoint(event.clientX, event.clientY);
        const { start, end } = selection();
        if (offset !== null && (start === end || offset < start || offset > end))
        {
            select(offset);
        }
    });

    /** A command from the context menu or the palette (rtleditor.js `command`). */
    function runCommand(name)
    {
        code.focus();
        switch (name)
        {
        case "definition":
        case "references":
        case "rename":
        case "format":
        case "quickFix":
        case "symbols":
            ask(name);
            break;
        case "comment":
            toggleComment();
            break;
        case "find":
            openFind(false);
            break;
        case "replace":
            openFind(true);
            break;
        case "selectAll":
            select(0, state.text.length);
            break;
        default:
            break;
        }
    }

    // Ctrl+click goes to the definition of what is clicked, as in VS Code's editor.
    code.addEventListener("click", (event) =>
    {
        if ((event.ctrlKey || event.metaKey) && !gutterHit(event)?.inGutter)
        {
            const offset = offsetAtPoint(event.clientX, event.clientY);
            if (offset !== null)
            {
                event.preventDefault();
                ask("definition", { offset });
            }
        }
    });

    // ------------------------------------------------------------------ requests to the extension

    let requestSequence = 0;
    const waiting = new Map();

    /** Asks the extension for a language feature at the caret (or 'extra.offset'); resolves to its answer. */
    function ask(feature, extra)
    {
        const id = ++requestSequence;
        const request = Object.assign({ type: "language", id, feature, offset: selection().start }, extra || {});
        vscode.postMessage(request);
        return new Promise((resolve) =>
        {
            waiting.set(id, resolve);
            setTimeout(() =>
            {
                if (waiting.delete(id))
                {
                    resolve(null);
                }
            }, 15000);
        });
    }

    function answered(message)
    {
        const resolve = waiting.get(message.id);
        if (resolve)
        {
            waiting.delete(message.id);
            resolve(message.result);
        }
    }

    /** The text offset under a point of the window, or null. */
    function offsetAtPoint(x, y)
    {
        let node = null;
        let offset = 0;
        if (document.caretPositionFromPoint)
        {
            const position = document.caretPositionFromPoint(x, y);
            node = position && position.offsetNode;
            offset = position ? position.offset : 0;
        }
        else if (document.caretRangeFromPoint)
        {
            const range = document.caretRangeFromPoint(x, y);
            node = range && range.startContainer;
            offset = range ? range.startOffset : 0;
        }
        return node && code.contains(node) ? offsetOfDom(node, offset) : null;
    }

    /*
     * Where the caret is on the screen. A collapsed range has no box at the start of a line or on
     * an empty one, which is how a list ended up in the corner of the page; a zero-width marker is
     * measured instead, and the selection put back.
     */
    function caretRect()
    {
        const current = window.getSelection();
        if (!current || !current.rangeCount || !code.contains(current.anchorNode))
        {
            return code.getBoundingClientRect();
        }
        const range = current.getRangeAt(0).cloneRange();
        range.collapse(false);
        const rects = range.getClientRects();
        if (rects.length && rects[0].height > 0)
        {
            return rects[rects.length - 1];
        }
        const { start, end } = selection();
        const marker = document.createElement("span");
        marker.textContent = "\u200B";
        range.insertNode(marker);
        const rect = marker.getBoundingClientRect();
        const parent = marker.parentNode;
        marker.remove();
        parent.normalize();
        select(start, end);
        return rect;
    }

    /** Places a floating box by the caret: below it, or above when 'above'; at its reading side. */
    function placeAtCaret(box, above)
    {
        const rect = caretRect();
        box.hidden = false;
        const width = Math.min(box.offsetWidth, window.innerWidth - 8);
        const left = state.rightToLeft ? rect.right - width : rect.left;
        box.style.left = `${Math.max(4, Math.min(left, window.innerWidth - width - 4))}px`;
        box.style.top = above
            ? `${Math.max(0, rect.top + window.scrollY - box.offsetHeight - 2)}px`
            : `${rect.bottom + window.scrollY + 2}px`;
    }

    // Ctrl+S, Ctrl+P and the rest belong to VS Code: the webview passes them on by itself.

    // ------------------------------------------------------------------ debug hovers
    //
    // While a debugged program is stopped, resting the pointer on a name asks the debugger for
    // its value (VS Code does this in its own editor; a custom editor has to ask itself).

    let hoverTimer = null;
    let hoverSequence = 0;

    /**
     * The span under the pointer worth a hover: while the program is stopped a name (its value),
     * otherwise any word (what the language says about it, as the text editor's hover does).
     */
    function hoverTarget(event)
    {
        const selector = state.execution ? ".k-name" : ".k-name, .k-keyword, .k-builtin, .k-type, .k-literal";
        const element = event.target instanceof Element ? event.target.closest(selector) : null;
        return element && code.contains(element) && element.textContent.trim() ? element : null;
    }

    /** The expression a name span stands for: the name and the fields following it (p.x). */
    function hoverExpression(element)
    {
        let expression = element.textContent;
        let previous = element.previousSibling;
        // Walk backwards over ".field" chains: "p" "." "x" are separate spans.
        while (previous && previous.textContent === "." && previous.previousSibling
            && previous.previousSibling.nodeType === Node.ELEMENT_NODE && previous.previousSibling.classList.contains("k-name"))
        {
            expression = `${previous.previousSibling.textContent}.${expression}`;
            previous = previous.previousSibling.previousSibling;
        }
        return expression.trim();
    }

    function hideTooltip()
    {
        tooltip.hidden = true;
        state.hover = null;
    }

    function showTooltip(element, text)
    {
        tooltip.textContent = text;
        tooltip.dir = "auto";
        tooltip.hidden = false;
        const rect = element.getBoundingClientRect();
        tooltip.style.top = `${rect.bottom + window.scrollY + 4}px`;
        const width = Math.min(tooltip.offsetWidth, window.innerWidth - 8);
        const left = state.rightToLeft ? rect.right - width : rect.left;
        tooltip.style.left = `${Math.max(4, Math.min(left, window.innerWidth - width - 4))}px`;
    }

    code.addEventListener("mousemove", (event) =>
    {
        const element = hoverTarget(event);
        if (!element)
        {
            if (hoverTimer)
            {
                clearTimeout(hoverTimer);
                hoverTimer = null;
            }
            if (state.hover && (!(event.target instanceof Element) || !tooltip.contains(event.target)))
            {
                hideTooltip();
            }
            return;
        }
        if (state.hover && state.hover.element === element)
        {
            return; // still on the same name
        }
        if (hoverTimer)
        {
            clearTimeout(hoverTimer);
        }
        const offset = offsetAtPoint(event.clientX, event.clientY);
        hoverTimer = setTimeout(() =>
        {
            hoverTimer = null;
            const id = ++hoverSequence;
            state.hover = { id, element, expression: hoverExpression(element), offset };
            if (state.execution)
            {
                vscode.postMessage({ type: "hover", id, expression: state.hover.expression });
            }
            else
            {
                languageHover(state.hover);
            }
        }, 300);
    });

    code.addEventListener("mouseleave", () =>
    {
        if (hoverTimer)
        {
            clearTimeout(hoverTimer);
            hoverTimer = null;
        }
        hideTooltip();
    });

    /** The debugger's answer: shown by the name it was asked for, unless the pointer moved on. */
    function showHoverResult(message)
    {
        if (!state.hover || state.hover.id !== message.id)
        {
            return;
        }
        if (!message.value)
        {
            languageHover(state.hover); // not a variable: what the language says about the word
            return;
        }
        const label = message.valueType
            ? `${state.hover.expression}: ${message.valueType} = ${message.value}`
            : `${state.hover.expression} = ${message.value}`;
        showTooltip(state.hover.element, label);
    }

    async function languageHover(hover)
    {
        if (hover.offset === null || hover.offset === undefined)
        {
            hideTooltip();
            return;
        }
        const answer = await ask("hover", { offset: hover.offset });
        if (state.hover !== hover)
        {
            return; // the pointer moved on
        }
        if (!answer || !answer.markdown)
        {
            hideTooltip();
            return;
        }
        showTooltip(hover.element, plainMarkdown(answer.markdown));
        state.hover = hover;
    }

    /** Markdown as readable plain text: fences, emphasis and code marks dropped. */
    function plainMarkdown(markdown)
    {
        return markdown
            .replace(/```[a-z]*\n?/g, "")
            .replace(/\*\*(.*?)\*\*/g, "$1")
            .replace(/`([^`]*)`/g, "$1")
            .replace(/\n---\n/g, "\n")
            .trim();
    }

    // ------------------------------------------------------------------ the gutter

    /** The line element a mouse event is on, and whether it is on the number (the gutter). */
    function gutterHit(event)
    {
        let line = event.target instanceof Element ? event.target.closest(".line") : null;
        if (!line)
        {
            // Below the last line, or on the padding: find the line at that height.
            line = Array.from(code.children).find((candidate) =>
            {
                const box = candidate.getBoundingClientRect();
                return event.clientY >= box.top && event.clientY < box.bottom;
            }) || null;
        }
        if (!line)
        {
            return null;
        }
        const box = line.getBoundingClientRect();
        const gutter = parseFloat(getComputedStyle(line).paddingInlineStart) || 0;
        const inGutter = state.rightToLeft
            ? event.clientX > box.right - gutter
            : event.clientX < box.left + gutter;
        // The outermost band of the gutter, before the line number: a test's play button lives
        // there (the rest of the gutter stays the breakpoint's).
        const band = parseFloat(getComputedStyle(line).fontSize) * 1.1 || 16;
        const index = Array.prototype.indexOf.call(code.children, line);
        const onTest = inGutter && state.tests.some((candidate) => candidate.line === index)
            && (state.rightToLeft ? event.clientX > box.right - band : event.clientX < box.left + band);
        return { index, inGutter, onTest };
    }

    code.addEventListener("mousedown", (event) =>
    {
        if (event.button !== 0)
        {
            return;
        }
        const hit = gutterHit(event);
        if (hit && hit.onTest)
        {
            event.preventDefault();
            vscode.postMessage({ type: "runTest", line: hit.index });
            return;
        }
        if (hit && hit.inGutter)
        {
            event.preventDefault(); // the caret stays where it is
            vscode.postMessage({ type: "toggleBreakpoint", line: hit.index });
        }
    });

    // ------------------------------------------------------------------ completion
    //
    // The items are the extension's completion providers' own (the list VS Code's editor shows),
    // asked for when the list opens - Ctrl+Space, a word begun, a "." typed - and filtered here
    // while the word goes on.

    const WORD = /[\p{L}\p{N}_]/u;

    /** What typing a piece of text opens: completion after a word character or '.', parameter hints after '(' or ','. */
    function afterTyping(data)
    {
        if (state.completion)
        {
            refilterCompletion();
        }
        else if (data === ".")
        {
            openCompletion(".");
        }
        else if (data.length === 1 && WORD.test(data) && !WORD.test(state.text[selection().start - 2] || ""))
        {
            openCompletion(); // the first letter of a word
        }
        if (data === "(" || data === "," || data === "\u060C")
        {
            showSignature(false);
        }
        else if (data === ")")
        {
            hideSignature();
        }
        else if (!signature.hidden)
        {
            showSignature(false);
        }
    }

    function localWordStart(offset)
    {
        let start = offset;
        while (start > 0 && WORD.test(state.text[start - 1]))
        {
            --start;
        }
        return start;
    }

    /*
     * Whether what has been typed since the list opened still belongs to one item: word
     * characters, and spaces too while some item's name goes on with them - Pashto built-ins
     * are often several words (فایل شته, کرښه چاپ).
     */
    function stillCompleting(prefix, items)
    {
        if ([...prefix].every((character) => WORD.test(character)))
        {
            return true;
        }
        if (/[^\p{L}\p{N}_ \u200C]/u.test(prefix))
        {
            return false;
        }
        const wanted = model.foldLetters(prefix);
        return items.some((item) => model.foldLetters(item.filterText || item.label).startsWith(wanted));
    }

    async function openCompletion(trigger)
    {
        const { start, end } = selection();
        if (start !== end)
        {
            closeCompletion();
            return;
        }
        const answer = await ask("completion", { offset: start, trigger: trigger || "" });
        if (!answer || !answer.items || !answer.items.length)
        {
            closeCompletion();
            return;
        }
        // The word is measured here, in the page's own text: the document may still lack the
        // letter just typed, and a start taken from it would leave that letter behind.
        state.completion = { wordStart: trigger === "." ? start : localWordStart(start), items: answer.items, shown: [], index: 0 };
        refilterCompletion();
    }

    function refilterCompletion()
    {
        const completion = state.completion;
        if (!completion)
        {
            return;
        }
        const caret = selection().start;
        const prefix = caret >= completion.wordStart ? state.text.slice(completion.wordStart, caret) : null;
        if (prefix === null || !stillCompleting(prefix, completion.items))
        {
            closeCompletion();
            return;
        }
        completion.shown = model.filterCompletions(prefix, completion.items, 200);
        if (!completion.shown.length || (completion.shown.length === 1 && completion.shown[0].label === prefix))
        {
            closeCompletion();
            return;
        }
        completion.index = 0;
        drawCompletion();
    }

    function drawCompletion()
    {
        const completion = state.completion;
        popup.replaceChildren();
        completion.shown.forEach((item, index) =>
        {
            const row = document.createElement("div");
            row.className = `item${index === completion.index ? " selected" : ""}`;
            row.dir = "auto";
            const label = document.createElement("span");
            label.className = `k-${item.kind}`;
            label.textContent = item.label;
            row.appendChild(label);
            if (item.detail)
            {
                const detail = document.createElement("span");
                detail.className = "detail";
                detail.textContent = item.detail;
                row.appendChild(detail);
            }
            row.title = item.documentation || "";
            row.addEventListener("mousedown", (event) =>
            {
                event.preventDefault();
                completion.index = index;
                accept();
            });
            popup.appendChild(row);
        });
        placeAtCaret(popup, false);
        const selected = popup.children[completion.index];
        if (selected && selected.scrollIntoView)
        {
            selected.scrollIntoView({ block: "nearest" });
        }
    }

    function closeCompletion()
    {
        state.completion = null;
        popup.hidden = true;
    }

    /** Inserts the chosen item over the word typed so far; a function's snippet leaves the caret between its brackets. */
    function accept()
    {
        const completion = state.completion;
        const item = completion.shown[completion.index];
        const { end } = selection();
        closeCompletion();
        // The typed word is replaced. A provider's range that starts earlier (an import path
        // after its quote, say) is kept only when the item's text really covers what lies before.
        const before = item.start < completion.wordStart ? state.text.slice(item.start, completion.wordStart) : "";
        const from = before && item.text.startsWith(before) ? item.start : completion.wordStart;
        const to = end;
        const caret = from + item.caret;
        commit(model.applyEdit(state.text, from, to, item.text), caret, caret, "complete");
        if (item.text.includes("("))
        {
            showSignature(false);
        }
    }

    /** Arrow keys, Enter, Tab and Escape while the list is open. True if the key was used. */
    function handleCompletionKey(event)
    {
        const completion = state.completion;
        const count = completion.shown.length;
        switch (event.key)
        {
        case "ArrowDown":
            completion.index = (completion.index + 1) % count;
            break;
        case "ArrowUp":
            completion.index = (completion.index - 1 + count) % count;
            break;
        case "PageDown":
            completion.index = Math.min(count - 1, completion.index + 10);
            break;
        case "PageUp":
            completion.index = Math.max(0, completion.index - 10);
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

    // ------------------------------------------------------------------ parameter hints

    async function showSignature(explicit)
    {
        const answer = await ask("signature");
        if (!answer)
        {
            if (explicit || !signature.hidden)
            {
                hideSignature();
            }
            return;
        }
        signature.replaceChildren();
        const label = document.createElement("div");
        label.className = "signature-label";
        label.dir = "auto";
        const highlight = answer.highlight;
        if (highlight)
        {
            label.append(answer.label.slice(0, highlight.start));
            const active = document.createElement("b");
            active.textContent = answer.label.slice(highlight.start, highlight.end);
            label.append(active, answer.label.slice(highlight.end));
        }
        else
        {
            label.textContent = answer.label;
        }
        signature.appendChild(label);
        if (answer.documentation)
        {
            const documentation = document.createElement("div");
            documentation.className = "detail";
            documentation.dir = "auto";
            documentation.textContent = plainMarkdown(answer.documentation);
            signature.appendChild(documentation);
        }
        placeAtCaret(signature, true);
    }

    function hideSignature()
    {
        signature.hidden = true;
    }

    // ------------------------------------------------------------------ zoom
    //
    // The editor's own zoom: Ctrl+= / Ctrl+- / Ctrl+0 arrive as "zoom" messages from
    // keybindings scoped to this editor (package.json), so VS Code's window zoom never fires;
    // Ctrl+wheel works here in the page. Only this editor's text changes size.

    function applyZoom()
    {
        code.style.fontSize = state.zoom === 0
            ? ""
            : `calc(var(--vscode-editor-font-size, 14px) * ${Math.pow(1.1, state.zoom).toFixed(3)})`;
    }

    function setZoom(level)
    {
        state.zoom = Math.max(-8, Math.min(12, Math.round(level) || 0));
        applyZoom();
        saveState();
    }

    window.addEventListener("wheel", (event) =>
    {
        if (event.ctrlKey)
        {
            event.preventDefault();
            setZoom(state.zoom + (event.deltaY < 0 ? 1 : -1));
        }
    }, { passive: false });

    // ------------------------------------------------------------------ find
    //
    // The editor's own find bar (Ctrl+F). VS Code's find widget for webviews searches the
    // rendered page and knows nothing of Pashto, so this one searches the text itself
    // (shared/search.js) and offers loose letters in Pashto mode: ی finds ي, ى, ۍ, ې and ئ,
    // ګ finds گ, and the diacritics are ignored. The matches are painted like the occurrences.

    /** Recomputes the matches of the find bar's query; nothing is scrolled or repainted here. */
    function computeFind(keep)
    {
        const kept = keep && state.find.index >= 0 ? state.find.matches[state.find.index] : null;
        state.find.matches = findBar.hidden || !findInput.value
            ? []
            : search.findMatches(state.text, findInput.value, state.find.loose);
        state.find.index = state.find.matches.length ? 0 : -1;
        if (kept)
        {
            const found = state.find.matches.findIndex((match) => match.start === kept.start);
            if (found >= 0)
            {
                state.find.index = found;
            }
        }
        findCount.textContent = state.find.matches.length
            ? `${state.find.index + 1}/${state.find.matches.length}`
            : (findInput.value ? "0/0" : "");
    }

    /** The text changed: the matches follow it (an edit never grabs the view, so no scrolling). */
    function refreshFind()
    {
        if (!findBar.hidden)
        {
            computeFind(true);
        }
    }

    /** What typing in the find bar does: new matches, painted, the first brought into view. */
    function updateFind()
    {
        computeFind(false);
        paintDecorations();
        revealMatch();
    }

    /** Brings the match the view stands on into sight - only when it is out of view. */
    function revealMatch()
    {
        const match = state.find.index >= 0 ? state.find.matches[state.find.index] : null;
        if (!match)
        {
            return;
        }
        const element = code.children[model.positionOf(state.text, match.start).line];
        if (!element || !element.scrollIntoView)
        {
            return;
        }
        const box = element.getBoundingClientRect();
        if (box.top < 0 || box.bottom > window.innerHeight)
        {
            element.scrollIntoView({ block: "center" });
        }
    }

    function moveFind(step)
    {
        if (!state.find.matches.length)
        {
            return;
        }
        state.find.index = (state.find.index + step + state.find.matches.length) % state.find.matches.length;
        findCount.textContent = `${state.find.index + 1}/${state.find.matches.length}`;
        paintDecorations();
        revealMatch();
    }

    /** Ctrl+F opens the bar to find; Ctrl+H (withReplace) unfolds the replace row as well. */
    function openFind(withReplace)
    {
        const { start, end } = selection();
        const selected = state.text.slice(start, end);
        findBar.hidden = false;
        if (withReplace !== undefined)
        {
            showReplace(Boolean(withReplace));
        }
        if (selected && !selected.includes("\n"))
        {
            findInput.value = selected;
        }
        findInput.focus();
        findInput.select();
        updateFind();
    }

    function closeFind()
    {
        findBar.hidden = true;
        state.find.matches = [];
        state.find.index = -1;
        paintDecorations();
        code.focus();
    }

    // ------------------------------------------------------------------ replace

    function showReplace(shown)
    {
        replaceRow.hidden = !shown;
        findExpand.textContent = shown ? "▾" : "▸";
        findExpand.setAttribute("aria-expanded", String(shown));
    }

    /** Replaces the match the view stands on and moves to the next one, as VS Code does. */
    function replaceCurrent()
    {
        const match = state.find.index >= 0 ? state.find.matches[state.find.index] : null;
        if (!match)
        {
            return;
        }
        const replacement = replaceInput.value;
        const caret = match.start + replacement.length;
        commit(model.applyEdit(state.text, match.start, match.end, replacement), caret, caret, "replace");
        // commit() recomputed the matches; stand on the first one at or after the replacement.
        const next = state.find.matches.findIndex((candidate) => candidate.start >= match.start);
        state.find.index = state.find.matches.length ? (next >= 0 ? next : 0) : -1;
        computeCount();
        paintDecorations();
        revealMatch();
    }

    /** Replaces every match in one edit (one Undo takes it all back). */
    function replaceEverything()
    {
        if (!state.find.matches.length)
        {
            return;
        }
        const replacement = replaceInput.value;
        let text = state.text;
        for (const match of [...state.find.matches].reverse())
        {
            text = model.applyEdit(text, match.start, match.end, replacement);
        }
        const caret = state.find.matches[0].start;
        commit(text, caret, caret, "replace");
        computeCount();
        paintDecorations();
    }

    function computeCount()
    {
        findCount.textContent = state.find.matches.length
            ? `${state.find.index + 1}/${state.find.matches.length}`
            : (findInput.value ? "0/0" : "");
    }

    function setLooseFind(loose)
    {
        state.find.loose = Boolean(loose);
        findLoose.setAttribute("aria-pressed", String(state.find.loose));
        findLoose.classList.toggle("on", state.find.loose);
        saveState();
        if (!findBar.hidden)
        {
            updateFind();
        }
    }

    findInput.addEventListener("input", updateFind);
    findInput.addEventListener("keydown", (event) =>
    {
        if (event.key === "Enter")
        {
            event.preventDefault();
            moveFind(event.shiftKey ? -1 : 1);
        }
        else if (event.key === "Escape")
        {
            event.preventDefault();
            closeFind();
        }
    });
    findLoose.addEventListener("click", () => setLooseFind(!state.find.loose));
    document.getElementById("find-previous").addEventListener("click", () => moveFind(-1));
    document.getElementById("find-next").addEventListener("click", () => moveFind(1));
    document.getElementById("find-close").addEventListener("click", closeFind);
    findExpand.addEventListener("click", () => showReplace(replaceRow.hidden));
    document.getElementById("replace-one").addEventListener("click", replaceCurrent);
    document.getElementById("replace-all").addEventListener("click", replaceEverything);
    replaceInput.addEventListener("keydown", (event) =>
    {
        if (event.key === "Enter")
        {
            event.preventDefault();
            if (event.ctrlKey && event.altKey)
            {
                replaceEverything();
            }
            else
            {
                replaceCurrent();
            }
        }
        else if (event.key === "Escape")
        {
            event.preventDefault();
            closeFind();
        }
    });

    window.addEventListener("keydown", (event) =>
    {
        const control = event.ctrlKey || event.metaKey;
        if (control && !event.altKey && !event.shiftKey && (event.key === "f" || event.key === "F"))
        {
            event.preventDefault();
            openFind(false);
            return;
        }
        if (control && !event.altKey && !event.shiftKey && (event.key === "h" || event.key === "H"))
        {
            event.preventDefault();
            openFind(true);
            return;
        }
        if (event.key === "F3" && !findBar.hidden)
        {
            event.preventDefault();
            moveFind(event.shiftKey ? -1 : 1);
            return;
        }
        // Escape while the editor has focus: the completion list and the hints come first.
        if (event.key === "Escape" && !findBar.hidden && !state.completion && signature.hidden)
        {
            event.preventDefault();
            closeFind();
        }
    });

    // ------------------------------------------------------------------ occurrences and colours

    let occurrenceTimer = null;
    document.addEventListener("selectionchange", () =>
    {
        if (document.activeElement !== code)
        {
            return;
        }
        clearTimeout(occurrenceTimer);
        occurrenceTimer = setTimeout(async () =>
        {
            const { start, end } = selection();
            const occurrences = start === end ? await ask("highlights", { offset: start }) : [];
            state.occurrences = occurrences || [];
            paintDecorations();
        }, 250);
    });

    async function refreshSemanticColours()
    {
        state.semantic = (await ask("semanticTokens")) || {};
        paintDecorations();
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
            state.diagnostics = message.diagnostics || [];
            state.breakpoints = message.breakpoints || [];
            state.execution = message.execution || null;
            state.text = message.text || "";
            state.history = new model.History(state.text, 0);
            document.documentElement.dir = state.rightToLeft ? "rtl" : "ltr";
            document.documentElement.lang = message.language || "";
            code.dir = state.rightToLeft ? "rtl" : "ltr";
            applyZoom();
            state.tests = message.tests || [];
            findLoose.hidden = !state.rightToLeft; // loose letters are a Pashto affair
            findLoose.setAttribute("aria-pressed", String(state.find.loose));
            findLoose.classList.toggle("on", state.find.loose);
            draw(true);
            drawProblems();
            code.focus();
            select(0);
            revealExecution();
            vscode.postMessage({ type: "ready" });
            refreshSemanticColours();
            break;
        case "update":
            // Another editor (the text editor, Undo in the Edit menu, a formatter) changed it.
            if (typeof message.text === "string" && message.text !== state.text)
            {
                const caret = selection();
                state.text = message.text;
                state.history.reset(message.text, caret.start);
                refreshFind();
                draw(false);
                const clamp = (offset) => Math.min(offset, state.text.length);
                if (document.activeElement === code)
                {
                    select(clamp(caret.start), clamp(caret.end));
                }
            }
            break;
        case "diagnostics":
            // A new analysis: new problems, and the semantic colours of the program as it is now.
            state.diagnostics = message.diagnostics || [];
            draw(false);
            drawProblems();
            refreshSemanticColours();
            break;
        case "languageResult":
            answered(message);
            break;
        case "breakpoints":
            state.breakpoints = message.breakpoints || [];
            draw(false);
            break;
        case "execution":
            state.execution = message.execution || null;
            hideTooltip(); // the values may have changed
            draw(false);
            revealExecution();
            break;
        case "hoverResult":
            showHoverResult(message);
            break;
        case "command":
            runCommand(String(message.name || ""));
            break;
        case "zoom":
            setZoom(message.step === 0 ? 0 : state.zoom + (message.step > 0 ? 1 : -1));
            break;
        case "tests":
            state.tests = message.tests || [];
            draw(false);
            break;
        case "reveal":
            select(typeof message.offset === "number"
                ? Math.min(message.offset, state.text.length)
                : model.offsetOf(state.text, message.line || 0, message.character || 0));
            code.focus();
            break;
        default:
            break;
        }
    });

    // Tell the extension the page is ready for its first message.
    vscode.postMessage({ type: "loaded" });

    // For the tests: the editor's state, read from outside.
    globalThis.thunderEditor = {
        state, selection, select, draw, gutterHit, hoverExpression, showHoverResult, caretRect, offsetAtPoint, openCompletion,
        openFind, closeFind, moveFind, setLooseFind, showReplace, replaceCurrent, replaceEverything, runCommand, setZoom,
    };
}());
