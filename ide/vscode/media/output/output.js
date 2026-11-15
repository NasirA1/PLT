// The Thunder Output view: the program's output, aligned to the program's side (src/outputview.js).
//
// Output arrives in pieces; each line is its own element, so it can take the direction of its
// own text (dir="auto": a Pashto line from the right, an English error message from the left)
// while the alignment of every line is the program's - right for Pashto, like the Thunder IDE's
// Output panel, so a line that is only a number does not fall to the other side.

"use strict";

(function output()
{
    const vscode = acquireVsCodeApi();
    const log = document.getElementById("output");
    const row = document.getElementById("input-row");
    const field = document.getElementById("input");
    let line = null; // the line still being written (no line break yet)

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
    }

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
            document.documentElement.dir = message.rightToLeft ? "rtl" : "ltr";
            document.body.classList.toggle("right-to-left", Boolean(message.rightToLeft));
            row.hidden = true;
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

    vscode.postMessage({ type: "ready" });
    globalThis.thunderOutput = { append, log };
}());
