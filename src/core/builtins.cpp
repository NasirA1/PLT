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
        constexpr BuiltinModule math = BuiltinModule::Math;
        constexpr BuiltinModule str_ = BuiltinModule::String;
        constexpr BuiltinModule rnd = BuiltinModule::Random;
        constexpr BuiltinModule bytes = BuiltinModule::Bytes;
        constexpr BuiltinModule file = BuiltinModule::File;
        constexpr BuiltinModule task_ = BuiltinModule::Task;

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
            {Substring, "string.substring", 3, 3, str_, "string.substring(s: string, start: int, end: int): string", "The characters from position start up to (not including) end."},
            {Trim, "string.trim", 1, 1, str_, "string.trim(s: string): string", "The text without spaces, tabs and line breaks at its start and end."},
            {ToUpper, "string.to_upper", 1, 1, str_, "string.to_upper(s: string): string", "The text with the letters a-z changed to A-Z."},
            {ToLower, "string.to_lower", 1, 1, str_, "string.to_lower(s: string): string", "The text with the letters A-Z changed to a-z."},
            {Split, "string.split", 2, 2, str_, "string.split(s: string, separator: string): string[]", "The pieces of the text between the separators (with separator \"\", the single characters)."},
            {Join, "string.join", 2, 2, str_, "string.join(parts: string[], separator: string): string", "The parts joined into one text, with the separator between them."},
            {Replace, "string.replace", 3, 3, str_, "string.replace(s: string, old: string, new: string): string", "The text with every occurrence of old replaced by new."},
            {StartsWith, "string.starts_with", 2, 2, str_, "string.starts_with(s: string, prefix: string): bool", "True if the text begins with prefix."},
            {EndsWith, "string.ends_with", 2, 2, str_, "string.ends_with(s: string, suffix: string): bool", "True if the text ends with suffix."},
            {Contains, "contains", 2, 2, core, "contains(x: string | T[], item): bool", "True if the text contains the part, or the array contains the element."},
            {IndexOf, "index_of", 2, 2, core, "index_of(x: string | T[], item): int", "The position of the first occurrence of the part or element, or -1 if there is none."},
            {Repeat, "string.repeat", 2, 2, str_, "string.repeat(s: string, n: int): string", "The text repeated n times."},
            {Reverse, "reverse", 1, 1, core, "reverse(x: string | T[]): string | T[]", "A new text or array with the characters or elements in reverse order."},
            {CharCode, "string.char_code", 1, 1, str_, "string.char_code(c: string): int", "The Unicode code point of a one-character string."},
            {FromCharCode, "string.from_char_code", 1, 1, str_, "string.from_char_code(code: int): string", "The one-character string with the given Unicode code point."},
            {IsInt, "string.is_int", 1, 1, str_, "string.is_int(s: string): bool", "True if to_int(s) would succeed."},
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
            {ForEach, "for_each", 2, 2, core, "for_each(a: T[], f: func(T))", "Calls f with every element of the array, in order."},
            {Get, "get", 3, 3, core, "get(m: {K: V}, key: K, default: V): V", "The value stored for the key, or default if the map does not contain it."},
            // ---- module "os"
            {Args, "os.args", 0, 0, os, "os.args(): string[]", "The command-line arguments given to the program (thundervm program.thc a b c)."},
            {GetEnv, "os.get_env", 1, 1, os, "os.get_env(name: string): string", "The value of an environment variable, or \"\" if it is not set."},
            {Run, "os.run", 1, 1, os, "os.run(command: string): int", "Runs a command of the operating system's shell; its output becomes part of the program's output. Returns the exit status."},
            {RunOutput, "os.run_output", 1, 1, os, "os.run_output(command: string): (int, string)", "Runs a shell command and returns its exit status and its output."},
            {ReadFile, "file.read", 1, 1, file, "file.read(path: string): string", "The contents of a text file (a runtime error if it cannot be read)."},
            {WriteFile, "file.write", 2, 2, file, "file.write(path: string, text: string)", "Creates or overwrites a text file."},
            {AppendFile, "file.append", 2, 2, file, "file.append(path: string, text: string)", "Adds text to the end of a file (creating it if necessary)."},
            {FileExists, "file.exists", 1, 1, file, "file.exists(path: string): bool", "True if a file or directory exists at the path."},
            {RemoveFile, "file.remove", 1, 1, file, "file.remove(path: string)", "Deletes a file (nothing happens if it does not exist)."},
            {ListDir, "os.list_dir", 1, 1, os, "os.list_dir(path: string): string[]", "The names of the entries in a directory, sorted."},
            {CurrentDir, "os.current_dir", 0, 0, os, "os.current_dir(): string", "The program's working directory."},
            {Time, "os.time", 0, 0, os, "os.time(): int", "Seconds since 1 January 1970 (UTC)."},
            {ClockMs, "os.clock_ms", 0, 0, os, "os.clock_ms(): int", "Milliseconds since the program started (for measuring how long something takes)."},
            {Sleep, "os.sleep", 1, 1, os, "os.sleep(ms: int)", "Pauses the program for the given number of milliseconds."},
            {DateTime, "os.date_time", 0, 0, os, "os.date_time(): string", "The local date and time as \"YYYY-MM-DD HH:MM:SS\"."},
            {Platform, "os.platform", 0, 0, os, "os.platform(): Platform", "The operating system as a member of the Platform enum: os.Platform.linux, os.Platform.windows, os.Platform.macos or os.Platform.unknown."},
            {Exit, "os.exit", 1, 1, os, "os.exit(code: int)", "Ends the program immediately with the given exit status."},
            // ---- random numbers
            {RandomInt, "random.int", 2, 2, rnd, "random.int(low: int, high: int): int", "A random whole number from low to high (both included)."},
            {SeedRandom, "random.seed", 1, 1, rnd, "random.seed(seed: int)", "Restarts the random numbers from a seed, so a program can repeat the same \"random\" sequence."},
            // ---- numbers and maths
            {ToFloat, "to_float", 1, 1, core, "to_float(x: string | int): float", "Parses a decimal number such as \"2.5\", or turns a whole number into a decimal; a runtime error if the text is not a number."},
            {IsNumber, "string.is_number", 1, 1, str_, "string.is_number(s: string): bool", "True if to_float(s) would succeed."},
            {Abs, "math.abs", 1, 1, math, "math.abs(x: int | float): int | float", "The distance of the number from zero: math.abs(-3) is 3."},
            {Min, "math.min", 2, 2, math, "math.min(a, b)", "The smaller of two numbers (or the earlier of two texts)."},
            {Max, "math.max", 2, 2, math, "math.max(a, b)", "The larger of two numbers (or the later of two texts)."},
            {Round, "math.round", 1, 1, math, "math.round(x: float): int", "The nearest whole number; a half is rounded away from zero."},
            {Floor, "math.floor", 1, 1, math, "math.floor(x: float): int", "The largest whole number that is not greater than x."},
            {Ceil, "math.ceil", 1, 1, math, "math.ceil(x: float): int", "The smallest whole number that is not less than x."},
            {Sqrt, "math.sqrt", 1, 1, math, "math.sqrt(x: float): float", "The square root of x (a runtime error if x is negative)."},
            {Pow, "math.pow", 2, 2, math, "math.pow(x: float, y: float): float", "x raised to the power y."},
            {Exp, "math.exp", 1, 1, math, "math.exp(x: float): float", "e raised to the power x."},
            {Log, "math.log", 1, 1, math, "math.log(x: float): float", "The natural logarithm of x (a runtime error if x is not positive)."},
            {Sin, "math.sin", 1, 1, math, "math.sin(x: float): float", "The sine of an angle given in radians."},
            {Cos, "math.cos", 1, 1, math, "math.cos(x: float): float", "The cosine of an angle given in radians."},
            {Tan, "math.tan", 1, 1, math, "math.tan(x: float): float", "The tangent of an angle given in radians."},
            {Pi, "math.pi", 0, 0, math, "math.pi(): float", "The number pi, 3.141592653589793."},
            {Random, "random.float", 0, 0, rnd, "random.float(): float", "A random decimal from 0 (included) to 1 (excluded)."},
            // ---- module "graphics"
            {Canvas, "graphics.canvas", 2, 2, gfx, "graphics.canvas(width: int, height: int)", "Sets the size of the drawing area in pixels and clears it to white (the default is 400 x 300)."},
            {Clear, "graphics.clear", 1, 1, gfx, "graphics.clear(color: string)", "Fills the whole canvas with a color, removing everything drawn before."},
            {Pixel, "graphics.pixel", 3, 3, gfx, "graphics.pixel(x: float, y: float, color: string)", "Colors one pixel."},
            {Line, "graphics.line", 5, 5, gfx, "graphics.line(x1: float, y1: float, x2: float, y2: float, color: string)", "Draws a line from (x1, y1) to (x2, y2)."},
            {Rect, "graphics.rect", 5, 5, gfx, "graphics.rect(x: float, y: float, width: float, height: float, color: string)", "Draws a filled rectangle whose top left corner is (x, y)."},
            {RectOutline, "graphics.rect_outline", 5, 5, gfx, "graphics.rect_outline(x: float, y: float, width: float, height: float, color: string)", "Draws the outline of a rectangle."},
            {Circle, "graphics.circle", 4, 4, gfx, "graphics.circle(x: float, y: float, radius: float, color: string)", "Draws a filled circle around the centre (x, y)."},
            {CircleOutline, "graphics.circle_outline", 4, 4, gfx, "graphics.circle_outline(x: float, y: float, radius: float, color: string)", "Draws the outline of a circle."},
            {Text, "graphics.text", 4, 4, gfx, "graphics.text(x: float, y: float, message: string, color: string)", "Writes text whose top left corner is at (x, y)."},
            {TextSize, "graphics.text_size", 1, 1, gfx, "graphics.text_size(size: float)", "The height of the text that graphics.text() writes from now on, in pixels (default 16)."},
            {LineWidth, "graphics.line_width", 1, 1, gfx, "graphics.line_width(width: float)", "The width of lines and outlines drawn from now on, in pixels (default 1)."},
            {Rgb, "graphics.rgb", 3, 3, gfx, "graphics.rgb(red: int, green: int, blue: int): string", "The color with these amounts of red, green and blue (each 0 to 255), e.g. \"#ff8800\"."},
            {Frame, "graphics.frame", 0, 0, gfx, "graphics.frame()", "Shows the picture, then waits for the next frame of the animation (see graphics.frame_rate)."},
            {Show, "graphics.show", 0, 0, gfx, "graphics.show()", "Shows the picture now, without waiting."},
            {FrameRate, "graphics.frame_rate", 1, 1, gfx, "graphics.frame_rate(fps: int)", "How many frames per second graphics.frame() shows (1 to 120, default 30)."},
            {KeyPressed, "graphics.key_pressed", 0, 0, gfx, "graphics.key_pressed(): string", "The next key the user pressed (\"left\", \"space\", \"a\", ...), or \"\" if there is none."},
            {KeyDown, "graphics.key_down", 1, 1, gfx, "graphics.key_down(key: string): bool", "True while the key is held down."},
            {MouseX, "graphics.mouse_x", 0, 0, gfx, "graphics.mouse_x(): int", "The x position of the mouse on the canvas."},
            {MouseY, "graphics.mouse_y", 0, 0, gfx, "graphics.mouse_y(): int", "The y position of the mouse on the canvas."},
            {MouseDown, "graphics.mouse_down", 0, 0, gfx, "graphics.mouse_down(): bool", "True while the mouse button is held down over the canvas."},
            {Format, "string.format", 1, variadic, str_, "string.format(template: string, values...): string",
                "Replaces each {} of the template with the next value; {x} writes a whole number in hexadecimal, {b} in binary. {{ and }} are literal braces."},
            {Hex, "hex", 1, 1, core, "hex(n: int): string", "A whole number written in hexadecimal: hex(42) is \"0x2a\" (to_int reads it back)."},
            {Bin, "bin", 1, 1, core, "bin(n: int): string", "A whole number written in binary: bin(10) is \"0b1010\" (to_int reads it back)."},
            // ---- bytes
            {ToByte, "to_byte", 1, 1, core, "to_byte(n: int): byte", "The low 8 bits of a whole number as a byte: to_byte(255) is 255, to_byte(256) is 0 and to_byte(-1) is 255."},
            {StringToBytes, "bytes.from_string", 1, 1, bytes, "bytes.from_string(s: string): byte[]", "The bytes of the text in UTF-8 (one byte per English letter, two or more per Pashto letter)."},
            {BytesToString, "bytes.to_string", 1, 1, bytes, "bytes.to_string(data: byte[]): string", "The text the bytes spell in UTF-8; a sequence that is not valid UTF-8 becomes the replacement character U+FFFD."},
            {BytesToInt, "bytes.to_int", 5, 5, bytes, "bytes.to_int(data: byte[], start: int, count: int, little_endian: bool, signed: bool): int", "The whole number stored in count bytes (1 to 8) from position start: little-endian (lowest byte first, as in WAV and BMP files) or big-endian (as on the network), signed (two's complement) or not."},
            {IntToBytes, "bytes.from_int", 3, 3, bytes, "bytes.from_int(value: int, count: int, little_endian: bool): byte[]", "The low count bytes (1 to 8) of a whole number, lowest byte first when little_endian is true; negative numbers are written in two's complement."},
            {ReadBytes, "file.read_bytes", 1, 1, file, "file.read_bytes(path: string): byte[]", "The whole contents of a file, byte for byte (nothing is converted; a runtime error if it cannot be read)."},
            {WriteBytes, "file.write_bytes", 2, 2, file, "file.write_bytes(path: string, data: byte[])", "Creates the file, or replaces its contents, with exactly these bytes."},
            {AppendBytes, "file.append_bytes", 2, 2, file, "file.append_bytes(path: string, data: byte[])", "Adds the bytes to the end of the file (creating it if necessary)."},
            // ---- reflection
            {NameOf, "name_of", 1, 1, core, "name_of(f: func): string",
                "The name a function was declared with, its module in front: name_of(geometry.area) is \"geometry.area\"."},
            // ---- the string module's forms of the general sequence functions and conversions
            {StringLen, "string.len", 1, 1, str_, "string.len(s: string): int", "The number of characters in the text (len for text only)."},
            {StringContains, "string.contains", 2, 2, str_, "string.contains(s: string, part: string): bool", "True if the text contains the part (contains for text only)."},
            {StringIndexOf, "string.index_of", 2, 2, str_, "string.index_of(s: string, part: string): int", "The position of the first occurrence of the part in the text, or -1 if there is none (index_of for text only)."},
            {StringReverse, "string.reverse", 1, 1, str_, "string.reverse(s: string): string", "The text with its characters in reverse order (reverse for text only)."},
            {StringSlice, "string.slice", 3, 3, str_, "string.slice(s: string, start: int, end: int): string", "The characters from position start up to (not including) end (slice for text only)."},
            {StringToInt, "string.to_int", 1, 1, str_, "string.to_int(s: string): int", "Parses a decimal integer; a runtime error if the text is not a number (to_int for text only)."},
            {StringToFloat, "string.to_float", 1, 1, str_, "string.to_float(s: string): float", "Parses a decimal number such as \"2.5\"; a runtime error if the text is not a number (to_float for text only)."},
            // ---- module "task": the concurrency kernel (docs/concurrency.md)
            {TaskSpawn, "task.spawn", 1, 1, task_, "task.spawn(action: func()): int",
                "Starts the action as a new task that runs beside this one, and answers the new task's number."},
            {TaskJoin, "task.join", 1, 1, task_, "task.join(task: int)",
                "Waits until that task has ended; the other tasks go on running meanwhile."},
            {TaskDone, "task.done", 1, 1, task_, "task.done(task: int): bool", "True once that task has ended."},
            {TaskSleep, "task.sleep", 1, 1, task_, "task.sleep(milliseconds: int)",
                "Lets this task wait for the given time; the other tasks run meanwhile (os.sleep does the same)."},
            {TaskYield, "task.yield", 0, 0, task_, "task.yield()",
                "Gives the other ready tasks a turn now, without waiting for the end of this task's time slice."},
            {TaskCurrent, "task.current", 0, 0, task_, "task.current(): int", "The number of the task that is running."},
            {TaskName, "task.name", 1, 1, task_, "task.name(task: int): string",
                "The name of a task: \"main\" for the one the program starts in, \"task#2\", \"task#3\", ... for the others."},
            {TaskCount, "task.count", 0, 0, task_, "task.count(): int", "How many tasks have not ended yet (the running one included)."},
            {TaskMutex, "task.mutex", 0, 0, task_, "task.mutex(): int",
                "Makes a new lock (a mutex), free, and answers its number."},
            {TaskLock, "task.lock", 1, 1, task_, "task.lock(mutex: int)",
                "Takes the lock, waiting while another task holds it; taking a lock this task already holds is an error."},
            {TaskTryLock, "task.try_lock", 1, 1, task_, "task.try_lock(mutex: int): bool",
                "Takes the lock if it is free and answers true; answers false at once instead of waiting."},
            {TaskUnlock, "task.unlock", 1, 1, task_, "task.unlock(mutex: int)",
                "Gives the lock back; only the task that holds it may do so. The task that has waited longest gets it."},
            {TaskCondition, "task.condition", 0, 0, task_, "task.condition(): int",
                "Makes a new condition - something tasks wait for inside a lock - and answers its number."},
            {TaskWait, "task.wait", 2, 2, task_, "task.wait(condition: int, mutex: int)",
                "Releases the lock, waits until the condition is signalled, then takes the lock back. The lock must be held."},
            {TaskSignal, "task.signal", 1, 1, task_, "task.signal(condition: int)",
                "Wakes the task that has waited longest on the condition (none: nothing happens)."},
            {TaskBroadcast, "task.broadcast", 1, 1, task_, "task.broadcast(condition: int)",
                "Wakes every task waiting on the condition."},
            // ---- stopping the program on purpose
            {Fail, "fail", 1, 1, core, "fail(message: string)",
                "Stops the program with a message of your own (runtime error R043), the way a wrong built-in call stops it. A library uses it to say what the program did wrong."},
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

    namespace
    {
        constexpr std::array<std::string_view, 13> color_members{"red", "green", "blue", "white", "black", "yellow", "orange",
            "pink", "gray", "brown", "purple", "sky", "gold"};
        constexpr std::array<std::string_view, 4> platform_members{"linux", "windows", "macos", "unknown"};

        constexpr std::array<BuiltinEnumInfo, builtin_enum_count> builtin_enum_table{{
            {BuiltinEnumId::Color, "Color", gfx, color_members,
                "The named colours: every drawing function takes a graphics.Color as well as a string such as \"#ff8800\" or graphics.rgb(...)."},
            {BuiltinEnumId::Platform, "Platform", os, platform_members, "The operating systems os.platform() can answer with."},
        }};
    }

    const BuiltinEnumInfo& builtin_enum_info(BuiltinEnumId id)
    {
        return builtin_enum_table.at(static_cast<std::size_t>(id));
    }

    std::span<const BuiltinEnumInfo> all_builtin_enums()
    {
        return builtin_enum_table;
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

    BuiltinId general_builtin(BuiltinId id)
    {
        switch (id)
        {
        case StringLen:
            return Len;
        case StringContains:
            return Contains;
        case StringIndexOf:
            return IndexOf;
        case StringReverse:
            return Reverse;
        case StringSlice:
            return Slice;
        case StringToInt:
            return ToInt;
        case StringToFloat:
            return ToFloat;
        default:
            return id;
        }
    }

    std::string_view module_canonical_name(BuiltinModule module)
    {
        switch (module)
        {
        case BuiltinModule::Os:
            return "os";
        case BuiltinModule::Graphics:
            return "graphics";
        case BuiltinModule::Math:
            return "math";
        case BuiltinModule::String:
            return "string";
        case BuiltinModule::Random:
            return "random";
        case BuiltinModule::Bytes:
            return "bytes";
        case BuiltinModule::File:
            return "file";
        case BuiltinModule::Task:
            return "task";
        case BuiltinModule::None:
            break;
        }
        return "";
    }

    bool module_needs_import(BuiltinModule module)
    {
        // The modules that reach outside the program - the operating system, the files and the
        // screen - are imported by name; the others (math, string, random, bytes) are always there.
        return module == BuiltinModule::Os || module == BuiltinModule::File || module == BuiltinModule::Graphics;
    }

    std::span<const BuiltinModule> all_builtin_modules()
    {
        static constexpr std::array<BuiltinModule, 8> modules{BuiltinModule::Math, BuiltinModule::String,
            BuiltinModule::Random, BuiltinModule::Bytes, BuiltinModule::Task, BuiltinModule::Os, BuiltinModule::File,
            BuiltinModule::Graphics};
        return modules;
    }

    std::string_view builtin_member_name(const BuiltinInfo& info)
    {
        const std::size_t dot = info.canonical_name.find('.');
        return dot == std::string_view::npos ? info.canonical_name : info.canonical_name.substr(dot + 1);
    }
}
