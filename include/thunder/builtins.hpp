// Built-in functions shared by the semantic analyser, the code generator and the VM.
//
// Most built-ins are always available (the "prelude"). The operating-system functions belong to
// the built-in module "os" and can only be used by a program that says "import os"; until then
// their names are ordinary identifiers. docs/library.md describes every built-in.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace thunder
{
    enum class BuiltinId : std::uint8_t
    {
        // ---- core
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
        // ---- input
        Input,      // input(prompt?): string   reads one line from the standard input
        EndOfInput, // end_of_input(): bool     true when there is nothing more to read
        // ---- strings
        Substring,    // substring(s, start, end): string
        Trim,         // trim(s): string
        ToUpper,      // to_upper(s): string
        ToLower,      // to_lower(s): string
        Split,        // split(s, separator): string[]
        Join,         // join(parts: string[], separator): string
        Replace,      // replace(s, old, new): string
        StartsWith,   // starts_with(s, prefix): bool
        EndsWith,     // ends_with(s, suffix): bool
        Contains,     // contains(string | T[], part | element): bool
        IndexOf,      // index_of(string | T[], part | element): int   (-1 if absent)
        Repeat,       // repeat(s, n): string
        Reverse,      // reverse(string | T[]): same type
        CharCode,     // char_code(c: string): int
        FromCharCode, // from_char_code(n: int): string
        IsInt,        // is_int(s): bool
        // ---- tuples (pairs)
        First,     // first(t): the first element of a tuple
        Second,    // second(t): the second element of a tuple
        Swap,      // swap(p: (A, B)): (B, A)
        Zip,       // zip(a: A[], b: B[]): (A, B)[]
        Enumerate, // enumerate(a: T[]): (int, T)[]
        Items,     // items(m: {K: V}): (K, V)[]
        // ---- lists (arrays) and maps
        Push,     // push(a: T[], v: T)
        Pop,      // pop(a: T[]): T
        Insert,   // insert(a: T[], i: int, v: T)
        RemoveAt, // remove_at(a: T[], i: int): T
        Slice,    // slice(string | T[], start, end): same type
        Concat,   // concat(a: T[], b: T[]): T[]
        Sort,     // sort(a: T[]): T[]   (int or string elements)
        Range,    // range(start, end): int[]
        Sum,      // sum(a: int[]): int
        Map,      // map(a: T[], f: func(T): U): U[]
        Filter,   // filter(a: T[], keep: func(T): bool): T[]
        Reduce,   // reduce(a: T[], initial: A, f: func(A, T): A): A
        Get,      // get(m: {K: V}, key: K, default: V): V
        // ---- module "os"
        Args,       // args(): string[]
        GetEnv,     // get_env(name): string
        Run,        // run(command): int
        RunOutput,  // run_output(command): (int, string)
        ReadFile,   // read_file(path): string
        WriteFile,  // write_file(path, text)
        AppendFile, // append_file(path, text)
        FileExists, // file_exists(path): bool
        RemoveFile, // remove_file(path)
        ListDir,    // list_dir(path): string[]
        CurrentDir, // current_dir(): string
        Time,       // time(): int        seconds since 1970-01-01 (UTC)
        ClockMs,    // clock_ms(): int    milliseconds since the program started
        Sleep,      // sleep(ms: int)
        DateTime,   // date_time(): string   local date and time, "YYYY-MM-DD HH:MM:SS"
        Platform,   // platform(): string  "linux", "windows", "macos" or "unknown"
        Exit,       // exit(code: int)
        // ---- random numbers (always available)
        RandomInt,  // random_int(low, high): int   (both included)
        SeedRandom, // seed_random(seed: int)
        // ---- numbers and maths (always available)
        ToFloat,  // to_float(string | int): float   parses or widens (runtime error if invalid)
        IsNumber, // is_number(s): bool              true if to_float(s) would succeed
        Abs,      // abs(x: int): int / abs(x: float): float
        Min,      // min(a, b)   the smaller of two numbers (or two strings)
        Max,      // max(a, b)   the larger of two numbers (or two strings)
        Round,    // round(x: float): int   to the nearest whole number (halves away from zero)
        Floor,    // floor(x: float): int   the largest whole number <= x
        Ceil,     // ceil(x: float): int    the smallest whole number >= x
        Sqrt,     // sqrt(x: float): float
        Pow,      // pow(x: float, y: float): float
        Exp,      // exp(x: float): float
        Log,      // log(x: float): float   natural logarithm
        Sin,      // sin(x: float): float   (radians)
        Cos,      // cos(x: float): float
        Tan,      // tan(x: float): float
        Pi,       // pi(): float
        Random,   // random(): float   a random decimal from 0 (included) to 1 (excluded)
        // ---- module "graphics" (see graphics.hpp and docs/graphics.md)
        Canvas,        // canvas(width, height)
        Clear,         // clear(color)
        Pixel,         // pixel(x, y, color)
        Line,          // line(x1, y1, x2, y2, color)
        Rect,          // rect(x, y, width, height, color)
        RectOutline,   // rect_outline(x, y, width, height, color)
        Circle,        // circle(x, y, radius, color)
        CircleOutline, // circle_outline(x, y, radius, color)
        Text,          // text(x, y, message, color)
        TextSize,      // text_size(size)
        LineWidth,     // line_width(width)
        Rgb,           // rgb(red, green, blue): string
        Frame,         // frame()      show the picture, then wait for the next frame
        Show,          // show()       show the picture now
        FrameRate,     // frame_rate(fps)
        KeyPressed,    // key_pressed(): string
        KeyDown,       // key_down(key): bool
        MouseX,        // mouse_x(): int
        MouseY,        // mouse_y(): int
        MouseDown,     // mouse_down(): bool
    };

    inline constexpr int variadic = -1;

    // The built-in modules a program can import by name (import os).
    enum class BuiltinModule : std::uint8_t
    {
        None, // always available
        Os,
        Graphics,
    };

    struct BuiltinInfo
    {
        BuiltinId id;
        std::string_view canonical_name; // English name; the language pack may translate it
        int min_arity;                   // fewest arguments
        int arity;                       // most arguments, or 'variadic'
        BuiltinModule module;            // the module that must be imported to use it
        std::string_view signature;      // shown in documentation and the IDE
        std::string_view description;
    };

    const BuiltinInfo& builtin_info(BuiltinId id);
    std::span<const BuiltinInfo> all_builtins();
    std::optional<BuiltinId> builtin_from_index(std::uint8_t index);
    inline constexpr std::uint8_t builtin_count = static_cast<std::uint8_t>(BuiltinId::MouseDown) + 1;

    // The English name of a built-in module ("os"); languages may add translations.
    std::string_view module_canonical_name(BuiltinModule module);
}
