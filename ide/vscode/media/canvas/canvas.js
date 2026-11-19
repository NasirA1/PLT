// The Thunder Canvas page: draws the frames of a debugged graphics program (src/canvasview.js)
// and reports the keys and the mouse, the way the Thunder IDE's Canvas panel does.
//
// A frame record keeps the first `keep` shapes the page already has and adds the rest (see
// ide/thunder_ide/core/canvas.py, the same protocol). The shapes are painted with the same
// meaning as the IDE's QPainter: positions are canvas pixels, the picture is scaled to fit the
// page and centred, (x, y) of a text is its top-left corner whatever the script.

"use strict";

(function canvasPage()
{
    const vscode = acquireVsCodeApi();
    const picture = document.getElementById("picture");
    const status = document.getElementById("status");
    const context = picture.getContext("2d");

    const state = {
        width: 400,
        height: 300,
        shapes: [],
        frame: 0,
        ended: false,
        mouseDown: false,
        lastMouse: "",
    };

    // ------------------------------------------------------------------ geometry

    /** (scale, left, top): where the canvas lands on the page, scaled to fit and centred. */
    function transform()
    {
        const scale = Math.max(0.05, Math.min(window.innerWidth / state.width, window.innerHeight / state.height));
        return {
            scale,
            left: (window.innerWidth - state.width * scale) / 2,
            top: (window.innerHeight - state.height * scale) / 2,
        };
    }

    function toCanvas(x, y)
    {
        const { scale, left, top } = transform();
        // The tiny epsilon undoes floating-point error, as in the IDE's QtCanvas.to_canvas.
        return {
            x: Math.floor((x - left) / scale + 1e-6),
            y: Math.floor((y - top) / scale + 1e-6),
        };
    }

    // ------------------------------------------------------------------ painting

    function draw()
    {
        const ratio = window.devicePixelRatio || 1;
        // Setting a canvas's size reallocates (and clears) its pixels: only when it changed, not
        // at every frame of an animation.
        const width = Math.max(1, Math.round(window.innerWidth * ratio));
        const height = Math.max(1, Math.round(window.innerHeight * ratio));
        if (picture.width !== width || picture.height !== height)
        {
            picture.width = width;
            picture.height = height;
            picture.style.width = `${window.innerWidth}px`;
            picture.style.height = `${window.innerHeight}px`;
        }
        context.setTransform(1, 0, 0, 1, 0, 0);
        context.clearRect(0, 0, picture.width, picture.height);
        // The clip and the styles of one frame must not reach the next (a resize used to reset
        // them, and the size is no longer set at every frame).
        context.save();
        const { scale, left, top } = transform();
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.fillStyle = "#3c3f41"; // the surround, as in the IDE
        context.fillRect(0, 0, window.innerWidth, window.innerHeight);
        context.translate(left, top);
        context.scale(scale, scale);
        context.beginPath();
        context.rect(0, 0, state.width, state.height);
        context.clip();
        context.fillStyle = "white";
        context.fillRect(0, 0, state.width, state.height);
        for (const shape of state.shapes)
        {
            try
            {
                paintShape(shape);
            }
            catch (error)
            {
                // a malformed shape is skipped, never fatal
            }
        }
        context.restore();
    }

    function stroke(color, width)
    {
        context.strokeStyle = String(color);
        context.lineWidth = Math.max(0.1, Number(width) || 1);
        context.lineCap = "round";
    }

    function paintShape(shape)
    {
        switch (shape[0])
        {
        case "clear":
            context.fillStyle = String(shape[1]);
            context.fillRect(0, 0, state.width, state.height);
            break;
        case "pixel":
            context.fillStyle = String(shape[3]);
            context.fillRect(shape[1], shape[2], 1, 1);
            break;
        case "line":
            stroke(shape[5], shape[6]);
            context.beginPath();
            context.moveTo(shape[1], shape[2]);
            context.lineTo(shape[3], shape[4]);
            context.stroke();
            break;
        case "rect":
            context.fillStyle = String(shape[5]);
            context.fillRect(shape[1], shape[2], shape[3], shape[4]);
            break;
        case "rect_outline":
            stroke(shape[5], shape[6]);
            context.strokeRect(shape[1], shape[2], shape[3], shape[4]);
            break;
        case "circle":
            context.fillStyle = String(shape[4]);
            context.beginPath();
            context.arc(shape[1], shape[2], Math.max(0, shape[3]), 0, Math.PI * 2);
            context.fill();
            break;
        case "circle_outline":
            stroke(shape[4], shape[5]);
            context.beginPath();
            context.arc(shape[1], shape[2], Math.max(0, shape[3]), 0, Math.PI * 2);
            context.stroke();
            break;
        case "text":
            // (x, y) is the top-left corner for every script; the browser shapes and orders
            // Arabic text itself, as Qt does for the IDE.
            context.fillStyle = String(shape[4]);
            context.font = `${Math.max(1, Math.floor(Number(shape[5]) || 16))}px sans-serif`;
            context.textAlign = "left";
            context.textBaseline = "top";
            context.fillText(String(shape[3]), shape[1], shape[2]);
            break;
        default:
            break;
        }
    }

    /** Applies one frame record: the first `keep` shapes stay, the rest are the record's. */
    function apply(record)
    {
        if (!record || record.type !== "frame")
        {
            return;
        }
        const keep = Math.max(0, Number(record.keep) || 0);
        const added = Array.isArray(record.shapes) ? record.shapes.filter(Array.isArray) : [];
        state.width = Math.max(1, Number(record.width) || state.width);
        state.height = Math.max(1, Number(record.height) || state.height);
        state.shapes = state.shapes.slice(0, keep).concat(added);
        state.frame = Number(record.n) || 0;
        scheduleDraw();
    }

    // Frames may arrive faster than the screen refreshes (a burst after a pause in the
    // messages): the picture is drawn once per refresh, with the newest frame - as the Thunder
    // IDE paints only the latest one - instead of once per frame.
    let drawPending = false;
    function scheduleDraw()
    {
        if (drawPending)
        {
            return;
        }
        drawPending = true;
        const paint = () =>
        {
            drawPending = false;
            draw();
        };
        if (typeof window.requestAnimationFrame === "function" && !document.hidden)
        {
            window.requestAnimationFrame(paint);
        }
        else
        {
            setTimeout(paint, 0); // a hidden page gets no animation frames, but must stay current
        }
    }

    // ------------------------------------------------------------------ keys and the mouse

    const SPECIAL = {
        ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down",
        " ": "space", Enter: "enter", Escape: "escape", Backspace: "backspace", Tab: "tab",
    };

    /** The name a program sees, as the IDE sends it (core/canvas.py key_name), or null. */
    function keyName(event)
    {
        if (SPECIAL[event.key])
        {
            return SPECIAL[event.key];
        }
        if (event.key.length === 1 && /[\p{L}\p{N}]/u.test(event.key))
        {
            return event.key.toLowerCase();
        }
        return null;
    }

    function send(text)
    {
        vscode.postMessage({ type: "event", text });
    }

    window.addEventListener("keydown", (event) =>
    {
        const name = keyName(event);
        if (name === null)
        {
            return;
        }
        event.preventDefault(); // Tab and the arrows belong to the program here
        send(`key down ${name}`); // auto-repeated presses count as presses, as in the IDE
    });

    window.addEventListener("keyup", (event) =>
    {
        const name = keyName(event);
        if (name === null || event.repeat)
        {
            return;
        }
        event.preventDefault();
        send(`key up ${name}`);
    });

    function reportMouse(event, pressed)
    {
        if (pressed !== undefined)
        {
            state.mouseDown = pressed;
        }
        const { x, y } = toCanvas(event.clientX, event.clientY);
        const line = `mouse ${x} ${y} ${state.mouseDown ? 1 : 0}`;
        if (line !== state.lastMouse)
        {
            state.lastMouse = line;
            send(line);
        }
    }

    picture.addEventListener("mousemove", (event) => reportMouse(event));
    picture.addEventListener("mousedown", (event) =>
    {
        picture.focus();
        reportMouse(event, true);
    });
    picture.addEventListener("mouseup", (event) => reportMouse(event, false));

    // ------------------------------------------------------------------ the extension

    window.addEventListener("message", (event) =>
    {
        const message = event.data || {};
        switch (message.type)
        {
        case "begin":
            state.shapes = [];
            state.width = 400;
            state.height = 300;
            state.frame = 0;
            state.ended = false;
            status.hidden = true;
            draw();
            break;
        case "frame":
            apply(message.record);
            break;
        case "end":
            state.ended = true; // the picture stays, as the IDE's canvas does
            break;
        default:
            break;
        }
    });

    window.addEventListener("resize", draw);
    draw();
    picture.focus();
    vscode.postMessage({ type: "ready" });
    globalThis.thunderCanvas = { apply, draw, state, toCanvas, keyName };
}());
