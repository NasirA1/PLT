# thunder_sockets: network sockets as a native module

TCP and UDP for Thunder programs, built on the platform's own sockets (BSD sockets on Linux
and macOS, Winsock on Windows) through the [native interface](../../../docs/native.md).

| File | What it is |
| --- | --- |
| `thunder_sockets.cpp` | the module: 19 functions, each registered under an English and a Pashto name |
| `../../thunder/en/sockets.th`, `../../thunder/ps/sockets.th` | the Thunder modules that declare them: `import "sockets"` |
| `../../../samples/en/35_sockets.th`, `../../../samples/ps/35_sockets.th` | a sample that is server and client at once |

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

Beyond the demo module, it shows a module that **keeps state between calls** (the table of
open sockets, the text received but not yet handed over) and that separates *conditions*
the program should handle (returned as -1 or `""`, explained by `socket_error()`) from
*mistakes* in the program (reported with `fail()`, which stops it with `R030`).
