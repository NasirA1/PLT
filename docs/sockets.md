# Sockets: talking over the network

*Part of the [Thunder SDK reference](README.md).*

A Thunder program can talk to a program on another computer (or to a web server, or to
itself) through **sockets**. One side *listens* on a port, the other *connects* to that
computer and port, and then text flows both ways until one side closes. That is all a chat
program, a game server or a web browser does underneath.

```thunder
import "sockets"

with server = tcp.listen("", 5000)               # wait for callers on port 5000
    with connection = tcp.accept(server)         # ... here comes one
        println(socket.receive_line(connection))     # what it said
        socket.send_line(connection, "hello back")   # the answer
# here "with" has closed the connection, then the listener
```

```thunder
import "sockets"

with connection = tcp.connect("192.168.1.20", 5000)  # the computer running the program above
    if connection == nothing
        println("cannot connect: ", socket.error())
    else
        socket.send_line(connection, "hello there")
        println(socket.receive_line(connection))      # "hello back"
```

Sockets come from a [native module](native.md): the C++ library `thunder_sockets`
(`lib/native/sockets/thunder_sockets.cpp`), built with the toolchain and sitting next to
`thundervm`. The Thunder file [`sockets.th`](../lib/thunder/en/sockets.th) declares four
modules on top of it - `tcp`, `udp`, `socket` and `host` - so a program needs only
`import "sockets"`: thunderc finds the SDK's files in `lib/thunder/<language>` next to itself.
In the IDE this document is under **Help > Sockets reference**; sample 38
(`samples/en/38_sockets.th`) runs a server and a client in one program.

---

## 1. The idea in five lines

| Word | Meaning |
| --- | --- |
| **address** | which computer: `"127.0.0.1"` is always *this* one, `"192.168.1.20"` a neighbour, `"example.org"` a name the network looks up |
| **port** | which program on that computer: a number from 1 to 65535; `0` asks the system for any free one |
| **TCP** | a *connection*: text arrives complete and in order, both ways, until one side closes |
| **UDP** | single *messages* (datagrams) to any address, with no connection; quick, but one may be lost |
| **socket** | a `socket.Socket`: what a program holds for a connection, a listener or a UDP socket (inside, the number the C++ side knows it by) |

The four modules split the work the same way:

| Module | What is in it |
| --- | --- |
| `tcp` | connections: `listen`, `accept`, `connect`, and `request` (one whole exchange) |
| `udp` | single messages: `open`, `send`, `receive`, `sender` |
| `socket` | what every socket can do: send and receive text or bytes, `close`, timeouts, `error` ... and the type `socket.Socket` |
| `host` | names and addresses: `host.address("example.org")`, `host.name()` |

Opening a socket can fail - nobody listens, the port is taken, the name is unknown. Then
`tcp.listen`, `tcp.accept`, `tcp.connect` and `udp.open` return **`nothing`** instead of a
socket, and `socket.error()` says why.

The module names are reserved in a program that imports `"sockets"`: a variable cannot be
called `socket`, `tcp`, `udp` or `host` (error `S065`). Call it `connection`, `listener`,
`sock` ...

---

## 2. TCP

| Function | Returns | What it does |
| --- | --- | --- |
| `tcp.listen(address, port): socket.Socket` | listener, or `nothing` | Starts waiting for connections. `address` `""` means every address of this computer; `port` `0` lets the system choose (ask `socket.port`). |
| `tcp.accept(listener): socket.Socket` | connection, or `nothing` | Waits for the next caller and returns the connection to it (after `socket.set_timeout` on the listener, also `nothing` when nobody came in time). |
| `tcp.connect(server, port): socket.Socket` | connection, or `nothing` | Connects to a listening program. `nothing` if nobody listens there, or the name is unknown. |
| `tcp.request(server, port, line): string` | the answer, or `""` | Connect, send one line, read one line, disconnect - a whole exchange in one call (written in Thunder, in `sockets.th`, with `with`). |

What every socket can do, in the module `socket`:

| Function | Returns | What it does |
| --- | --- | --- |
| `socket.send(sock, text): int` | bytes sent, or -1 | Sends the text (as UTF-8 bytes). |
| `socket.send_line(sock, text): int` | bytes sent, or -1 | Sends the text and a line break. |
| `socket.receive(sock, max_bytes): string` | text, or `""` | Waits for up to `max_bytes` of text. |
| `socket.receive_line(sock): string` | a line, or `""` | Waits for a whole line and returns it without the line break. |
| `socket.send_bytes(sock, data: byte[]): int` | bytes sent, or -1 | Sends raw bytes as they are (a file, a packet of a binary protocol). |
| `socket.receive_bytes(sock, max_bytes): byte[]` | bytes, or `[]` | Waits for up to `max_bytes` of whatever arrives, as a `byte[]`; see "Bytes" in the [library reference](library.md). |
| `socket.is_open(sock): bool` | | True while the connection can still deliver something. |
| `socket.close(sock)` | | Closes a connection, listener or UDP socket; the other side notices. `with` calls it for you (section 3); closing a socket twice does no harm. |
| `socket.set_timeout(sock, milliseconds): bool` | | How long `tcp.accept`, `socket.receive` and `udp.receive` may wait; -1 (the default) means for ever. A listener's timeout is inherited by the connections it accepts. |
| `socket.port(sock): int` | | The port on this computer (useful after `tcp.listen(address, 0)`). |
| `socket.peer(sock): string` | `"ip:port"` | Who is at the other end of a connection. |
| `socket.error(): string` | | Why the last call that failed (`nothing`, -1 or `""`) did so: `"Connection refused"`, `"timed out"`, `"closed by the other side"`, ... |
| `socket.from_handle(handle): socket.Socket` | socket, or `nothing` | The socket for a number the C++ side returned (`nothing` for -1). The modules use it; so may a native library of your own that opens sockets. |

**Lines are the natural unit.** Most simple protocols are "one line each way": the client
sends a line, the server reads it with `socket.receive_line`, answers with
`socket.send_line`. `socket.receive` is for text that is not line-shaped; it returns whatever
has arrived, up to the limit, and `""` once the other side has closed.

**Waiting.** `tcp.accept`, `socket.receive` and `socket.receive_line` *wait* until something
happens - like `input()` waits for the user. A program that must not wait for ever sets a
timeout:

```thunder
import "sockets"

# Reads one line, waiting two seconds at most.
func read_line(connection: socket.Socket): string
    socket.set_timeout(connection, 2000)          # two seconds
    var line = socket.receive_line(connection)
    if line == "" and not socket.is_open(connection)
        println("the other side went away")
    else if line == ""
        println("nothing arrived: ", socket.error())  # "timed out"
    return line
```

---

## 3. Closing sockets: `with`

A socket stands for something outside the program - a connection the other side is holding
open, a port the system has lent out - so it must be given back when the program is done with
it. `with` does that: the socket it opens is closed when the block ends, **however** it ends -
at the end of the block, or by a `return`, `break` or `continue` that leaves it.

```thunder
import "sockets"

with connection = tcp.connect("localhost", 7000)
    if connection != nothing
        socket.send_line(connection, "hello")
# connection is closed here
```

This is how `tcp.request` is written in `sockets.th`: an early `return` from inside the block
still closes the connection, and the value is worked out before it is closed.

```thunder
import "sockets"

func request(server: string, port: int, line: string): string
    with connection = tcp.connect(server, port)
        if connection == nothing
            return ""
        socket.send_line(connection, line)
        return socket.receive_line(connection)     # read first, then closed
```

* **Nothing is not closed.** When opening failed, the name holds `nothing` and the end of the
  block simply passes it by; test for `nothing` inside the block, as above.
* **Nested blocks close the innermost first:** in the first example the connection is closed
  before the listener.
* **Closing twice does no harm.** A program may still call `socket.close` itself inside the
  block (to let the other side see the end, say); the close at the end of the block then does
  nothing more.
* **A socket you already have** goes in without a name: `with listener` closes `listener` at
  the end of the block.
* The name is a constant inside the block: assigning to it is an error (`S012`).
* Not closed: when the program stops with a runtime error or `os.exit()`. The operating system
  frees every socket of a process that ends, so nothing is left behind.

`with` works for any struct whose module has a `close` function for it - `socket.Socket` is
one; the [language reference](language.md) and sample 39 (`samples/en/39_with.th`) show the
whole statement. **In Pashto the keyword comes at the end of the line**, as the language says
it - "ساکټ سره", *with the socket*:

```thunder
# ژبه: ps
راوړه "sockets"

اړیکه = ټي_سي_پي.ونښلېږه("localhost"، ۷۰۰۰) سره
    که اړیکه != هېڅ
        ساکټ.کرښه_ولېږه(اړیکه، "سلام")
```

`thunderc --translate` (and the IDE's Translate) moves the keyword from one end of the line to
the other.

---

## 4. UDP

| Function | Returns | What it does |
| --- | --- | --- |
| `udp.open(port): socket.Socket` | socket, or `nothing` | Opens a UDP socket on a port of this computer (`0`: any free port). |
| `udp.send(sock, server, port, text): int` | bytes sent, or -1 | Sends one message to `server:port`. |
| `udp.receive(sock, max_bytes): string` | the message, or `""` | Waits for one message. |
| `udp.send_bytes(sock, server, port, data: byte[]): int` | bytes sent, or -1 | A datagram of raw bytes. |
| `udp.receive_bytes(sock, max_bytes): byte[]` | bytes, or `[]` | A datagram as a `byte[]`. |
| `udp.sender(): string` | `"ip:port"` | Who sent the message `udp.receive` last returned - so a server can answer it. |

A UDP socket is a `socket.Socket` like any other: `socket.set_timeout`, `socket.port` and
`socket.close` work on it, and `with` closes it.

```thunder
import "sockets"

with receiver = udp.open(0)
    with sender = udp.open(0)
        udp.send(sender, "127.0.0.1", socket.port(receiver), "a datagram")
        println(udp.receive(receiver, 100))       # a datagram
```

UDP has no connection and no guarantee: a message may arrive late, twice or never. It suits
things where the newest value matters more than every value - the position of a player in a
game, a sensor reading every second.

---

## 5. Names and addresses

| Function | What it does |
| --- | --- |
| `host.address(name): string` | The IP address of a name (`"example.org"` → `"93.184.216.34"`), `""` if the network does not know it. |
| `host.name(): string` | This computer's own name. |

---

## 6. Two kinds of trouble

Something going wrong *on the network* is normal and is reported as a **value**: `nothing`
instead of a socket, -1 or `""`, with `socket.error()` saying why. A program decides what to
do about it - try again, tell the user, give up.

A **mistake in the program** - using a socket after it was closed, calling `tcp.accept` on a
connection instead of a listener, `udp.send` on a TCP socket - stops the program with runtime
error `R030` and a message such as `socket.send_line: socket 0 is not open`, exactly as a
wrong built-in call would. Using the `nothing` a failed `tcp.connect` returned as if it were a
socket stops it too (`R033`, "the value is nothing"). Those are bugs to fix, not conditions to
handle.

---

## 7. Running and packaging

* In the IDE, **Run** just works: `thunder_sockets` is built with the toolchain and found next
  to `thundervm`. Outside the IDE the same applies to `thundervm program.thc`.
* A [packaged](packaging.md) program gets the library copied next to it automatically,
  because it is an ordinary native library (`import native "thunder_sockets"` in
  `sockets.th`): ship the directory, not only the executable.
* To use sockets in a program of your own, just write `import "sockets"`: the file is found
  in `lib/thunder/<language>` next to `thunderc` (copy it next to your program, or use `-I <dir>`,
  to pin another version).
* The library uses the platform's own sockets - BSD sockets on Linux and macOS, Winsock on
  Windows - and nothing else. Its source, about 800 lines of C++, is a worked example of a
  native module that keeps state (the table of open sockets) between calls.
* `sockets.th` is also the worked example of [native functions inside a module](native.md#native-functions-inside-a-module):
  `module socket` declares `native func _send(handle: int, text: string): int`, and the C++
  side registers that function under its qualified name, `"socket._send"` (and the Pashto
  `"ساکټ._ولېږه"`). The `_` keeps these raw, number-taking functions private to their module;
  the public `socket.send` wraps them in the `socket.Socket` type.

**A note on safety.** A program that listens on `""` (every address) can be reached from other
computers on the network; on `"127.0.0.1"` only from the same machine. The usual rules of the
[native interface](native.md) apply: the library runs with the rights of the process, and
`thundervm --no-native` switches it (and every native library) off.

---

## 8. Pashto names

`sockets.th` exists in Pashto too (`lib/thunder/ps/sockets.th`), with the same four modules;
the library registers every function under both names.

| English | Pashto |
| --- | --- |
| module `tcp` | `ټي_سي_پي` |
| `tcp.listen` / `tcp.accept` / `tcp.connect` / `tcp.request` | `ټي_سي_پي.اورېدونکی_جوړ_کړه` / `ټي_سي_پي.اړیکه_ومنه` / `ټي_سي_پي.ونښلېږه` / `ټي_سي_پي.غوښتنه` |
| module `socket`, type `socket.Socket` | `ساکټ`, `ساکټ.ساکټ` |
| `socket.send` / `socket.send_line` | `ساکټ.ولېږه` / `ساکټ.کرښه_ولېږه` |
| `socket.receive` / `socket.receive_line` | `ساکټ.واخله` / `ساکټ.کرښه_واخله` |
| `socket.send_bytes` / `socket.receive_bytes` | `ساکټ.بایټونه_ولېږه` / `ساکټ.بایټونه_واخله` |
| `socket.is_open` | `ساکټ.پرانیستی_دی` |
| `socket.close` | `ساکټ.وتړه` |
| `socket.set_timeout` | `ساکټ.وخت_حد` |
| `socket.port` / `socket.peer` | `ساکټ.پورټ` / `ساکټ.مقابل_لوری` |
| `socket.error` / `socket.from_handle` | `ساکټ.تېروتنه` / `ساکټ.له_نښې` |
| module `udp` | `یو_ډي_پي` |
| `udp.open` / `udp.send` / `udp.receive` / `udp.sender` | `یو_ډي_پي.پرانیزه` / `یو_ډي_پي.ولېږه` / `یو_ډي_پي.واخله` / `یو_ډي_پي.لېږونکی` |
| `udp.send_bytes` / `udp.receive_bytes` | `یو_ډي_پي.بایټونه_ولېږه` / `یو_ډي_پي.بایټونه_واخله` |
| module `host` | `کوربه` |
| `host.address` / `host.name` | `کوربه.پته` / `کوربه.نوم` |
| `with name = resource` | `نوم = سرچینه سره` (the keyword last) |

**Before 0.7** these were plain functions with prefixes - `tcp_connect`, `socket_send_line`,
`socket_close`, `host_address` ... - and a socket was a bare `int`, with -1 for failure. Each
now lives in its module under the short name (`tcp.connect`, `socket.send_line`,
`socket.close`, `host.address`), `socket_open` became `socket.is_open`, and `request` became
`tcp.request`.
