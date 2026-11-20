// Objects: methods, constructors, interfaces, mixins and closures.

#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/compiler.hpp"
#include "thunder/vm.hpp"

#include <sstream>
#include <string>
#include <vector>

using namespace thunder;
using test::diagnostic_codes;
using test::run_output;

using Codes = std::vector<std::string>;

namespace
{
    // The user-facing shape of the feature, straight from the design discussion.
    const char* button_program = R"(interface Widget
    func draw()
interface Clickable
    on_click: func()
mixin UsefulMixin
    func useful()
        println("useful")
struct Button: Widget, Clickable, UsefulMixin
    _title: string  # private; on_click comes from Clickable
    Button(title: string)
        _title = title
        useful()  # the mixin's method
    func get_title(): string
        return _title
    func draw()
        println("[", _title, "]")
)";
}

TEST_CASE("oop: methods and the constructor")
{
    CHECK_EQ(run_output("struct Point\n"
                        "    x: int\n    y: int\n\n"
                        "    Point(x_value: int, y_value: int)\n        x = x_value\n        y = y_value\n\n"
                        "    func length_squared(): int\n        return x * x + y * y\n\n"
                        "    func move(dx: int, dy: int)\n        x += dx\n        y += dy\n"
                        "var p = Point(3, 4)\n"
                        "println(p.length_squared())\n"
                        "p.move(1, -4)\n"
                        "println(p.x, \" \", p.y, \" \", p.length_squared())\n"),
        "25\n4 0 16\n");
    // A method may call another method; the receiver is the same value.
    CHECK_EQ(run_output("struct Counter\n"
                        "    count: int\n\n"
                        "    Counter()\n        count = 0\n\n"
                        "    func bump()\n        count += 1\n\n"
                        "    func bump_twice()\n        bump()\n        bump()\n"
                        "var c = Counter()\nc.bump_twice()\nc.bump()\nprintln(c.count)\n"),
        "3\n");
    // Without a constructor, a struct is still built from its fields (as before).
    CHECK_EQ(run_output("struct Pair\n    a: int\n    b: int\n\n    func sum(): int\n        return a + b\n"
                        "println(Pair(1, 2).sum(), \" \", Pair(b: 5, a: 4).sum())\n"),
        "3 9\n");
    // The constructor runs on a struct whose fields start at their defaults.
    CHECK_EQ(run_output("struct Partial\n    known: int\n    rest: string\n\n    Partial(known_value: int)\n"
                        "        known = known_value\n"
                        "var p = Partial(7)\nprintln(p.known, \" \\\"\", p.rest, \"\\\"\")\n"),
        "7 \"\"\n");
    // With a constructor, construction goes through it: field counts no longer apply.
    CHECK_EQ(diagnostic_codes("struct Partial\n    a: int\n    b: int\n\n    Partial(v: int)\n        a = v\n"
                              "var p = Partial(1, 2)\n"),
        Codes{"S009"});
    CHECK_EQ(diagnostic_codes("struct Partial\n    a: int\n\n    Partial(v: int)\n        a = v\n"
                              "var p = Partial(a: 1)\n"),
        Codes{"S033"}); // named arguments name fields, not constructor parameters
    // A constructor has no return type.
    CHECK_EQ(diagnostic_codes("struct Bad\n    a: int\n\n    Bad(): int\n        a = 1\n"), Codes{"S079"});
    // A member name is used once: a method cannot share a field's name, nor another method's.
    CHECK_EQ(diagnostic_codes("struct Twice\n    x: int\n\n    func x(): int\n        return 1\nprintln(Twice(1).x)\n"),
        Codes{"S080"});
    CHECK_EQ(diagnostic_codes("struct Twice\n    a: int\n\n    func f(): int\n        return 1\n\n"
                              "    func f(): int\n        return 2\nprintln(Twice(1).f())\n"),
        Codes{"S080"});
    // Methods are called, not read like fields.
    CHECK_EQ(diagnostic_codes("struct S\n    a: int\n\n    func f(): int\n        return a\n"
                              "var s = S(1)\nvar g = s.f\n"),
        Codes{"S078"});
}

TEST_CASE("oop: private members start with '_'")
{
    // Fields and methods ending in '_' are the struct's own business.
    CHECK_EQ(diagnostic_codes("struct Safe\n    _code: int\n\n    Safe(code: int)\n        _code = code\n"
                              "var s = Safe(7)\nprintln(s._code)\n"),
        Codes{"S076"});
    CHECK_EQ(diagnostic_codes("struct Safe\n    _code: int\n\n    Safe(code: int)\n        _code = code\n\n"
                              "    func _check(): bool\n        return _code > 0\n"
                              "var s = Safe(7)\nprintln(s._check())\n"),
        Codes{"S076"});
    // Writing a private field from outside is just as private.
    CHECK_EQ(diagnostic_codes("struct Safe\n    _code: int\n\n    Safe(code: int)\n        _code = code\n"
                              "var s = Safe(7)\ns._code = 9\n"),
        Codes{"S076"});
    // Inside the struct's own methods they are plain members.
    CHECK_EQ(run_output("struct Safe\n    _code: int\n\n    Safe(code: int)\n        _code = code\n\n"
                        "    func reveal(): int\n        return _code\n"
                        "println(Safe(7).reveal())\n"),
        "7\n");
}

TEST_CASE("oop: interfaces dispatch to the struct behind the value")
{
    CHECK_EQ(run_output("interface Shape\n    func area(): float\n"
                        "struct Square: Shape\n    side: float\n\n    func area(): float\n        return side * side\n"
                        "struct Circle: Shape\n    r: float\n\n    func area(): float\n        return 3.0 * r * r\n"
                        "var shapes: Shape[] = [Square(2.0), Circle(1.0)]\n"
                        "for shape in shapes\n    println(shape.area())\n"),
        "4.0\n3.0\n");
    // A closure-valued member ("on_click: func()") satisfies an interface and is called
    // through it; the user-facing Button example, end to end.
    CHECK_EQ(run_output(std::string(button_program)
                        + "var b = Button(\"OK\")\nprintln(b.get_title())\n"
                          "b.on_click = func() println(\"clicked \", b.get_title())\n"
                          "var w: Widget = b\nw.draw()\n"
                          "var c: Clickable = b\nc.on_click()\n"),
        "useful\nOK\n[OK]\nclicked OK\n");
    // nothing fits an interface variable, compares with it, and refuses method calls at run time.
    CHECK_EQ(run_output("interface Shape\n    func area(): float\n"
                        "var s: Shape = nothing\nprintln(s == nothing)\n"),
        "true\n");
    const auto missing = test::run("interface Shape\n    func area(): float\n"
                                   "var s: Shape = nothing\nprintln(s.area())\n");
    REQUIRE(missing.error.has_value());
    CHECK_EQ(missing.error->code, "R036");
    // A struct beside an interface it implements compares by identity.
    CHECK_EQ(run_output("interface I\n    func f(): int\n"
                        "struct A: I\n    n: int\n\n    func f(): int\n        return n\n"
                        "var a = A(1)\nvar i: I = a\nprintln(i == a, \" \", a == i)\n"),
        "true true\n");
}

TEST_CASE("oop: interface conformance is checked at the struct")
{
    CHECK_EQ(diagnostic_codes("interface Shape\n    func area(): float\n"
                              "struct Square: Shape\n    side: float\n"),
        Codes{"S074"});
    CHECK_EQ(diagnostic_codes("interface Shape\n    func area(): float\n"
                              "struct Square: Shape\n    side: float\n\n    func area(): int\n        return 1\n"),
        Codes{"S075"});
    // A closure member must be a field of exactly that function type.
    CHECK_EQ(diagnostic_codes("interface Clickable\n    on_click: func()\n"
                              "struct B: Clickable\n    on_click: func(int)\nvar b = B(nothing)\n"),
        (Codes{"S075", "S056"}));
    // Only interfaces and structs may follow ':'.
    CHECK_EQ(diagnostic_codes("enum Color\n    red\nstruct S: Color\n    n: int\n"), Codes{"S071"});
    // An interface value has no fields; its members are called.
    CHECK_EQ(diagnostic_codes("interface Clickable\n    on_click: func()\n"
                              "struct B: Clickable\n    on_click: func()\n"
                              "var c: Clickable = B(nothing)\nvar f = c.on_click\n"),
        (Codes{"S056", "S078"})); // S056: nothing cannot be the func() field's value here
    CHECK_EQ(diagnostic_codes("interface I\n    func f(): int\n"
                              "struct A: I\n    n: int\n\n    func f(): int\n        return n\n"
                              "var i: I = A(1)\nprintln(i.g())\n"),
        Codes{"S077"});
    // A plain struct is not an interface value.
    CHECK_EQ(diagnostic_codes("interface I\n    func f(): int\n"
                              "struct A\n    n: int\nvar i: I = A(1)\n"),
        Codes{"S005"});
}

TEST_CASE("oop: a mixin's fields and methods are copied in, one level deep")
{
    // Fields and methods come along; the struct's own method of the same name wins.
    CHECK_EQ(run_output("mixin Named\n    name: string\n\n    func describe(): string\n        return \"I am \" + name\n"
                        "struct Robot: Named\n    power: int\n\n    Robot(power_value: int)\n"
                        "        name = \"robot\"\n        power = power_value\n"
                        "struct Pirate: Named\n    ship: string\n\n    Pirate()\n"
                        "        name = \"pirate\"\n        ship = \"The Squid\"\n\n"
                        "    func describe(): string\n        return name + \" of \" + ship\n"
                        "println(Robot(9).describe())\nprintln(Pirate().describe())\n"),
        "I am robot\npirate of The Squid\n");
    // A mixin may implement an interface; the including struct inherits the promise.
    CHECK_EQ(run_output("interface Greeter\n    func greet(): string\n"
                        "mixin Friendly: Greeter\n    func greet(): string\n        return \"hello\"\n"
                        "struct Door: Friendly\n    id: int\n"
                        "var g: Greeter = Door(1)\nprintln(g.greet())\n"),
        "hello\n");
    // One level only: a mixin with a mixin of its own is refused (at the mixin).
    CHECK_EQ(diagnostic_codes("mixin A\n    func f(): int\n        return 1\n"
                              "mixin B: A\n    n: int\n"
                              "struct C: B\n    m: int\n"),
        Codes{"S072"});
    // A mixin cannot be generic, and an unknown name is neither interface nor mixin.
    CHECK_EQ(diagnostic_codes("mixin Box<T>\n    value: T\nstruct S: Box\n    n: int\n"), Codes{"S073"});
    CHECK_EQ(diagnostic_codes("struct S: Mystery\n    n: int\n"), Codes{"S071"});
}

TEST_CASE("oop: only a mixin can be included, and a mixin is not a type")
{
    // A plain struct after ':' is a type of its own, not a body to copy.
    CHECK_EQ(diagnostic_codes("struct Named\n    name: string\nstruct Robot: Named\n    power: int\n"), Codes{"S082"});
    CHECK_EQ(diagnostic_codes("struct Box<T>\n    value: T\nstruct S: Box\n    n: int\n"), Codes{"S082"});
    // A mixin is never built and never names a variable's type.
    CHECK_EQ(diagnostic_codes("mixin Named\n    name: string\nvar n: Named = nothing\n"), Codes{"S083"});
    CHECK_EQ(diagnostic_codes("mixin Named\n    name: string\nvar n = Named()\n"), Codes{"S083"});
    CHECK_EQ(diagnostic_codes("mixin Named\n    name: string\n    Named()\n        name = \"x\"\n"), Codes{"S084"});
    // A type name cannot be both a struct and a mixin.
    CHECK_EQ(diagnostic_codes("mixin Named\n    name: string\nstruct Named\n    n: int\n"), Codes{"S028"});
    // The Pashto keyword, with either gaf (the Pashto ګ or the Dari/Persian گ).
    for (const char* keyword : {"ګډون", "گډون"})
    {
        CHECK_EQ(run_output(std::string("# ژبه: ps\n") + keyword + " نوم_لرونکی\n    نوم: متن\n\n"
                            "    دنده ووایه(): متن\n        راګرځه \"زه \" + نوم\n"
                            "جوړښت روبوټ: نوم_لرونکی\n    ځواک: عدد\n"
                            "متغیر ر = روبوټ(نوم: \"ر\"، ځواک: ۱)\nکرښه چاپ(ر.ووایه())\n"),
            "زه ر\n");
    }
}

TEST_CASE("oop: an interface's closure members are part of every struct that implements it")
{
    // Written once, in the interface: the struct has the field without repeating it.
    CHECK_EQ(run_output("interface Clickable\n    on_click: func()\n"
                        "struct Button: Clickable\n    title: string\n"
                        "var clicks = 0\nvar b = Button(title: \"OK\")\n"
                        "b.on_click = func() clicks += 1\nb.on_click()\n"
                        "var c: Clickable = b\nc.on_click()\nprintln(clicks)\n"),
        "2\n");
    // With parameters and a result; through a mixin's interface; in a generic struct.
    CHECK_EQ(run_output("interface Scorer\n    score: func(int): int\n"
                        "mixin Scoring: Scorer\n    func twice(n: int): int\n        return score(score(n))\n"
                        "struct Game: Scoring\n    name: string\n"
                        "var g = Game(name: \"g\")\ng.score = func(n: int): int n + 1\nprintln(g.twice(5))\n"
                        "struct Box<T>: Scorer\n    value: T\n"
                        "var b: Box<int> = Box(value: 1)\nb.score = func(n: int): int n * 10\nprintln(b.score(4))\n"),
        "7\n40\n");
    // Writing it again is allowed, and must then match the interface.
    CHECK_EQ(diagnostic_codes("interface Clickable\n    on_click: func()\n"
                              "struct Button: Clickable\n    on_click: func()\n"),
        Codes{});
    CHECK_EQ(diagnostic_codes("interface Clickable\n    on_click: func()\n"
                              "struct Button: Clickable\n    on_click: func(int)\n"),
        Codes{"S075"});
}

TEST_CASE("oop: generic structs have methods and constructors per instance")
{
    CHECK_EQ(run_output("struct Box<T>\n"
                        "    _items: T[]\n\n"
                        "    Box()\n        _items = []\n\n"
                        "    func put(value: T)\n        push(_items, value)\n\n"
                        "    func count(): int\n        return len(_items)\n\n"
                        "    func last(): T\n        return _items[len(_items) - 1]\n"
                        "var a: Box<int> = Box()\na.put(4)\na.put(5)\n"
                        "var b: Box<string> = Box()\nb.put(\"x\")\n"
                        "println(a.count(), \" \", a.last(), \" \", b.count(), \" \", b.last())\n"),
        "2 5 1 x\n");
    // The private members of one instance are private outside the struct's own methods.
    CHECK_EQ(diagnostic_codes("struct Box<T>\n    _items: T[]\n\n    Box()\n        _items = []\n"
                              "var b: Box<int> = Box()\nprintln(len(b._items))\n"),
        Codes{"S076"});
    // A generic struct may promise an interface; every instance is checked.
    CHECK_EQ(run_output("interface Sized\n    func size(): int\n"
                        "struct Bag<T>: Sized\n    _items: T[]\n\n    Bag()\n        _items = []\n\n"
                        "    func put(value: T)\n        push(_items, value)\n\n"
                        "    func size(): int\n        return len(_items)\n"
                        "var bag: Bag<string> = Bag()\nbag.put(\"a\")\n"
                        "var sized: Sized = bag\nprintln(sized.size())\n"),
        "1\n");
}

TEST_CASE("oop: closures capture the variables around them")
{
    // The closure and its maker share the captured variable, both ways.
    CHECK_EQ(run_output("func make_counter(start: int): func(): int\n"
                        "    var count = start\n"
                        "    var bump = func(): int\n"
                        "        count += 1\n"
                        "        return count\n"
                        "    return bump\n"
                        "var a = make_counter(10)\nvar b = make_counter(100)\n"
                        "println(a(), \" \", a(), \" \", b(), \" \", a())\n"),
        "11 12 101 13\n");
    // Two closures over one variable see each other's writes.
    CHECK_EQ(run_output("func pair(): (func(), func(): int)\n"
                        "    var total = 0\n"
                        "    var add = func() total += 5\n"
                        "    var read = func(): int total\n"
                        "    return (add, read)\n"
                        "var (add, read) = pair()\nadd()\nadd()\nprintln(read())\n"),
        "10\n");
    // Nested function values hand the box inward.
    CHECK_EQ(run_output("func outer(): func(): int\n"
                        "    var x = 1\n"
                        "    var middle = func(): int\n"
                        "        var inner = func(): int\n"
                        "            x += 1\n"
                        "            return x\n"
                        "        return inner() + x\n"
                        "    return middle\n"
                        "var m = outer()\nprintln(m(), \" \", m())\n"),
        "4 6\n");
    // The inline form: one statement (or, with a return type, one expression) on the line.
    CHECK_EQ(run_output("var count = 0\n"
                        "var tick = func() count += 1\n"
                        "tick()\ntick()\nprintln(count)\n"),
        "2\n");
    // A closure may be passed where a function is expected (map, filter).
    CHECK_EQ(run_output("var factor = 3\n"
                        "println(map([1, 2, 3], func(n: int): int n * factor))\n"),
        "[3, 6, 9]\n");
    // A captured constant stays constant.
    CHECK_EQ(diagnostic_codes("func f(): func()\n    const k = 1\n    var g = func() k = 2\n    return g\n"),
        (Codes{"W001", "S012"})); // W001: k is written, never read
    // A function value still needs its body.
    CHECK_EQ(diagnostic_codes("var f = func()\nprintln(1)\n"), Codes{"P020"});
}

TEST_CASE("oop: closures survive the .thc file (serialize, verify, run)")
{
    const bytecode::Program program = test::compile_ok(
        "struct Greeter\n    _prefix: string\n\n    Greeter(prefix: string)\n        _prefix = prefix\n\n"
        "    func make(): func(string): string\n"
        "        var count = 0\n"
        "        var greet = func(name: string): string\n"
        "            count += 1\n"
        "            return _prefix + \" \" + name + \" #\" + str(count)\n"
        "        return greet\n"
        "var greet = Greeter(\"hi\").make()\nprintln(greet(\"ada\"))\nprintln(greet(\"alan\"))\n");
    const std::vector<std::uint8_t> bytes = bytecode::serialize(program);
    const auto loaded = bytecode::deserialize(bytes);
    REQUIRE(loaded.has_value());
    CHECK(!bytecode::verify(*loaded).has_value());
    std::ostringstream output;
    Vm vm(*loaded, output);
    const ExecResult result = vm.run();
    CHECK(!result.error.has_value());
    CHECK_EQ(output.str(), "hi ada #1\nhi alan #2\n");
}

TEST_CASE("oop: 'with' closes a struct through its close() method")
{
    CHECK_EQ(run_output("struct File\n    name: string\n\n    func close()\n        println(\"closed \", name)\n"
                        "with f = File(\"a\")\n    println(\"using \", f.name)\nprintln(\"done\")\n"),
        "using a\nclosed a\ndone\n");
    // Without a close() method a struct is not a resource.
    CHECK_EQ(diagnostic_codes("struct Plain\n    n: int\nwith p = Plain(1)\n    pass\n"), Codes{"S081"});
    // Methods of a generic struct close every instance.
    CHECK_EQ(run_output("struct Box<T>\n    value: T\n\n    func close()\n        println(\"closed \", value)\n"
                        "with a = Box(1)\n    pass\nwith b = Box(\"x\")\n    pass\n"),
        "closed 1\nclosed x\n");
}

TEST_CASE("oop: the right-to-left language declares the same objects")
{
    // برسېر is 'interface'; methods, constructors and closures read the same way.
    CHECK_EQ(run_output("# ژبه: ps\n"
                        "برسېر وسيله\n"
                        "    دنده وښيه()\n"
                        "جوړښت تڼۍ: وسيله\n"
                        "    سرليک_: متن\n"
                        "    تڼۍ(سرليک: متن)\n"
                        "        سرليک_ = سرليک\n"
                        "    دنده وښيه()\n"
                        "        کرښه چاپ(\"[\" + سرليک_ + \"]\")\n"
                        "متغیر ت = تڼۍ(\"سمه\")\n"
                        "متغیر و: وسيله = ت\n"
                        "و.وښيه()\n"),
        "[سمه]\n");
    // A Pashto closure captures its variable like the English one.
    CHECK_EQ(run_output("# ژبه: ps\n"
                        "متغیر شمېر = ۰\n"
                        "متغیر زیات = دنده() شمېر += ۱\n"
                        "زیات()\nزیات()\n"
                        "کرښه چاپ(شمېر)\n"),
        "۲\n");
}

TEST_CASE("oop: the syntax tree names the new parts")
{
    CHECK_EQ(test::parse_program("interface I\n    func f(): int\n    g: func()\n"),
        "(Program (Interface I (Method f) (Field g)))");
    CHECK_EQ(test::parse_program("struct S: I, M\n    a: int\n\n    S(v: int)\n        a = v\n\n"
                                 "    func f(): int\n        return a\n"),
        "(Program (Struct S (Base I) (Base M) (Field a) (Constructor S (Param v) (Block (Assign = (Name a) "
        "(Name v)))) (Method f (Block (Return (Name a))))))");
    CHECK_EQ(test::parse_expr("func(x: int): int x * 2"),
        "(Lambda (Param x) (Block (Return (Binary * (Name x) (Int 2)))))");
}

TEST_CASE("oop: a struct made without arguments starts with every field at its default")
{
    // Tally() as well as Tally(total: 0): no arguments at all leave every field at its default.
    CHECK_EQ(run_output("struct Tally\n    _total: int\n    names: string[]\n\n"
                        "    func add(n: int)\n        _total += n\n\n    func total(): int\n        return _total\n"
                        "var t = Tally()\nt.add(3)\nprintln(t.total(), \" \", len(t.names))\n"),
        "3 0\n");
    // Positional arguments still give every field.
    CHECK_EQ(diagnostic_codes("struct P\n    x: int\n    y: int\nvar p = P(1)\n"), Codes{"S009"});
}

TEST_CASE("oop: a struct with bases may have no body of its own")
{
    CHECK_EQ(run_output("mixin Greets\n    func hello(): string\n        return \"hi\"\n"
                        "interface Clickable\n    on_click: func()\n"
                        "struct Button: Greets, Clickable\n"
                        "var b = Button()\nvar n = 0\nb.on_click = func() n += 1\nb.on_click()\nprintln(b.hello(), \" \", n)\n"),
        "hi 1\n");
    // Without bases a struct still needs its block.
    CHECK_EQ(diagnostic_codes("struct Empty\nprintln(1)\n"), Codes{"P012"});
}

// ---------------------------------------------------------------- "my", the receiver

TEST_CASE("oop: 'my' is the value a method was called on")
{
    // Returning it chains calls; it is the same thing a bare member name reaches.
    CHECK_EQ(run_output("struct B\n    _parts: string[]\n\n"
                        "    func add(p: string): B\n        push(_parts, p)\n        return my\n\n"
                        "    func text(): string\n        return string.join(_parts, \" \")\n"
                        "var b = B()\nprintln(b.add(\"hello\").add(\"world\").text())"),
        "hello world\n");
    CHECK_EQ(run_output("struct P\n    x: int\n\n    func same(): bool\n        return my.x == x\n"
                        "println(P(2).same())"),
        "true\n");
    // It may be handed to a function, and to one that takes the interface.
    CHECK_EQ(run_output("interface Named\n    func name(): string\n"
                        "func greet(n: Named): string\n    return \"hi \" + n.name()\n"
                        "struct D: Named\n    func name(): string\n        return \"rex\"\n\n"
                        "    func hello(): string\n        return greet(my)\n"
                        "println(D().hello())"),
        "hi rex\n");
    // A method called through it is the receiver's method.
    CHECK_EQ(run_output("struct C\n    n: int\n\n    func twice(): int\n        return my.n * 2\n\n"
                        "    func four(): int\n        return my.twice() * 2\n"
                        "println(C(3).four())"),
        "12\n");
    // Outside a method there is no receiver.
    CHECK_EQ(diagnostic_codes("println(my)"), Codes{"S095"});
    CHECK_EQ(diagnostic_codes("func f(): int\n    return my\n"), Codes{"S095"});
}

// ---------------------------------------------------------------- what interfaces can carry

TEST_CASE("oop: an interface's members take and answer with any type")
{
    CHECK_EQ(run_output("struct Point\n    x: int\n    y: int\n"
                        "interface Shape\n    func move(p: Point): Point\n    func corners(): Point[]\n"
                        "struct Box: Shape\n"
                        "    func move(p: Point): Point\n        return Point(p.x + 1, p.y)\n\n"
                        "    func corners(): Point[]\n        return [Point(0, 0)]\n"
                        "var s: Shape = Box()\nprintln(s.move(Point(1, 2)).x, len(s.corners()))"),
        "21\n");
    // A struct declared below the interface works just as well, and so do maps, tuples and
    // function types in the signature.
    CHECK_EQ(run_output("interface Store\n    func put(k: string, v: Item)\n    func all(): {string: Item}\n"
                        "struct Item\n    n: int\n"
                        "struct Memory: Store\n    data: {string: Item}\n\n"
                        "    func put(k: string, v: Item)\n        data[k] = v\n\n"
                        "    func all(): {string: Item}\n        return data\n"
                        "var m: Store = Memory({})\nm.put(\"a\", Item(1))\nprintln(len(m.all()))"),
        "1\n");
    CHECK_EQ(diagnostic_codes("interface I\n    func f(p: Nope)\n"), Codes{"S031"});
}

TEST_CASE("oop: an interface may extend other interfaces")
{
    CHECK_EQ(run_output("interface Drawable\n    func draw()\n"
                        "interface Control: Drawable\n    func enabled(): bool\n"
                        "struct Switch: Control\n"
                        "    func draw()\n        println(\"[ ]\")\n\n"
                        "    func enabled(): bool\n        return true\n"
                        "var c: Control = Switch()\nc.draw()\nprintln(c.enabled())\n"
                        "var d: Drawable = Switch()\nd.draw()"),
        "[ ]\ntrue\n[ ]\n");
    // The extended interface's members must be there too.
    // Once for B (which promises f through A) and once for A itself.
    CHECK_EQ(diagnostic_codes("interface A\n    func f()\ninterface B: A\n    func g()\n"
                              "struct S: B\n    func g()\n        pass\n"),
        (Codes{"S074", "S074"}));
    // One line: an interface that only joins others. And a closure member comes along.
    CHECK_EQ(run_output("interface Clickable\n    on_click: func()\n"
                        "interface Named\n    func name(): string\n"
                        "interface Widget: Clickable, Named\n"
                        "struct Key: Widget\n    func name(): string\n        return \"k\"\n"
                        "var k = Key()\nk.on_click = func() println(\"hit\")\n"
                        "var w: Widget = k\nw.on_click()\nprintln(w.name())"),
        "hit\nk\n");
    CHECK_EQ(diagnostic_codes("struct S\n    n: int\ninterface I: S\n    func f()\n"), Codes{"S096"});
    // An interface value fits a variable of an interface it extends.
    CHECK_EQ(run_output("interface A\n    func f(): int\n"
                        "interface B: A\n    func g(): int\n"
                        "struct S: B\n    func f(): int\n        return 1\n\n"
                        "    func g(): int\n        return 2\n"
                        "var b: B = S()\nvar a: A = b\nprintln(a.f())"),
        "1\n");
    CHECK_EQ(diagnostic_codes("interface A\n    func f()\ninterface B\n    func g()\n"
                              "struct S: A, B\n    func f()\n        pass\n\n    func g()\n        pass\n"
                              "var a: A = S()\nvar b: B = a\n"),
        Codes{"S005"});
    CHECK(!diagnostic_codes("interface A: B\n    func f()\ninterface B: A\n    func g()\n").empty());
}

TEST_CASE("oop: an interface method may come with a default body")
{
    CHECK_EQ(run_output("interface Greeter\n"
                        "    func name(): string\n\n"
                        "    func greet()\n        println(\"Hello, \", name(), \"!\")\n"
                        "struct Dog: Greeter\n    func name(): string\n        return \"Rex\"\n"
                        "Dog().greet()\nvar g: Greeter = Dog()\ng.greet()"),
        "Hello, Rex!\nHello, Rex!\n");
    // The struct's own method wins.
    CHECK_EQ(run_output("interface G\n    func hi()\n        println(\"default\")\n"
                        "struct S: G\n    func hi()\n        println(\"mine\")\n"
                        "S().hi()"),
        "mine\n");
    // A default may use "my" and the struct's fields.
    CHECK_EQ(run_output("interface Sized\n    func size(): int\n\n"
                        "    func describe(): string\n        return \"size \" + str(my.size())\n"
                        "struct Bag: Sized\n    items: int[]\n\n"
                        "    func size(): int\n        return len(items)\n"
                        "println(Bag([1, 2]).describe())"),
        "size 2\n");
    // An inherited default comes through an extending interface too.
    CHECK_EQ(run_output("interface A\n    func hi()\n        println(\"a\")\n"
                        "interface B: A\n    func bye()\n        println(\"b\")\n"
                        "struct S: B\nS().hi()\nS().bye()"),
        "a\nb\n");
}

// ---------------------------------------------------------------- block bodies in brackets

TEST_CASE("closures: '->' is not part of the language")
{
    // The arrow was taken out: ':' says what a function answers with, and nothing else does.
    // What is left is a '-' followed by a '>', which is a plain syntax error wherever it stands.
    CHECK(!test::diagnostic_codes("var twice = func(n: int) -> int\n    return n * 2\n").empty());
    CHECK(!test::diagnostic_codes("func add(a: int, b: int) -> int\n    return a + b\n").empty());
    CHECK(!test::diagnostic_codes("var f: func(int) -> int = nothing\n").empty());
}

TEST_CASE("closures: a block body may be written inside brackets")
{
    CHECK_EQ(test::run_output("func run_it(action: func(): int): int\n"
                              "    return action()\n"
                              "println(run_it(func(): int\n"
                              "    var total = 0\n"
                              "    for i in range(1, 4)\n"
                              "        total += i\n"
                              "    return total\n"
                              "))\n"),
        "6\n");
    // The block also ends at the bracket that holds it, wherever that bracket stands.
    CHECK_EQ(test::run_output("func run_it(action: func(): int): int\n"
                              "    return action()\n"
                              "println(run_it(func(): int\n"
                              "    return 7))\n"),
        "7\n");
    // A function value with no result, and the captured variables of the line it stands on.
    CHECK_EQ(test::run_output("func later(action: func())\n"
                              "    action()\n"
                              "var log = \"\"\n"
                              "later(func()\n"
                              "    log += \"a\"\n"
                              "    log += \"b\"\n"
                              ")\n"
                              "println(log)\n"),
        "ab\n");
}

TEST_CASE("closures: a line break inside brackets still means nothing elsewhere")
{
    CHECK_EQ(test::run_output("println(1,\n"
                              "        2,\n"
                              "        3)\n"),
        "123\n");
    CHECK_EQ(test::run_output("var a = [1,\n"
                              "         2]\n"
                              "println(len(a))\n"),
        "2\n");
}
