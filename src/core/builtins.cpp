#include "thunder/builtins.hpp"

#include <array>
#include <cstddef>

namespace thunder
{
    namespace
    {
        constexpr std::array<BuiltinInfo, builtin_count> builtin_table{{
            {BuiltinId::Print, "print", variadic, "print(value, ...)", "Writes the values to the output without a newline."},
            {BuiltinId::Println, "println", variadic, "println(value, ...)", "Writes the values to the output followed by a newline."},
            {BuiltinId::Len, "len", 1, "len(s: string | T[]): int", "Number of characters in a string or elements in an array."},
            {BuiltinId::Str, "str", 1, "str(value): string", "Converts any value to its text form."},
            {BuiltinId::ToInt, "to_int", 1, "to_int(s: string): int", "Parses a decimal integer; a runtime error if the text is not a number."},
            {BuiltinId::Array, "array", 2, "array(n: int, value: T): T[]", "Creates an array of n copies of value (arrays and structs are copied, not shared)."},
            {BuiltinId::Inspect, "inspect", 1, "inspect(value): string", "A detailed, multi-line description of a value; structs are shown field by field with their types."},
            {BuiltinId::Dump, "dump", 1, "dump(value)", "Prints inspect(value) followed by a newline."},
            {BuiltinId::Copy, "copy", 1, "copy(value: T): T", "An independent copy of a struct or array (nested values are copied too)."},
        }};
    }

    const BuiltinInfo& builtin_info(BuiltinId id)
    {
        return builtin_table.at(static_cast<std::size_t>(id));
    }

    std::span<const BuiltinInfo> all_builtins()
    {
        return builtin_table;
    }

    std::optional<BuiltinId> builtin_from_index(std::uint8_t index)
    {
        if (index >= builtin_count)
        {
            return std::nullopt;
        }
        return static_cast<BuiltinId>(index);
    }
}
