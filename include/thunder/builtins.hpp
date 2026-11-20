// Built-in functions shared by the semantic analyser, the code generator and the VM.
//
// The built-ins are organised in modules. The default module (printing, conversions, lists,
// maps, tuples and the higher-order functions) is used without a prefix: len(a), push(a, 1). Every
// other built-in is written with its module in front: math.sqrt(x), string.trim(s),
// random.int(1, 6), bytes.from_string(s). The modules that reach outside the program - os, file
// and graphics - must also be imported ("import file"). canonical_name is the qualified English
// name ("math.sqrt"); the language packs translate the module and the member separately.
// docs/library.md describes every built-in.
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
        ForEach,  // for_each(a: T[], f: func(T))   calls f with every element, in order
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
        // ---- appended later (bytecode stores the index, so new built-ins only ever go at the end)
        Format,        // format(template, values...): string   {} str, {x} hex, {b} binary
        Hex,           // hex(n): string                         42 -> "0x2a"
        Bin,           // bin(n): string                         10 -> "0b1010"
        // ---- bytes (raw data; see docs/library.md "Bytes")
        ToByte,        // to_byte(n: int): byte                 the low 8 bits (256 -> 0, -1 -> 255)
        StringToBytes, // string_to_bytes(s): byte[]            the UTF-8 encoding of the text
        BytesToString, // bytes_to_string(data: byte[]): string decodes UTF-8 (invalid sequences become U+FFFD)
        BytesToInt,    // bytes_to_int(data, start, count, little_endian, signed): int
        IntToBytes,    // int_to_bytes(value, count, little_endian): byte[]
        ReadBytes,     // read_bytes(path): byte[]              (module os) the whole file, byte for byte
        WriteBytes,    // write_bytes(path, data: byte[])       (module os)
        AppendBytes,   // append_bytes(path, data: byte[])      (module os)
        // ---- reflection
        NameOf,        // name_of(f): string   the declared (qualified) name of a function value
        // ---- the string module's own forms of general built-ins (len, contains, ...): the same
        // operation, for text only. The VM runs each as its general form (see general_builtin).
        StringLen,      // string.len(s): int
        StringContains, // string.contains(s, part): bool
        StringIndexOf,  // string.index_of(s, part): int
        StringReverse,  // string.reverse(s): string
        StringSlice,    // string.slice(s, start, end): string
        StringToInt,    // string.to_int(s): int
        StringToFloat,  // string.to_float(s): float
        // ---- module "task": the concurrency kernel (green threads run by the VM itself; see
        // docs/concurrency.md). Tasks, mutexes and conditions are named by small whole numbers
        // ("handles"), exactly as sockets are; import "concurrency" wraps them in objects.
        TaskSpawn,     // task.spawn(action: func()): int   starts a task, answers its number
        TaskJoin,      // task.join(task: int)              waits until that task has ended
        TaskDone,      // task.done(task: int): bool        true once it has ended
        TaskSleep,     // task.sleep(ms: int)               only this task waits
        TaskYield,     // task.yield()                      let the other tasks run now
        TaskCurrent,   // task.current(): int               the running task's number
        TaskName,      // task.name(task: int): string      "main", "task#2", ...
        TaskCount,     // task.count(): int                 how many tasks have not ended
        TaskMutex,     // task.mutex(): int                 a new lock, free
        TaskLock,      // task.lock(mutex: int)             takes it, waiting if another task holds it
        TaskTryLock,   // task.try_lock(mutex: int): bool   takes it if it is free; never waits
        TaskUnlock,    // task.unlock(mutex: int)           gives it back (the holder only)
        TaskCondition, // task.condition(): int             a new condition
        TaskWait,      // task.wait(condition: int, mutex: int)  releases, waits, takes it back
        TaskSignal,    // task.signal(condition: int)       wakes the task that has waited longest
        TaskBroadcast, // task.broadcast(condition: int)    wakes every waiting task
        // ---- stopping the program on purpose
        Fail, // fail(message: string)  stops the program with this message (runtime error R043)
    };

    inline constexpr int variadic = -1;

    // The built-in modules. None is the default module, whose functions need no prefix.
    enum class BuiltinModule : std::uint8_t
    {
        None, // the default module: print, len, push, map, ...
        Os,
        Graphics,
        Math,
        String,
        Random,
        Bytes,
        File,
        Task, // the concurrency kernel: spawn, join, sleep, mutex, condition (docs/concurrency.md)
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
    inline constexpr std::uint8_t builtin_count = static_cast<std::uint8_t>(BuiltinId::Fail) + 1;
    // The general built-in a specialised one stands for (string.len -> len); the built-in itself
    // when it is not a specialisation. The arguments are the same, so the VM runs the general one.
    BuiltinId general_builtin(BuiltinId id);
    // The member part of a built-in's canonical name: "sqrt" for "math.sqrt", "len" for "len".
    std::string_view builtin_member_name(const BuiltinInfo& info);

    // ---- built-in enums: named values that used to be strings ("red", "linux"). Each belongs to a
    // module like a built-in function does, and its name and members are translated by the
    // language packs. The order of the members is their position (to_int(Color.red) is 0).
    enum class BuiltinEnumId : std::uint8_t
    {
        Color,    // module graphics: the named colours a drawing function accepts
        Platform, // module os: what platform() answers
    };

    struct BuiltinEnumInfo
    {
        BuiltinEnumId id;
        std::string_view canonical_name;  // English name; the language pack may translate it
        BuiltinModule module;
        std::span<const std::string_view> members; // English member names, in position order
        std::string_view description;
    };

    const BuiltinEnumInfo& builtin_enum_info(BuiltinEnumId id);
    std::span<const BuiltinEnumInfo> all_builtin_enums();
    inline constexpr std::uint8_t builtin_enum_count = static_cast<std::uint8_t>(BuiltinEnumId::Platform) + 1;

    // The English name of a built-in module ("os"); languages may add translations.
    std::string_view module_canonical_name(BuiltinModule module);
    // True for the modules a program must import before using them (os, file, graphics).
    bool module_needs_import(BuiltinModule module);
    // Every built-in module except the default one, in the order the documentation lists them.
    std::span<const BuiltinModule> all_builtin_modules();
}
