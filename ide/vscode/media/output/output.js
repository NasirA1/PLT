// The Thunder Output view: the program's output, aligned to the program's side (src/outputview.js).
//
// Output arrives in pieces; each line is its own element, so it can take the direction of its
// own text (dir="auto": a Pashto line from the right, an English error message from the left)
// while the alignment of every line is the program's - right for Pashto, like the Thunder IDE's
// Output panel, so a line that is only a number does not fall to the other side.
//
// The page also carries what the Debug Console has and a webview view cannot be given:
// Ctrl+= / Ctrl+- zoom the text (Ctrl+0 puts it back), and Ctrl+F opens a find bar - VS Code's
// find widget exists only for editor webviews, not for views in the panel. In Pashto mode the
// find bar offers loose letters (shared/search.js): ی finds ي, ى, ۍ, ې and ئ too.

"use strict";

(function output()
{
    const vscode = acquireVsCodeApi();
    const search = globalThis.ThunderSearch;
    const log = document.getElementById("output");
    const row = document.getElementById("input-row");
    const field = document.getElementById("input");
    const findBar = document.getElementById("find-bar");
    const findInput = document.getElementById("find-input");
    const findCount = document.getElementById("find-count");
    const findLoose = document.getElementById("find-loose");
    let line = null; // the line still being written (no line break yet)

    const stored = (vscode.getState && vscode.getState()) || {};
    const state = {
        zoom: clampZoom(stored.zoom || 0),
        loose: Boolean(stored.loose),
        rightToLeft: false,
        matches: [],      // [{element, start, end}] of the find query, in reading order
        index: -1,        // the match the view stands on
    };

    function remember()
    {
        if (vscode.setState)
        {
            vscode.setState({ zoom: state.zoom, loose: state.loose });
        }
    }

    // ------------------------------------------------------------------ the output itself

    function newLine(category)
    {
        line = document.createElement("div");
        line.className = `line ${category}`;
        line.dir = "auto";
        log.appendChild(line);
        return line;
    }

    function append(text, category)
    {
        const pieces = text.split("\n");
        pieces.forEach((piece, index) =>
        {
            if (index > 0)
            {
                line = null; // a line break ends the line
            }
            if (piece === "" && index === pieces.length - 1)
            {
                return; // the text ended with a line break
            }
            if (!line || !line.classList.contains(category))
            {
                newLine(category);
            }
            line.textContent += piece;
        });
        window.scrollTo(0, document.body.scrollHeight);
        if (!findBar.hidden)
        {
            updateFind(true);
        }
    }

    // ------------------------------------------------------------------ zoom

    function clampZoom(level)
    {
        return Math.max(-8, Math.min(12, Math.round(level) || 0));
    }

    function applyZoom()
    {
        // The factor multiplies the view's own size (a bare percentage would be read against
        // the page's root, not against the editor font size the stylesheet uses).
        document.body.style.fontSize = state.zoom === 0
            ? ""
            : `calc(var(--vscode-editor-font-size, 13px) * ${Math.pow(1.1, state.zoom).toFixed(3)})`;
    }

    function setZoom(level)
    {
        state.zoom = clampZoom(level);
        applyZoom();
        remember();
    }

    // ------------------------------------------------------------------ find

    /** Every match in the log, each on the line that holds it, in reading order. */
    function findAll(query)
    {
        const matches = [];
        for (const element of log.children)
        {
            for (const span of search.findMatches(element.textContent, query, state.loose))
            {
                matches.push({ element, start: span.start, end: span.end });
            }
        }
        return matches;
    }

    /** A DOM range over [start, end) of a line's text, however the text nodes fall. */
    function rangeIn(element, start, end)
    {
        const range = document.createRange();
        const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT);
        let offset = 0;
        let node = walker.nextNode();
        let placed = 0;
        while (node && placed < 2)
        {
            const length = node.nodeValue.length;
            if (placed === 0 && start <= offset + length)
            {
                range.setStart(node, start - offset);
                placed = 1;
            }
            if (placed === 1 && end <= offset + length)
            {
                range.setEnd(node, end - offset);
                placed = 2;
            }
            offset += length;
            node = walker.nextNode();
        }
        return placed === 2 ? range : null;
    }

    function paintMatches()
    {
        if (typeof CSS === "undefined" || !CSS.highlights || typeof Highlight !== "function")
        {
            return;
        }
        const all = new Highlight();
        const current = new Highlight();
        state.matches.forEach((match, index) =>
        {
            const range = rangeIn(match.element, match.start, match.end);
            if (range)
            {
                (index === state.index ? current : all).add(range);
            }
        });
        CSS.highlights.set("thunder-find", all);
        CSS.highlights.set("thunder-find-current", current);
    }

    function clearMatches()
    {
        if (typeof CSS !== "undefined" && CSS.highlights)
        {
            CSS.highlights.delete("thunder-find");
            CSS.highlights.delete("thunder-find-current");
        }
    }

    /** Recomputes the matches of what is typed; `keep` holds on to the match the view stands on. */
    function updateFind(keep)
    {
        const kept = keep && state.index >= 0 ? state.matches[state.index] : null;
        state.matches = findInput.value ? findAll(findInput.value) : [];
        state.index = state.matches.length ? 0 : -1;
        if (kept)
        {
            const found = state.matches.findIndex((match) =>
                match.element === kept.element && match.start === kept.start);
            if (found >= 0)
            {
                state.index = found;
            }
        }
        findCount.textContent = state.matches.length ? `${state.index + 1}/${state.matches.length}` : "0/0";
        paintMatches();
    }

    function moveFind(step)
    {
        if (!state.matches.length)
        {
            return;
        }
        state.index = (state.index + step + state.matches.length) % state.matches.length;
        findCount.textContent = `${state.index + 1}/${state.matches.length}`;
        paintMatches();
        state.matches[state.index].element.scrollIntoView({ block: "nearest" });
    }

    function openFind()
    {
        const selected = String(window.getSelection() || "");
        findBar.hidden = false;
        if (selected && !selected.includes("\n"))
        {
            findInput.value = selected;
        }
        findInput.focus();
        findInput.select();
        updateFind(false);
    }

    function closeFind()
    {
        findBar.hidden = true;
        state.matches = [];
        state.index = -1;
        clearMatches();
    }

    function setLoose(loose)
    {
        state.loose = Boolean(loose);
        findLoose.setAttribute("aria-pressed", String(state.loose));
        findLoose.classList.toggle("on", state.loose);
        remember();
        if (!findBar.hidden)
        {
            updateFind(false);
        }
    }

    // ------------------------------------------------------------------ keys and buttons

    window.addEventListener("keydown", (event) =>
    {
        const control = event.ctrlKey || event.metaKey;
        if (control && !event.altKey && (event.key === "f" || event.key === "F"))
        {
            event.preventDefault();
            openFind();
            return;
        }
        if (control && !event.altKey
            && (event.key === "+" || event.key === "=" || event.code === "NumpadAdd"))
        {
            event.preventDefault();
            setZoom(state.zoom + 1);
            return;
        }
        if (control && !event.altKey && (event.key === "-" || event.code === "NumpadSubtract"))
        {
            event.preventDefault();
            setZoom(state.zoom - 1);
            return;
        }
        if (control && !event.altKey && (event.key === "0" || event.code === "Numpad0"))
        {
            event.preventDefault();
            setZoom(0);
            return;
        }
        if (event.key === "F3" && !findBar.hidden)
        {
            event.preventDefault();
            moveFind(event.shiftKey ? -1 : 1);
            return;
        }
        if (event.key === "Escape" && !findBar.hidden)
        {
            event.preventDefault();
            closeFind();
        }
    });

    findInput.addEventListener("input", () => updateFind(false));
    findInput.addEventListener("keydown", (event) =>
    {
        if (event.key === "Enter")
        {
            event.preventDefault();
            moveFind(event.shiftKey ? -1 : 1);
        }
    });
    findLoose.addEventListener("click", () => setLoose(!state.loose));
    document.getElementById("find-previous").addEventListener("click", () => moveFind(-1));
    document.getElementById("find-next").addEventListener("click", () => moveFind(1));
    document.getElementById("find-close").addEventListener("click", closeFind);

    // ------------------------------------------------------------------ the extension

    row.addEventListener("submit", (event) =>
    {
        event.preventDefault();
        const text = field.value;
        append(`${text}\n`, "echo");
        field.value = "";
        row.hidden = true;
        vscode.postMessage({ type: "input", text });
    });

    window.addEventListener("message", (event) =>
    {
        const message = event.data || {};
        switch (message.type)
        {
        case "begin":
            log.replaceChildren();
            line = null;
            state.rightToLeft = Boolean(message.rightToLeft);
            document.documentElement.dir = state.rightToLeft ? "rtl" : "ltr";
            document.body.classList.toggle("right-to-left", state.rightToLeft);
            findLoose.hidden = !state.rightToLeft; // loose letters are a Pashto affair
            row.hidden = true;
            if (!findBar.hidden)
            {
                updateFind(false);
            }
            break;
        case "append":
            append(message.text, message.category === "stdout" ? "stdout" : message.category);
            break;
        case "input":
            row.hidden = false;
            field.focus();
            break;
        case "end":
            row.hidden = true;
            if (message.text)
            {
                append(message.text, "console");
            }
            break;
        default:
            break;
        }
    });

    applyZoom();
    setLoose(state.loose);
    vscode.postMessage({ type: "ready" });
    globalThis.thunderOutput = { append, log, openFind, closeFind, moveFind, setLoose, setZoom, state };
}());
