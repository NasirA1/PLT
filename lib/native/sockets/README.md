# thunder_sockets: network sockets as a native module

TCP and UDP for Thunder programs, built on the platform's own sockets (BSD sockets on Linux
and macOS, Winsock on Windows) through the [native interface](../../../docs/native.md).

| File | What it is |
| --- | --- |
| `thunder_sockets.cpp` | the native library: 23 functions, each registered under its qualified English and Pashto name |
| `../../thunder/en/sockets.th`, `../../thunder/ps/sockets.th` | the Thunder side, `import "sockets"`: the modules `tcp`, `udp`, `socket` and `host` (Pashto `ټي_سي_پي`, `یو_ډي_پي`, `ساکټ`, `کوربه`) |
| `../../../samples/en/38_sockets.th`, `../../../samples/ps/38_sockets.th` | a sample that is server and client at once |

It is built with the toolchain (`../CMakeLists.txt`) and lands next to `thunderc` and
`thundervm`, so **Run** in the IDE and `thundervm` on the command line find it; a packaged
program gets it copied alongside. The reference is [`docs/sockets.md`](../../../docs/sockets.md).

On its own, without CMake:

```sh
c++ -std=c++17 -shared -fPIC -I../../../include -o libthunder_sockets.so thunder_sockets.cpp
```

```powershell
cl /std:c++17 /LD /I..\..\..\include thunder_sockets.cpp ws2_32.lib /Fe:thunder_sockets.dll
```

## Names: native functions inside modules

`sockets.th` declares the native functions *inside* its modules, as private members, and wraps
them in ordinary Thunder functions that take and return a `socket.Socket` - a struct holding the
handle the C++ side knows the socket by:

```thunder
import native "thunder_sockets"

module socket
    struct Socket
        handle: int

    native func _send(handle: int, text: string): int

    func send(s: Socket, text: string): int
        return _send(s.handle, text)
```

A member's name is its qualified name - the compiler renames `_send` inside `socket` to
`socket._send` - and that is the name the VM asks the library for. So `thunder_module_init`
registers every function under the qualified names of both languages' `sockets.th`, module and
all: `"socket._send"` and `"ساکټ._ولېږه"`, `"tcp._connect"` and `"ټي_سي_پي._ونښلېږه"`,
`"udp._open"` and `"یو_ډي_پي._پرانیزه"`, and `host.address` / `کوربه.پته` and `host.name` /
`کوربه.نوم`, which `sockets.th` declares without a wrapper. A program uses the wrappers:

```thunder
import "sockets"

with connection = tcp.connect("localhost", 7000)    # a socket.Socket, or nothing
    if connection != nothing
        socket.send_line(connection, "hello")
        println(socket.receive_line(connection))
# closed here by socket.close, which "with" calls at the end of the block
```

Because `socket.close` takes a `socket.Socket`, a socket is a **resource**: `with` closes it
when its block ends, however the block ends ([language reference](../../../docs/language.md),
section 11).

Beyond the demo library, it shows native code that **keeps state between calls** (the table of
open sockets, the text received but not yet handed over) and that separates *conditions* the
program should handle (returned as -1 or `""` - which the Thunder side turns into `nothing` where
a socket was expected - and explained by `socket.error()`) from *mistakes* in the program
(reported with `fail()`, which stops it with `R030`).
