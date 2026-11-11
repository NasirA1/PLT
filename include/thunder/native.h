/* thunder/native.h - the Thunder C++ Native Interface (TNI).
 *
 * A native module is a shared library (libNAME.so, NAME.dll, libNAME.dylib) that a Thunder
 * program loads with
 *
 *     import native "NAME"
 *     native func add(a: int, b: int): int
 *
 * The library exports one function, thunder_module_init, which registers the C or C++ functions
 * the program may call. Each registered function has the same shape:
 *
 *     ThunderValue add(ThunderEnv* env, const ThunderValue* args, int32_t count);
 *
 * and may call back into Thunder through thunder_call(). See docs/native.md and
 * lib/native/ for a complete, buildable example.
 *
 * This header is plain C (and valid C++), uses only <stdint.h>, and is the whole interface:
 * values are a small struct you build yourself, so there is nothing to allocate or free.
 */
#ifndef THUNDER_NATIVE_H
#define THUNDER_NATIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever the layout below changes; the VM refuses a module built against another. */
#define THUNDER_NATIVE_ABI 1

/* How a shared library exports a symbol on this platform. */
#if defined(_WIN32)
#define THUNDER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define THUNDER_EXPORT __attribute__((visibility("default")))
#else
#define THUNDER_EXPORT
#endif

/* The types that can cross the boundary. Arrays, maps, tuples and structs cannot: a native
 * function takes and returns single values, which keeps ownership questions out of the way. */
typedef enum ThunderKind
{
    THUNDER_VOID = 0,
    THUNDER_INT = 1,
    THUNDER_FLOAT = 2,
    THUNDER_BOOL = 3,
    THUNDER_STRING = 4,
    THUNDER_FUNCTION = 5 /* a Thunder function, to be called with thunder_call */
} ThunderKind;

/* One value. Only the field that matches 'kind' is meaningful.
 *
 * Strings are UTF-8 and are *not* owned by this struct:
 *   - an argument's text is valid until the native function returns;
 *   - a returned text is copied by the VM before the native function's memory can go away, so
 *     returning a string literal or a buffer that lives until the return is safe.
 */
typedef struct ThunderValue
{
    int32_t kind;         /* a ThunderKind */
    int32_t function;     /* THUNDER_FUNCTION: which function (do not interpret it) */
    int64_t integer;      /* THUNDER_INT, THUNDER_BOOL (0 or 1) */
    double  number;       /* THUNDER_FLOAT */
    const char* text;     /* THUNDER_STRING: UTF-8, not necessarily zero-terminated */
    int64_t text_length;  /* THUNDER_STRING: length in bytes */
} ThunderValue;

/* The running program, as a native function sees it. Opaque. */
typedef struct ThunderEnv ThunderEnv;

/* What the VM offers a native module. The pointer stays valid for the whole run. */
typedef struct ThunderApi
{
    uint32_t abi_version;

    /* Calls a Thunder function value with 'count' arguments and returns its result (a void value
     * for a function that returns nothing). If the Thunder code fails, the error is remembered:
     * check thunder_failed(env) and return promptly. */
    ThunderValue (*call)(ThunderEnv* env, ThunderValue function, const ThunderValue* args, int32_t count);

    /* Stops the Thunder program with a runtime error. It *returns*: a C function cannot be
     * unwound safely from here, so return from the native function straight afterwards. */
    void (*fail)(ThunderEnv* env, const char* message);

    /* Non-zero once fail() has been called, or a callback has failed. */
    int (*failed)(const ThunderEnv* env);
} ThunderApi;

/* A function a native module registers. 'count' is how many arguments the Thunder declaration
 * had, so it always matches; the VM has already checked the types. */
typedef ThunderValue (*ThunderFunction)(ThunderEnv* env, const ThunderValue* args, int32_t count);

/* Passed to thunder_module_init so the module can say what it provides. */
typedef struct ThunderRegistry
{
    uint32_t abi_version;
    void* opaque;
    /* Registers 'function' under 'name'; non-zero on success. */
    int (*add)(struct ThunderRegistry* registry, const char* name, ThunderFunction function);
} ThunderRegistry;

/* Every native module defines this. Return non-zero on success.
 *
 *     THUNDER_EXPORT int thunder_module_init(ThunderRegistry* reg, const ThunderApi* api)
 *     {
 *         thunder_api = api;
 *         return reg->add(reg, "add", add_impl);
 *     }
 */
THUNDER_EXPORT int thunder_module_init(ThunderRegistry* registry, const ThunderApi* api);
typedef int (*ThunderModuleInit)(ThunderRegistry* registry, const ThunderApi* api);

/* ---------------------------------------------------------------- building values
 * Plain inline helpers: there is nothing to allocate, so there is nothing to free. */

static inline ThunderValue thunder_void(void)
{
    ThunderValue v;
    v.kind = THUNDER_VOID;
    v.function = 0;
    v.integer = 0;
    v.number = 0;
    v.text = 0;
    v.text_length = 0;
    return v;
}

static inline ThunderValue thunder_int(int64_t value)
{
    ThunderValue v = thunder_void();
    v.kind = THUNDER_INT;
    v.integer = value;
    return v;
}

static inline ThunderValue thunder_float(double value)
{
    ThunderValue v = thunder_void();
    v.kind = THUNDER_FLOAT;
    v.number = value;
    return v;
}

static inline ThunderValue thunder_bool(int value)
{
    ThunderValue v = thunder_void();
    v.kind = THUNDER_BOOL;
    v.integer = value ? 1 : 0;
    return v;
}

/* 'text' must stay valid until the native function returns; the VM copies it. */
static inline ThunderValue thunder_text(const char* text, int64_t length)
{
    ThunderValue v = thunder_void();
    v.kind = THUNDER_STRING;
    v.text = text;
    v.text_length = length;
    return v;
}

#ifdef __cplusplus
}
#endif

#endif /* THUNDER_NATIVE_H */
