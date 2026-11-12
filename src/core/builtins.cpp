#include "thunder/builtins.hpp"

#include <array>
#include <cstddef>

namespace thunder
{
    namespace
    {
        using enum BuiltinId;
        constexpr BuiltinModule core = BuiltinModule::None;
        constexpr BuiltinModule os = BuiltinModule::Os;
        constexpr BuiltinModule gfx = BuiltinModule::Graphics;

        constexpr std::array<BuiltinInfo, builtin_count> builtin_table{{
            // ---- core
            {Print, "print", 0, variadic, core, "print(value, ...)", "Writes the values to the output without a newline."},
            {Println, "println", 0, variadic, core, "println(value, ...)", "Writes the values to the output followed by a newline."},
            {Len, "len", 1, 1, core, "len(x: string | T[] | tuple | map): int", "Number of characters in a string, or of elements in an array, tuple or map."},
            {Str, "str", 1, 1, core, "str(value): string", "Converts any value to its text form."},
            {ToInt, "to_int", 1, 1, core, "to_int(s: string): int", "Parses a decimal integer; a runtime error if the text is not a number."},
            {Array, "array", 2, 2, core, "array(n: int, value: T): T[]", "Creates an array of n copies of value (arrays and structs are copied, not shared)."},
            {Inspect, "inspect", 1, 1, core, "inspect(value): string", "A detailed, multi-line description of a value; structs are shown field by field with their types."},
            {Dump, "dump", 1, 1, core, "dump(value)", "Prints inspect(value) followed by a newline."},
            {Copy, "copy", 1, 1, core, "copy(value: T): T", "An independent copy of a struct, array, tuple or map (nested values are copied too)."},
            {Has, "has", 2, 2, core, "has(m: {K: V}, key: K): bool", "True if the map contains the key."},
            {Remove, "remove", 2, 2, core, "remove(m: {K: V}, key: K)", "Removes the key and its value from the map (nothing happens if the key is absent)."},
            {Keys, "keys", 1, 1, core, "keys(m: {K: V}): K[]", "The keys of a map as an array, in the order they were added."},
            {Values, "values", 1, 1, core, "values(m: {K: V}): V[]", "The values of a map as an array, in the order their keys were added."},
            // ---- input
            {Input, "input", 0, 1, core, "input(prompt: string = \"\"): string", "Writes the prompt (if any), then reads one line from the input and returns it without its line break; \"\" at the end of the input."},
            {EndOfInput, "end_of_input", 0, 0, core, "end_of_input(): bool", "True if the input has no more lines to read."},
            // ---- strings
            {Substring, "substring", 3, 3, core, "substring(s: string, start: int, end: int): string", "The characters from position start up to (not including) end."},
            {Trim, "trim", 1, 1, core, "trim(s: string): string", "The text without spaces, tabs and line breaks at its start and end."},
            {ToUpper, "to_upper", 1, 1, core, "to_upper(s: string): string", "The text with the letters a-z changed to A-Z."},
            {ToLower, "to_lower", 1, 1, core, "to_lower(s: string): string", "The text with the letters A-Z changed to a-z."},
            {Split, "split", 2, 2, core, "split(s: string, separator: string): string[]", "The pieces of the text between the separators (with separator \"\", the single characters)."},
            {Join, "join", 2, 2, core, "join(parts: string[], separator: string): string", "The parts joined into one text, with the separator between them."},
            {Replace, "replace", 3, 3, core, "replace(s: string, old: string, new: string): string", "The text with every occurrence of old replaced by new."},
            {StartsWith, "starts_with", 2, 2, core, "starts_with(s: string, prefix: string): bool", "True if the text begins with prefix."},
            {EndsWith, "ends_with", 2, 2, core, "ends_with(s: string, suffix: string): bool", "True if the text ends with suffix."},
            {Contains, "contains", 2, 2, core, "contains(x: string | T[], item): bool", "True if the text contains the part, or the array contains the element."},
            {IndexOf, "index_of", 2, 2, core, "index_of(x: string | T[], item): int", "The position of the first occurrence of the part or element, or -1 if there is none."},
            {Repeat, "repeat", 2, 2, core, "repeat(s: string, n: int): string", "The text repeated n times."},
            {Reverse, "reverse", 1, 1, core, "reverse(x: string | T[]): string | T[]", "A new text or array with the characters or elements in reverse order."},
            {CharCode, "char_code", 1, 1, core, "char_code(c: string): int", "The Unicode code point of a one-character string."},
            {FromCharCode, "from_char_code", 1, 1, core, "from_char_code(code: int): string", "The one-character string with the given Unicode code point."},
            {IsInt, "is_int", 1, 1, core, "is_int(s: string): bool", "True if to_int(s) would succeed."},
            // ---- tuples (pairs)
            {First, "first", 1, 1, core, "first(t: (A, B, ...)): A", "The first element of a tuple."},
            {Second, "second", 1, 1, core, "second(t: (A, B, ...)): B", "The second element of a tuple."},
            {Swap, "swap", 1, 1, core, "swap(p: (A, B)): (B, A)", "The pair with its two elements exchanged."},
            {Zip, "zip", 2, 2, core, "zip(a: A[], b: B[]): (A, B)[]", "Pairs the elements at the same positions (as many pairs as the shorter array has elements)."},
            {Enumerate, "enumerate", 1, 1, core, "enumerate(a: T[]): (int, T)[]", "Pairs every element with its position."},
            {Items, "items", 1, 1, core, "items(m: {K: V}): (K, V)[]", "The entries of a map as (key, value) pairs, in the order they were added."},
            // ---- lists (arrays) and maps
            {Push, "push", 2, 2, core, "push(a: T[], value: T)", "Adds the value to the end of the array."},
            {Pop, "pop", 1, 1, core, "pop(a: T[]): T", "Removes the last element of the array and returns it."},
            {Insert, "insert", 3, 3, core, "insert(a: T[], index: int, value: T)", "Inserts the value at the position (0 to len(a)); later elements move up."},
            {RemoveAt, "remove_at", 2, 2, core, "remove_at(a: T[], index: int): T", "Removes the element at the position and returns it; later elements move down."},
            {Slice, "slice", 3, 3, core, "slice(x: string | T[], start: int, end: int): string | T[]", "A new text or array with the characters or elements from start up to (not including) end."},
            {Concat, "concat", 2, 2, core, "concat(a: T[], b: T[]): T[]", "A new array with the elements of a followed by those of b."},
            {Sort, "sort", 1, 1, core, "sort(a: T[]): T[]", "A new array with the elements in ascending order (ints or strings)."},
            {Range, "range", 2, 2, core, "range(start: int, end: int): int[]", "The numbers from start up to (not including) end."},
            {Sum, "sum", 1, 1, core, "sum(a: int[]): int", "The sum of the numbers."},
            {Map, "map", 2, 2, core, "map(a: T[], f: func(T): U): U[]", "A new array with f applied to every element."},
            {Filter, "filter", 2, 2, core, "filter(a: T[], keep: func(T): bool): T[]", "A new array with the elements for which keep returns true."},
            {Reduce, "reduce", 3, 3, core, "reduce(a: T[], initial: A, f: func(A, T): A): A", "Combines the elements from left to right: f(f(initial, a[0]), a[1]) ..."},
            {Get, "get", 3, 3, core, "get(m: {K: V}, key: K, default: V): V", "The value stored for the key, or default if the map does not contain it."},
            // ---- module "os"
            {Args, "args", 0, 0, os, "args(): string[]", "The command-line arguments given to the program (thundervm program.thc a b c)."},
            {GetEnv, "get_env", 1, 1, os, "get_env(name: string): string", "The value of an environment variable, or \"\" if it is not set."},
            {Run, "run", 1, 1, os, "run(command: string): int", "Runs a command of the operating system's shell; its output becomes part of the program's output. Returns the exit status."},
            {RunOutput, "run_output", 1, 1, os, "run_output(command: string): (int, string)", "Runs a shell command and returns its exit status and its output."},
            {ReadFile, "read_file", 1, 1, os, "read_file(path: string): string", "The contents of a text file (a runtime error if it cannot be read)."},
            {WriteFile, "write_file", 2, 2, os, "write_file(path: string, text: string)", "Creates or overwrites a text file."},
            {AppendFile, "append_file", 2, 2, os, "append_file(path: string, text: string)", "Adds text to the end of a file (creating it if necessary)."},
            {FileExists, "file_exists", 1, 1, os, "file_exists(path: string): bool", "True if a file or directory exists at the path."},
            {RemoveFile, "remove_file", 1, 1, os, "remove_file(path: string)", "Deletes a file (nothing happens if it does not exist)."},
            {ListDir, "list_dir", 1, 1, os, "list_dir(path: string): string[]", "The names of the entries in a directory, sorted."},
            {CurrentDir, "current_dir", 0, 0, os, "current_dir(): string", "The program's working directory."},
            {Time, "time", 0, 0, os, "time(): int", "Seconds since 1 January 1970 (UTC)."},
            {ClockMs, "clock_ms", 0, 0, os, "clock_ms(): int", "Milliseconds since the program started (for measuring how long something takes)."},
            {Sleep, "sleep", 1, 1, os, "sleep(ms: int)", "Pauses the program for the given number of milliseconds."},
            {DateTime, "date_time", 0, 0, os, "date_time(): string", "The local date and time as \"YYYY-MM-DD HH:MM:SS\"."},
            {Platform, "platform", 0, 0, os, "platform(): string", "The operating system: \"linux\", \"windows\", \"macos\" or \"unknown\"."},
            {Exit, "exit", 1, 1, os, "exit(code: int)", "Ends the program immediately with the given exit status."},
            // ---- random numbers
            {RandomInt, "random_int", 2, 2, core, "random_int(low: int, high: int): int", "A random whole number from low to high (both included)."},
            {SeedRandom, "seed_random", 1, 1, core, "seed_random(seed: int)", "Restarts the random numbers from a seed, so a program can repeat the same \"random\" sequence."},
            // ---- numbers and maths
            {ToFloat, "to_float", 1, 1, core, "to_float(x: string | int): float", "Parses a decimal number such as \"2.5\", or turns a whole number into a decimal; a runtime error if the text is not a number."},
            {IsNumber, "is_number", 1, 1, core, "is_number(s: string): bool", "True if to_float(s) would succeed."},
            {Abs, "abs", 1, 1, core, "abs(x: int | float): int | float", "The distance of the number from zero: abs(-3) is 3."},
            {Min, "min", 2, 2, core, "min(a, b)", "The smaller of two numbers (or the earlier of two texts)."},
            {Max, "max", 2, 2, core, "max(a, b)", "The larger of two numbers (or the later of two texts)."},
            {Round, "round", 1, 1, core, "round(x: float): int", "The nearest whole number; a half is rounded away from zero."},
            {Floor, "floor", 1, 1, core, "floor(x: float): int", "The largest whole number that is not greater than x."},
            {Ceil, "ceil", 1, 1, core, "ceil(x: float): int", "The smallest whole number that is not less than x."},
            {Sqrt, "sqrt", 1, 1, core, "sqrt(x: float): float", "The square root of x (a runtime error if x is negative)."},
            {Pow, "pow", 2, 2, core, "pow(x: float, y: float): float", "x raised to the power y."},
            {Exp, "exp", 1, 1, core, "exp(x: float): float", "e raised to the power x."},
            {Log, "log", 1, 1, core, "log(x: float): float", "The natural logarithm of x (a runtime error if x is not positive)."},
            {Sin, "sin", 1, 1, core, "sin(x: float): float", "The sine of an angle given in radians."},
            {Cos, "cos", 1, 1, core, "cos(x: float): float", "The cosine of an angle given in radians."},
            {Tan, "tan", 1, 1, core, "tan(x: float): float", "The tangent of an angle given in radians."},
            {Pi, "pi", 0, 0, core, "pi(): float", "The number pi, 3.141592653589793."},
            {Random, "random", 0, 0, core, "random(): float", "A random decimal from 0 (included) to 1 (excluded)."},
            // ---- module "graphics"
            {Canvas, "canvas", 2, 2, gfx, "canvas(width: int, height: int)", "Sets the size of the drawing area in pixels and clears it to white (the default is 400 x 300)."},
            {Clear, "clear", 1, 1, gfx, "clear(color: string)", "Fills the whole canvas with a color, removing everything drawn before."},
            {Pixel, "pixel", 3, 3, gfx, "pixel(x: float, y: float, color: string)", "Colors one pixel."},
            {Line, "line", 5, 5, gfx, "line(x1: float, y1: float, x2: float, y2: float, color: string)", "Draws a line from (x1, y1) to (x2, y2)."},
            {Rect, "rect", 5, 5, gfx, "rect(x: float, y: float, width: float, height: float, color: string)", "Draws a filled rectangle whose top left corner is (x, y)."},
            {RectOutline, "rect_outline", 5, 5, gfx, "rect_outline(x: float, y: float, width: float, height: float, color: string)", "Draws the outline of a rectangle."},
            {Circle, "circle", 4, 4, gfx, "circle(x: float, y: float, radius: float, color: string)", "Draws a filled circle around the centre (x, y)."},
            {CircleOutline, "circle_outline", 4, 4, gfx, "circle_outline(x: float, y: float, radius: float, color: string)", "Draws the outline of a circle."},
            {Text, "text", 4, 4, gfx, "text(x: float, y: float, message: string, color: string)", "Writes text whose top left corner is at (x, y)."},
            {TextSize, "text_size", 1, 1, gfx, "text_size(size: float)", "The height of the text that text() writes from now on, in pixels (default 16)."},
            {LineWidth, "line_width", 1, 1, gfx, "line_width(width: float)", "The width of lines and outlines drawn from now on, in pixels (default 1)."},
            {Rgb, "rgb", 3, 3, gfx, "rgb(red: int, green: int, blue: int): string", "The color with these amounts of red, green and blue (each 0 to 255), e.g. \"#ff8800\"."},
            {Frame, "frame", 0, 0, gfx, "frame()", "Shows the picture, then waits for the next frame of the animation (see frame_rate)."},
            {Show, "show", 0, 0, gfx, "show()", "Shows the picture now, without waiting."},
            {FrameRate, "frame_rate", 1, 1, gfx, "frame_rate(fps: int)", "How many frames per second frame() shows (1 to 120, default 30)."},
            {KeyPressed, "key_pressed", 0, 0, gfx, "key_pressed(): string", "The next key the user pressed (\"left\", \"space\", \"a\", ...), or \"\" if there is none."},
            {KeyDown, "key_down", 1, 1, gfx, "key_down(key: string): bool", "True while the key is held down."},
            {MouseX, "mouse_x", 0, 0, gfx, "mouse_x(): int", "The x position of the mouse on the canvas."},
            {MouseY, "mouse_y", 0, 0, gfx, "mouse_y(): int", "The y position of the mouse on the canvas."},
            {MouseDown, "mouse_down", 0, 0, gfx, "mouse_down(): bool", "True while the mouse button is held down over the canvas."},
            {Format, "format", 1, variadic, core, "format(template: string, values...): string",
                "Replaces each {} of the template with the next value; {x} writes a whole number in hexadecimal, {b} in binary. {{ and }} are literal braces."},
            {Hex, "hex", 1, 1, core, "hex(n: int): string", "A whole number written in hexadecimal: hex(42) is \"0x2a\" (to_int reads it back)."},
            {Bin, "bin", 1, 1, core, "bin(n: int): string", "A whole number written in binary: bin(10) is \"0b1010\" (to_int reads it back)."},
        }};

        constexpr bool table_in_order()
        {
            for (std::size_t i = 0; i < builtin_table.size(); ++i)
            {
                if (static_cast<std::size_t>(builtin_table[i].id) != i)
                {
                    return false;
                }
            }
            return true;
        }
        static_assert(table_in_order(), "builtin_table must list the built-ins in BuiltinId order");
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

    std::string_view module_canonical_name(BuiltinModule module)
    {
        switch (module)
        {
        case BuiltinModule::Os:
            return "os";
        case BuiltinModule::Graphics:
            return "graphics";
        case BuiltinModule::None:
            break;
        }
        return "";
    }
}
