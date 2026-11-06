// Built-in functions shared by the semantic analyser, the code generator and the VM.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace thunder
{
    enum class BuiltinId : std::uint8_t
    {
        Print,   // print(values...)       writes the values with no separator
        Println, // println(values...)     like print, then a newline
        Len,     // len(string | T[]): int number of characters / elements
        Str,     // str(value): string     text form of any value
        ToInt,   // to_int(string): int     parses a decimal integer (runtime error if invalid)
        Array,   // array(n: int, v: T): T[]  new array of n copies of v
        Inspect, // inspect(value): string  detailed, multi-line description (struct fields with types)
        Dump,    // dump(value)            prints inspect(value) followed by a newline
        Copy,    // copy(value: T): T      an independent (deep) copy of a struct or array
        Has,     // has(m: {K: V}, key: K): bool   true if the map contains the key
        Remove,  // remove(m: {K: V}, key: K)      removes the key (nothing happens if it is absent)
        Keys,    // keys(m: {K: V}): K[]           the keys, in insertion order
        Values,  // values(m: {K: V}): V[]         the values, in insertion order
    };

    inline constexpr int variadic = -1;

    struct BuiltinInfo
    {
        BuiltinId id;
        std::string_view canonical_name; // English name; the language pack may translate it
        int arity;                       // number of arguments, or 'variadic'
        std::string_view signature;      // shown in documentation and the IDE
        std::string_view description;
    };

    const BuiltinInfo& builtin_info(BuiltinId id);
    std::span<const BuiltinInfo> all_builtins();
    std::optional<BuiltinId> builtin_from_index(std::uint8_t index);
    inline constexpr std::uint8_t builtin_count = 13;
}
