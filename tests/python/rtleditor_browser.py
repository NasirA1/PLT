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
<div id="tooltip" hidden></div>
<div id="signature" hidden></div>
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
    "typing the start of a word asks the extension, and Enter inserts the provider's item": r"""
        const editor = window.thunderEditor;
        const code = document.getElementById('code');
        editor.select(editor.state.text.length);
        window.sent = [];
        for (const ch of 'کر') {
            code.dispatchEvent(new InputEvent('beforeinput', {inputType: 'insertText', data: ch, cancelable: true, bubbles: true}));
        }
        const asked = window.sent.filter(m => m.type === 'language' && m.feature === 'completion');
        if (asked.length !== 1) return {ok: false, detail: {asked: asked.length}};
        const caret = editor.selection().start;
        window.postMessage({type: 'languageResult', id: asked[0].id, result: {wordStart: caret - 1, items: [
            {label: 'کرښه چاپ', kind: 'builtin', detail: 'کرښه چاپ(ارزښت)', text: 'کرښه چاپ()', caret: 9,
             start: caret - 1, end: caret, filterText: 'کرښه چاپ', sortText: '1'},
            {label: 'که', kind: 'keyword', text: 'که', caret: 2, start: caret - 1, end: caret, filterText: 'که', sortText: '2'}]}}, '*');
        return new Promise(resolve => setTimeout(() => {
            const popup = document.getElementById('completion');
            const offered = Array.from(popup.querySelectorAll('.item')).map(i => i.textContent);
            const visible = !popup.hidden;
            code.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', cancelable: true, bubbles: true}));
            const text = editor.state.text;
            resolve({ok: visible && offered.length === 1 && offered[0].startsWith('کرښه چاپ')
                         && text.endsWith('کرښه چاپ()') && editor.selection().start === text.length - 1 && popup.hidden,
                     detail: {visible, offered, text: text.slice(-20), caret: editor.selection().start, length: text.length}});
        }, 80));
    """,
    "Ctrl+Space on an empty line opens the list at the caret, not in the corner of the page": r"""
        const editor = window.thunderEditor;
        const code = document.getElementById('code');
        editor.state.text = 'متغیر الف = ۱\n\nمتغیر ب = ۲';
        editor.draw(true);
        code.focus();
        editor.select(editor.state.text.indexOf('\n') + 1);     // the empty middle line
        window.sent = [];
        code.dispatchEvent(new KeyboardEvent('keydown', {key: ' ', code: 'Space', ctrlKey: true, cancelable: true, bubbles: true}));
        const asked = window.sent.filter(m => m.type === 'language' && m.feature === 'completion');
        if (asked.length !== 1) return {ok: false, detail: {asked: asked.length}};
        const offset = editor.selection().start;
        const items = Array.from({length: 80}, (_, i) => ({label: 'نوم' + i, kind: 'name', text: 'نوم' + i, caret: 4 + String(i).length,
                                                          start: offset, end: offset, filterText: 'نوم' + i, sortText: String(i).padStart(3, '0')}));
        window.postMessage({type: 'languageResult', id: asked[0].id, result: {wordStart: offset, items}}, '*');
        return new Promise(resolve => setTimeout(() => {
            const popup = document.getElementById('completion');
            const box = popup.getBoundingClientRect();
            const line = document.querySelectorAll('#code .line')[1].getBoundingClientRect();
            const shown = popup.querySelectorAll('.item').length;
            code.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', cancelable: true, bubbles: true}));
            resolve({ok: !popup.hidden || shown > 0 ? (shown === 80 && Math.abs(box.top - line.bottom) < 12 && box.right > window.innerWidth / 2)
                                                    : false,
                     detail: {shown, popupTop: box.top, lineBottom: line.bottom, popupRight: box.right, width: window.innerWidth}});
        }, 80));
    """,
    "Ctrl+/ comments the line out and back in": r"""
        const editor = window.thunderEditor;
        const code = document.getElementById('code');
        editor.state.text = 'متغیر الف = ۱';
        editor.draw(true);
        editor.select(3);
        const press = () => code.dispatchEvent(new KeyboardEvent('keydown', {key: '/', code: 'Slash', ctrlKey: true, cancelable: true, bubbles: true}));
        press();
        const commented = editor.state.text;
        press();
        return {ok: commented === '# متغیر الف = ۱' && editor.state.text === 'متغیر الف = ۱', detail: {commented, back: editor.state.text}};
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
    "a click on a line number asks for a breakpoint there, a click in the code does not": r"""
        const editor = window.thunderEditor;
        const line = document.querySelectorAll('#code .line')[1];
        const box = line.getBoundingClientRect();
        const y = box.top + box.height / 2;
        // The number stands at the right of a right-to-left program: that is the gutter.
        const onNumber = editor.gutterHit({target: line, clientX: box.right - 10, clientY: y});
        const onCode = editor.gutterHit({target: line, clientX: box.left + 10, clientY: y});
        window.sent = [];
        document.getElementById('code').dispatchEvent(new MouseEvent('mousedown',
            {button: 0, clientX: box.right - 10, clientY: y, bubbles: true, cancelable: true}));
        const asked = window.sent.filter(m => m.type === 'toggleBreakpoint');
        return {ok: onNumber.inGutter && onNumber.index === 1 && !onCode.inGutter
                    && asked.length === 1 && asked[0].line === 1,
                detail: {onNumber, onCode, asked}};
    """,
    "resting on a name while stopped asks for its value, and the answer is shown by the name": r"""
        const editor = window.thunderEditor;
        window.postMessage({type: 'execution', execution: {line: 0, column: 0, historic: false, focused: false}}, '*');
        return new Promise(resolve => setTimeout(() => {
            const name = Array.from(document.querySelectorAll('#code .k-name')).find(n => n.textContent.trim().length > 0);
            const expression = editor.hoverExpression(name);
            window.sent = [];
            const box = name.getBoundingClientRect();
            name.dispatchEvent(new MouseEvent('mousemove', {clientX: box.left + 2, clientY: box.top + 2, bubbles: true}));
            setTimeout(() => {
                const asked = window.sent.filter(m => m.type === 'hover');
                if (asked.length !== 1) {
                    resolve({ok: false, detail: {asked, expression}});
                    return;
                }
                editor.showHoverResult({type: 'hoverResult', id: asked[0].id, value: '۴۲', valueType: 'عدد'});
                const tooltip = document.getElementById('tooltip');
                const shown = !tooltip.hidden && tooltip.textContent.includes('۴۲') && tooltip.textContent.includes(expression);
                window.postMessage({type: 'execution', execution: null}, '*');
                setTimeout(() => resolve({ok: shown && tooltip.hidden,
                                           detail: {asked, text: tooltip.textContent, hiddenAfter: tooltip.hidden}}), 50);
            }, 400);
        }, 50));
    """,
    "the breakpoints and the stopped line are marked on their lines": r"""
        window.postMessage({type: 'breakpoints', breakpoints: [1]}, '*');
        window.postMessage({type: 'execution', execution: {line: 0, column: 0, historic: false, focused: false}}, '*');
        return new Promise(resolve => setTimeout(() => {
            const lines = document.querySelectorAll('#code .line');
            const number = getComputedStyle(lines[1], '::before').content;
            const arrow = getComputedStyle(lines[0], '::after').content;
            const marked = lines[0].classList.contains('current');
            window.postMessage({type: 'execution', execution: null}, '*');
            setTimeout(() => resolve({
                ok: lines[1].classList.contains('breakpoint') && number.includes('\u25CF')
                    && marked && arrow.includes('\u25C0') && !lines[0].classList.contains('current'),
                detail: {number, arrow, marked, classes: Array.from(lines).map(l => l.className)}}), 50);
        }, 50));
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
        "diagnostics": [],
        "text": "# ژبه: ps\nمتغیر ن = \"سلام\"\nمتغیر name = -۳",
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
