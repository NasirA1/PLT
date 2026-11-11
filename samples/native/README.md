# A Thunder native module, from start to finish

This directory is a complete example of the [Thunder Native Interface](../../docs/native.md):
calling C++ from Thunder, and C++ calling back. A second, larger module lives in
[`sockets/`](sockets/): network sockets for Thunder programs ([docs/sockets.md](../../docs/sockets.md)).

| File | What it is |
| --- | --- |
| `thunder_demo.cpp` | the module: six functions in C++, registered with one entry point |
| `demo.th` | the Thunder program that uses them |
| `CMakeLists.txt` | how it is built here (and a template for a module of your own) |

The module needs **one header** and links against nothing:
[`include/thunder/native.h`](../../include/thunder/native.h).

## Building and running

The module is built with the rest of the toolchain, so after `./run_tests.sh` or a plain
`cmake --build` it is already in `build/<config>/bin/` next to `thunderc` and `thundervm`:

```sh
cd <thunder>
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release

cd samples/native
../../build/release/bin/thunderc demo.th -o demo.thc
../../build/release/bin/thundervm --native-path ../../build/release/bin demo.thc
```

`--native-path` says where the library is. Copy `libthunder_demo.so` (or `thunder_demo.dll`)
next to `demo.thc` instead and no option is needed: a program always looks beside itself first.

To build it on its own, without CMake:

```sh
c++ -std=c++17 -shared -fPIC -I../../include -o libthunder_demo.so thunder_demo.cpp
```

```powershell
cl /std:c++17 /LD /I..\..\include thunder_demo.cpp /Fe:thunder_demo.dll
```

## What it prints

```
hypot(3, 4) = 5.0
HELLO FROM THUNDER!
checksum = 39286270
27 / 4 = 6
primes below 30: [2, 3, 5, 7, 11, 13, 17, 19, 23, 29]
apply_twice(7, triple) = 63
demo.th:39:9: runtime error [R030]: the native function failed: divide() cannot divide by zero
```

The last line is deliberate: it shows how C++ reports an error back to Thunder. Delete the
last line of `demo.th` to see the program end normally.

## Making it an application

The demo can be packaged like any other program; the library is copied next to the
executable automatically ([packaging reference](../../docs/packaging.md)):

```sh
../../build/release/bin/thunderc demo.th --package demoapp -I ../../build/release/bin
./demoapp
```

## What each function shows

| Function | Shows |
| --- | --- |
| `hypot` | the simplest case: decimals in, a decimal out |
| `shout` | taking and returning text (UTF-8, with a length, never owned by Thunder) |
| `checksum` | reading the bytes of a string |
| `divide` | reporting an error with `fail()`, which becomes runtime error `R030` |
| `each_prime` | **C++ calling Thunder**: a sieve that calls a Thunder function per prime |
| `apply_twice` | a callback that returns a value, used twice |
