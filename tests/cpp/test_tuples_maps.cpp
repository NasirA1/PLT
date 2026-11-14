// Tuples and maps (associative arrays): parsing, type checking, code generation and execution.
#include "helpers.hpp"
#include "minitest.hpp"
#include "thunder/bytecode.hpp"
#include "thunder/compiler.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/value.hpp"

using namespace thunder;
using test::diagnostic_codes;
using test::parse_expr;
using test::parse_program;
using test::run_output;
using Codes = std::vector<std::string>;

TEST_CASE("tuples: literals, types, element access and unpacking")
{
    CHECK_EQ(parse_expr("(1, \"a\", true)"), "(Tuple (Int 1) (String \"a\") (Bool true))");
    CHECK_EQ(parse_expr("(1)"), "(Int 1)"); // parentheses alone group an expression
    CHECK_EQ(parse_expr("p.0.1"), "(TupleIndex 1 (TupleIndex 0 (Name p)))");
    CHECK_EQ(parse_program("var (a, b) = pair"), "(Program (Var (a, b) (Name pair)))");
    CHECK_EQ(parse_program("var t: (int, string[])\nfunc f(): (int, int)\n    return (1, 2)"),
        "(Program (Var t) (Function f (Block (Return (Tuple (Int 1) (Int 2))))))");
    CHECK(diagnostic_codes("var t: (int) = 1") == Codes{"P015"});
    CHECK(diagnostic_codes("var (a) = 1") == Codes{"P015"});
    CHECK(diagnostic_codes("var p = (1, 2)\np.0 = 3") == Codes{"P005"}); // tuples are immutable

    CHECK_EQ(run_output("var pair = (1, \"one\")\nprintln(pair, \" \", pair.0, \" \", pair.1, \" \", len(pair))"),
        "(1, \"one\") 1 one 2\n");
    CHECK_EQ(run_output("var (n, name) = (7, \"x\")\nprintln(n + 1, name)"), "8x\n");
    CHECK_EQ(run_output("func min_max(a: int, b: int): (int, int)\n    if a < b\n        return (a, b)\n    return (b, a)\n"
                        "var (lo, hi) = min_max(9, 4)\nprintln(lo, \" \", hi, \" \", (lo, hi) == (4, 9), \" \", (1, 2) != (1, 2))"),
        "4 9 true false\n");
    CHECK_EQ(run_output("var t: (int, (string, bool))\nprintln(t, \" \", t.1.1, \" \", str(t) == \"(0, (\\\"\\\", false))\")"),
        "(0, (\"\", false)) false true\n");
    CHECK_EQ(run_output("var a = ([1], 2)\nvar b = copy(a)\na.0[0] = 9\nprintln(a, \" \", b, \" \", a == b)"),
        "([9], 2) ([1], 2) false\n");
    CHECK_EQ(run_output("var items: (int, string)[] = [(1, \"a\"), (2, \"b\")]\nprintln(items[1].1, len(items))"), "b2\n");
}

TEST_CASE("tuples: semantic errors")
{
    CHECK(diagnostic_codes("var p = (1, 2)\nprintln(p.2)") == Codes{"S042"});
    CHECK(diagnostic_codes("var x = 1\nprintln(x.0)") == Codes{"S041"});
    CHECK(diagnostic_codes("var (a, b) = 3") == Codes{"S044"});
    CHECK(diagnostic_codes("var (a, b) = (1, 2, 3)") == Codes{"S044"});
    CHECK(diagnostic_codes("var t: (int, string) = (1, 2)") == Codes{"S005"});
    CHECK(diagnostic_codes("var ok = (1, \"a\") == (1, 2)") == Codes{"S006"});
    CHECK(diagnostic_codes("var p = (1, 2)\nprintln(p[0])") == Codes{"S018"});
    CHECK(diagnostic_codes("func f(): (int, int)\n    return 1") == Codes{"S027"});
    CHECK(diagnostic_codes("struct S\n    pair: (int, S)") == Codes{"S030"}); // no cycles through tuples either
}

TEST_CASE("maps: literals, indexing, built-ins and display")
{
    CHECK_EQ(parse_expr("{\"a\": 1, \"b\": 2}"), "(Map (String \"a\") (Int 1) (String \"b\") (Int 2))");
    CHECK_EQ(parse_program("var m: {string: int} = {}"), "(Program (Var m (Map)))");
    CHECK_EQ(run_output("var ages = {\"nasir\": 40, \"sara\": 7}\nages[\"ali\"] = 12\nages[\"sara\"] = 8\n"
                        "println(ages, \" \", ages[\"nasir\"], \" \", len(ages), \" \", has(ages, \"sara\"), \" \", has(ages, \"x\"))\n"
                        "println(keys(ages), \" \", values(ages))\nremove(ages, \"nasir\")\nremove(ages, \"nobody\")\nprintln(ages)"),
        "{\"nasir\": 40, \"sara\": 8, \"ali\": 12} 40 3 true false\n[\"nasir\", \"sara\", \"ali\"] [40, 8, 12]\n{\"sara\": 8, \"ali\": 12}\n");
    CHECK_EQ(run_output("var empty: {string: int} = {}\nvar m: {string: int}\nprintln(empty, len(m), \" \", {1: true, 1: false})"),
        "{}0 {1: false}\n");
    CHECK_EQ(run_output("var nested: {int: (string, bool)[]} = {1: [(\"a\", true)]}\nprintln(nested[1][0].0, \" \", nested)"),
        "a {1: [(\"a\", true)]}\n");
    CHECK_EQ(run_output("var by_flag = {true: \"yes\", false: \"no\"}\nprintln(by_flag[1 > 0], by_flag[false])"), "yesno\n");
    // Maps are reference types: copy() makes an independent one.
    CHECK_EQ(run_output("var a = {\"k\": [1]}\nvar b = a\nvar c = copy(a)\nb[\"k\"][0] = 2\nprintln(a, b, c)"),
        "{\"k\": [2]}{\"k\": [2]}{\"k\": [1]}\n");
    CHECK_EQ(run_output("var m = {\"a\": 1}\nprintln(m[\"b\"])"), "ERROR R011: key \"b\" is not in the map");
}

TEST_CASE("maps: semantic errors")
{
    CHECK(diagnostic_codes("var e = {}") == Codes{"S038"});
    CHECK(diagnostic_codes("var m = {[1]: 2}") == Codes{"S039"});
    CHECK(diagnostic_codes("var m: {bool[]: int} = {}") == Codes{"S039"});
    CHECK(diagnostic_codes("var m = {1: \"a\", \"b\": 2}") == (Codes{"S040", "S040"}));
    CHECK(diagnostic_codes("var m = {\"k\": 1}\nprintln(m[true])") == Codes{"S043"});
    CHECK(diagnostic_codes("var m = {\"k\": 1}\nm[\"k\"] = \"v\"") == Codes{"S005"});
    CHECK(diagnostic_codes("var m = {\"k\": 1}\nremove(m, 3)") == Codes{"S043"});
    CHECK(diagnostic_codes("println(has([1], 2))") == Codes{"S026"});
    CHECK(diagnostic_codes("println(keys(\"s\"))") == Codes{"S026"});
    CHECK(diagnostic_codes("var m = {\"k\": 1}\nvar b = m == m") == Codes{"S006"});
    CHECK(diagnostic_codes("var m: {string: int} = {}\nvar k: string[] = keys(m)\nvar v: int[] = values(m)\nprintln(k, v)").empty());
}

TEST_CASE("tuples and maps: bytecode, verifier and runtime values")
{
    using namespace thunder::bytecode;
    const Program program = test::compile_ok("var m = {\"a\": (1, 2)}\nvar (x, y) = m[\"a\"]\nprintln(x + y)");
    const std::string listing = disassemble_text(program);
    CHECK_CONTAINS(listing, "MAKE_TUPLE     2");
    CHECK_CONTAINS(listing, "MAKE_MAP       1");
    CHECK_CONTAINS(listing, "TUPLE_GET      1          ; element #1");
    const auto loaded = deserialize(serialize(program));
    REQUIRE(loaded.has_value());
    CHECK(!verify(*loaded).has_value());

    const Value tuple = Value::from_tuple({Value::from_int(1), Value::from_string("s")});
    CHECK_EQ(tuple.to_debug(), "(1, \"s\")");
    CHECK_EQ(tuple.inspect(), "(1, \"s\")");
    CHECK_EQ(std::string(value_kind_name(tuple.kind())), "tuple");
    const Value map = Value::from_map({{Value::from_string("k"), Value::from_int(1)}, {Value::from_bool(true), Value::from_int(2)}});
    CHECK_EQ(map.to_display(), "{\"k\": 1, true: 2}");
    CHECK_EQ(std::string(value_kind_name(map.kind())), "map");
    CHECK(map.as_map().find(Value::from_int(7)) == nullptr);
    CHECK(!map.as_map().erase(Value::from_int(7)));
    CHECK(map.as_map().erase(Value::from_string("k")));
    CHECK_EQ(map.to_display(), "{true: 2}");
    CHECK(map == map);
    CHECK(!(map == map.deep_copy()));
    CHECK_EQ(MapObject::encode(Value::from_array({})), "?");
    const DisplayLocale& pashto = pashto_pack().display;
    CHECK_EQ(Value::from_map({{Value::from_string("a"), Value::from_int(1)}}).to_display(pashto), "{«a»: ۱}");
}

TEST_CASE("tuples and maps: Pashto names and messages")
{
    const SourceFile source("p.th", "# ژبه: ps\nمتغیر م = {\"a\": ۱}\nکرښه چاپ(لري(م, \"a\"), کیلۍ(م), ارزښتونه(م))\nلیرې کړه(م, \"a\")\nکرښه چاپ(م, اوږدوالی((۱, ۲)))\n");
    const CompileResult result = compile(source);
    REQUIRE(result.succeeded());
    const SourceFile bad("p.th", "# ژبه: ps\nمتغیر م = {}\n");
    const CompileResult failed = compile(bad);
    REQUIRE(failed.diagnostics.items().size() == 1);
    CHECK_EQ(failed.diagnostics.items()[0].code, "S038");
    CHECK_CONTAINS(failed.diagnostics.items()[0].message, "نقشه");
}
