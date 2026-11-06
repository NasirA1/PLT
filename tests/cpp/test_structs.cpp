#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/value.hpp"

using test::diagnostic_codes;
using test::run_output;
using Codes = std::vector<std::string>;

namespace
{
    const char* const point = "struct Point\n    x: int\n    y: int\n";

    std::string with_point(const std::string& code)
    {
        return std::string(point) + code;
    }
}

TEST_CASE("structs: construction, field access and display")
{
    CHECK_EQ(run_output(with_point("var p = Point(3, 4)\nprintln(p.x + p.y, \" \", p)")), "7 Point(x: 3, y: 4)\n");
    CHECK_EQ(run_output(with_point("var p = Point(y: 2, x: 1)\nprintln(p)")), "Point(x: 1, y: 2)\n");
    CHECK_EQ(run_output(with_point("var p = Point(y: 9)\nprintln(p)")), "Point(x: 0, y: 9)\n");
    CHECK_EQ(run_output(with_point("var p: Point\nprintln(p, \" \", str(p))")), "Point(x: 0, y: 0) Point(x: 0, y: 0)\n");
}

TEST_CASE("structs: fields can be assigned, including nested fields and array elements")
{
    const char* program = R"(
struct Point
    x: int
    y: int
struct Shape
    name: string
    corners: Point[]
    origin: Point
var s = Shape(name: "square", corners: [Point(0, 0), Point(1, 1)])
s.origin.x = 5
s.corners[1].y = 7
s.name = s.name + "!"
println(s)
)";
    CHECK_EQ(run_output(program),
        "Shape(name: \"square!\", corners: [Point(x: 0, y: 0), Point(x: 1, y: 7)], origin: Point(x: 5, y: 0))\n");
}

TEST_CASE("structs: are shared by reference; copy() makes an independent copy")
{
    const char* program = R"(
struct Box
    items: int[]
    count: int
func fill(b: Box)
    b.items = [1, 2, 3]
    b.count = 3
var a = Box(items: [], count: 0)
var alias = a
fill(alias)
var c = copy(a)
c.items[0] = 99
c.count = 1
println(a, " ", c)
var grid = array(2, Box([0], 0))
grid[0].items[0] = 5
println(grid)
)";
    CHECK_EQ(run_output(program),
        "Box(items: [1, 2, 3], count: 3) Box(items: [99, 2, 3], count: 1)\n[Box(items: [5], count: 0), Box(items: [0], count: 0)]\n");
}

TEST_CASE("structs: named arguments are evaluated left to right")
{
    const char* program = R"(
struct Pair
    a: int
    b: int
var order = ""
func mark(name: string, value: int): int
    order = order + name
    return value
var p = Pair(b: mark("b", 2), a: mark("a", 1))
println(order, " ", p)
)";
    CHECK_EQ(run_output(program), "ba Pair(a: 1, b: 2)\n");
}

TEST_CASE("structs: inspect and dump show fields with their types")
{
    const char* program = R"(
struct Point
    x: int
    y: int
struct Line
    from: Point
    to: Point
    label: string
    weights: int[]
var line = Line(Point(1, 2), Point(3, 4), "edge", [5])
var text = inspect(line)
dump(line.from)
println(len(text) > 0, " ", inspect(42), " ", inspect("s"))
dump(line)
)";
    CHECK_EQ(run_output(program),
        "Point\n    x: int = 1\n    y: int = 2\n"
        "true 42 \"s\"\n"
        "Line\n    from: Point\n        x: int = 1\n        y: int = 2\n"
        "    to: Point\n        x: int = 3\n        y: int = 4\n"
        "    label: string = \"edge\"\n    weights: int[] = [5]\n");
}

TEST_CASE("structs: functions can take and return structs; structs can be declared after use")
{
    const char* program = R"(
func midpoint(a: Point, b: Point): Point
    return Point((a.x + b.x) / 2, (a.y + b.y) / 2)
println(midpoint(Point(0, 0), Point(4, 6)))
struct Point
    x: int
    y: int
)";
    CHECK_EQ(run_output(program), "Point(x: 2, y: 3)\n");
}

TEST_CASE("structs: semantic errors")
{
    CHECK(diagnostic_codes("struct A\n    x: int\nstruct A\n    y: int") == Codes{"S028"});
    CHECK(diagnostic_codes("struct print\n    x: int") == Codes{"S028"});
    CHECK(diagnostic_codes("struct A\n    x: int\nfunc A()\n    pass") == Codes{"S028"});
    CHECK(diagnostic_codes("struct A\n    x: int\nvar A = 1") == Codes{"S028"});
    CHECK(diagnostic_codes("struct A\n    x: int, x: bool") == Codes{"S029"});
    CHECK(diagnostic_codes("struct A\n    next: A") == Codes{"S030"});
    CHECK(diagnostic_codes("struct A\n    b: B\nstruct B\n    list: A[]").size() == 1);
    CHECK(diagnostic_codes("struct A\n    b: Missing") == Codes{"S031"});
    CHECK(diagnostic_codes("var p: Missing") == Codes{"S031"});
    CHECK(diagnostic_codes("func f(p: Missing)\n    pass") == Codes{"S031"});
    CHECK(diagnostic_codes("func f(): Missing\n    pass") == Codes{"S031"});
    CHECK(diagnostic_codes(with_point("var p = Point(1, 2)\nprintln(p.z)")) == Codes{"S032"});
    CHECK(diagnostic_codes(with_point("var p = Point(z: 1)")) == Codes{"S032"});
    CHECK(diagnostic_codes("func f(a: int)\n    pass\nf(a: 1)") == Codes{"S033"});
    CHECK(diagnostic_codes("println(x: 1)") == Codes{"S033"});
    CHECK(diagnostic_codes(with_point("var p = Point(x: 1, 2)")) == Codes{"S034"});
    CHECK(diagnostic_codes(with_point("var p = Point(x: 1, x: 2)")) == Codes{"S035"});
    CHECK(diagnostic_codes(with_point("var p = Point")) == Codes{"S036"});
    CHECK(diagnostic_codes("var n = 5\nprintln(n.x)") == Codes{"S037"});
    CHECK(diagnostic_codes(with_point("var p = Point(1)")) == Codes{"S009"});
    CHECK(diagnostic_codes(with_point("var p = Point(1, \"2\")")) == Codes{"S005"});
    CHECK(diagnostic_codes(with_point("var p = Point(1, 2)\np.x = true")) == Codes{"S005"});
    CHECK(diagnostic_codes(with_point("var b = Point(1, 2) == Point(1, 2)")) == Codes{"S006"});
    CHECK(diagnostic_codes(with_point("const p = Point(1, 2)\np.x = 3")).empty()); // the binding is constant, not the value
}

TEST_CASE("structs: built-in copy, inspect and dump are type checked")
{
    CHECK(diagnostic_codes(with_point("var p: Point = copy(Point(1, 2))\nvar s: string = inspect(p)\ndump(p)")).empty());
    CHECK(diagnostic_codes("var x = dump(1)") == Codes{"S022"});
    CHECK(diagnostic_codes("var a: int[] = copy([])").empty());
    CHECK(diagnostic_codes("copy()") == Codes{"S009"});
}

TEST_CASE("structs: the symbol model records struct layouts")
{
    using namespace thunder;
    const SourceFile source("t.th", with_point("struct Path\n    points: Point[]"));
    const CompileResult result = compile(source);
    REQUIRE(result.model != nullptr);
    REQUIRE(result.model->structs.size() == 2);
    const StructInfo& path = *result.model->structs[1];
    CHECK_EQ(path.name, "Path");
    CHECK_EQ(path.field_index("points"), 0);
    CHECK_EQ(path.field_index("nope"), -1);
    CHECK_EQ(path.fields[0].type.to_string(), "Point[]");
    REQUIRE(result.program.has_value());
    REQUIRE(result.program->structs.size() == 2);
    CHECK_EQ(result.program->structs[1].field_types[0], "Point[]");
}

TEST_CASE("structs: bytecode round trip and disassembly")
{
    using namespace thunder::bytecode;
    const Program program = test::compile_ok(with_point("var p = Point(y: 1, x: 2)\np.x = p.y\nprintln(p)"));
    const auto loaded = deserialize(serialize(program));
    REQUIRE(loaded.has_value());
    CHECK(loaded->structs == program.structs);
    CHECK_EQ(loaded->language, "en");
    const std::string listing = disassemble_text(program);
    CHECK_CONTAINS(listing, "; structs (1)");
    CHECK_CONTAINS(listing, "$0 Point { #0 x: int, #1 y: int }");
    CHECK_CONTAINS(listing, "NEW_STRUCT     0          ; Point");
    CHECK_CONTAINS(listing, "DUP");
    CHECK_CONTAINS(listing, "SET_FIELD      1          ; field #1");
    CHECK_CONTAINS(listing, "GET_FIELD      1          ; field #1");
}

TEST_CASE("structs: the verifier and the VM reject malformed struct code")
{
    using namespace thunder;
    using namespace thunder::bytecode;
    const auto op = [](Opcode o) { return static_cast<std::uint8_t>(o); };

    Program program;
    Function main;
    main.name = "<main>";
    main.code = {op(Opcode::NewStruct), 0, 0, op(Opcode::Halt)};
    program.functions.push_back(main);
    CHECK_CONTAINS(*verify(program), "struct $0 does not exist");

    program.structs.push_back(StructLayout{"P", {"x"}, {"int"}});
    program.functions[0].code = {op(Opcode::GetField), 3, 0, op(Opcode::Halt)};
    CHECK_CONTAINS(*verify(program), "field #3 does not exist in any struct");

    program.structs[0].field_types.clear();
    program.functions[0].code = {op(Opcode::Halt)};
    CHECK_CONTAINS(*verify(program), "inconsistent field information");
    program.structs[0].field_types = {"int"};

    const auto run_code = [&](std::vector<std::uint8_t> code)
    {
        program.functions[0].code = std::move(code);
        std::ostringstream out;
        Vm vm(program, out);
        const ExecResult result = vm.run();
        return result.error ? result.error->message : std::string("ok");
    };
    CHECK_CONTAINS(run_code({op(Opcode::NewStruct), 0, 0, op(Opcode::Halt)}), "operand stack underflow");
    CHECK_CONTAINS(run_code({op(Opcode::True), op(Opcode::GetField), 0, 0, op(Opcode::Halt)}), "GET_FIELD expects");
    CHECK_CONTAINS(run_code({op(Opcode::True), op(Opcode::True), op(Opcode::SetField), 0, 0, op(Opcode::Halt)}),
        "SET_FIELD expects");
    CHECK_CONTAINS(run_code({op(Opcode::Dup), op(Opcode::Halt)}), "operand stack underflow");
    CHECK_EQ(run_code({op(Opcode::True), op(Opcode::NewStruct), 0, 0, op(Opcode::Dup), op(Opcode::GetField), 0, 0,
                 op(Opcode::Pop), op(Opcode::Pop), op(Opcode::Halt)}),
        "ok");
}

TEST_CASE("values: struct values")
{
    using namespace thunder;
    const StructLayout layout{"P", {"name", "tags"}, {"string", "int[]"}};
    const Value p = Value::from_struct(&layout, {Value::from_string("a"), Value::from_array({Value::from_int(1)})});
    CHECK(p.is_struct());
    CHECK_EQ(value_kind_name(p.kind()), "struct");
    CHECK_EQ(p.to_display(), "P(name: \"a\", tags: [1])");
    CHECK_EQ(p.inspect(), "P\n    name: string = \"a\"\n    tags: int[] = [1]");
    CHECK(p == p);
    const Value q = p.deep_copy();
    CHECK(!(p == q));
    q.as_struct().fields[1].as_array().elements[0] = Value::from_int(7);
    CHECK_EQ(p.to_display(), "P(name: \"a\", tags: [1])");
}
