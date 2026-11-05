#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/sema.hpp"

using test::diagnostic_codes;
using Codes = std::vector<std::string>;

TEST_CASE("sema: a valid program has no diagnostics")
{
    CHECK(diagnostic_codes(R"(
        var count: int = 0;
        const name = "Thunder";
        var flags = [true, false];
        var grid: int[][] = [[1, 2], [3]];
        func add(a: int, b: int): int
        {
            return a + b;
        }
        func greet(who: string)
        {
            println("hi ", who);
        }
        count = add(count, 1);
        greet(name);
        println(flags[0] && !flags[1], grid[1][0], len(name), str(3), to_int("4"), array(2, "x"));
    )").empty());
}

TEST_CASE("sema: undeclared names and redeclarations")
{
    CHECK(diagnostic_codes("println(x);") == Codes{"S001"});
    CHECK(diagnostic_codes("x = 1;") == Codes{"S001"});
    CHECK(diagnostic_codes("var x = 1; var x = 2;") == Codes{"S002"});
    CHECK(diagnostic_codes("func f(a: int, a: int) { }") == Codes{"S002"});
    CHECK(diagnostic_codes("func f() { } func f() { }") == Codes{"S003"});
    CHECK(diagnostic_codes("func print() { }") == Codes{"S004"});
    CHECK(diagnostic_codes("undefined();") == Codes{"S001"});
}

TEST_CASE("sema: inner scopes may shadow outer names; names do not leak out of blocks")
{
    CHECK(diagnostic_codes("var x = 1; { var x = \"s\"; println(x); } println(x);").empty());
    CHECK(diagnostic_codes("{ var y = 1; println(y); } println(y);") == Codes{"S001"});
    CHECK(diagnostic_codes("for (var i = 0; i < 2; i = i + 1) { } println(i);") == Codes{"S001"});
}

TEST_CASE("sema: a variable cannot be used in its own initialiser")
{
    CHECK(diagnostic_codes("var x = x + 1;") == Codes{"S001"});
}

TEST_CASE("sema: functions can be called before their declaration and use any global")
{
    CHECK(diagnostic_codes("println(twice()); var base = 21; func twice(): int { return base * 2; }").empty());
}

TEST_CASE("sema: top-level code cannot use a global declared later")
{
    CHECK(diagnostic_codes("println(later); var later = 1;") == Codes{"S001"});
}

TEST_CASE("sema: assignment type checking")
{
    CHECK(diagnostic_codes("var x: int = \"s\";") == Codes{"S005"});
    CHECK(diagnostic_codes("var x = 1; x = true;") == Codes{"S005"});
    CHECK(diagnostic_codes("var a = [1]; a[0] = \"s\";") == Codes{"S005"});
    CHECK(diagnostic_codes("const c = 1; c = 2;") == Codes{"S012"});
    CHECK(diagnostic_codes("var s = \"abc\"; s[0] = \"x\";") == Codes{"S024"});
}

TEST_CASE("sema: operator type checking")
{
    CHECK(diagnostic_codes("var x = 1 + true;") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = \"a\" + 1;") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = \"a\" - \"b\";") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = true < false;") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = [1] == [1];") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = 1 && 2;") == Codes{"S006"});
    CHECK(diagnostic_codes("var x = -true;") == Codes{"S007"});
    CHECK(diagnostic_codes("var x = !1;") == Codes{"S007"});
    CHECK(diagnostic_codes("var ok = \"a\" < \"b\" && \"a\" == \"a\" && true != false && -1 < 0;").empty());
}

TEST_CASE("sema: errors do not cascade through expressions")
{
    // 'y' is undeclared: only that is reported, not the '+' or the assignment.
    CHECK(diagnostic_codes("var x: int = y + 1 * 2;") == Codes{"S001"});
}

TEST_CASE("sema: conditions must be bool")
{
    CHECK(diagnostic_codes("if (1) { }") == Codes{"S008"});
    CHECK(diagnostic_codes("while (\"s\") { }") == Codes{"S008"});
    CHECK(diagnostic_codes("for (; 0;) { }") == Codes{"S008"});
}

TEST_CASE("sema: calls are checked against signatures")
{
    CHECK(diagnostic_codes("func f(a: int) { } f();") == Codes{"S009"});
    CHECK(diagnostic_codes("func f(a: int) { } f(\"s\");") == Codes{"S010"});
    CHECK(diagnostic_codes("var v = 1; v();") == Codes{"S011"});
    CHECK(diagnostic_codes("func f() { } var x = f;") == Codes{"S025"});
    CHECK(diagnostic_codes("var x = println;") == Codes{"S025"});
    CHECK(diagnostic_codes("func f() { } var x = f();") == Codes{"S022"});
    CHECK(diagnostic_codes("println(println());") == Codes{"S022"});
}

TEST_CASE("sema: built-in functions are type checked")
{
    CHECK(diagnostic_codes("var n = len(5);") == Codes{"S026"});
    CHECK(diagnostic_codes("var n = len();") == Codes{"S009"});
    CHECK(diagnostic_codes("var n = len(\"a\", \"b\");") == Codes{"S009"});
    CHECK(diagnostic_codes("var n = to_int(5);") == Codes{"S010"});
    CHECK(diagnostic_codes("var a = array(\"3\", 0);") == Codes{"S010"});
    CHECK(diagnostic_codes("var a: int[][] = array(3, []);").empty());
    CHECK(diagnostic_codes("var s: string = str([1, 2]);").empty());
    CHECK(diagnostic_codes("print(); println();").empty());
}

TEST_CASE("sema: break and continue only inside loops")
{
    CHECK(diagnostic_codes("break;") == Codes{"S013"});
    CHECK(diagnostic_codes("continue;") == Codes{"S013"});
    CHECK(diagnostic_codes("func f() { break; }") == Codes{"S013"});
    CHECK(diagnostic_codes("while (true) { if (true) { break; } continue; }").empty());
}

TEST_CASE("sema: return statements")
{
    CHECK(diagnostic_codes("return;") == Codes{"S014"});
    CHECK(diagnostic_codes("return 1;") == Codes{"S014"});
    CHECK(diagnostic_codes("func f(): int { return; }") == Codes{"S015"});
    CHECK(diagnostic_codes("func f() { return 1; }") == Codes{"S016"});
    CHECK(diagnostic_codes("func f(): int { return \"s\"; }") == Codes{"S027"});
}

TEST_CASE("sema: every path of a non-void function must return")
{
    CHECK(diagnostic_codes("func f(x: int): int { if (x > 0) { return 1; } }") == Codes{"S017"});
    CHECK(diagnostic_codes("func f(x: int): int { while (true) { return 1; } }") == Codes{"S017"});
    CHECK(diagnostic_codes("func f(x: int): int { if (x > 0) { return 1; } else { return 2; } }").empty());
    CHECK(diagnostic_codes("func f(x: int): int { if (x > 0) { return 1; } else if (x < 0) { return 2; } else { return 0; } }")
              .empty());
    CHECK(diagnostic_codes("func f(x: int): int { { return x; } }").empty());
}

TEST_CASE("sema: indexing")
{
    CHECK(diagnostic_codes("var x = 5; var y = x[0];") == Codes{"S018"});
    CHECK(diagnostic_codes("var a = [1]; var y = a[\"0\"];") == Codes{"S019"});
    CHECK(diagnostic_codes("var s = \"abc\"; var c: string = s[1];").empty());
}

TEST_CASE("sema: array literals")
{
    CHECK(diagnostic_codes("var a = [1, \"two\"];") == Codes{"S020"});
    CHECK(diagnostic_codes("var a = [];") == Codes{"S021"});
    CHECK(diagnostic_codes("var a: string[] = [];").empty());
    CHECK(diagnostic_codes("var a: int[][] = [[], [1]];").empty());
    CHECK(diagnostic_codes("func f(a: int[]) { } f([]);").empty());
}

TEST_CASE("sema: main() must have the right signature")
{
    CHECK(diagnostic_codes("func main(x: int) { }") == Codes{"S023"});
    CHECK(diagnostic_codes("func main(): int { return 0; }") == Codes{"S023"});
    CHECK(diagnostic_codes("func main() { }").empty());
}

TEST_CASE("sema: warnings for unused variables and unreachable code")
{
    CHECK(diagnostic_codes("func f() { var unused = 1; }") == Codes{"W001"});
    CHECK(diagnostic_codes("{ var t = 1; }") == Codes{"W001"});
    CHECK(diagnostic_codes("var global_is_fine = 1;").empty());
    CHECK(diagnostic_codes("func f(): int { return 1; println(2); println(3); }") == Codes{"W002"});
    CHECK(diagnostic_codes("while (true) { break; println(1); }") == Codes{"W002"});
    CHECK(diagnostic_codes("func f(x: bool) { if (x) { return; } else { return; } println(1); }") == Codes{"W002"});
}

TEST_CASE("sema: symbols, slots and scopes are recorded")
{
    using namespace thunder;
    const SourceFile source("t.th", "var g = 1; const h = \"x\"; func f(a: int, b: int): int { var c = a + b; return c; } "
                                    "for (var i = 0; i < 1; i = i + 1) { println(g, h, f(i, 1)); }");
    const CompileResult result = compile(source);
    REQUIRE(result.model != nullptr);
    const SemanticModel& model = *result.model;

    REQUIRE(model.globals.size() == 2);
    CHECK_EQ(model.globals[0]->name, "g");
    CHECK(model.globals[1]->is_const);
    CHECK_EQ(model.globals[1]->slot, 1);

    REQUIRE(model.functions.size() == 2);
    CHECK_EQ(model.functions[0].name, "<main>");
    CHECK_EQ(model.functions[0].local_count, 1); // the for-loop variable i
    const FunctionInfo& f = model.functions[1];
    CHECK_EQ(f.name, "f");
    CHECK_EQ(f.local_count, 3);
    CHECK_EQ(f.locals[2]->name, "c");
    CHECK(f.locals[0]->kind == SymbolKind::Parameter);
    CHECK_EQ(symbol_kind_name(f.locals[2]->kind), "local");
    CHECK_EQ(symbol_kind_name(model.globals[0]->kind), "global");
    CHECK_EQ(symbol_kind_name(f.locals[0]->kind), "parameter");

    CHECK_EQ(model.global_scope.kind, "global");
    REQUIRE(model.global_scope.children.size() == 2);
    CHECK_EQ(model.global_scope.children[0].kind, "for");
    CHECK_EQ(model.global_scope.children[1].kind, "function");
    CHECK_EQ(model.global_scope.children[1].name, "f");
    CHECK_EQ(model.main_function, -1);
}

TEST_CASE("sema: expression types are annotated on the AST")
{
    using namespace thunder;
    const SourceFile source("t.th", "var a = [\"x\"]; var n = len(a[0] + \"y\") * 2;");
    const CompileResult result = compile(source);
    REQUIRE(result.ast != nullptr);
    const auto& decl = static_cast<const VarDeclStmt&>(*result.ast->items[1]);
    CHECK_EQ(decl.initializer->type.to_string(), "int");
    const auto& first = static_cast<const VarDeclStmt&>(*result.ast->items[0]);
    CHECK_EQ(first.symbol->type.to_string(), "string[]");
}

TEST_CASE("types: names and helpers")
{
    using thunder::Type;
    CHECK_EQ(Type::array_of(Type::array_of(Type::bool_type())).to_string(), "bool[][]");
    CHECK_EQ(Type::void_type().to_string(), "void");
    CHECK_EQ(Type::error().to_string(), "<error>");
    CHECK(Type::array_of(Type::int_type()).element() == Type::int_type());
    CHECK(Type::string_type().is_string());
    CHECK(!Type::array_of(Type::string_type()).is_string());
}
