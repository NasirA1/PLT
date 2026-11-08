// Decimal (floating point) numbers: literals, arithmetic, widening and the maths built-ins.
#include "helpers.hpp"
#include "minitest.hpp"

using test::diagnostic_codes;
using test::parse_expr;
using test::run_output;
using Codes = std::vector<std::string>;

TEST_CASE("floats: literals need a digit on both sides of the point")
{
    CHECK_EQ(parse_expr("3.14"), "(Float 3.14)");
    CHECK_EQ(parse_expr("0.5"), "(Float 0.5)");
    // "1." is the integer 1 followed by '.', and ".5" never starts a number, so neither parses.
    CHECK_EQ(parse_expr("1."), "");
    CHECK_EQ(parse_expr(".5"), "");
    // A number that follows a '.' is a tuple element, never a decimal part.
    CHECK_EQ(parse_expr("p.0.1"), "(TupleIndex 1 (TupleIndex 0 (Name p)))");
}

TEST_CASE("floats: printing always shows a fractional part")
{
    CHECK_EQ(run_output("println(1.0, \" \", 0.5, \" \", -2.75, \" \", 1.0 / 3.0)"),
        "1.0 0.5 -2.75 0.3333333333333333\n");
    CHECK_EQ(run_output("println(str(2.0), \" \", [1.5, 2.0], \" \", (1.5, \"a\"))"),
        "2.0 [1.5, 2.0] (1.5, \"a\")\n");
}

TEST_CASE("floats: arithmetic and comparisons")
{
    CHECK_EQ(run_output("println(1.5 + 2.25, \" \", 1.5 - 2.0, \" \", 1.5 * 2.0, \" \", 3.0 / 2.0, \" \", -1.5)"),
        "3.75 -0.5 3.0 1.5 -1.5\n");
    CHECK_EQ(run_output("println(1.5 < 2.0, 2.0 <= 2.0, 1.5 > 2.0, 2.0 == 2.0, 1.5 != 2.0)"),
        "truetruefalsetruetrue\n");
}

TEST_CASE("floats: an int is widened where a float is wanted")
{
    CHECK_EQ(run_output("println(1 + 0.5, \" \", 7 / 2.0, \" \", 2.0 * 3)"), "1.5 3.5 6.0\n");
    CHECK_EQ(run_output("var x: float = 2\nprintln(x)"), "2.0\n");
    CHECK_EQ(run_output("var x = 1.0\nx = 3\nprintln(x)"), "3.0\n");
    CHECK_EQ(run_output("func half(x: float): float\n    return x / 2\nprintln(half(5))"), "2.5\n");
    CHECK_EQ(run_output("println([1, 2.5, 3])"), "[1.0, 2.5, 3.0]\n");
    CHECK_EQ(run_output("struct P\n    x: float\nprintln(P(3))"), "P(x: 3.0)\n");
    CHECK_EQ(run_output("println({\"a\": 1, \"b\": 2.5})"), "{\"a\": 1.0, \"b\": 2.5}\n");
    CHECK_EQ(run_output("var t: (float, string) = (1, \"x\")\nprintln(t)"), "(1.0, \"x\")\n");
    // The conversion never goes the other way.
    CHECK(diagnostic_codes("var n: int = 1.5") == Codes{"S005"});
    CHECK(diagnostic_codes("var n: int = 1\nn = 2.5") == Codes{"S005"});
}

TEST_CASE("floats: operations that are only defined for whole numbers")
{
    CHECK(diagnostic_codes("println(1.5 % 2.0)") == Codes{"S006"});
    CHECK(diagnostic_codes("var a = [1, 2]\nprintln(a[1.0])") == Codes{"S019"});
}

TEST_CASE("floats: division by zero and results that do not fit are runtime errors")
{
    CHECK_EQ(run_output("println(1.0 / 0.0)"), "ERROR R001: division by zero");
    CHECK_EQ(run_output("var big = 1.0\nfor var i = 0, i < 400, i = i + 1\n    big = big * 10.0\nprintln(big)"),
        "ERROR R025: the result of 9.999999999999998e+307 * 10.0 is too large for a decimal number");
}

TEST_CASE("floats: conversions between text, whole numbers and decimals")
{
    CHECK_EQ(run_output("println(to_float(\"2.5\"), \" \", to_float(3), \" \", to_int(2.9), \" \", to_int(-2.9))"),
        "2.5 3.0 2 -2\n");
    CHECK_EQ(run_output("println(is_number(\"2.5\"), is_number(\"x\"), is_int(\"2.5\"), is_int(\"7\"))"),
        "truefalsefalsetrue\n");
    CHECK_EQ(run_output("println(to_float(\"x\"))"), "ERROR R026: cannot convert \"x\" to float");
}

TEST_CASE("floats: the maths built-ins")
{
    CHECK_EQ(run_output("println(sqrt(9.0), \" \", pow(2.0, 10.0), \" \", round(2.5), \" \", round(-2.5))"),
        "3.0 1024.0 3 -3\n");
    CHECK_EQ(run_output("println(floor(2.7), \" \", ceil(2.1), \" \", floor(-2.1), \" \", ceil(-2.7))"),
        "2 3 -3 -2\n");
    CHECK_EQ(run_output("println(abs(-3), \" \", abs(-3.25), \" \", min(2, 5), \" \", max(2.5, 1.0), \" \", min(1, 2.5))"),
        "3 3.25 2 2.5 1.0\n");
    CHECK_EQ(run_output("println(min(\"b\", \"a\"), max(\"b\", \"a\"))"), "ab\n");
    CHECK_EQ(run_output("println(sum([1.5, 2.5]), \" \", sort([2.5, 1.0]), \" \", exp(0.0), \" \", log(1.0))"),
        "4.0 [1.0, 2.5] 1.0 0.0\n");
    CHECK_EQ(run_output("println(sqrt(-1.0))"), "ERROR R027: 'sqrt' is not defined for -1.0");
    CHECK_EQ(run_output("println(log(0.0))"), "ERROR R027: 'log' is not defined for 0.0");
    // random() stays inside its range whatever the seed.
    CHECK_EQ(run_output("var ok = true\nfor var i = 0, i < 100, i = i + 1\n"
                        "    var r = random()\n    if r < 0.0 or r >= 1.0\n        ok = false\nprintln(ok)"),
        "true\n");
}

TEST_CASE("floats: the maths built-ins are type checked")
{
    CHECK(diagnostic_codes("println(sqrt(\"x\"))") == Codes{"S010"});
    CHECK(diagnostic_codes("println(abs(\"x\"))") == Codes{"S026"});
    CHECK(diagnostic_codes("println(min(1, \"x\"))") == Codes{"S010"});
    CHECK(diagnostic_codes("println(round(2))").empty()); // an int widens to a float argument
}

TEST_CASE("floats: a decimal literal that is too large is a lexical error")
{
    const std::string digits(320, '9');
    CHECK(diagnostic_codes("var x = " + digits + "." + digits) == Codes{"L009"});
}
