// The program being debugged: `thundervm --debug` at the other end of two pipes.
//
// The virtual machine speaks a small protocol of JSON lines (docs/debugging.md): requests go
// in, responses and events come out. This class turns that into promises and event handlers.
// It is written against a *transport* - {write(line), onLine(callback), onClose(callback),
// kill()} - rather than against a child process, so the adapter can be tested with a scripted
// virtual machine in place of the real one (test/adapter.test.js).

"use strict";

const childProcess = require("child_process");

class Debuggee
{
    constructor(transport)
    {
        this.transport = transport;
        this.sequence = 1;
        this.pending = new Map();   // seq -> {resolve, reject}
        this.handlers = new Map();  // event -> [callback]
        this.closed = false;
        this.started = false;
        this.held = [];             // lines that arrived before start()
        transport.onLine((line) => (this.started ? this._receive(line) : this.held.push(line)));
        transport.onClose((code) =>
        {
            this.closed = true;
            for (const { reject } of this.pending.values())
            {
                reject(new Error("the program ended before it answered"));
            }
            this.pending.clear();
            this._emit("close", { code });
        });
    }

    /**
     * Starts delivering events. The machine speaks first (ready, then the entry stop), and the
     * lines it sends before the adapter has registered its handlers are held until then.
     */
    start()
    {
        this.started = true;
        const held = this.held;
        this.held = [];
        for (const line of held)
        {
            this._receive(line);
        }
    }

    /** Calls `callback(body)` for every event of a kind ("stopped", "output", "exited", ...). */
    on(event, callback)
    {
        if (!this.handlers.has(event))
        {
            this.handlers.set(event, []);
        }
        this.handlers.get(event).push(callback);
        return this;
    }

    /** Sends a request; the promise is the response body, or rejects with the VM's message. */
    request(command, args)
    {
        if (this.closed)
        {
            return Promise.reject(new Error("the program has ended"));
        }
        const seq = this.sequence++;
        const message = Object.assign({ seq, command }, args || {});
        return new Promise((resolve, reject) =>
        {
            this.pending.set(seq, { resolve, reject });
            this.transport.write(JSON.stringify(message));
        });
    }

    /** Ends the program: asks politely, gives it a moment to say goodbye, then makes sure. */
    async terminate()
    {
        if (this.closed)
        {
            return;
        }
        const closed = new Promise((resolve) => this.on("close", resolve));
        try
        {
            await Promise.race([this.request("terminate"), wait(500)]);
        }
        catch (error)
        {
            // it was already going
        }
        await Promise.race([closed, wait(1000)]);
        this.transport.kill();
    }

    _receive(line)
    {
        const text = line.trim();
        if (!text)
        {
            return;
        }
        let message = null;
        try
        {
            message = JSON.parse(text);
        }
        catch (error)
        {
            this._emit("garbage", { text });
            return;
        }
        if (message.type === "response")
        {
            const waiting = this.pending.get(message.request_seq);
            if (!waiting)
            {
                return;
            }
            this.pending.delete(message.request_seq);
            if (message.success)
            {
                waiting.resolve(message.body || {});
            }
            else
            {
                waiting.reject(new Error(message.message || `${message.command} failed`));
            }
            return;
        }
        if (message.type === "event")
        {
            this._emit(message.event, message);
        }
    }

    _emit(event, body)
    {
        for (const callback of (this.handlers.get(event) || []))
        {
            callback(body);
        }
    }
}

function wait(milliseconds)
{
    return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

/**
 * Starts `command` and wraps its pipes as a transport.
 *
 * Standard error is not part of the protocol; whatever the VM prints there is handed to
 * `options.onStderr`. The transport's `onClose` fires once, with the exit code (null if the
 * process could not be started, in which case `onError` says why first).
 */
function spawnTransport(command, options)
{
    const settings = options || {};
    const child = childProcess.spawn(command[0], command.slice(1), {
        cwd: settings.cwd,
        env: settings.env || process.env,
        windowsHide: true,
    });
    const lineListeners = [];
    const closeListeners = [];
    let closed = false;
    let remainder = "";
    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) =>
    {
        remainder += chunk;
        let newline = remainder.indexOf("\n");
        while (newline >= 0)
        {
            const line = remainder.slice(0, newline);
            remainder = remainder.slice(newline + 1);
            for (const listener of lineListeners)
            {
                listener(line);
            }
            newline = remainder.indexOf("\n");
        }
    });
    child.stderr.on("data", (chunk) =>
    {
        if (settings.onStderr)
        {
            settings.onStderr(chunk);
        }
    });
    const finish = (code) =>
    {
        if (closed)
        {
            return;
        }
        closed = true;
        if (remainder.trim())
        {
            for (const listener of lineListeners)
            {
                listener(remainder);
            }
        }
        for (const listener of closeListeners)
        {
            listener(code);
        }
    };
    child.on("error", (error) =>
    {
        if (settings.onError)
        {
            settings.onError(error);
        }
        finish(null);
    });
    child.on("close", (code) => finish(code));
    return {
        write(line)
        {
            if (!closed && child.stdin.writable)
            {
                child.stdin.write(line + "\n");
            }
        },
        onLine(callback)
        {
            lineListeners.push(callback);
        },
        onClose(callback)
        {
            closeListeners.push(callback);
        },
        kill()
        {
            if (!closed)
            {
                child.kill();
            }
        },
        get pid()
        {
            return child.pid;
        },
    };
}

module.exports = { Debuggee, spawnTransport };
