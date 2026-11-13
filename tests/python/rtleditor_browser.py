"""Drives the VS Code extension's right-to-left editor in a real browser engine (Qt WebEngine).

Run by tests/python/test_vscode_extension.py in a process of its own (Qt WebEngine must be set
up before any QApplication exists, and the other Qt tests have one). It loads the editor's page
exactly as the extension does - direction.js, model.js and editor.js - with a stand-in for VS
Code's ``acquireVsCodeApi`` that records what the page sends, then plays scenarios: typing,
Enter, Backspace, undo, completion, a change from another editor, the compiler's problems, and
- the point of it all - where the tokens of a Pashto line actually land on the screen.

Prints one JSON object: {"scenario": {"ok": bool, "detail": ...}, ...}.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

EXTENSION = Path(__file__).resolve().parents[2] / "ide" / "vscode"

PAGE = """<!DOCTYPE html>
<html><head><meta charset="UTF-8">
<style>{css}</style>
<style>:root {{ --thunder-font-family: "DejaVu Sans", sans-serif; }}</style>
</head><body>
<div id="code" contenteditable="true" spellcheck="false"></div>
<div id="problems" hidden></div>
<div id="completion" hidden></div>
<script>
window.sent = [];
window.acquireVsCodeApi = () => ({{
    postMessage: (message) => window.sent.push(message),
    setState: () => {{}},
}});
</script>
<script>{direction}</script>
<script>{model}</script>
<script>{editor}</script>
</body></html>"""

# Each scenario is JavaScript that returns {ok, detail}. They run in order on one page.
SCENARIOS = {
    "init draws one line per line, numbered, right to left": r"""
        const lines = document.querySelectorAll('#code .line');
        const dir = document.getElementById('code').dir;
        return {ok: lines.length === 3 && dir === 'rtl' && document.documentElement.dir === 'rtl',
                detail: {lines: lines.length, dir}};
    """,
    "the line numbers stand at the right of a right-to-left program": r"""
        const line = document.querySelectorAll('#code .line')[1];
        const number = getComputedStyle(line, '::before');
        const box = line.getBoundingClientRect();
        const first = line.querySelector('.tok').getBoundingClientRect();
        // The number's box is absolutely placed; where it is shows in its 'right' being 0.
        return {ok: number.right === '0px' && first.right < box.right - 30,
                detail: {right: number.right, left: number.left, lineRight: box.right, firstTokenRight: first.right}};
    """,
    "a Pashto line reads from the right: keyword rightmost, value leftmost": r"""
        const line = document.querySelectorAll('#code .line')[1];
        const tokens = Array.from(line.querySelectorAll('.tok')).map(t => [t.textContent, t.getBoundingClientRect()]);
        const rights = tokens.map(([text, rect]) => [text, Math.round(rect.right)]);
        let ok = tokens.length >= 3;
        for (let i = 1; i < tokens.length; ++i) ok = ok && tokens[i - 1][1].left > tokens[i][1].left;
        return {ok, detail: rights};
    """,
    "a Latin name inside a Pashto line stays in order, not reversed": r"""
        const line = document.querySelectorAll('#code .line')[2];
        const toks = Array.from(line.querySelectorAll('.tok'));
        const order = toks.map(t => [t.textContent, Math.round(t.getBoundingClientRect().left)]);
        const name = toks.find(t => t.textContent === 'name');
        const keyword = toks.find(t => t.textContent === 'متغیر');
        const value = toks.find(t => t.textContent === '-۳');
        const ok = name && keyword && value
            && keyword.getBoundingClientRect().left > name.getBoundingClientRect().left
            && name.getBoundingClientRect().left > value.getBoundingClientRect().left;
        return {ok, detail: order};
    """,
    "a negative number keeps its sign on the left": r"""
        const value = Array.from(document.querySelectorAll('#code .line')[2].querySelectorAll('.tok'))
            .find(t => t.textContent === '-۳');
        const range = document.createRange();
        const text = value.firstChild;           // the "-" text node
        range.setStart(text, 0); range.setEnd(text, 1);
        const sign = range.getBoundingClientRect();
        const digit = value.querySelector('.k-number').getBoundingClientRect();
        return {ok: sign.left < digit.left, detail: {sign: sign.left, digit: digit.left}};
    """,
    "typing inserts at the caret and tells the extension": r"""
        const editor = window.thunderEditor;
        const end = editor.state.text.indexOf('\n');          // end of the first line
        document.getElementById('code').focus();
        editor.select(end);
        window.sent = [];
        for (const ch of ' ۱') {
            document.getElementById('code').dispatchEvent(new InputEvent('beforeinput',
                {inputType: 'insertText', data: ch, cancelable: true, bubbles: true}));
        }
        const firstLine = editor.state.text.split('\n')[0];
        const edits = window.sent.filter(m => m.type === 'edit');
        const caret = editor.selection();
        return {ok: firstLine.endsWith(' ۱') && edits.length === 2
                    && edits[1].text === editor.state.text && caret.start === end + 2,
                detail: {firstLine, edits: edits.length, caret, expected: end + 2}};
    """,
    "Enter after a line that opens a block indents the next line": r"""
        const editor = window.thunderEditor;
        editor.state.text = 'که ن > ۰';
        editor.draw(true);
        editor.select(editor.state.text.length);
        document.getElementById('code').dispatchEvent(new InputEvent('beforeinput',
            {inputType: 'insertParagraph', cancelable: true, bubbles: true}));
        const caret = editor.selection();
        return {ok: editor.state.text === 'که ن > ۰\n    ' && caret.start === editor.state.text.length
                    && document.querySelectorAll('#code .line').length === 2,
                detail: {text: editor.state.text, caret}};
    """,
    "Backspace in the indentation takes a whole level": r"""
        const editor = window.thunderEditor;
        document.getElementById('code').dispatchEvent(new InputEvent('beforeinput',
            {inputType: 'deleteContentBackward', cancelable: true, bubbles: true}));
        return {ok: editor.state.text === 'که ن > ۰\n', detail: editor.state.text};
    """,
    "Ctrl+Z undoes and Ctrl+Y redoes": r"""
        const editor = window.thunderEditor;
        const code = document.getElementById('code');
        code.dispatchEvent(new KeyboardEvent('keydown', {key: 'z', ctrlKey: true, cancelable: true, bubbles: true}));
        const undone = editor.state.text;
        code.dispatchEvent(new KeyboardEvent('keydown', {key: 'y', ctrlKey: true, cancelable: true, bubbles: true}));
        const redone = editor.state.text;
        return {ok: undone === 'که ن > ۰\n    ' && redone === 'که ن > ۰\n',
                detail: {undone, redone}};
    """,
    "Tab inserts four spaces": r"""
        const editor = window.thunderEditor;
        editor.select(editor.state.text.length);
        document.getElementById('code').dispatchEvent(new KeyboardEvent('keydown',
            {key: 'Tab', cancelable: true, bubbles: true}));
        return {ok: editor.state.text === 'که ن > ۰\n    ', detail: editor.state.text};
    """,
    "typing the start of a built-in offers it, and Enter accepts it": r"""
        const editor = window.thunderEditor;
        const code = document.getElementById('code');
        editor.select(editor.state.text.length);
        for (const ch of 'کر') {
            code.dispatchEvent(new InputEvent('beforeinput', {inputType: 'insertText', data: ch, cancelable: true, bubbles: true}));
        }
        const popup = document.getElementById('completion');
        const offered = Array.from(popup.querySelectorAll('.item')).map(i => i.textContent);
        const visible = !popup.hidden;
        code.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', cancelable: true, bubbles: true}));
        const text = editor.state.text;
        return {ok: visible && offered.some(o => o.startsWith('کرښه چاپ')) && text.endsWith('    کرښه چاپ')
                    && popup.hidden,
                detail: {visible, offered, text}};
    """,
    "a change from another editor replaces the text": r"""
        const editor = window.thunderEditor;
        window.postMessage({type: 'update', text: 'متغیر الف = ۱\nمتغیر ب = ۲'}, '*');
        return new Promise(resolve => setTimeout(() => resolve({
            ok: editor.state.text === 'متغیر الف = ۱\nمتغیر ب = ۲'
                && document.querySelectorAll('#code .line').length === 2,
            detail: editor.state.text}), 50));
    """,
    "the compiler's problems mark their line and are listed": r"""
        window.postMessage({type: 'diagnostics', diagnostics: [
            {line: 1, character: 0, code: 'S005', message: 'wrong type', severity: 'error'}]}, '*');
        return new Promise(resolve => setTimeout(() => {
            const lines = document.querySelectorAll('#code .line');
            const problems = document.getElementById('problems');
            resolve({ok: lines[1].classList.contains('error') && !lines[0].classList.contains('error')
                         && !problems.hidden && problems.textContent.includes('S005'),
                     detail: {title: lines[1].title, problems: problems.textContent}});
        }, 50));
    """,
    "the text of the editor is exactly the program, with no marks": r"""
        const text = Array.from(document.querySelectorAll('#code .line')).map(l => l.textContent).join('\n');
        const marks = /[⁦-⁩‎‏‪-‮]/.test(text);
        return {ok: text === window.thunderEditor.state.text && !marks, detail: {text, marks}};
    """,
}


def main() -> int:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    os.environ.setdefault("QTWEBENGINE_CHROMIUM_FLAGS", "--no-sandbox --disable-gpu")
    from PySide6.QtCore import QCoreApplication, QEventLoop, Qt, QTimer, QUrl
    QCoreApplication.setAttribute(Qt.ApplicationAttribute.AA_ShareOpenGLContexts)
    from PySide6.QtWebEngineWidgets import QWebEngineView
    from PySide6.QtWidgets import QApplication

    app = QApplication(sys.argv[:1])
    view = QWebEngineView()
    view.resize(900, 600)

    read = lambda *parts: (EXTENSION.joinpath(*parts)).read_text(encoding="utf-8")  # noqa: E731
    language = json.loads(read("data", "language-info.ps.json"))
    words = {}
    for keyword in language["keywords"]:
        for spelling in keyword["spellings"]:
            words[spelling] = "keyword" if keyword["category"] == "operator" else keyword["category"]
    for builtin in language["builtins"]:
        words[builtin["name"]] = "builtin"
    page = PAGE.format(css=read("media", "rtleditor", "editor.css"), direction=read("src", "direction.js"),
                       model=read("media", "rtleditor", "model.js"), editor=read("media", "rtleditor", "editor.js"))

    loop = QEventLoop()
    view.loadFinished.connect(lambda ok: loop.quit())
    view.setHtml(page, QUrl("file:///thunder/"))
    view.show()
    QTimer.singleShot(30000, loop.quit)
    loop.exec()

    def run(script: str):
        result = {}

        def done(value):
            result["value"] = value
            loop.quit()

        view.page().runJavaScript(script, 0, done)
        QTimer.singleShot(15000, loop.quit)
        loop.exec()
        return result.get("value")

    init = {
        "type": "init", "language": "ps", "rightToLeft": True, "words": words,
        "blockWords": {"opening": ["که", "کنه", "ترڅو", "لپاره", "دنده", "جوړښت"],
                       "leaving": ["راګرځه", "ودرېږه", "دوام", "تېر"]},
        "candidates": [{"label": builtin["name"], "kind": "builtin", "detail": builtin["signature"]}
                       for builtin in language["builtins"]],
        "diagnostics": [],
        "text": "// ژبه: ps\nمتغیر ن = \"سلام\"\nمتغیر name = -۳",
    }
    run(f"window.postMessage({json.dumps(init, ensure_ascii=False)}, '*'); true")
    QTimer.singleShot(100, loop.quit)
    loop.exec()

    screenshot = os.environ.get("THUNDER_EDITOR_SCREENSHOT")
    if screenshot:  # for looking at it, not for the tests
        sample = (EXTENSION.parents[1] / "samples" / "project" / "pashto" / "main.th").read_text(encoding="utf-8")
        run(f"window.postMessage({json.dumps({'type': 'update', 'text': sample}, ensure_ascii=False)}, '*'); true")
        for _ in range(10):
            QTimer.singleShot(50, loop.quit)
            loop.exec()
        view.grab().save(screenshot)
        run(f"window.postMessage({json.dumps({'type': 'update', 'text': init['text']}, ensure_ascii=False)}, '*'); true")

    results = {}
    for name, body in SCENARIOS.items():
        # Scenarios may return a promise; it is awaited, then reported as JSON.
        script = ("(async () => { try { const r = await (async () => {" + body + "})(); "
                  "window.__result = JSON.stringify(r); } catch (e) { window.__result = JSON.stringify("
                  "{ok: false, detail: String(e && e.stack || e)}); } })(); true")
        run(script)
        value = None
        for _ in range(100):
            value = run("window.__result || ''")
            if value:
                break
            QTimer.singleShot(20, loop.quit)
            loop.exec()
        run("window.__result = ''; true")
        results[name] = json.loads(value) if value else {"ok": False, "detail": "no result"}
    print(json.dumps(results, ensure_ascii=False))
    view.close()
    app.quit()
    return 0 if all(entry["ok"] for entry in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
