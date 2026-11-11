# The Thunder Native Interface (calling C++)

A Thunder program can call functions written in **C or C++**, and that code can call back into
Thunder. It works the way Java's JNI does: the C++ side is an ordinary shared library, the
Thunder side declares what it may call, and a small C interface sits between them.

```thunder
import native "mathx"                       // loads libmathx.so / mathx.dll
native func fast_hypot(x: float, y: float): float

println(fast_hypot(3, 4))                   // 5.0
```

The interface is deliberately small: **one header** (`include/thunder/native.h`, about 150
lines), **one entry point** in the library, and **one struct** for values. There is nothing to
allocate, nothing to free and no reference counting to get wrong.

A complete, buildable example is in [`lib/native/`](../lib/native/): a module in C++
(`thunder_demo.cpp`), the Thunder program that uses it (`demo.th`) and a `README.md` with the
build commands. A larger, real one is the [sockets module](sockets.md)
(`lib/native/sockets/`), which gives Thunder programs TCP and UDP. In the IDE this document
is under **Help > Native interface reference**.

---

## 1. The Thunder side

| Line | Meaning |
| --- | --- |
| `import native "mathx"` | Load the library `mathx` when the program starts. May appear several times; only at the top level, like any other `import`. |
| `native func name(params): type` | A function the library provides. No body: it is in the library. |

A `native func` behaves like any other function: it can be called before the line that declares
it, its arguments are **type-checked when the program is compiled**, and a wrong call is a
compile error, not a crash. The one difference is that a native function is not a *value*: you
cannot pass `fast_hypot` itself to `map`, because there is no Thunder function to pass
(error `S025`).

### Which types may cross

| Thunder type | In C | Notes |
| --- | --- | --- |
| `int` | `int64_t` | |
| `float` | `double` | A whole number is widened as usual, so `fast_hypot(3, 4)` is fine. |
| `bool` | `int` (0 or 1) | |
| `string` | `const char*` + length | UTF-8, **not** necessarily zero-terminated. |
| `func(...)` | `ThunderValue` | **Parameters only**: a callback the C++ code can invoke. |
| nothing | `THUNDER_VOID` | A `native func` without a return type. |

Arrays, maps, tuples and structs may **not** cross (compile error `S053`). That is the line
that keeps the interface simple: there is no shared ownership to reason about, so neither side
can free something the other is still using. Pass an array by walking it in Thunder and calling
the native function once per element, or let the native code call back.

A native function may take at most 16 parameters (`S055`), and a program that declares one
without importing a library is an error (`S054`).

---

## 2. The C++ side

A module is a shared library that includes `<thunder/native.h>` and exports one function:

```cpp
#include <thunder/native.h>
#include <cmath>

static const ThunderApi* host = nullptr;

static ThunderValue fast_hypot(ThunderEnv*, const ThunderValue* args, int32_t)
{
    return thunder_float(std::hypot(args[0].number, args[1].number));
}

extern "C" THUNDER_EXPORT int thunder_module_init(ThunderRegistry* reg, const ThunderApi* api)
{
    if (api->abi_version != THUNDER_NATIVE_ABI)
    {
        return 0;                       // built against another version of the interface
    }
    host = api;
    return reg->add(reg, "fast_hypot", fast_hypot);
}
```

Every registered function has the same shape:

```c
ThunderValue f(ThunderEnv* env, const ThunderValue* args, int32_t count);
```

`count` always matches the Thunder declaration, and the VM has already checked the types, so a
function can read `args[0].number` without testing anything.

### Values

`ThunderValue` is a plain struct: a `kind` and the fields that go with it. Build one with the
inline helpers — `thunder_void()`, `thunder_int(n)`, `thunder_float(x)`, `thunder_bool(b)`,
`thunder_text(pointer, length)`.

**Strings.** An argument's text is valid until the function returns. A text you *return* is
copied by the VM as soon as the function returns, so returning a string literal, or a `static`
/ member buffer that lives that long, is safe; returning a pointer into a local array that has
already gone out of scope is not.

### Reporting an error

```c
host->fail(env, "divide() cannot divide by zero");
return thunder_void();          /* fail() returns: leave straight away */
```

The Thunder program then stops with runtime error `R030`, naming the message. `fail` *returns*
rather than unwinding, because a C++ exception cannot safely be thrown through a C frame that
may have been built by another compiler.

### Calling back into Thunder

```c
/* native func each_prime(limit: int, report: func(int)) */
ThunderValue argument = thunder_int(n);
host->call(env, args[1], &argument, 1);
if (host->failed(env))
{
    return thunder_void();      /* the Thunder callback failed: let the VM report it */
}
```

`call` runs the Thunder function on the same virtual machine — the debugger, the step limit and
the call-depth limit all still apply — and returns its result. If the Thunder code fails, the
error is remembered instead of being thrown: check `failed(env)` after each call and return.

---

## 3. Building a module

A module needs nothing but the header; it links against no Thunder library.

```sh
# Linux / macOS
c++ -std=c++17 -shared -fPIC -I<thunder>/include -o libmathx.so mathx.cpp
```

```powershell
# Windows (Developer Command Prompt)
cl /std:c++17 /LD /I<thunder>\include mathx.cpp /Fe:mathx.dll
```

With CMake, as `lib/native/CMakeLists.txt` does:

```cmake
add_library(mathx MODULE mathx.cpp)
target_include_directories(mathx PRIVATE <thunder>/include)
set_target_properties(mathx PROPERTIES PREFIX "lib" CXX_VISIBILITY_PRESET hidden)
```

The compiler and the C++ standard are yours to choose: the boundary is plain C, so a module
built with a different compiler from the VM still works.

### Where the library is looked for

`import native "mathx"` looks for `libmathx.so` (Linux), `mathx.dll` (Windows) or
`libmathx.dylib` (macOS), in this order:

1. the directory of the program being run;
2. the directory of `thundervm` itself (this is where a [packaged](packaging.md) program keeps
   its libraries);
3. each `--native-path <dir>` given to `thundervm`, then each directory in the
   `THUNDER_NATIVE_PATH` environment variable;
4. wherever the operating system's own loader looks.

`thundervm --no-native` refuses to load any library at all: the program stops with `R028`.

---

## 4. Under the bonnet

* A native declaration becomes an entry in the program's **native table** (name, arity and the
  kind of each parameter), and a call becomes one instruction, `CALL_NATIVE i`. You can see
  both in the IDE's Bytecode tab and with `thunderc --emit asm`.
* The libraries are loaded **once, before the first statement runs**, so a missing library or a
  missing function is reported immediately (`R028`, `R029`) rather than halfway through.
* Arguments are converted from the VM's stack into `ThunderValue`s on the way in; the result is
  converted back according to the **declared** return type, so a module that returns the wrong
  kind cannot put a surprising value into a typed program.
* `dlopen`/`LoadLibrary` live behind `os::Library` (`src/core/os.cpp`); the registry and the
  host functions are in `src/core/native.cpp` and `src/core/vm_native.cpp`.

**A note on safety.** Native code runs with the full rights of the process: it is not checked,
sandboxed or limited by the VM. A Thunder program is otherwise completely safe — there is no
undefined behaviour anywhere in the language — and `import native` is the one door out of that.
Only load libraries you built or trust, exactly as you would with a Python extension module or
a Java JNI library. In a classroom, `thundervm --no-native` closes the door.

---

## 5. Error codes

| Code | Phase | Meaning |
| --- | --- | --- |
| `S053` | semantic | a native function uses a type that cannot cross the boundary |
| `S054` | semantic | `native func` without `import native "..."` |
| `S055` | semantic | a native function has more than 16 parameters |
| `R028` | runtime | the library cannot be loaded (not found, not a module, or switched off) |
| `R029` | runtime | the library does not provide a function of that name |
| `R030` | runtime | the native function called `fail()` |

---

## 6. Pashto names

| English | Pashto |
| --- | --- |
| `import native "x"` | `راوړه بهرنی "x"` |
| `native func` | `بهرنی دنده` |

The library's name and the function names stay as they are: they are symbols in a C++ file,
not Thunder words. `thunderc --translate` converts the keywords around them.

```thunder
// ژبه: ps
راوړه بهرنی "mathx"
بهرنی دنده fast_hypot(س: اعشاري، ص: اعشاري): اعشاري
کرښه چاپ(fast_hypot(۳، ۴))
```
