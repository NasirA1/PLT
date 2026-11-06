#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/parser.hpp"

using test::parse_expr;
using test::parse_program;

TEST_CASE("parser: precedence of arithmetic operators")
{
    CHECK_EQ(parse_expr("1 + 2 * 3"), "(Binary + (Int 1) (Binary * (Int 2) (Int 3)))");
    CHECK_EQ(parse_expr("1 * 2 + 3"), "(Binary + (Binary * (Int 1) (Int 2)) (Int 3))");
    CHECK_EQ(parse_expr("(1 + 2) * 3"), "(Binary * (Binary + (Int 1) (Int 2)) (Int 3))");
    CHECK_EQ(parse_expr("10 % 3 / 2"), "(Binary / (Binary % (Int 10) (Int 3)) (Int 2))");
}

TEST_CASE("parser: binary operators are left-associative")
{
    CHECK_EQ(parse_expr("1 - 2 - 3"), "(Binary - (Binary - (Int 1) (Int 2)) (Int 3))");
    CHECK_EQ(parse_expr("a && b && c"), "(Binary && (Binary && (Name a) (Name b)) (Name c))");
}

TEST_CASE("parser: comparison, equality and logical precedence")
{
    CHECK_EQ(parse_expr("a < b == c > d"), "(Binary == (Binary < (Name a) (Name b)) (Binary > (Name c) (Name d)))");
    CHECK_EQ(parse_expr("a || b && c"), "(Binary || (Name a) (Binary && (Name b) (Name c)))");
    CHECK_EQ(parse_expr("x + 1 <= y * 2 || !done"),
        "(Binary || (Binary <= (Binary + (Name x) (Int 1)) (Binary * (Name y) (Int 2))) (Unary ! (Name done)))");
}

TEST_CASE("parser: unary operators bind tighter than binary ones")
{
    CHECK_EQ(parse_expr("-a * b"), "(Binary * (Unary - (Name a)) (Name b))");
    CHECK_EQ(parse_expr("- -3"), "(Unary - (Unary - (Int 3)))");
    CHECK_EQ(parse_expr("-(3)"), "(Unary - (Int 3))");
    CHECK_EQ(parse_expr("10+5*( (12 / 2) * 5) - -12"),
        "(Binary - (Binary + (Int 10) (Binary * (Int 5) (Binary * (Binary / (Int 12) (Int 2)) (Int 5)))) (Unary - (Int 12)))");
}

TEST_CASE("parser: postfix calls and indexing bind tightest")
{
    CHECK_EQ(parse_expr("-a[0]"), "(Unary - (Index (Name a) (Int 0)))");
    CHECK_EQ(parse_expr("grid[i][j]"), "(Index (Index (Name grid) (Name i)) (Name j))");
    CHECK_EQ(parse_expr("f(1, g(2))[3]"), "(Index (Call f (Int 1) (Call g (Int 2))) (Int 3))");
    CHECK_EQ(parse_expr("f()"), "(Call f)");
}

TEST_CASE("parser: literals")
{
    CHECK_EQ(parse_expr("[1, 2, 3]"), "(Array (Int 1) (Int 2) (Int 3))");
    CHECK_EQ(parse_expr("[]"), "(Array)");
    CHECK_EQ(parse_expr("true"), "(Bool true)");
    CHECK_EQ(parse_expr("\"a\\n\""), "(String \"a\\n\")");
}

TEST_CASE("parser: expression errors")
{
    CHECK_EQ(parse_expr("1 +"), "");
    CHECK_EQ(parse_expr("(1"), "");
    CHECK_EQ(parse_expr("2 3"), "");
    CHECK_EQ(parse_expr("(1)(2)"), "");
    CHECK_EQ(parse_expr("[1, 2"), "");
}

TEST_CASE("parser: declarations and statements")
{
    CHECK_EQ(parse_program("var x: int = 1; const y = 2; var z: string[];"),
        "(Program (Var x (Int 1)) (Const y (Int 2)) (Var z))");
    CHECK_EQ(parse_program("x = 1; a[0] = 2; f();"),
        "(Program (Assign = (Name x) (Int 1)) (Assign = (Index (Name a) (Int 0)) (Int 2)) (ExprStmt (Call f)))");
    CHECK_EQ(parse_program("while (true)\n    break\n    continue"),
        "(Program (While (Bool true) (Block (Break) (Continue))))");
    CHECK_EQ(parse_program("if x\n    pass"), "(Program (If (Name x) (Block (Pass))))");
    CHECK_EQ(parse_program("while x\n    pass\ny = 1"), "(Program (While (Name x) (Block (Pass))) (Assign = (Name y) (Int 1)))");
}

TEST_CASE("parser: if / else if / else")
{
    CHECK_EQ(parse_program("if (a)\n    pass\nelse if (b)\n    pass\nelse\n    x = 1"),
        "(Program (If (Name a) (Block (Pass)) (If (Name b) (Block (Pass)) (Block (Assign = (Name x) (Int 1))))))");
}

TEST_CASE("parser: for loops with optional parts")
{
    CHECK_EQ(parse_program("for (var i = 0; i < 3; i = i + 1)\n    pass"),
        "(Program (For (Var i (Int 0)) (Binary < (Name i) (Int 3)) (Assign = (Name i) (Binary + (Name i) (Int 1))) (Block (Pass))))");
    CHECK_EQ(parse_program("for var i = 0; i < 3; i = i + 1\n    pass"), // the parentheses are optional
        "(Program (For (Var i (Int 0)) (Binary < (Name i) (Int 3)) (Assign = (Name i) (Binary + (Name i) (Int 1))) (Block (Pass))))");
    CHECK_EQ(parse_program("for (;;)\n    pass"), "(Program (For (Block (Pass))))");
    CHECK_EQ(parse_program("for ;;\n    pass"), "(Program (For (Block (Pass))))");
    CHECK_EQ(parse_program("for (i = 0; ; f())\n    pass"),
        "(Program (For (Assign = (Name i) (Int 0)) (ExprStmt (Call f)) (Block (Pass))))");
}

TEST_CASE("parser: functions")
{
    CHECK_EQ(parse_program("func add(a: int, b: int): int\n    return a + b"),
        "(Program (Function add (Param a) (Param b) (Block (Return (Binary + (Name a) (Name b))))))");
    CHECK_EQ(parse_program("func f(rows: int[][])\n    return"), "(Program (Function f (Param rows) (Block (Return))))");
}

TEST_CASE("parser: syntax errors are reported with codes")
{
    CHECK(test::diagnostic_codes("var x = 5 var y = 6") == std::vector<std::string>{"P010"});
    CHECK(test::diagnostic_codes("var x = ;") == std::vector<std::string>{"P002"});
    CHECK(test::diagnostic_codes("var x: 5 = 1;") == std::vector<std::string>{"P003"});
    CHECK(test::diagnostic_codes("}") == std::vector<std::string>{"P002"});
    CHECK(test::diagnostic_codes("1 + 2 = 3;") == std::vector<std::string>{"P005"});
    CHECK(test::diagnostic_codes("var f = (1)(2);") == std::vector<std::string>{"P006"});
    CHECK(test::diagnostic_codes("func f()\n    func g()\n        pass") == std::vector<std::string>{"P007"});
    CHECK(test::diagnostic_codes("var x;") == std::vector<std::string>{"P008"});
    CHECK(test::diagnostic_codes("const c: int;") == std::vector<std::string>{"P001"});
    CHECK(test::diagnostic_codes("if (x) y = 1;") == std::vector<std::string>{"P012"});
    CHECK(test::diagnostic_codes("if x {\n    y = 1\n}") == std::vector<std::string>{"P014"}); // braces: one clear error
    CHECK(test::diagnostic_codes("x = 1\n    y = 2\n    z = 3\nw = 4") == std::vector<std::string>{"P013"});
    CHECK(test::diagnostic_codes("if x\npass") == std::vector<std::string>{"P012"});
    CHECK(test::diagnostic_codes("if (x >)\n    y = 1\nz = 2") == std::vector<std::string>{"P002"}); // the body is skipped, not reported again
}

TEST_CASE("parser: semicolons are optional; a line break ends a statement")
{
    CHECK_EQ(parse_program("var x = 1\nvar y = 2\nx = y"),
        "(Program (Var x (Int 1)) (Var y (Int 2)) (Assign = (Name x) (Name y)))");
    CHECK_EQ(parse_program("var x = 1; var y = 2;"), "(Program (Var x (Int 1)) (Var y (Int 2)))");
    CHECK(test::diagnostic_codes("var x = 1\nprintln(x)\n").empty());
}

TEST_CASE("parser: an operator at the end of a line continues the expression")
{
    CHECK_EQ(parse_program("var x = 1 +\n    2 *\n    3"),
        "(Program (Var x (Binary + (Int 1) (Binary * (Int 2) (Int 3)))))");
    CHECK_EQ(parse_program("var ok = a &&\n    b"), "(Program (Var ok (Binary && (Name a) (Name b))))");
}

TEST_CASE("parser: an operator at the start of a line starts a new statement")
{
    // Unlike JavaScript, "a" newline "-b" is two statements, so a line never silently joins the
    // previous one.
    CHECK_EQ(parse_program("x = a\n-b"), "(Program (Assign = (Name x) (Name a)) (ExprStmt (Unary - (Name b))))");
    CHECK_EQ(parse_program("f\n(x)"), "(Program (ExprStmt (Name f)) (ExprStmt (Name x)))");
    CHECK_EQ(parse_program("a\n[1]"), "(Program (ExprStmt (Name a)) (ExprStmt (Array (Int 1))))");
}

TEST_CASE("parser: line breaks inside parentheses and brackets are ignored")
{
    CHECK_EQ(parse_program("f(1,\n  2)"), "(Program (ExprStmt (Call f (Int 1) (Int 2))))");
    CHECK_EQ(parse_program("var a = [\n    1,\n    2\n]"), "(Program (Var a (Array (Int 1) (Int 2))))");
    CHECK_EQ(parse_program("var x = (1\n + 2)"), "(Program (Var x (Binary + (Int 1) (Int 2))))");
    CHECK_EQ(parse_program("if (a\n&& b)\n    pass"), "(Program (If (Binary && (Name a) (Name b)) (Block (Pass))))");
    CHECK_EQ(parse_program("for (var i = 0;\ni < 2;\ni = i + 1)\n    pass"),
        "(Program (For (Var i (Int 0)) (Binary < (Name i) (Int 2)) (Assign = (Name i) (Binary + (Name i) (Int 1))) (Block (Pass))))");
}

TEST_CASE("parser: return at the end of a line returns no value")
{
    CHECK_EQ(parse_program("func f()\n    return"), "(Program (Function f (Block (Return))))");
    CHECK_EQ(parse_program("func f(): int\n    return 1"), "(Program (Function f (Block (Return (Int 1)))))");
}

TEST_CASE("parser: two statements on one line need a ';'")
{
    const auto result = test::run("var x = 5 var y = 6\n");
    REQUIRE(result.diagnostics.size() == 1);
    CHECK_EQ(result.diagnostics[0], "1:11: P010");
    CHECK(test::diagnostic_codes("break continue") == std::vector<std::string>{"P010"});
}

TEST_CASE("parser: structs, fields and named arguments")
{
    CHECK_EQ(parse_program("struct P\n    x: int\n    tags: string[]"),
        "(Program (Struct P (Field x) (Field tags)))");
    CHECK_EQ(parse_program("struct P\n    x: int, y: int\n    z: bool"),
        "(Program (Struct P (Field x) (Field y) (Field z)))");
    CHECK_EQ(parse_expr("p.x.y"), "(Field y (Field x (Name p)))");
    CHECK_EQ(parse_expr("a[0].x[1]"), "(Index (Field x (Index (Name a) (Int 0))) (Int 1))");
    CHECK_EQ(parse_expr("P(x: 1, y: 2)"), "(Call P (Int 1) (Int 2))");
    CHECK_EQ(parse_program("p.x = 3"), "(Program (Assign = (Field x (Name p)) (Int 3)))");
    CHECK_EQ(parse_program("var p: P\nvar q: P[]"), "(Program (Var p) (Var q))");
    CHECK(test::diagnostic_codes("struct P\n    x: int y: int") == std::vector<std::string>{"P001"});
    CHECK(test::diagnostic_codes("func f()\n    struct Q\n        x: int") == std::vector<std::string>{"P011"});
    CHECK(test::diagnostic_codes("var x = p.") == std::vector<std::string>{"P001"});
}

TEST_CASE("parser: error recovery reports several independent errors")
{
    const auto codes = test::diagnostic_codes("var a = ;\nvar b = 2;\nvar c = );\nprintln(b);\n");
    CHECK(codes == (std::vector<std::string>{"P002", "P002"}));
}

TEST_CASE("parser: recovery inside blocks and functions")
{
    const auto codes = test::diagnostic_codes("func f()\n    var a =\n    var b = 1 +\nvar c =");
    CHECK(codes == (std::vector<std::string>{"P002", "P002", "P002"}));
}

TEST_CASE("parser: too many errors stops parsing")
{
    std::string text;
    for (int i = 0; i < 80; ++i)
    {
        text += "var = ;\n";
    }
    const auto codes = test::diagnostic_codes(text);
    CHECK_EQ(codes.back(), "P009");
    CHECK(codes.size() <= thunder::DiagnosticBag::max_errors + 1);
}

TEST_CASE("parser: binding power table")
{
    using thunder::TokenKind;
    CHECK(thunder::infix_binding_power(TokenKind::Star) > thunder::infix_binding_power(TokenKind::Plus));
    CHECK(thunder::infix_binding_power(TokenKind::Plus) > thunder::infix_binding_power(TokenKind::Less));
    CHECK(thunder::infix_binding_power(TokenKind::Less) > thunder::infix_binding_power(TokenKind::Equal));
    CHECK(thunder::infix_binding_power(TokenKind::Equal) > thunder::infix_binding_power(TokenKind::AndAnd));
    CHECK(thunder::infix_binding_power(TokenKind::AndAnd) > thunder::infix_binding_power(TokenKind::OrOr));
    CHECK_EQ(thunder::infix_binding_power(TokenKind::Semicolon), thunder::binding_power::none);
}

TEST_CASE("parser: a parser built from tokens without EndOfFile still terminates")
{
    using namespace thunder;
    DiagnosticBag diagnostics;
    std::vector<Token> tokens(1);
    tokens[0].kind = TokenKind::IntLiteral;
    tokens[0].lexeme = "1";
    tokens[0].int_value = 1;
    Parser parser(tokens, english_pack(), diagnostics);
    const ExprPtr expr = parser.parse_standalone_expression();
    REQUIRE(expr != nullptr);
    CHECK_EQ(to_sexpr(*expr), "(Int 1)");

    DiagnosticBag empty_diagnostics;
    Parser empty(std::vector<Token>{}, english_pack(), empty_diagnostics);
    CHECK(empty.parse_program().items.empty());
}

TEST_CASE("ast: tree text and node descriptions")
{
    using namespace thunder;
    const SourceFile source("t.th", "func f(a: int): int\n    return a\nvar s = \"x\\t\"\nf(1)");
    const CompileResult result = compile(source);
    REQUIRE(result.ast != nullptr);
    const std::string tree = to_tree_text(*result.ast);
    CHECK_CONTAINS(tree, "Program\n");
    CHECK_CONTAINS(tree, "├── Function f : int");
    CHECK_CONTAINS(tree, "│   ├── param: Param a : int");
    CHECK_CONTAINS(tree, "├── Var s : string");
    CHECK_CONTAINS(tree, "value: String \"x\\t\" : string");
    CHECK_CONTAINS(tree, "ExprStmt");
    CHECK_EQ(operator_text(TokenKind::OrOr), "||");
}
