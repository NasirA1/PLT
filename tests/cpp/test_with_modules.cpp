// Module blocks ("module geometry"), the built-in modules (math.sqrt, string.trim, ...), name_of,
// and the "with" statement that closes resources - in English and in Pashto, where "with" is
// written after the handle ("لاستی سره = ...") or, without one, at the end of the line ("ساکټ سره").
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/translate.hpp"

using namespace thunder;
using test::diagnostic_codes;
using test::run_output;
using Codes = std::vector<std::string>;

namespace
{
    const char* geometry = R"(module geometry
    struct Point
        x: int
        y: int

    enum Shape
        circle, square

    const ORIGIN_X = 0
    var made = 0

    func point(x: int, y: int): Point
        made += 1
        return Point(x, y)

    func taxicab(a: Point, b: Point): int
        return _distance(a.x, b.x) + _distance(a.y, b.y)

    func _distance(a: int, b: int): int
        return math.abs(a - b)
)";

    std::string with_geometry(const std::string& rest)
    {
        return std::string(geometry) + rest;
    }

    // A resource that says when it is opened and closed.
    const char* log_module = R"(module log
    struct Log
        name: string

    func open(name: string): Log
        println("open ", name)
        return Log(name)

    func close(l: Log)
        println("close ", l.name)
)";

    std::string with_log(const std::string& rest)
    {
        return std::string(log_module) + rest;
    }
}

TEST_CASE("module blocks: members are written with the module's name outside, without it inside")
{
    CHECK_EQ(run_output(with_geometry("var a = geometry.point(1, 2)\nvar b: geometry.Point = geometry.point(4, 6)\n"
                                      "println(geometry.taxicab(a, b), \" \", geometry.made, \" \", geometry.ORIGIN_X, \" \", "
                                      "geometry.Shape.square, \" \", geometry.Shape.count)")),
        "7 2 0 square 2\n");
    // Structs are made, enums named and variables assigned through the module, too.
    CHECK_EQ(run_output(with_geometry("var p = geometry.Point(y: 5, x: 3)\ngeometry.made = 10\nprintln(p, \" \", geometry.made, \" \", "
                                      "geometry.Shape(1), \" \", [geometry.Shape.circle])")),
        "geometry.Point(x: 3, y: 5) 10 square [geometry.Shape.circle]\n");
    // A function of a module is a value like any other, and knows its name.
    CHECK_EQ(run_output(with_geometry("var f = geometry.taxicab\nprintln(name_of(f), \" \", f(geometry.point(0, 0), geometry.point(1, 1)))")),
        "geometry.taxicab 2\n");
}

TEST_CASE("module blocks: the rules - private members, reserved names, one declaration, declarations only")
{
    CHECK_EQ(diagnostic_codes(with_geometry("println(geometry._distance(1, 2))")), Codes{"S066"});
    CHECK_EQ(diagnostic_codes(with_geometry("println(geometry.nope(1))")), Codes{"S070"});
    CHECK_EQ(diagnostic_codes(with_geometry("println(taxicab(geometry.point(0, 0), geometry.point(1, 1)))")), Codes{"S067"});
    CHECK_EQ(diagnostic_codes(with_geometry("var geometry = 1")), Codes{"S065"});
    CHECK_EQ(diagnostic_codes(with_geometry("func f(geometry: int): int\n    return geometry")), Codes{"S065"});
    CHECK_EQ(diagnostic_codes(with_geometry("module geometry\n    const A = 1")), Codes{"S064"});
    CHECK_EQ(diagnostic_codes("module math\n    const A = 1"), Codes{"S064"}); // a built-in module's name
    CHECK_EQ(diagnostic_codes("module m\n    println(1)"), Codes{"S047"});
    // A module's variable read before the module is declared: not declared yet, as for any global.
    CHECK_EQ(diagnostic_codes("println(counter.start)\nmodule counter\n    var start = 5"), Codes{"S001"});
    CHECK_EQ(diagnostic_codes("module a\n    module b\n        const C = 1"), Codes{"P017"});
    CHECK_EQ(diagnostic_codes("func f()\n    module a\n        const C = 1"), Codes{"P017"});
}

TEST_CASE("module blocks: names inside a module - locals hide members, built-ins stay built-ins")
{
    // stack.push is the module's; plain push inside it is still the built-in.
    const char* program = R"(module stack
    struct Stack
        items: int[]

    func new(): Stack
        return Stack([])

    func push(s: Stack, value: int)
        push(s.items, value)

    func push_twice(s: Stack, value: int)
        stack.push(s, value)
        stack.push(s, value)

    func size(s: Stack): int
        var new = len(s.items)
        return new

var s = stack.new()
stack.push_twice(s, 7)
println(s.items, " ", stack.size(s))
)";
    CHECK_EQ(run_output(program), "[7, 7] 2\n");
}

TEST_CASE("module blocks: generic members, and modules in imported files")
{
    const char* program = R"(module box
    struct Box<T>
        value: T

    func of<T>(value: T): Box<T>
        return Box(value)

    func get<T>(b: Box<T>): T
        return b.value

var b: box.Box<string> = box.of("hi")
println(box.get(b), " ", box.get(box.of(3)))
)";
    CHECK_EQ(run_output(program), "hi 3\n");
}

TEST_CASE("module blocks: the built-in modules")
{
    CHECK_EQ(run_output("println(math.sqrt(16.0), \" \", string.trim(\"  a \"), \" \", random.int(4, 4), \" \", "
                        "bytes.to_string(bytes.from_string(\"ok\")), \" \", string.format(\"{x}\", 255))"),
        "4.0 a 4 ok 0xff\n");
    // The modules that reach outside the program are imported first.
    CHECK_EQ(diagnostic_codes("println(os.time() > 0)"), Codes{"S049"});
    CHECK_EQ(diagnostic_codes("println(file.exists(\"x\"))"), Codes{"S049"});
    CHECK(diagnostic_codes("import file\nprintln(file.exists(\"x\"))").empty());
    // A built-in of a module, written without it, says where it lives.
    CHECK_EQ(diagnostic_codes("println(sqrt(2.0))"), Codes{"S067"});
    CHECK_EQ(diagnostic_codes("println(math.nope(2.0))"), Codes{"S070"});
    // The default module needs no prefix and is not a module: len, push, map ...
    CHECK_EQ(run_output("var a = [3, 1]\npush(a, 2)\nprintln(sort(a), \" \", len(a))"), "[1, 2, 3] 3\n");
    // random.int and random.float: a type word after the dot is a member's name.
    CHECK_EQ(run_output("random.seed(1)\nvar f = random.float()\nprintln(f >= 0.0 and f < 1.0)"), "true\n");
    // Module names are reserved like the built-ins' names.
    CHECK_EQ(diagnostic_codes("var math = 1"), Codes{"S065"});
    CHECK(diagnostic_codes("var file = 1").empty()); // until "import file", file is an ordinary name
}

TEST_CASE("module blocks: name_of")
{
    CHECK_EQ(run_output("func double(n: int): int\n    return n * 2\nprintln(name_of(double))"), "double\n");
    CHECK_EQ(run_output("var f: func(int): int\nprintln(\"[\", name_of(f), \"]\")"), "[]\n");
    CHECK_EQ(diagnostic_codes("println(name_of(3))"), Codes{"S026"});
}

TEST_CASE("module blocks: in Pashto, with every keyboard's spelling of the keyword")
{
    const char* program = "# ژبه: ps\nټولګی هندسه\n    دنده دوه_ځله(ن: عدد): عدد\n        راګرځه ن * ۲\n"
                          "ټولگى شمېرې\n    ثابت لس = ۱۰\n"
                          "کرښه چاپ(هندسه.دوه_ځله(شمېرې.لس)، \" \"، ریاضي.جذر(۹٫۰)، \" \"، متن.لوی توري کړه(\"ab\"))\n";
    CHECK_EQ(run_output(program), "۲۰ ۳٫۰ AB\n");
}

TEST_CASE("module blocks: translation writes each module and its members in the other language")
{
    const SourceFile english("e.th", "module geometry\n    func area(r: float): float\n        return math.pi() * r * r\n"
                                     "println(geometry.area(1.0), string.trim(\" x \"), random.int(1, 2))\n");
    const TranslationResult pashto = translate(english, english_pack(), pashto_pack());
    REQUIRE(pashto.ok);
    CHECK_CONTAINS(pashto.text, "ټولګی geometry");
    CHECK_CONTAINS(pashto.text, "ریاضي.پای()");
    CHECK_CONTAINS(pashto.text, "متن.پاک کړه(\" x \")");
    CHECK_CONTAINS(pashto.text, "تصادفي.عدد(۱، ۲)");
    CHECK_CONTAINS(pashto.text, "geometry.area(۱٫۰)"); // the program's own names stay as they are
    const TranslationResult back = translate(SourceFile("p.th", pashto.text), english_pack(), english_pack());
    REQUIRE(back.ok);
    CHECK_CONTAINS(back.text, "return math.pi() * r * r");
    CHECK_CONTAINS(back.text, "println(geometry.area(1.0), string.trim(\" x \"), random.int(1, 2))");
}

TEST_CASE("with: the resource is closed at the end of its block, innermost first")
{
    CHECK_EQ(run_output(with_log("with a = log.open(\"a\")\n    with b = log.open(\"b\")\n        println(\"inside\")\nprintln(\"after\")")),
        "open a\nopen b\ninside\nclose b\nclose a\nafter\n");
    // "with value": a resource made earlier, without a name of its own.
    CHECK_EQ(run_output(with_log("var l = log.open(\"l\")\nwith l\n    println(\"use\")\nprintln(\"end\")")),
        "open l\nuse\nclose l\nend\n");
    // nothing is not closed.
    CHECK_EQ(run_output(with_log("var none: log.Log = nothing\nwith none\n    println(\"empty\")")), "empty\n");
}

TEST_CASE("with: return, break and continue close what they leave, and only that")
{
    const std::string program = with_log(R"(func find(numbers: int[]): int
    with l = log.open("find")
        for n in numbers
            if n < 0
                return n * 10
    return 0

println(find([1, -2, 3]))
for i in range(0, 4)
    with l = log.open(str(i))
        if i == 1
            continue
        if i == 2
            break
        println("body ", i)
with outer = log.open("outer")
    for i in range(0, 2)
        with inner = log.open("inner")
            if i == 0
                continue
            break
    println("still open: outer")
)");
    CHECK_EQ(run_output(program), "open find\nclose find\n-20\nopen 0\nbody 0\nclose 0\nopen 1\nclose 1\nopen 2\nclose 2\n"
                                  "open outer\nopen inner\nclose inner\nopen inner\nclose inner\nstill open: outer\nclose outer\n");
}

TEST_CASE("with: what can be a resource")
{
    // A value that is not a struct (an int handle) is closed by the module of the function that made it.
    const char* handles = "module handle\n    var open_count = 0\n    func open(): int\n        open_count += 1\n        return open_count\n"
                          "    func close(h: int)\n        println(\"closed \", h)\nwith h = handle.open()\n    println(\"using \", h)\n"
                          "println(\"done\")\n";
    CHECK_EQ(run_output(handles), "using 1\nclosed 1\ndone\n");
    // A plain variable holding such a handle says nothing about whose it is.
    CHECK_EQ(diagnostic_codes("module handle\n    func open(): int\n        return 1\n    func close(h: int)\n        pass\n"
                              "var kept = handle.open()\nwith kept\n    pass\n"), Codes{"S068"});
    CHECK_EQ(diagnostic_codes("with x = 5\n    pass"), Codes{"S068"}); // no module made it
    CHECK_EQ(diagnostic_codes("with x = math.abs(5)\n    pass"), Codes{"S068"}); // a built-in module has no close
    CHECK_EQ(diagnostic_codes("struct Plain\n    n: int\nwith p = Plain(1)\n    pass"), Codes{"S069"});
    // A struct of the program itself is closed by a close function beside it, at the top level.
    CHECK_EQ(run_output("struct Lock\n    n: int\nfunc close(l: Lock)\n    println(\"unlocked \", l.n)\nwith l = Lock(3)\n    println(\"locked\")"),
        "locked\nunlocked 3\n");
    // The name is the block's: it cannot be pointed at another resource.
    CHECK_EQ(diagnostic_codes(with_log("with l = log.open(\"a\")\n    l = log.open(\"b\")")), Codes{"S012"});
    // A generic function's resources are closed in every instance.
    CHECK_EQ(run_output(with_log("func twice<T>(value: T): T\n    with l = log.open(str(value))\n        return value\n"
                                 "println(twice(1), twice(\"x\"))")),
        "open 1\nclose 1\nopen x\nclose x\n1x\n");
}

TEST_CASE("with: Pashto writes it after the handle or at the end of the line, and the translator moves it")
{
    const char* pashto = "# ژبه: ps\nټولګی یادښت\n    جوړښت یادښت\n        نوم: متن\n"
                         "    دنده پرانیزه(نوم: متن): یادښت\n        کرښه چاپ(\"پرانیستل \"، نوم)\n        راګرځه یادښت(نوم)\n"
                         "    دنده وتړه(ی: یادښت)\n        کرښه چاپ(\"تړل \"، ی.نوم)\n"
                         "ی سره = یادښت.پرانیزه(\"الف\")\n    کرښه چاپ(\"دننه\")\n"
                         "یادښت.پرانیزه(\"ب\") سره\n    کرښه چاپ(\"دننه\")\n";
    CHECK_EQ(run_output(pashto), "پرانیستل الف\nدننه\nتړل الف\nپرانیستل ب\nدننه\nتړل ب\n");
    // In the other place, each language says where the keyword goes - and understands it anyway.
    CHECK_EQ(diagnostic_codes(with_log("l = log.open(\"a\") with\n    pass")), Codes{"P018"});
    CHECK_EQ(diagnostic_codes("# ژبه: ps\nسره x = ۱\n    تېر\n"), Codes{"P018"}); // (a syntax error: types are not checked)
    CHECK_EQ(diagnostic_codes("# ژبه: ps\nx = ۱ سره\n    تېر\n"), Codes{"P018"}); // the keyword belongs after the handle
    // A bare value without a handle: the keyword ends the line (the handle form needs "=" after it).
    CHECK_EQ(run_output("# ژبه: ps\nټولګی ی\n    جوړښت ی\n        ن: عدد\n    دنده وتړه(س: ی)\n        کرښه چاپ(\"تړل\")\nمتغیر الف = ی.ی(۱)\nالف سره\n    کرښه چاپ(\"دننه\")\n"),
        "دننه\nتړل\n");

    const SourceFile english("e.th", with_log("with l = log.open(\"a\")\n    println(\"in\")\n"));
    const TranslationResult to_pashto = translate(english, english_pack(), pashto_pack());
    REQUIRE(to_pashto.ok);
    CHECK_CONTAINS(to_pashto.text, "\nl سره = log.open(\"a\")\n    کرښه چاپ(\"in\")");
    CHECK_EQ(run_output(to_pashto.text), "open a\nin\nclose a\n"); // close is understood in every language
    const TranslationResult back = translate(SourceFile("p.th", to_pashto.text), pashto_pack(), english_pack());
    REQUIRE(back.ok);
    CHECK_CONTAINS(back.text, "\nwith l = log.open(\"a\")\n    println(\"in\")");
}
