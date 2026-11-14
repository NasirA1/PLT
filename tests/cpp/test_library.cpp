// The built-in library: input, text, tuple (pair) and list functions, function values and
// higher-order functions (map, filter, reduce), and the "os" module.
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/compiler.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/os.hpp"

#include <filesystem>
#include <format>
#include <sstream>

using namespace thunder;
using test::diagnostic_codes;
using test::parse_program;
using test::run_output;
using Codes = std::vector<std::string>;

namespace
{
    struct Outcome
    {
        std::string output;
        std::string error; // runtime error code, or "" if there was none
        int exit_code = 0;
    };

    // Compiles and runs 'text' with the given standard input and program arguments.
    Outcome run_with(const std::string& text, const std::string& input = "", std::vector<std::string> arguments = {})
    {
        const SourceFile source("test.th", text);
        const CompileResult result = compile(source);
        Outcome outcome;
        if (!result.program)
        {
            outcome.error = "COMPILE";
            for (const Diagnostic& diagnostic : result.diagnostics.items())
            {
                outcome.error += " " + diagnostic.code;
            }
            return outcome;
        }
        std::istringstream in(input);
        std::ostringstream out;
        Vm vm(*result.program, out, VmOptions{}, &in);
        vm.set_arguments(std::move(arguments));
        const ExecResult exec = vm.run();
        outcome.output = out.str();
        outcome.error = exec.error ? exec.error->code : std::string();
        outcome.exit_code = exec.exit_code;
        return outcome;
    }

    std::string error_of(const std::string& text)
    {
        return run_with(text).error;
    }
}

TEST_CASE("library: format, hex and bin")
{
    using test::run_output;
    CHECK_EQ(run_output("println(hex(42), \" \", bin(10), \" \", hex(-10), \" \", bin(0))"), "0x2a 0b1010 -0xa 0b0\n");
    CHECK_EQ(run_output("println(to_int(\"0x2a\"), \" \", to_int(\"0B11\"), \" \", to_int(hex(-255)))"), "42 3 -255\n");
    CHECK_EQ(run_output("println(format(\"{} is {x} is {b}\", 10, 10, 10))"), "10 is 0xa is 0b1010\n");
    CHECK_EQ(run_output("println(format(\"{{}} and {not a placeholder} stay\"))"), "{} and {not a placeholder} stay\n");
    CHECK_EQ(run_output("println(format(\"{}+{}\", \"a\", 1.5))"), "a+1.5\n");
    // The most negative int round-trips through both notations.
    CHECK_EQ(run_output("println(to_int(hex(-9223372036854775807 - 1)) == -9223372036854775807 - 1)"), "true\n");

    const test::RunResult too_few = test::run("println(format(\"{} {}\", 1))");
    REQUIRE(too_few.error.has_value());
    CHECK_EQ(too_few.error->code, "R031");
    const test::RunResult wrong = test::run("println(format(\"{x}\", \"a\"))");
    REQUIRE(wrong.error.has_value());
    CHECK_EQ(wrong.error->code, "R032");
    const test::RunResult too_many = test::run("println(format(\"{}\", 1, 2))");
    REQUIRE(too_many.error.has_value());
    CHECK_EQ(too_many.error->code, "R031");
}

TEST_CASE("library: input reads lines and the end of the input")
{
    const char* program = R"(
var name = input("Your name? ")
println("Hello, ", name, "!")
var total = 0
while not end_of_input()
    total = total + to_int(input())
println("total ", total, " then [", input(), "]"))";
    const Outcome outcome = run_with(program, "Ada\n1\n2\r\n3");
    CHECK_EQ(outcome.output, "Your name? Hello, Ada!\ntotal 6 then []\n");
    CHECK_EQ(outcome.error, "");

    // Without an input stream a program simply sees the end of the input.
    CHECK_EQ(run_output("println(end_of_input(), \"[\", input(), \"]\")"), "true[]\n");
    CHECK(diagnostic_codes("var x = input(1)") == Codes{"S010"});
    CHECK(diagnostic_codes("var x = input(\"a\", \"b\")") == Codes{"S009"});
}

TEST_CASE("library: text functions")
{
    CHECK_EQ(run_output("var s = \"  Hello, World \\n\"\nprintln(\"[\", trim(s), \"]\", to_upper(\"ab1\"), to_lower(\"AB1\"))"),
        "[Hello, World]AB1ab1\n");
    CHECK_EQ(run_output("println(substring(\"thunder\", 1, 4), \" \", slice(\"thunder\", 0, 0), \" \", substring(\"پښتو\", 1, 3))"),
        "hun  ښت\n");
    CHECK_EQ(run_output("println(split(\"a,b,,c\", \",\"), split(\"\", \",\"), split(\"ab\", \"\"), split(\"a--b\", \"--\"))"),
        "[\"a\", \"b\", \"\", \"c\"][\"\"][\"a\", \"b\"][\"a\", \"b\"]\n");
    CHECK_EQ(run_output("println(join([\"a\", \"b\"], \", \"), \"|\", join([\"x\"], \"-\"), \"|\", join(split(\"a b\", \" \"), \"+\"))"),
        "a, b|x|a+b\n");
    CHECK_EQ(run_output("println(replace(\"a.b.c\", \".\", \"::\"), \" \", replace(\"aaa\", \"aa\", \"b\"), \" \", replace(\"x\", \"\", \"y\"))"),
        "a::b::c ba x\n");
    CHECK_EQ(run_output("println(starts_with(\"thunder\", \"thu\"), ends_with(\"thunder\", \"der\"), starts_with(\"a\", \"ab\"))"),
        "truetruefalse\n");
    CHECK_EQ(run_output("println(contains(\"thunder\", \"und\"), index_of(\"thunder\", \"und\"), index_of(\"پښتو ژبه\", \"ژبه\"), index_of(\"a\", \"b\"))"),
        "true25-1\n");
    CHECK_EQ(run_output("println(repeat(\"ab\", 3), \"|\", repeat(\"x\", 0), \"|\", reverse(\"abc\"), \"|\", reverse(\"پښتو\"))"),
        "ababab||cba|وتښپ\n");
    CHECK_EQ(run_output("println(char_code(\"A\"), \" \", char_code(\"ښ\"), \" \", from_char_code(97), from_char_code(1690))"),
        "65 1690 aښ\n");
    CHECK_EQ(run_output("println(is_int(\" 42 \"), is_int(\"-7\"), is_int(\"4x\"), is_int(\"\"), is_int(\"۴۲\"))"),
        "truetruefalsefalsetrue\n");

    CHECK_EQ(error_of("println(substring(\"abc\", 2, 1))"), "R012");
    CHECK_EQ(error_of("println(substring(\"abc\", 0, 4))"), "R012");
    CHECK_EQ(error_of("println(slice(\"abc\", -1, 2))"), "R012");
    CHECK_EQ(error_of("println(repeat(\"a\", -1))"), "R014");
    CHECK_EQ(error_of("println(repeat(\"ab\", 9000000000))"), "R007");
    CHECK_EQ(error_of("println(char_code(\"ab\"))"), "R015");
    CHECK_EQ(error_of("println(char_code(\"\"))"), "R015");
    CHECK_EQ(error_of("println(from_char_code(-1))"), "R016");
    CHECK_EQ(error_of("println(from_char_code(55296))"), "R016");
    CHECK_EQ(run_output("println(substring(\"abc\", 1, 9))"),
        "ERROR R012: range 1..9 is out of bounds for a length of 3");

    CHECK(diagnostic_codes("var x = trim(1)") == Codes{"S010"});
    CHECK(diagnostic_codes("var x = join([1], \",\")") == Codes{"S010"});
    CHECK(diagnostic_codes("var x = substring(\"a\", 0)") == Codes{"S009"});
}

TEST_CASE("library: pairs and tuples")
{
    CHECK_EQ(run_output("var p = (1, \"one\")\nprintln(first(p), \" \", second(p), \" \", swap(p), \" \", first((1, 2, 3)))"),
        "1 one (\"one\", 1) 1\n");
    CHECK_EQ(run_output("println(zip([1, 2, 3], [\"a\", \"b\"]), enumerate([\"x\"]), zip([1], [true]))"),
        "[(1, \"a\"), (2, \"b\")][(0, \"x\")][(1, true)]\n");
    CHECK_EQ(run_output("var m = {\"a\": 1, \"b\": 2}\nfor var i = 0, i < len(items(m)), i = i + 1\n    var (k, v) = items(m)[i]\n    print(k, v, \" \")"),
        "a1 b2 ");
    CHECK_EQ(run_output("var m = {1: \"one\"}\nprintln(get(m, 1, \"?\"), get(m, 2, \"?\"))"), "one?\n");

    CHECK(diagnostic_codes("var x = first(1)") == Codes{"S026"});
    CHECK(diagnostic_codes("var x = swap((1, 2, 3))") == Codes{"S026"});
    CHECK(diagnostic_codes("var x = zip(1, [1])") == Codes{"S026"});
    CHECK(diagnostic_codes("var m = {1: \"a\"}\nvar x = get(m, \"k\", \"d\")") == Codes{"S043"});
    CHECK(diagnostic_codes("var m = {1: \"a\"}\nvar x = get(m, 1, 2)") == Codes{"S010"});
    CHECK(diagnostic_codes("var x = items([1])") == Codes{"S026"});
}

TEST_CASE("library: list functions")
{
    CHECK_EQ(run_output(R"(
var xs = [3, 1, 2]
push(xs, 5)
insert(xs, 0, 9)
insert(xs, 5, 7)
println(xs)
println(pop(xs), " ", remove_at(xs, 0), " ", xs, " ", len(xs)))"),
        "[9, 3, 1, 2, 5, 7]\n7 9 [3, 1, 2, 5] 4\n");
    CHECK_EQ(run_output("var xs = [3, 1, 2]\nvar s = sort(xs)\nprintln(s, xs, sort([\"b\", \"a\", \"ab\"]), reverse(xs))"),
        "[1, 2, 3][3, 1, 2][\"a\", \"ab\", \"b\"][2, 1, 3]\n");
    CHECK_EQ(run_output("println(slice([1, 2, 3, 4], 1, 3), concat([1], [2, 3]), range(2, 5), range(3, 3), range(5, 1), sum([1, 2, 3]), sum(range(0, 0)))"),
        "[2, 3][1, 2, 3][2, 3, 4][][]60\n");
    CHECK_EQ(run_output("println(contains([1, 2], 2), contains([\"a\"], \"b\"), index_of([(1, 2), (3, 4)], (3, 4)), index_of([true], false))"),
        "truefalse1-1\n");
    CHECK_EQ(run_output("var grid: int[][] = []\npush(grid, [])\npush(grid[0], 4)\nprintln(grid)"), "[[4]]\n");

    CHECK_EQ(error_of("var e: int[] = []\nprintln(pop(e))"), "R013");
    CHECK_EQ(error_of("var xs = [1]\ninsert(xs, 2, 0)"), "R003");
    CHECK_EQ(error_of("var xs = [1]\nprintln(remove_at(xs, 1))"), "R003");
    CHECK_EQ(error_of("println(slice([1], 0, 2))"), "R012");
    CHECK_EQ(error_of("println(sum([9223372036854775807, 1]))"), "R002");
    CHECK_EQ(error_of("println(len(range(0, 100000000)))"), "R007");
    CHECK_EQ(error_of("println(len(range(-9223372036854775807, 9223372036854775807)))"), "R007");

    CHECK(diagnostic_codes("var xs = [1]\npush(xs, \"a\")") == Codes{"S010"});
    CHECK(diagnostic_codes("push(1, 2)") == Codes{"S026"});
    CHECK(diagnostic_codes("var x = sort([true])") == Codes{"S026"});
    CHECK(diagnostic_codes("struct P\n    x: int\nvar x = contains([P(1)], P(1))") == Codes{"S026"});
    CHECK(diagnostic_codes("var x = contains(\"abc\", 1)") == Codes{"S010"});
    CHECK(diagnostic_codes("var x = reverse(1)") == Codes{"S026"});
    CHECK(diagnostic_codes("var x = sum([\"a\"])") == Codes{"S026"});
    CHECK(diagnostic_codes("var x = pop([1])\nvar y: string = x") == Codes{"S005"});
}

TEST_CASE("function values: types, references and calls")
{
    CHECK_EQ(parse_program("var f: func(int, string): bool\nvar g: func()\nvar h: (func(int): int)[]"),
        "(Program (Var f) (Var g) (Var h))");
    const char* program = R"(
func double(n: int): int
    return n * 2
func shout()
    println("!")
func apply(f: func(int): int, x: int): int
    return f(x)
func pick(up: bool): func(int): int
    if up
        return double
    return negate
func negate(n: int): int
    return -n
var f = double
var g: func(int): int
var actions: (func(int): int)[] = [double, negate]
var s: func() = shout
s()
println(f(4), " ", apply(negate, 3), " ", pick(true)(5), " ", actions[1](2), " ", f)
println(g)
g = f
println(g(1), " ", inspect(f)))";
    CHECK_EQ(run_output(program), "!\n8 -3 10 -2 <func double>\n<func ?>\n2 <func double>\n");
    CHECK_EQ(error_of("var f: func(int): int\nprintln(f(1))"), "R020");
    CHECK_EQ(run_output("var f: func(int): int\nprintln(f(1))"),
        "ERROR R020: a function variable was called before a function was assigned to it");

    // A struct may hold a function; its default value is "no function".
    CHECK_EQ(run_output("struct Button\n    label: string\n    on_click: func(): int\nfunc one(): int\n    return 1\n"
                        "var b = Button(label: \"ok\")\nprintln(b)\nb.on_click = one\nprintln(b.on_click())"),
        "Button(label: \"ok\", on_click: <func ?>)\n1\n");

    CHECK(diagnostic_codes("func f(n: int): int\n    return n\nvar g: func(string): int = f") == Codes{"S005"});
    CHECK(diagnostic_codes("func f(n: int): int\n    return n\nvar x = f(\"a\")") == Codes{"S010"});
    CHECK(diagnostic_codes("var g: func(int): int\nvar x = g()") == Codes{"S009"});
    CHECK(diagnostic_codes("var g: func(int): int\nvar x = g(\"a\")") == Codes{"S010"});
    CHECK(diagnostic_codes("var g: func(int)\nvar x = g(1)") == Codes{"S022"});
    CHECK(diagnostic_codes("var a = [1]\nvar x = a[0](1)") == Codes{"S011"});
    CHECK(diagnostic_codes("func f(n: int): int\n    return n\nf = f") == Codes{"S012"});
    CHECK(diagnostic_codes("func f()\n    pass\nvar x = f == f") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = 1(2)") == Codes{"P006"});
    CHECK(diagnostic_codes("var x = len") == Codes{"S025"});
    CHECK(diagnostic_codes("func f(n: int): int\n    return n\nvar x = [f][0](a: 1)") == Codes{"S033"});
    // A variable holding a function shadows a function of the same name.
    CHECK_EQ(run_output("func f(): int\n    return 1\nfunc g(): int\n    return 2\nfunc main()\n    var f = g\n    println(f())"),
        "2\n");
}

TEST_CASE("library: map, filter and reduce call Thunder functions")
{
    const char* program = R"(
func square(n: int): int
    return n * n
func is_odd(n: int): bool
    return n % 2 == 1
func add(total: int, n: int): int
    return total + n
func longest(best: string, word: string): string
    if len(word) > len(best)
        return word
    return best
func describe(n: int): string
    return "#" + str(n)
var numbers = range(1, 6)
println(map(numbers, square), filter(numbers, is_odd), reduce(numbers, 0, add))
println(reduce(["a", "abc", "ab"], "", longest), map(numbers, describe))
var empty: int[] = []
println(map(empty, square), filter(empty, is_odd), reduce(empty, 7, add)))";
    CHECK_EQ(run_output(program),
        "[1, 4, 9, 16, 25][1, 3, 5]15\nabc[\"#1\", \"#2\", \"#3\", \"#4\", \"#5\"]\n[][]7\n");

    // A runtime error inside the function points at the function, called from the built-in.
    const auto result = test::run("func bad(n: int): int\n    return 10 / n\nprintln(map([1, 0], bad))");
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R001");
    CHECK_EQ(result.error->position.line, 2u);
    REQUIRE(result.error->call_stack.size() == 2);
    CHECK_EQ(result.error->call_stack[0].function, "bad");
    CHECK_EQ(result.error->call_stack[1].position.line, 3u);

    // The function may change the array it is mapped over: map works on the elements it started with.
    CHECK_EQ(run_output("var xs = [1, 2]\nfunc grow(n: int): int\n    push(xs, n)\n    return n\nprintln(map(xs, grow), xs)"),
        "[1, 2][1, 2, 1, 2]\n");
    // Step limits and the call depth still apply inside map.
    VmOptions limited;
    limited.max_steps = 50;
    CHECK_EQ(test::run("func f(n: int): int\n    while true\n        pass\n    return n\nprintln(map([1], f))", limited).error->code, "R009");

    CHECK(diagnostic_codes("func f(s: string): int\n    return 1\nvar x = map([1], f)") == Codes{"S051"});
    CHECK(diagnostic_codes("func f(n: int)\n    pass\nvar x = map([1], f)") == Codes{"S051"});
    CHECK(diagnostic_codes("func f(n: int): int\n    return n\nvar x = filter([1], f)") == Codes{"S051"});
    CHECK(diagnostic_codes("func f(a: int, b: int): string\n    return \"\"\nvar x = reduce([1], 0, f)") == Codes{"S051"});
    CHECK(diagnostic_codes("var x = map([1], 5)") == Codes{"S051"});
    CHECK(diagnostic_codes("var x = map(1, 5)") == Codes{"S026"});
    const SourceFile source("t.th", "func f(s: string): int\n    return 1\nvar x = map([1], f)");
    const CompileResult result_s051 = compile(source);
    REQUIRE(!result_s051.diagnostics.items().empty());
    CHECK_EQ(result_s051.diagnostics.items()[0].message,
        "argument 2 of 'map' must be a function of type 'func(int): ?' but has type 'func(string): int'");
}

TEST_CASE("os module: functions need 'import os'")
{
    CHECK(diagnostic_codes("println(platform())") == Codes{"S049"});
    CHECK(diagnostic_codes("var x = run") == Codes{"S049"});
    CHECK(diagnostic_codes("import os\nprintln(platform())").empty());
    CHECK(diagnostic_codes("import سیسټم\nprintln(platform())").empty()); // a translated module name
    CHECK(diagnostic_codes("import system") == Codes{"S050"});
    CHECK(diagnostic_codes("if true\n    import os") == Codes{"P016"});
    CHECK(diagnostic_codes("import 5") == Codes{"P001"});
    // Without the import, the names are ordinary identifiers.
    CHECK_EQ(run_output("var run = 3\nfunc time(): int\n    return 4\nprintln(run + time())"), "7\n");
    CHECK(diagnostic_codes("import os\nfunc time(): int\n    return 4") == Codes{"S004"});
}

TEST_CASE("os module: arguments, environment, exit")
{
    const Outcome outcome = run_with("import os\nprintln(args(), len(args()))\nexit(3)\nprintln(\"never\")", "", {"a", "b c"});
    CHECK_EQ(outcome.output, "[\"a\", \"b c\"]2\n");
    CHECK_EQ(outcome.exit_code, 3);
    CHECK_EQ(outcome.error, "");
    CHECK_EQ(run_with("import os\nprintln(get_env(\"THUNDER_SURELY_NOT_SET_42\") == \"\")").output, "true\n");
    CHECK_EQ(run_with("import os\nvar p = platform()\nprintln(p == \"linux\" or p == \"windows\" or p == \"macos\" or p == \"unknown\")").output,
        "true\n");
    CHECK_EQ(run_with("import os\nprintln(time() > 1600000000, \" \", len(date_time()), \" \", clock_ms() >= 0)").output, "true 19 true\n");
    CHECK_EQ(run_with("import os\nvar t = clock_ms()\nsleep(20)\nsleep(-5)\nprintln(clock_ms() - t >= 20)").output, "true\n");
    CHECK(!os::current_directory().empty());
}

TEST_CASE("os module: files and commands")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "thunder_os_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "notes.txt").generic_string();
    const std::string program = std::format(R"(import os
var path = "{0}"
println(file_exists(path))
write_file(path, "one\n")
append_file(path, "two\n")
println(read_file(path), file_exists(path), list_dir("{1}"))
remove_file(path)
remove_file(path)
println(file_exists(path))
println(contains(list_dir("{1}/.."), "thunder_os_test")))",
        path, dir.generic_string());
    const Outcome files = run_with(program);
    CHECK_EQ(files.output, "false\none\ntwo\ntrue[\"notes.txt\"]\nfalse\ntrue\n");
    CHECK_EQ(files.error, "");

    CHECK_EQ(error_of(std::format("import os\nprintln(read_file(\"{}/missing.txt\"))", dir.generic_string())), "R017");
    CHECK_EQ(error_of(std::format("import os\nprintln(read_file(\"{}\"))", dir.generic_string())), "R017");
    CHECK_EQ(error_of(std::format("import os\nwrite_file(\"{}/no/such/dir/x.txt\", \"\")", dir.generic_string())), "R018");
    CHECK_EQ(error_of(std::format("import os\nremove_file(\"{}\")", dir.generic_string())), "R018");
    CHECK_EQ(error_of(std::format("import os\nprintln(list_dir(\"{}/missing\"))", dir.generic_string())), "R017");
    fs::remove_all(dir);

    // Commands run in the system shell; "echo" and "exit" exist in sh and cmd.exe alike.
    const Outcome command = run_with("import os\nvar status = run(\"echo hello\")\nprintln(\"status \", status)\n"
                                     "var (code, text) = run_output(\"echo captured\")\nprintln(code, \" \", trim(text))\n"
                                     "println(run(\"exit 3\"))");
    CHECK_EQ(command.output, "hello\nstatus 0\n0 captured\n3\n");
}

TEST_CASE("library: every built-in is described and has a module")
{
    for (const BuiltinInfo& builtin : all_builtins())
    {
        CHECK(!builtin.signature.empty());
        CHECK(!builtin.description.empty());
        CHECK(builtin.min_arity <= builtin.arity || builtin.arity == variadic);
        CHECK(builtin_info(builtin.id).id == builtin.id);
        const std::string description = pashto_pack().term("builtin." + std::string(builtin.canonical_name));
        CHECK(description != "builtin." + std::string(builtin.canonical_name)); // translated
    }
    CHECK(!builtin_from_index(builtin_count).has_value());
    CHECK_EQ(module_canonical_name(BuiltinModule::Os), "os");
    CHECK_EQ(module_canonical_name(BuiltinModule::None), "");
    CHECK(find_builtin_module("os") == BuiltinModule::Os);
    CHECK(find_builtin_module("سیسټم") == BuiltinModule::Os);
    CHECK(!find_builtin_module("geometry").has_value());
    CHECK_EQ(english_pack().spelling(BuiltinModule::Os), "os");
    CHECK_EQ(pashto_pack().spelling(BuiltinModule::Os), "سیسټم");
}

TEST_CASE("library: Pashto programs use the translated names")
{
    const SourceFile source("p.th", "# ژبه: ps\nمتغیر لړ = [۳, ۱, ۲]\nورزیات کړه(لړ, ۴)\nکرښه چاپ(ترتیب کړه(لړ), \" \", مجموعه(لړ), \" \", لوی توري کړه(\"ab\"), \" \", لومړی((۱, ۲)))\n");
    const CompileResult result = compile(source);
    REQUIRE(result.program.has_value());
    std::ostringstream out;
    Vm vm(*result.program, out);
    vm.run();
    CHECK_EQ(out.str(), "[۱، ۲، ۳، ۴] ۱۰ AB ۱\n");
    CHECK_EQ(Value::from_function(0, nullptr).to_display(pashto_pack().display), "<دنده ?>");
}
