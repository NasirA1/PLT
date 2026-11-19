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
<style>
/* The variables VS Code's Dark+ theme really defines: the symbolIcon keyword/boolean/constant
   colours are the plain foreground there, which is how keywords came to look uncoloured. */
:root {{ --vscode-foreground: #cccccc; --vscode-editor-foreground: #d4d4d4;
         --vscode-symbolIcon-keywordForeground: #cccccc; --vscode-symbolIcon-booleanForeground: #cccccc;
         --vscode-symbolIcon-constantForeground: #cccccc; --vscode-symbolIcon-functionForeground: #b180d7;
         --vscode-symbolIcon-classForeground: #ee9d28; --vscode-charts-blue: #3794ff;
         --vscode-debugTokenExpression-boolean: #4e94ce; --vscode-debugTokenExpression-number: #b5cea8;
         --vscode-debugTokenExpression-string: #ce9178; }}
</style>
</head><body>
<div id="find-bar" hidden>
<button id="find-expand" type="button"></button>
<div id="find-rows">
<div class="find-row">
<input id="find-input"><button id="find-loose" type="button" hidden></button><span id="find-count"></span>
<button id="find-previous" type="button"></button><button id="find-next" type="button"></button>
<button id="find-close" type="button"></button>
</div>
<div class="find-row" id="replace-row" hidden>
<input id="replace-input"><button id="replace-one" type="button"></button><button id="replace-all" type="button"></button>
</div>
</div>
</div>
<div id="code" contenteditable="true" spellcheck="false"></div>
<div id="test-peek" hidden role="alert"></div>
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
<script>{search}</script>
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
    "choosing a multi-word built-in replaces the letter typed, even when the answer lags a letter": r"""
        const editor = window.thunderEditor;
        const code = document.getElementById('code');
        editor.state.text = 'متغیر الف = ۱\n';
        editor.draw(true);
        code.focus();
        editor.select(editor.state.text.length);
        window.sent = [];
        code.dispatchEvent(new InputEvent('beforeinput', {inputType: 'insertText', data: 'ف', cancelable: true, bubbles: true}));
        const asked = window.sent.filter(m => m.type === 'language' && m.feature === 'completion');
        if (asked.length !== 1) return {ok: false, detail: {asked: asked.length}};
        const caret = editor.selection().start;
        // The document had not received the letter yet: the provider's word (and ranges) start at the caret.
        const items = ['فایل شته', 'فایل ولوله', 'که'].map((label, i) => ({label, kind: 'builtin', text: label + '()', caret: label.length + 1,
            start: caret, end: caret, filterText: label, sortText: String(i)}));
        window.postMessage({type: 'languageResult', id: asked[0].id, result: {wordStart: caret, items}}, '*');
        return new Promise(resolve => setTimeout(() => {
            // Go on typing the second word: the list stays open over the space.
            for (const ch of 'ایل ش') {
                code.dispatchEvent(new InputEvent('beforeinput', {inputType: 'insertText', data: ch, cancelable: true, bubbles: true}));
            }
            const offered = Array.from(document.querySelectorAll('#completion .item')).map(i => i.firstChild.textContent);
            code.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', cancelable: true, bubbles: true}));
            const last = editor.state.text.split('\n').pop();
            resolve({ok: offered.length === 1 && offered[0] === 'فایل شته' && last === 'فایل شته()',
                     detail: {offered, last}});
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
    "the page scrolls to the stopped line only when it is out of view": r"""
        const editor = window.thunderEditor;
        editor.state.text = Array.from({length: 120}, (_, i) => 'متغیر الف' + i + ' = ' + i).join('\n');
        editor.draw(true);
        window.scrollTo(0, 0);
        const post = (line) => window.postMessage({type: 'execution', execution: {line, column: 0, historic: false, focused: false}}, '*');
        return new Promise(resolve => {
            post(2);
            setTimeout(() => {
                const afterVisible = window.scrollY;
                post(4);
                setTimeout(() => {
                    const afterNearby = window.scrollY;      // still in view: no scrolling
                    post(100);
                    setTimeout(() => {
                        const afterFar = window.scrollY;      // out of view: brought to the middle
                        const box = document.querySelectorAll('#code .line')[100].getBoundingClientRect();
                        window.postMessage({type: 'execution', execution: null}, '*');
                        resolve({ok: afterVisible === 0 && afterNearby === 0 && afterFar > 0
                                     && box.top >= 0 && box.bottom <= window.innerHeight,
                                 detail: {afterVisible, afterNearby, afterFar, top: box.top, height: window.innerHeight}});
                    }, 60);
                }, 60);
            }, 60);
        });
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
    "Ctrl+F opens the editor's find bar; loose letters find every spelling of yeh": r"""
        const editor = window.thunderEditor;
        editor.state.text = 'سلام ي ى ۍ ې ئ ی';
        editor.draw(true);
        window.dispatchEvent(new KeyboardEvent('keydown', {key: 'f', ctrlKey: true, cancelable: true}));
        const bar = document.getElementById('find-bar');
        const opened = !bar.hidden;
        const input = document.getElementById('find-input');
        const count = () => document.getElementById('find-count').textContent;
        editor.setLooseFind(false);
        input.value = 'ى';
        input.dispatchEvent(new Event('input', {bubbles: true}));
        const strict = count();
        editor.setLooseFind(true);
        const loose = count();
        const painted = Boolean(CSS.highlights && CSS.highlights.get('thunder-find'));
        editor.moveFind(1);
        const moved = count();
        editor.closeFind();
        const closed = bar.hidden && getComputedStyle(bar).display === 'none'
                       && !(CSS.highlights && CSS.highlights.get('thunder-find')
                            && CSS.highlights.get('thunder-find').size);
        return {ok: opened && strict === '1/1' && loose === '1/6' && painted && moved === '2/6' && closed,
                detail: {opened, strict, loose, moved, closed}};
    """,
    "the find bar hides until Ctrl+F; Ctrl+H unfolds Replace, and the X really closes it": """
        const editor = window.thunderEditor;
        const bar = document.getElementById('find-bar');
        const hiddenAtRest = getComputedStyle(bar).display === 'none';
        editor.state.text = 'متغير ن = ۱\\nچاپ(ناحيه)';
        editor.draw(true);
        window.dispatchEvent(new KeyboardEvent('keydown', {key: 'h', ctrlKey: true, cancelable: true}));
        const openedWithReplace = getComputedStyle(bar).display !== 'none'
            && !document.getElementById('replace-row').hidden;
        editor.setLooseFind(true);
        const input = document.getElementById('find-input');
        input.value = 'ي';
        input.dispatchEvent(new Event('input', {bubbles: true}));
        document.getElementById('replace-input').value = 'ی';
        editor.replaceCurrent();
        const afterOne = editor.state.text.startsWith('متغیر');
        editor.replaceEverything();
        const afterAll = editor.state.text === 'متغیر ن = ۱\\nچاپ(ناحیه)';
        document.getElementById('find-close').click();
        const closedByX = getComputedStyle(bar).display === 'none';
        return {ok: hiddenAtRest && openedWithReplace && afterOne && afterAll && closedByX,
                detail: {hiddenAtRest, openedWithReplace, afterOne, afterAll, closedByX,
                         text: editor.state.text}};
    """,
    "a context menu command reaches the page: Select All selects, Find opens, Toggle Line Comment comments": r"""
        const editor = window.thunderEditor;
        editor.closeFind();
        editor.state.text = 'متغیر ن = ۱\nکرښه چاپ(ن)';
        editor.draw(true);
        const length = editor.state.text.length;
        window.postMessage({type: 'command', name: 'selectAll'}, '*');
        await new Promise(r => setTimeout(r, 50));
        const all = editor.selection();
        window.postMessage({type: 'command', name: 'find'}, '*');
        await new Promise(r => setTimeout(r, 50));
        const findOpen = getComputedStyle(document.getElementById('find-bar')).display !== 'none';
        editor.closeFind();
        editor.select(0);
        window.postMessage({type: 'command', name: 'comment'}, '*');
        await new Promise(r => setTimeout(r, 50));
        const commented = editor.state.text.startsWith('# متغیر');
        return {ok: all.start === 0 && all.end === length && findOpen && commented,
                detail: {all, length, findOpen, text: editor.state.text}};
    """,
    "keywords and literals are coloured, not left in the plain text colour (as in VS Code's themes)": r"""
        const editor = window.thunderEditor;
        editor.closeFind();
        editor.state.text = 'متغیر ن = رښتیا\nکرښه چاپ(ن)';
        editor.draw(true);
        const colour = (selector) => getComputedStyle(document.querySelector(selector)).color;
        const plain = colour('#code .k-name');
        const keyword = colour('#code .k-keyword');
        const literal = colour('#code .k-literal');
        const builtin = colour('#code .k-builtin');
        const foreground = 'rgb(204, 204, 204)';   // --vscode-foreground above: the old stylesheet's keyword colour
        return {ok: keyword !== plain && keyword !== foreground && literal !== plain && literal !== foreground
                    && builtin !== plain && keyword !== builtin,
                detail: {plain, foreground, keyword, literal, builtin}};
    """,
    "the compiler's colours follow the text while it is typed, and a late answer lands on the right letters": r"""
        if (typeof CSS === 'undefined' || !CSS.highlights) return {ok: true, detail: 'no Custom Highlight API here'};
        const editor = window.thunderEditor;
        editor.closeFind();
        const code = document.getElementById('code');
        const text = 'دنده سلام()\n    کرښه چاپ(۱)\nسلام()';
        window.postMessage({type: 'update', text}, '*');
        await new Promise(r => setTimeout(r, 30));
        const wait = () => new Promise(r => setTimeout(r, 40));
        const name = 'سلام';
        const first = text.indexOf(name), second = text.lastIndexOf(name);
        const ranges = {function: [[first, first + name.length], [second, second + name.length]]};
        const painted = () => Array.from(CSS.highlights.get('thunder-fn') || []).map(r => r.toString());
        const answer = async (analysed) => {
            window.sent = [];
            window.postMessage({type: 'diagnostics', diagnostics: []}, '*');   // a new analysis: the page asks for colours
            await wait();
            const asked = window.sent.filter(m => m.type === 'language' && m.feature === 'semanticTokens');
            if (!asked.length) throw new Error('the page did not ask for semantic colours');
            window.postMessage({type: 'languageResult', id: asked[asked.length - 1].id, result: {text: analysed, ranges}}, '*');
            await wait();
        };
        await answer(text);
        const before = painted();
        // Two characters typed at the start of the file: the painted ranges must move with the text.
        code.focus();
        editor.select(0);
        for (const ch of '# ') {
            code.dispatchEvent(new InputEvent('beforeinput', {inputType: 'insertText', data: ch, cancelable: true, bubbles: true}));
        }
        const after = painted();
        // An answer computed for the text before the edit, arriving late, lands on the same letters.
        await answer(text);
        const late = painted();
        const expected = name + '|' + name;
        return {ok: before.join('|') === expected && after.join('|') === expected && late.join('|') === expected,
                detail: {before, after, late}};
    """,
    "the clipboard events carry cut, copy and paste between the model and the clipboard": r"""
        const editor = window.thunderEditor;
        editor.closeFind();
        const code = document.getElementById('code');
        const text = 'که رښتیا\n    کرښه چاپ(۱)\nکرښه چاپ(۲)';
        window.postMessage({type: 'update', text}, '*');
        await new Promise(r => setTimeout(r, 30));
        const from = editor.state.text.indexOf('    کرښه');
        const to = editor.state.text.indexOf('\n', from);
        const make = (type, data) => {
            const transfer = new DataTransfer();
            if (data !== undefined) { transfer.setData('text/plain', data); }
            return [new ClipboardEvent(type, {clipboardData: transfer, bubbles: true, cancelable: true}), transfer];
        };
        code.focus();
        editor.select(from, to);
        const [copyEvent, copied] = make('copy');
        code.dispatchEvent(copyEvent);
        const copyOk = copyEvent.defaultPrevented && copied.getData('text/plain') === '    کرښه چاپ(۱)'
            && editor.state.text === text;
        const [cutEvent, cutData] = make('cut');
        code.dispatchEvent(cutEvent);
        const cutOk = cutEvent.defaultPrevented && cutData.getData('text/plain') === '    کرښه چاپ(۱)'
            && editor.state.text === 'که رښتیا\n\nکرښه چاپ(۲)';
        editor.select(editor.state.text.length);
        const [pasteEvent] = make('paste', '\nکرښه چاپ(۳)');
        code.dispatchEvent(pasteEvent);
        const pasteOk = pasteEvent.defaultPrevented && editor.state.text === 'که رښتیا\n\nکرښه چاپ(۲)\nکرښه چاپ(۳)';
        // an empty selection leaves copy and cut to the browser (there is nothing to take)
        editor.select(0, 0);
        const [emptyCut] = make('cut');
        code.dispatchEvent(emptyCut);
        const emptyOk = !emptyCut.defaultPrevented && editor.state.text === 'که رښتیا\n\nکرښه چاپ(۲)\nکرښه چاپ(۳)';
        const lines = editor.state.text.split('\n');
        const domOk = code.children.length === lines.length && code.childNodes.length === code.children.length
            && Array.from(code.children).every((line) => line.classList.contains('line'));
        const caret = editor.selection();
        return {ok: copyOk && cutOk && pasteOk && emptyOk && domOk,
                detail: {copyOk, cutOk, pasteOk, emptyOk, domOk, text: editor.state.text, caret}};
    """,
    "a cut or paste from the context menu (non-cancelable beforeinput) leaves no stray lines or indentation": r"""
        const editor = window.thunderEditor;
        editor.closeFind();
        const code = document.getElementById('code');
        const text = 'که رښتیا\n    کرښه چاپ(۱)\nکرښه چاپ(۲)';
        window.postMessage({type: 'update', text}, '*');
        await new Promise(r => setTimeout(r, 30));
        const health = () => {
            const lines = editor.state.text.split('\n');
            const divs = Array.from(code.children);
            const okChildren = divs.length === lines.length && divs.every((d, i) =>
                d.classList.contains('line') && (d.textContent === lines[i] || (lines[i] === '' && d.textContent === '')));
            const noStray = code.childNodes.length === code.children.length; // no loose text nodes
            return {okChildren, noStray, divs: divs.length, lines: lines.length};
        };
        // Cut the middle line through the context menu: execCommand fires a NON-cancelable
        // beforeinput, then the browser deletes the selected DOM itself (its div and number go).
        const lineStart = text.indexOf('    کرښه');
        const lineEnd = text.indexOf('\n', lineStart);
        code.focus();
        editor.select(lineStart, lineEnd + 1);
        code.dispatchEvent(new InputEvent('beforeinput', {inputType: 'deleteByCut', cancelable: false, bubbles: true}));
        if (code.children.length > 1) { code.children[1].remove(); }      // what the browser would do
        code.dispatchEvent(new InputEvent('input', {inputType: 'deleteByCut', bubbles: true}));
        await new Promise(r => setTimeout(r, 10));
        const afterCut = health();
        const cutText = editor.state.text;
        // Paste it back at the end, again non-cancelable: the browser also inserts its own copy,
        // indented however the caret's line was - the "extra indentation".
        editor.select(editor.state.text.length);
        const dt = new DataTransfer();
        dt.setData('text/plain', '\n    کرښه چاپ(۱)');
        code.dispatchEvent(new InputEvent('beforeinput', {inputType: 'insertFromPaste', dataTransfer: dt, cancelable: false, bubbles: true}));
        const stray = document.createElement('div');                       // what the browser would do
        stray.textContent = '        کرښه چاپ(۱)';
        code.appendChild(stray);
        code.appendChild(document.createTextNode('ن'));
        code.dispatchEvent(new InputEvent('input', {inputType: 'insertFromPaste', bubbles: true}));
        await new Promise(r => setTimeout(r, 10));
        const afterPaste = health();
        const caret = editor.selection();
        return {ok: cutText === 'که رښتیا\nکرښه چاپ(۲)' && afterCut.okChildren && afterCut.noStray
                    && editor.state.text === 'که رښتیا\nکرښه چاپ(۲)\n    کرښه چاپ(۱)'
                    && afterPaste.okChildren && afterPaste.noStray && caret.start === editor.state.text.length,
                detail: {cutText, afterCut, afterPaste, text: editor.state.text, caret}};
    """,
    "tests: the buttons show each state, a suite sums up its tests, and a failure opens a panel under its line": r"""
        const text = "# ژبه: ps\n@آزموینه\nجوړښت ډله\n    دنده جمع()\n        تېر\n    دنده ناسمه()\n        تېر\nمتغیر ن = ۱\n";
        window.postMessage({type: 'update', text}, '*');
        window.postMessage({type: 'tests', tests: [{name: 'جمع', suite: 'ډله', line: 3}, {name: 'ناسمه', suite: 'ډله', line: 5}],
                            suites: [{name: 'ډله', line: 1}]}, '*');
        window.postMessage({type: 'testResults', results: [{key: 'ډله/جمع', state: 'running', messages: []},
                                                          {key: 'ډله/ناسمه', state: 'running', messages: []}]}, '*');
        return new Promise(resolve => setTimeout(() => {
            const lines = document.querySelectorAll('#code .line');
            const running = lines[1].classList.contains('test-running') && lines[3].classList.contains('test-running');
            window.postMessage({type: 'testResults', results: [{key: 'ډله/جمع', state: 'passed', messages: []},
                {key: 'ډله/ناسمه', state: 'failed', messages: ['تمه ۲ وه، ۱ ترلاسه شو']}]}, '*');
            setTimeout(() => {
                const peek = document.getElementById('test-peek');
                const l = document.querySelectorAll('#code .line');
                const passed = l[3].classList.contains('test-passed');
                const failed = l[5].classList.contains('test-failed') && l[1].classList.contains('test-failed');
                const inline = getComputedStyle(l[5], '::after').content;
                const open = !peek.hidden && peek.textContent.includes('تمه ۲ وه') && peek.textContent.includes('ناسمه');
                const top = peek.getBoundingClientRect().top;
                const below = top >= l[5].getBoundingClientRect().bottom - 1 && top < l[6].getBoundingClientRect().top;
                const room = parseFloat(l[5].style.marginBottom) > 0;
                const arrow = getComputedStyle(peek, '::before').borderBottomWidth;
                // The suite's button (on the annotation's line) runs the suite.
                window.sent = [];
                const box = l[1].getBoundingClientRect();
                document.getElementById('code').dispatchEvent(new MouseEvent('mousedown',
                    {button: 0, clientX: box.right - 4, clientY: box.top + box.height / 2, bubbles: true, cancelable: true}));
                const ran = window.sent.filter(m => m.type === 'runTest');
                document.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}));
                const closed = peek.hidden && !l[5].style.marginBottom;
                window.postMessage({type: 'testResults', results: []}, '*');
                window.postMessage({type: 'tests', tests: [], suites: []}, '*');
                setTimeout(() => resolve({
                    ok: running && passed && failed && inline.includes('تمه') && open && below && room && arrow === '8px'
                        && ran.length === 1 && ran[0].line === 1 && closed,
                    detail: {running, passed, failed, inline, open, below, room, arrow, ran, closed}}), 30);
            }, 80);
        }, 80));
    """,
    "zoom messages and Ctrl+wheel size the editor's text, and only this editor's": r"""
        const editor = window.thunderEditor;
        const sizeOf = () => parseFloat(getComputedStyle(document.getElementById('code')).fontSize);
        const base = sizeOf();
        window.postMessage({type: 'zoom', step: 1}, '*');
        await new Promise(r => setTimeout(r, 50));
        const grown = sizeOf();
        window.dispatchEvent(new WheelEvent('wheel', {deltaY: 100, ctrlKey: true, cancelable: true}));
        window.dispatchEvent(new WheelEvent('wheel', {deltaY: 100, ctrlKey: true, cancelable: true}));
        const shrunk = sizeOf();
        window.postMessage({type: 'zoom', step: 0}, '*');
        await new Promise(r => setTimeout(r, 50));
        const reset = sizeOf();
        editor.setZoom(0);
        return {ok: grown > base && shrunk < base && Math.abs(reset - base) < 0.5,
                detail: {base, grown, shrunk, reset}};
    """,
    "a test's line shows a play button in the gutter, and clicking it runs the test": r"""
        const editor = window.thunderEditor;
        editor.state.text = 'راوړه "testing"\nازموینه("جمع"، جمع)';
        editor.draw(true);
        window.postMessage({type: 'tests', tests: [{name: 'جمع', line: 1}]}, '*');
        return new Promise(resolve => setTimeout(() => {
            const lines = document.querySelectorAll('#code .line');
            const marked = lines[1].classList.contains('test') && !lines[0].classList.contains('test');
            const painted = getComputedStyle(lines[1]).backgroundImage.includes('svg');
            // A click in the outermost band of the gutter (inline-start: the right, in RTL).
            const box = lines[1].getBoundingClientRect();
            window.sent = [];
            lines[1].dispatchEvent(new MouseEvent('mousedown', {button: 0, bubbles: true, cancelable: true,
                clientX: box.right - 4, clientY: box.top + box.height / 2}));
            const ran = window.sent.find(m => m.type === 'runTest');
            // Further in sits the number: that click stays the breakpoint's.
            window.sent = [];
            lines[1].dispatchEvent(new MouseEvent('mousedown', {button: 0, bubbles: true, cancelable: true,
                clientX: box.right - 40, clientY: box.top + box.height / 2}));
            const breakpoint = window.sent.find(m => m.type === 'toggleBreakpoint');
            window.postMessage({type: 'tests', tests: []}, '*');
            setTimeout(() => resolve({ok: marked && painted && ran && ran.line === 1
                                          && breakpoint && breakpoint.line === 1
                                          && !lines[1].classList.contains('test'),
                detail: {marked, painted, ran, breakpoint}}), 50);
        }, 50));
    """,
}


OUTPUT_PAGE = """<!DOCTYPE html>
<html dir="ltr"><head><meta charset="UTF-8"><style>{css}</style>
<style>body {{ font-family: "DejaVu Sans", sans-serif; }}</style></head><body>
<div id="find-bar" hidden>
<input id="find-input"><button id="find-loose" type="button" hidden></button><span id="find-count"></span>
<button id="find-previous" type="button"></button><button id="find-next" type="button"></button>
<button id="find-close" type="button"></button>
</div>
<div id="output"></div>
<form id="input-row" hidden><input id="input"></form>
<script>
window.sent = [];
window.acquireVsCodeApi = () => ({{ postMessage: (message) => window.sent.push(message) }});
</script>
<script>{search}</script>
<script>{script}</script>
</body></html>"""


# Each scenario stores its JSON result in window.__out when it is done (they may wait for a
# repaint first); the runner polls for it.
OUTPUT_SCENARIOS = {
    "the Thunder Output view aligns every Pashto line right, a number and an English error too": r"""
        window.postMessage({type: 'begin', rightToLeft: true, title: 'p.th'}, '*');
        window.postMessage({type: 'append', text: 'سلام نړۍ\n۴۲\n', category: 'stdout'}, '*');
        window.postMessage({type: 'append', text: 'R003: division by zero\n', category: 'stderr'}, '*');
        setTimeout(() => {
            const lines = Array.from(document.querySelectorAll('#output .line'));
            const right = document.getElementById('output').getBoundingClientRect().right;
            const ends = lines.map(line => {
                const range = document.createRange(); range.selectNodeContents(line);
                const rects = range.getClientRects(); return Math.round(rects[rects.length - 1].right);
            });
            window.__out = JSON.stringify({ok: lines.length === 3 && ends.every(end => Math.abs(end - right) < 3),
                                    detail: {texts: lines.map(l => l.textContent), ends, right: Math.round(right)}});
        }, 100);
    """,
    "zoom messages and Ctrl+wheel size the output's text, and only it": r"""
        const output = window.thunderOutput;
        const sizeOf = () => parseFloat(getComputedStyle(document.body).fontSize);
        const base = sizeOf();
        window.postMessage({type: 'zoom', step: 1}, '*');
        setTimeout(() => {
            const grown = sizeOf();
            window.dispatchEvent(new WheelEvent('wheel', {deltaY: 100, ctrlKey: true, cancelable: true}));
            window.dispatchEvent(new WheelEvent('wheel', {deltaY: 100, ctrlKey: true, cancelable: true}));
            const shrunk = sizeOf();
            const plainWheel = new WheelEvent('wheel', {deltaY: -100, cancelable: true});
            window.dispatchEvent(plainWheel);   // without Ctrl the wheel scrolls, never zooms
            const afterPlain = sizeOf();
            window.postMessage({type: 'zoom', step: 0}, '*');
            setTimeout(() => {
                const reset = sizeOf();
                window.__out = JSON.stringify({ok: grown > base && shrunk < base && afterPlain === shrunk
                                                   && Math.abs(reset - base) < 0.5 && output.state.zoom === 0
                                                   && !plainWheel.defaultPrevented,
                                               detail: {base, grown, shrunk, afterPlain, reset}});
            }, 50);
        }, 50);
    """,
    "Ctrl+F finds in the output; loose letters find every spelling of a Pashto letter": r"""
        const output = window.thunderOutput;
        window.postMessage({type: 'begin', rightToLeft: true, title: 'p.th'}, '*');
        window.postMessage({type: 'append', text: 'ګل گل\nي ى ۍ ې ئ ی\n', category: 'stdout'}, '*');
        setTimeout(() => {
            window.dispatchEvent(new KeyboardEvent('keydown', {key: 'f', ctrlKey: true, cancelable: true}));
            const bar = document.getElementById('find-bar');
            const opened = !bar.hidden;
            const looseButtonShown = !document.getElementById('find-loose').hidden;
            const input = document.getElementById('find-input');
            const count = () => document.getElementById('find-count').textContent;
            output.setLoose(false);
            input.value = 'ى';
            input.dispatchEvent(new Event('input', {bubbles: true}));
            const strict = count();
            output.setLoose(true);
            const loose = count();
            const painted = Boolean(CSS.highlights && CSS.highlights.get('thunder-find'));
            output.moveFind(1);
            const moved = count();
            input.value = 'گ';
            input.dispatchEvent(new Event('input', {bubbles: true}));
            const gaf = count();
            window.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', cancelable: true}));
            const closed = bar.hidden && getComputedStyle(bar).display === 'none';
            window.__out = JSON.stringify({ok: opened && looseButtonShown && strict === '1/1' && loose === '1/6'
                                               && painted && moved === '2/6' && gaf === '1/2' && closed,
                                           detail: {opened, looseButtonShown, strict, loose, moved, gaf, closed}});
        }, 100);
    """,
}


def output_view_scenarios(view, loop, read) -> dict:
    """The Thunder Output view (media/output): alignment, zoom and the find bar."""
    from PySide6.QtCore import QTimer, QUrl
    page = OUTPUT_PAGE.format(css=read("media", "output", "output.css"),
                              search=read("media", "shared", "search.js"),
                              script=read("media", "output", "output.js"))
    view.loadFinished.connect(lambda ok: loop.quit())
    view.setHtml(page, QUrl("file:///thunder-output/"))
    QTimer.singleShot(30000, loop.quit)
    loop.exec()
    result = {}

    def done(value):
        result["value"] = value
        loop.quit()

    results = {}
    for name, body in OUTPUT_SCENARIOS.items():
        script = ("window.__out = ''; (() => { try {" + body + "} catch (e) {"
                  "window.__out = JSON.stringify({ok: false, detail: String(e && e.stack || e)}); } })(); true")
        view.page().runJavaScript(script, 0, done)
        QTimer.singleShot(15000, loop.quit)
        loop.exec()
        value = None
        for _ in range(100):
            result.clear()
            view.page().runJavaScript("window.__out || ''", 0, done)
            QTimer.singleShot(15000, loop.quit)
            loop.exec()
            value = result.get("value")
            if value:
                break
            QTimer.singleShot(20, loop.quit)
            loop.exec()
        results[name] = json.loads(value) if value else {"ok": False, "detail": "no result"}
    return results


CANVAS_PAGE = """<!DOCTYPE html>
<html><head><meta charset="UTF-8"><style>{css}</style></head><body>
<canvas id="picture" tabindex="0"></canvas>
<div id="status" hidden></div>
<script>
window.sent = [];
window.acquireVsCodeApi = () => ({{ postMessage: (message) => window.sent.push(message) }});
</script>
<script>{script}</script>
</body></html>"""


CANVAS_SCENARIOS = {
    "the Thunder Canvas paints the frame's shapes and reports the keys and the mouse": """
        window.postMessage({type: 'begin', title: 'p.th'}, '*');
        window.postMessage({type: 'frame', record: {type: 'frame', n: 1, width: 50, height: 40, keep: 0,
            shapes: [['clear', '#0000ff'], ['rect', 5, 6, 10, 10, '#ff0000']]}}, '*');
        setTimeout(() => {
            const canvas = window.thunderCanvas;
            const context = document.getElementById('picture').getContext('2d');
            const ratio = window.devicePixelRatio || 1;
            const state = canvas.state;
            const scale = Math.max(0.05, Math.min(window.innerWidth / state.width, window.innerHeight / state.height));
            const left = (window.innerWidth - state.width * scale) / 2;
            const top = (window.innerHeight - state.height * scale) / 2;
            const sample = (x, y) => Array.from(context.getImageData(
                Math.round((left + (x + 0.5) * scale) * ratio), Math.round((top + (y + 0.5) * scale) * ratio), 1, 1).data);
            const inside = sample(10, 10);   // the red rectangle
            const outside = sample(30, 30);  // the blue background
            const round = canvas.toCanvas(left + 20 * scale, top + 10 * scale); // a page point maps back
            window.sent = [];
            window.dispatchEvent(new KeyboardEvent('keydown', {key: 'ArrowLeft', cancelable: true}));
            window.dispatchEvent(new KeyboardEvent('keydown', {key: 'A', cancelable: true}));
            const keys = window.sent.filter(m => m.type === 'event').map(m => m.text);
            window.__out = JSON.stringify({
                ok: inside[0] > 200 && inside[2] < 80 && outside[2] > 200 && outside[0] < 80
                    && round.x === 20 && round.y === 10
                    && keys.join('|') === 'key down left|key down a',
                detail: {inside, outside, round, keys}});
        }, 100);
    """,
}


def canvas_scenarios(view, loop, read) -> dict:
    """The Thunder Canvas page (media/canvas): the picture, scaled and centred, keys and mouse."""
    from PySide6.QtCore import QTimer, QUrl
    page = CANVAS_PAGE.format(css=read("media", "canvas", "canvas.css"),
                              script=read("media", "canvas", "canvas.js"))
    view.loadFinished.connect(lambda ok: loop.quit())
    view.setHtml(page, QUrl("file:///thunder-canvas/"))
    QTimer.singleShot(30000, loop.quit)
    loop.exec()
    result = {}

    def done(value):
        result["value"] = value
        loop.quit()

    results = {}
    for name, body in CANVAS_SCENARIOS.items():
        script = ("window.__out = ''; (() => { try {" + body + "} catch (e) {"
                  "window.__out = JSON.stringify({ok: false, detail: String(e && e.stack || e)}); } })(); true")
        view.page().runJavaScript(script, 0, done)
        QTimer.singleShot(15000, loop.quit)
        loop.exec()
        value = None
        for _ in range(100):
            result.clear()
            view.page().runJavaScript("window.__out || ''", 0, done)
            QTimer.singleShot(15000, loop.quit)
            loop.exec()
            value = result.get("value")
            if value:
                break
            QTimer.singleShot(20, loop.quit)
            loop.exec()
        results[name] = json.loads(value) if value else {"ok": False, "detail": "no result"}
    return results


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
                       search=read("media", "shared", "search.js"),
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
        "blockWords": {"opening": ["که", "کنه", "ترڅو", "تکرار", "دنده", "جوړښت"],
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
    results.update(output_view_scenarios(view, loop, read))
    results.update(canvas_scenarios(view, loop, read))
    print(json.dumps(results, ensure_ascii=False))
    view.close()
    app.quit()
    return 0 if all(entry["ok"] for entry in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
