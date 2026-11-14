# Sockets: talking over the network

*Part of the [Thunder SDK reference](README.md).*

A Thunder program can talk to a program on another computer (or to a web server, or to
itself) through **sockets**. One side *listens* on a port, the other *connects* to that
computer and port, and then text flows both ways until one side closes. That is all a chat
program, a game server or a web browser does underneath.

```thunder
import "sockets"

var server = tcp_listen("", 5000)            # wait for callers on port 5000
var connection = tcp_accept(server)          # ... here comes one
println(socket_receive_line(connection))     # what it said
socket_send_line(connection, "hello back")   # the answer
```

```thunder
import "sockets"

var sock = tcp_connect("192.168.1.20", 5000) # the computer running the program above
socket_send_line(sock, "hello there")
println(socket_receive_line(sock))           # "hello back"
socket_close(sock)
```

Sockets come from a [native module](native.md): the C++ library `thunder_sockets`
(`lib/native/sockets/thunder_sockets.cpp`), built with the toolchain and sitting next to
`thundervm`. The Thunder module [`sockets.th`](../lib/thunder/en/sockets.th) declares its
functions, so a program needs only `import "sockets"`: thunderc finds the SDK's modules in
`lib/thunder/<language>` next to itself. In the IDE this document is under
**Help > Sockets reference**; sample 35 runs a server and a client in one program.

---

## 1. The idea in five lines

| Word | Meaning |
| --- | --- |
| **address** | which computer: `"127.0.0.1"` is always *this* one, `"192.168.1.20"` a neighbour, `"example.org"` a name the network looks up |
| **port** | which program on that computer: a number from 1 to 65535; `0` asks the system for any free one |
| **TCP** | a *connection*: text arrives complete and in order, both ways, until one side closes |
| **UDP** | single *messages* (datagrams) to any address, with no connection; quick, but one may be lost |
| **socket** | the handle (an `int`) a program holds for a connection, a listener or a UDP socket |

---

## 2. TCP

| Function | Returns | What it does |
| --- | --- | --- |
| `tcp_listen(host, port): int` | listener, or -1 | Starts waiting for connections. `host` `""` means every address of this computer; `port` `0` lets the system choose (ask `socket_port`). |
| `tcp_accept(listener): int` | connection, or -1 | Waits for the next caller and returns the connection to it. |
| `tcp_connect(host, port): int` | connection, or -1 | Connects to a listening program. -1 if nobody listens there, or the name is unknown. |
| `socket_send(sock, text): int` | bytes sent, or -1 | Sends the text (as UTF-8 bytes). |
| `socket_send_line(sock, text): int` | bytes sent, or -1 | Sends the text and a line break. |
| `socket_receive(sock, max_bytes): string` | text, or `""` | Waits for up to `max_bytes` of text. |
| `socket_receive_line(sock): string` | a line, or `""` | Waits for a whole line and returns it without the line break. |
| `socket_open(sock): bool` | | True while the connection can still deliver something. |
| `socket_close(sock)` | | Closes a connection, listener or UDP socket; the other side notices. |
| `socket_set_timeout(sock, milliseconds): bool` | | How long `tcp_accept`, `socket_receive` and `udp_receive` may wait; -1 (the default) means for ever. A listener's timeout is inherited by the connections it accepts. |
| `socket_port(sock): int` | | The port on this computer (useful after `tcp_listen(host, 0)`). |
| `socket_peer(sock): string` | `"ip:port"` | Who is at the other end of a connection. |
| `socket_error(): string` | | Why the last call that returned -1 or `""` did so: `"Connection refused"`, `"timed out"`, `"closed by the other side"`, ... |
| `request(host, port, line): string` | the answer, or `""` | Connect, send one line, read one line, disconnect — a whole exchange in one call (this one is written in Thunder, in `sockets.th`). |

**Lines are the natural unit.** Most simple protocols are "one line each way": the client
sends a line, the server reads it with `socket_receive_line`, answers with
`socket_send_line`. `socket_receive` is for text that is not line-shaped; it returns whatever
has arrived, up to the limit, and `""` once the other side has closed.

**Waiting.** `tcp_accept`, `socket_receive` and `socket_receive_line` *wait* until something
happens — like `input()` waits for the user. A program that must not wait for ever sets a
timeout:

```thunder
socket_set_timeout(connection, 2000)          # two seconds
var line = socket_receive_line(connection)
if line == "" and not socket_open(connection)
    println("the other side went away")
else if line == ""
    println("nothing arrived: ", socket_error())  # "timed out"
```

---

## 3. UDP

| Function | Returns | What it does |
| --- | --- | --- |
| `udp_open(port): int` | socket, or -1 | Opens a UDP socket on a port of this computer (`0`: any free port). |
| `udp_send(sock, host, port, text): int` | bytes sent, or -1 | Sends one message to `host:port`. |
| `udp_receive(sock, max_bytes): string` | the message, or `""` | Waits for one message. |
| `udp_sender(): string` | `"ip:port"` | Who sent the message `udp_receive` last returned — so a server can answer it. |

UDP has no connection and no guarantee: a message may arrive late, twice or never. It suits
things where the newest value matters more than every value — the position of a player in a
game, a sensor reading every second.

---

## 4. Names and addresses

| Function | What it does |
| --- | --- |
| `host_address(name): string` | The IP address of a name (`"example.org"` → `"93.184.216.34"`), `""` if the network does not know it. |
| `host_name(): string` | This computer's own name. |

---

## 5. Two kinds of trouble

Something going wrong *on the network* is normal and is reported as a **value**: -1 or `""`,
with `socket_error()` saying why. A program decides what to do about it — try again, tell
the user, give up.

A **mistake in the program** — using a handle that is not open, calling `tcp_accept` on a
connection instead of a listener, `udp_send` on a TCP socket — stops the program with runtime
error `R030` and a message such as `socket_send: socket 99 is not open`, exactly as a wrong
built-in call would. Those are bugs to fix, not conditions to handle.

---

## 6. Running and packaging

* In the IDE, **Run** just works: `thunder_sockets` is built with the toolchain and found next
  to `thundervm`. Outside the IDE the same applies to `thundervm program.thc`.
* A [packaged](packaging.md) program gets the library copied next to it automatically,
  because it is an ordinary native library (`import native "thunder_sockets"` in
  `sockets.th`): ship the directory, not only the executable.
* To use sockets in a program of your own, just write `import "sockets"`: the module is found
  in `lib/thunder/<language>` next to `thunderc` (copy it next to your program, or use `-I <dir>`,
  to pin another version).
* The library uses the platform's own sockets — BSD sockets on Linux and macOS, Winsock on
  Windows — and nothing else. Its source, about 500 lines of C++, is a worked example of a
  native module that keeps state (the table of open sockets) between calls.

**A note on safety.** A program that listens on `""` (every address) can be reached from other
computers on the network; on `"127.0.0.1"` only from the same machine. The usual rules of the
[native interface](native.md) apply: the library runs with the rights of the process, and
`thundervm --no-native` switches it (and every native library) off.

---

## 7. Pashto names

`sockets.th` exists in Pashto too (`lib/thunder/ps/sockets.th`); the library registers
every function under both names.

| English | Pashto |
| --- | --- |
| `tcp_listen` | `اورېدونکی_جوړ_کړه` |
| `tcp_accept` | `اړیکه_ومنه` |
| `tcp_connect` | `ونښلېږه` |
| `socket_send` / `socket_send_line` | `ساکټ_ولېږه` / `ساکټ_کرښه_ولېږه` |
| `socket_receive` / `socket_receive_line` | `ساکټ_واخله` / `ساکټ_کرښه_واخله` |
| `socket_open` | `ساکټ_پرانیستی_دی` |
| `socket_close` | `ساکټ_وتړه` |
| `socket_set_timeout` | `ساکټ_وخت_حد` |
| `socket_port` / `socket_peer` | `ساکټ_پورټ` / `ساکټ_مقابل_لوری` |
| `socket_error` | `ساکټ_تېروتنه` |
| `udp_open` / `udp_send` / `udp_receive` / `udp_sender` | `یو_ډي_پي_پرانیزه` / `یو_ډي_پي_ولېږه` / `یو_ډي_پي_واخله` / `یو_ډي_پي_لېږونکی` |
| `host_address` / `host_name` | `د_کوربه_پته` / `د_کوربه_نوم` |
| `request` | `غوښتنه` |
