// A complete Thunder native module, in C++.
//
// Build it (see samples/native/README.md), then run demo.th next to the library:
//
//     thunderc demo.th -o demo.thc && thundervm demo.thc
//
// Everything a module needs is in <thunder/native.h>: a value struct, three host functions
// and one entry point. There is nothing to allocate and nothing to free.

#include <thunder/native.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // The host's functions, kept from thunder_module_init.
    const ThunderApi* host = nullptr;

    // The text a function returns must stay alive until the VM has copied it, which happens as
    // soon as the function returns. One buffer per function is therefore enough.
    std::string returned;

    std::string text_of(ThunderValue value)
    {
        if (value.kind != THUNDER_STRING || value.text == nullptr || value.text_length <= 0)
        {
            return {};
        }
        return std::string(value.text, static_cast<std::size_t>(value.text_length));
    }

    // native func hypot(x: float, y: float): float
    ThunderValue demo_hypot(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        return thunder_float(std::hypot(args[0].number, args[1].number));
    }

    // native func shout(text: string): string
    ThunderValue demo_shout(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        returned = text_of(args[0]);
        for (char& c : returned)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        returned += "!";
        return thunder_text(returned.data(), static_cast<int64_t>(returned.size()));
    }

    // native func checksum(text: string): int
    ThunderValue demo_checksum(ThunderEnv*, const ThunderValue* args, int32_t)
    {
        int64_t sum = 0;
        const std::string text = text_of(args[0]);
        for (const unsigned char c : text)
        {
            sum = (sum * 31 + c) % 1000000007;
        }
        return thunder_int(sum);
    }

    // native func divide(a: int, b: int): int  - shows how a native function reports an error.
    ThunderValue demo_divide(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        if (args[1].integer == 0)
        {
            host->fail(env, "divide() cannot divide by zero");
            return thunder_void(); // fail() returns: leave straight away
        }
        return thunder_int(args[0].integer / args[1].integer);
    }

    // native func each_prime(limit: int, report: func(int))
    //
    // Calls back into Thunder once per prime below 'limit'. This is the other direction of the
    // interface: C++ driving Thunder code.
    ThunderValue demo_each_prime(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        const int64_t limit = args[0].integer;
        const ThunderValue report = args[1];
        std::vector<bool> composite(limit > 2 ? static_cast<std::size_t>(limit) : 2, false);
        for (int64_t n = 2; n < limit; ++n)
        {
            if (composite[static_cast<std::size_t>(n)])
            {
                continue;
            }
            for (int64_t multiple = n * n; multiple < limit; multiple += n)
            {
                composite[static_cast<std::size_t>(multiple)] = true;
            }
            const ThunderValue argument = thunder_int(n);
            host->call(env, report, &argument, 1);
            if (host->failed(env))
            {
                break; // the Thunder callback failed: stop and let the VM report it
            }
        }
        return thunder_void();
    }

    // native func apply_twice(value: int, f: func(int): int): int
    ThunderValue demo_apply_twice(ThunderEnv* env, const ThunderValue* args, int32_t)
    {
        ThunderValue argument = thunder_int(args[0].integer);
        for (int round = 0; round < 2 && !host->failed(env); ++round)
        {
            argument = host->call(env, args[1], &argument, 1);
        }
        return thunder_int(argument.integer);
    }
}

extern "C" THUNDER_EXPORT int thunder_module_init(ThunderRegistry* registry, const ThunderApi* api)
{
    if (registry == nullptr || api == nullptr || api->abi_version != THUNDER_NATIVE_ABI)
    {
        return 0; // a module built against another version of the interface
    }
    host = api;
    return registry->add(registry, "hypot", demo_hypot) && registry->add(registry, "shout", demo_shout)
        && registry->add(registry, "checksum", demo_checksum) && registry->add(registry, "divide", demo_divide)
        && registry->add(registry, "each_prime", demo_each_prime)
        && registry->add(registry, "apply_twice", demo_apply_twice);
}
