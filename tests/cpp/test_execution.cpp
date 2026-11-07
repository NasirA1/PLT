#include "helpers.hpp"
#include "minitest.hpp"

using test::run_output;

TEST_CASE("run: hello world")
{
    CHECK_EQ(run_output("println(\"Hello, Thunder!\")"), "Hello, Thunder!\n");
}

TEST_CASE("run: print joins values without separators")
{
    CHECK_EQ(run_output("print(1, \"a\", true)\nprint(false)\nprintln()\nprintln([1, 2], [\"x\"], [[true]])"),
        "1atruefalse\n[1, 2][\"x\"][[true]]\n");
}

TEST_CASE("run: arithmetic follows precedence and C-style integer division")
{
    CHECK_EQ(run_output("println(10+5*( (12 / 2) * 5) - -12)"), "172\n");
    CHECK_EQ(run_output("println(20 * 15 + ((450 / 15) - (2 * (11 - 5)) + 10))"), "328\n");
    CHECK_EQ(run_output("println(7 - 2 - 1, \" \", 8 / -2, \" \", -7 / 2, \" \", -7 % 3, \" \", 7 % -3)"),
        "4 -4 -3 -1 1\n");
}

TEST_CASE("run: comparisons and logic")
{
    CHECK_EQ(run_output("println(1 < 2, 2 <= 2, 3 > 4, 4 >= 5, 1 == 1, 1 != 1)"), "truetruefalsefalsetruefalse\n");
    CHECK_EQ(run_output("println(\"apple\" < \"banana\", \"b\" >= \"a\", \"x\" == \"x\", true == false, true != false)"),
        "truetruetruefalsetrue\n");
    CHECK_EQ(run_output("println(true and false, true or false, not true, false or false, true and true)"),
        "falsetruefalsefalsetrue\n");
}

TEST_CASE("run: 'and' and 'or' short-circuit")
{
    const char* program = R"(
var calls = 0
func touch(result: bool): bool
    calls = calls + 1
    return result
var a = false and touch(true)
var b = true or touch(true)
var c = true and touch(false)
var d = false or touch(true)
println(a, b, c, d, " calls=", calls))";
    CHECK_EQ(run_output(program), "falsetruefalsetrue calls=2\n");
}

TEST_CASE("run: variables, constants and default values")
{
    CHECK_EQ(run_output("var i: int\nvar b: bool\nvar s: string\nvar a: int[]\nprintln(i, b, \"[\", s, \"]\", a, len(a))"),
        "0false[][]0\n");
    CHECK_EQ(run_output("const x = 2\nvar y = x * 21\ny = y + 0\nprintln(y)"), "42\n");
}

TEST_CASE("run: if / else if / else")
{
    const char* program = R"(
func classify(n: int): string
    if (n < 0)
        return "negative"
    else if (n == 0)
        return "zero"
    else
        return "positive"
println(classify(-5), " ", classify(0), " ", classify(7))
if (false)
    println("never"))";
    CHECK_EQ(run_output(program), "negative zero positive\n");
}

TEST_CASE("run: while loops with break and continue")
{
    const char* program = R"(
var i = 0
var sum = 0
while (true)
    i = i + 1
    if (i > 10)
        break
    if (i % 2 == 0)
        continue
    sum = sum + i
println(sum))";
    CHECK_EQ(run_output(program), "25\n");
}

TEST_CASE("run: for loops, including continue running the step")
{
    const char* program = R"(
var out = ""
for (var i = 0, i < 6, i = i + 1)
    if (i == 2)
        continue
    if (i == 5)
        break
    out = out + str(i)
var n = 0
for (,,)
    n = n + 1
    if (n == 3)
        break
var j = 10
for (j = 0, j < 2, j = j + 1)
    pass
println(out, " ", n, " ", j))";
    CHECK_EQ(run_output(program), "0134 3 2\n");
}

TEST_CASE("run: nested loops and block-scoped variables get fresh values")
{
    const char* program = R"(
for (var i = 0, i < 3, i = i + 1)
    var row = ""
    for (var j = 0, j <= i, j = j + 1)
        row = row + "*"
    println(row))";
    CHECK_EQ(run_output(program), "*\n**\n***\n");
}

TEST_CASE("run: functions, recursion and main()")
{
    const char* program = R"(
func fact(n: int): int
    if (n <= 1)
        return 1
    return n * fact(n - 1)
func main()
    println("main: ", fact(10))
println("top-level first"))";
    CHECK_EQ(run_output(program), "top-level first\nmain: 3628800\n");
}

TEST_CASE("run: void functions may return early or fall off the end")
{
    const char* program = R"(
func show(n: int)
    if (n < 0)
        println("negative")
        return
    println(n)
show(-1)
show(3))";
    CHECK_EQ(run_output(program), "negative\n3\n");
}

TEST_CASE("run: discarded function results are popped")
{
    const char* program = R"(
func one(): int
    return 1
for (var i = 0, i < 1000, i = i + 1)
    one()
    i + 1
println("ok"))";
    const auto result = test::run(program);
    CHECK_EQ(result.output, "ok\n");
    CHECK(result.stats.max_stack_depth < 10);
}

TEST_CASE("run: arrays are references; array() makes independent copies")
{
    const char* program = R"(
var a = [1, 2, 3]
var b = a
b[0] = 99
func set_last(xs: int[], v: int)
    xs[len(xs) - 1] = v
set_last(a, 7)
var grid = array(2, array(2, 0))
grid[0][1] = 5
println(a, " ", b, " ", grid, " ", len(grid))
var words: string[] = []
println(len(words), array(0, 1)))";
    CHECK_EQ(run_output(program), "[99, 2, 7] [99, 2, 7] [[0, 5], [0, 0]] 2\n0[]\n");
}

TEST_CASE("run: strings")
{
    const char* program = R"(
var s = "Thunder"
var reversed = ""
for (var i = len(s) - 1, i >= 0, i = i - 1)
    reversed = reversed + s[i]
println(reversed, " ", len(s), " ", s[0] == "T", " ", str(12) + str(true))
println(to_int("42") + to_int("-8"), " ", to_int("  7 "), " ", to_int("+3"))
var p = "پښتو"
println(len(p), " ", p[1], " ", "tab\there"))";
    CHECK_EQ(run_output(program), "rednuhT 7 true 12true\n34 7 3\n4 ښ tab\there\n");
}

TEST_CASE("run: global variables are visible inside functions")
{
    CHECK_EQ(run_output("var counter = 0\nfunc bump()\n    counter = counter + 1\nbump()\nbump()\nprintln(counter)"), "2\n");
}

TEST_CASE("runtime errors: division and modulo by zero")
{
    CHECK_EQ(run_output("println(1 / 0)"), "ERROR R001: division by zero");
    CHECK_EQ(run_output("var z = 0\nprintln(5 % z)"), "ERROR R001: division by zero");
}

TEST_CASE("runtime errors: integer overflow is detected")
{
    CHECK_EQ(run_output("println(9223372036854775807 + 1)"), "ERROR R002: integer overflow in 9223372036854775807 + 1");
    CHECK_CONTAINS(run_output("var m = -9223372036854775807 - 1\nprintln(m)"), "-9223372036854775808");
    CHECK_CONTAINS(run_output("var m = -9223372036854775807 - 2"), "ERROR R002");
    CHECK_CONTAINS(run_output("var m = 4611686018427387904 * 2"), "ERROR R002");
    CHECK_CONTAINS(run_output("var m = -4611686018427387904 * 3"), "ERROR R002");
    CHECK_CONTAINS(run_output("var m = (-9223372036854775807 - 1) / -1"), "ERROR R002");
    CHECK_CONTAINS(run_output("var m = -(-9223372036854775807 - 1)"), "ERROR R002");
    CHECK_EQ(run_output("println((-9223372036854775807 - 1) % -1)"), "0\n");
}

TEST_CASE("runtime errors: out-of-bounds indexing")
{
    CHECK_EQ(run_output("var a = [1, 2]\nprintln(a[2])"), "ERROR R003: index 2 is out of bounds for an array of length 2");
    CHECK_EQ(run_output("var a = [1, 2]\na[-1] = 0"), "ERROR R003: index -1 is out of bounds for an array of length 2");
    CHECK_EQ(run_output("println(\"abc\"[3])"), "ERROR R004: index 3 is out of bounds for a string of length 3");
}

TEST_CASE("runtime errors: conversions and array sizes")
{
    CHECK_EQ(run_output("println(to_int(\"12x\"))"), "ERROR R005: cannot convert \"12x\" to int");
    CHECK_CONTAINS(run_output("println(to_int(\"\"))"), "ERROR R005");
    CHECK_CONTAINS(run_output("println(to_int(\"99999999999999999999\"))"), "ERROR R005");
    CHECK_EQ(run_output("var a = array(-1, 0)"), "ERROR R006: array size cannot be negative (got -1)");
    CHECK_CONTAINS(run_output("var a = array(100000000, 0)"), "ERROR R007");
}

TEST_CASE("runtime errors: unbounded recursion overflows the call stack")
{
    CHECK_CONTAINS(run_output("func f(n: int): int\n    return f(n + 1)\nprintln(f(0))"), "ERROR R008");
}

TEST_CASE("runtime errors: the step limit stops infinite loops")
{
    thunder::VmOptions options;
    options.max_steps = 1000;
    const auto result = test::run("var i = 0\nwhile (true)\n    i = i + 1", options);
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R009");
    CHECK_EQ(result.stats.steps, 1000u);
}

TEST_CASE("runtime errors: position and call stack")
{
    const auto result = test::run("func inner(d: int): int\n    return 10 / d\nfunc outer(): int\n    return inner(0)\nprintln(outer())");
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->position.line, 2u);
    CHECK_EQ(result.error->position.column, 15u); // the '/' operator
    REQUIRE(result.error->call_stack.size() == 3);
    CHECK_EQ(result.error->call_stack[0].function, "inner");
    CHECK_EQ(result.error->call_stack[1].function, "outer");
    CHECK_EQ(result.error->call_stack[1].position.line, 4u);
    CHECK_EQ(result.error->call_stack[2].function, "<main>");
    CHECK_EQ(result.error->call_stack[2].position.line, 5u);
}

TEST_CASE("run: output written before a runtime error is kept")
{
    CHECK_EQ(run_output("println(\"before\")\nvar a: int[] = []\nprintln(a[0])"),
        "before\nERROR R003: index 0 is out of bounds for an array of length 0");
}

TEST_CASE("run: statistics are collected")
{
    const auto result = test::run("func f(n: int): int\n    if (n == 0)\n        return 0\n    return f(n - 1)\nprintln(f(5))");
    CHECK_EQ(result.stats.calls, 7u); // <main> + 6 calls to f
    CHECK_EQ(result.stats.max_call_depth, 7u);
    CHECK(result.stats.steps > 20);
}

TEST_CASE("compile: compilation stops at the first failing phase")
{
    using namespace thunder;
    const SourceFile lexical("t.th", "var x = 1 # 2");
    const CompileResult lex_result = compile(lexical);
    CHECK(lex_result.ast == nullptr);
    CHECK(lex_result.timings.size() == 4);
    CHECK(lex_result.timings[0].ran && !lex_result.timings[0].succeeded);
    CHECK(!lex_result.timings[1].ran);

    const SourceFile syntax("t.th", "var x = ");
    const CompileResult syntax_result = compile(syntax);
    CHECK(syntax_result.ast != nullptr);
    CHECK(syntax_result.model == nullptr);

    const SourceFile semantic("t.th", "var x: int = true");
    const CompileResult semantic_result = compile(semantic);
    CHECK(semantic_result.model != nullptr);
    CHECK(!semantic_result.program.has_value());
    CHECK(semantic_result.timings[2].ran && !semantic_result.timings[2].succeeded);
    CHECK(!semantic_result.timings[3].ran);

    const SourceFile good("dir/sub/t.th", "println(1)");
    const CompileResult good_result = compile(good);
    REQUIRE(good_result.succeeded());
    CHECK_EQ(good_result.program->source_name, "t.th");
    CHECK(good_result.timings[3].succeeded);
}

TEST_CASE("compile: warnings do not stop compilation")
{
    const auto result = test::run("func f()\n    var unused = 1\nf()\nprintln(\"ran\")");
    CHECK(result.compiled);
    CHECK_EQ(result.output, "ran\n");
    REQUIRE(result.diagnostics.size() == 1);
    CHECK_CONTAINS(result.diagnostics[0], "W001");
}
