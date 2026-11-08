// "for ... in" loops and compound assignment (x += 1).
#include "helpers.hpp"
#include "minitest.hpp"

using test::diagnostic_codes;
using test::run_output;
using Codes = std::vector<std::string>;

TEST_CASE("for-in: walks arrays, strings and maps")
{
    CHECK_EQ(run_output("for v in [1, 2, 3]\n    print(v, \" \")\nprintln()"), "1 2 3 \n");
    CHECK_EQ(run_output("for c in \"abج\"\n    print(c, \".\")\nprintln()"), "a.b.ج.\n");
    CHECK_EQ(run_output("var m = {\"a\": 1, \"b\": 2}\nfor k in m\n    print(k, \"=\", m[k], \" \")\nprintln()"),
        "a=1 b=2 \n");
    CHECK_EQ(run_output("for i in range(0, 4)\n    print(i)\nprintln()"), "0123\n");
    CHECK_EQ(run_output("for v in []\n    println(v)\nprintln(\"done\")"), "COMPILE ERROR 1:10: S021");
}

TEST_CASE("for-in: unpacks tuple elements into several variables")
{
    CHECK_EQ(run_output("for (i, w) in enumerate([\"a\", \"b\"])\n    print(i, w, \" \")\nprintln()"), "0a 1b \n");
    CHECK_EQ(run_output("var m = {\"a\": 1}\nfor (k, v) in items(m)\n    println(k, v)"), "a1\n");
    const Codes expected{"W001", "S044"};
    CHECK(diagnostic_codes("for (a, b) in [1, 2]\n    println(a)") == expected);
}

TEST_CASE("for-in: break, continue and nesting")
{
    CHECK_EQ(run_output("for v in [1, 2, 3, 4]\n    if v == 2\n        continue\n    if v == 4\n        break\n"
                        "    print(v)\nprintln()"),
        "13\n");
    CHECK_EQ(run_output("for a in [1, 2]\n    for b in [\"x\", \"y\"]\n        print(a, b, \" \")\nprintln()"),
        "1x 1y 2x 2y \n");
}

TEST_CASE("for-in: the loop variable is local, and the length is re-read each round")
{
    CHECK(diagnostic_codes("for v in [1]\n    println(v)\nprintln(v)") == Codes{"S001"});
    // Walking a copy is how you change an array while going through it.
    CHECK_EQ(run_output("var a = [1, 2]\nfor v in copy(a)\n    push(a, v)\nprintln(a)"), "[1, 2, 1, 2]\n");
    // Walking the array itself sees the change, as documented (the position is compared with the
    // current length each round).
    CHECK_EQ(run_output("var a = [1]\nfor v in a\n    if len(a) < 3\n        push(a, v + 1)\nprintln(a)"),
        "[1, 2, 3]\n");
    // A map is always safe: its keys are taken before the first round.
    CHECK_EQ(run_output("var m = {\"a\": 1}\nfor k in m\n    m[k + \"!\"] = 2\nprintln(m)"),
        "{\"a\": 1, \"a!\": 2}\n");
}

TEST_CASE("for-in: a value that cannot be walked is reported")
{
    CHECK(diagnostic_codes("for v in 3\n    println(v)") == Codes{"S052"});
    CHECK(diagnostic_codes("for v in (1, 2)\n    println(v)") == Codes{"S052"});
}

TEST_CASE("for-in: the classic for loop still parses")
{
    CHECK_EQ(run_output("for var i = 0, i < 3, i = i + 1\n    print(i)\nprintln()"), "012\n");
    CHECK_EQ(run_output("var i = 0\nfor i = 0, i < 2, i = i + 1\n    print(i)\nprintln()"), "01\n");
}

TEST_CASE("compound assignment: variables, elements and fields")
{
    CHECK_EQ(run_output("var x = 10\nx += 5\nx -= 3\nx *= 2\nx /= 4\nx %= 4\nprintln(x)"), "2\n");
    CHECK_EQ(run_output("var s = \"a\"\ns += \"b\"\nprintln(s)"), "ab\n");
    CHECK_EQ(run_output("var a = [1, 2]\na[1] += 10\nprintln(a)"), "[1, 12]\n");
    CHECK_EQ(run_output("struct P\n    x: int\nvar p = P(1)\np.x += 41\nprintln(p.x)"), "42\n");
    CHECK_EQ(run_output("var m = {\"a\": 1}\nm[\"a\"] += 1\nprintln(m)"), "{\"a\": 2}\n");
    CHECK_EQ(run_output("var x = 1.0\nx += 1\nprintln(x)"), "2.0\n");
}

TEST_CASE("compound assignment: the target is evaluated only once")
{
    const char* program = R"(
var calls = 0
var a = [1, 2, 3]
func which(): int
    calls = calls + 1
    return 1
a[which()] += 10
println(a, " ", calls)
)";
    CHECK_EQ(run_output(program), "[1, 12, 3] 1\n");
}

TEST_CASE("compound assignment: type errors and constants")
{
    CHECK(diagnostic_codes("var x = 1\nx += \"a\"") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = 1.5\nx %= 2.0") == Codes{"S006"});
    CHECK(diagnostic_codes("const x = 1\nx += 1") == Codes{"S012"});
    CHECK(diagnostic_codes("var s = \"a\"\ns -= \"b\"") == Codes{"S006"});
}
