// Generics: structs and functions with type parameters ("struct Stack<T>", "func first<T>"),
// instantiated once per combination of concrete types. The types are deduced from the
// arguments or from the type the result is assigned to; they are never written at a call.

#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"

#include <string>
#include <vector>

using namespace thunder;
using test::diagnostic_codes;
using test::run_output;
using Codes = std::vector<std::string>;

namespace
{
    const std::string STACK = "struct Stack<T>\n"
                              "    items: T[]\n"
                              "func stack_push<T>(stack: Stack<T>, value: T)\n"
                              "    push(stack.items, value)\n"
                              "func stack_pop<T>(stack: Stack<T>): T\n"
                              "    return pop(stack.items)\n"
                              "func new_stack<T>(): Stack<T>\n"
                              "    var empty: T[] = []\n"
                              "    return Stack(items: empty)\n";
}

TEST_CASE("generics: one template serves many element types")
{
    CHECK_EQ(run_output(STACK + "var numbers = Stack(items: [1, 2])\n"
                                "stack_push(numbers, 3)\n"
                                "var words = Stack(items: [\"a\"])\n"
                                "stack_push(words, \"b\")\n"
                                "println(stack_pop(numbers), \" \", stack_pop(words), \" \", numbers)"),
        "3 b Stack<int>(items: [1, 2])\n");
}

TEST_CASE("generics: the declared type deduces what the arguments cannot")
{
    // new_stack() has no argument to learn T from: the annotation supplies it.
    CHECK_EQ(run_output(STACK + "var s: Stack<int> = new_stack()\n"
                                "stack_push(s, 7)\n"
                                "println(stack_pop(s), \" \", len(s.items))"),
        "7 0\n");
    // The same through a generic struct construction with an empty literal.
    CHECK_EQ(run_output(STACK + "var s: Stack<string> = Stack(items: [])\n"
                                "stack_push(s, \"x\")\n"
                                "println(s)"),
        "Stack<string>(items: [\"x\"])\n");
}

TEST_CASE("generics: functions over arrays, with several type parameters")
{
    CHECK_EQ(run_output("func first_of<T>(values: T[]): T\n"
                        "    return values[0]\n"
                        "println(first_of([9, 8]), \" \", first_of([\"x\", \"y\"]), \" \", first_of([1.5]))"),
        "9 x 1.5\n");
    CHECK_EQ(run_output("struct Pair<A, B>\n"
                        "    first: A\n"
                        "    second: B\n"
                        "func swapped<A, B>(pair: Pair<A, B>): Pair<B, A>\n"
                        "    return Pair(first: pair.second, second: pair.first)\n"
                        "println(swapped(Pair(first: 1, second: \"one\")))"),
        "Pair<string, int>(first: \"one\", second: 1)\n");
}

TEST_CASE("generics: instances may be nested and recursive")
{
    // Stack<Stack<int>>: the element type is itself an instance.
    CHECK_EQ(run_output(STACK + "var stacks: Stack<Stack<int>> = Stack(items: [new_stack()])\n"
                                "stack_push(stacks.items[0], 5)\n"
                                "println(stacks)"),
        "Stack<Stack<int>>(items: [Stack<int>(items: [5])])\n");
    // A generic linked node: recursion and nothing together.
    CHECK_EQ(run_output("struct Node<T>\n"
                        "    value: T\n"
                        "    next: Node<T>\n"
                        "func chain_text<T>(node: Node<T>): string\n"
                        "    if node.next == nothing\n"
                        "        return str(node.value)\n"
                        "    return str(node.value) + \" -> \" + chain_text(node.next)\n"
                        "var tail = Node(value: \"b\", next: nothing)\n"
                        "println(chain_text(Node(value: \"a\", next: tail)))"),
        "a -> b\n");
    // A generic function may call itself: one instance per element type.
    CHECK_EQ(run_output("func sum_of<T>(values: T[], from: int): T\n"
                        "    if from == len(values) - 1\n"
                        "        return values[from]\n"
                        "    return values[from] + sum_of(values, from + 1)\n"
                        "println(sum_of([1, 2, 3], 0), \" \", sum_of([\"a\", \"b\"], 0))"),
        "6 ab\n");
}

TEST_CASE("generics: '>>' and '>=' are split when they close type arguments")
{
    // "Stack<Stack<int>>" lexes its tail as one '>>' token; the parser carves it up.
    CHECK_EQ(run_output(STACK + "var stacks: Stack<Stack<int>> = Stack(items: [new_stack()])\n"
                                "stack_push(stacks.items[0], 5)\n"
                                "println(len(stacks.items[0].items))"),
        "1\n");
    // "Box<int>= 1" lexes '>=' at the seam; the '=' is given back to the declaration.
    CHECK_EQ(run_output("struct Box<T>\n    value: T\nvar b: Box<int>= Box(value: 7)\nprintln(b.value)"),
        "7\n");
    // '>' still compares, and a comparison may not end a continued line by accident:
    // the next statement after a generic declaration starts normally.
    CHECK_EQ(run_output("struct Box<T>\n    value: T\nvar x = 3 > 2\nprintln(x)"), "true\n");
}

TEST_CASE("generics: semantic errors")
{
    // Wrong number of type arguments, and type arguments on things that take none.
    CHECK(diagnostic_codes("struct Box<T>\n    value: T\nvar b: Box") == Codes{"S057"});
    CHECK(diagnostic_codes("struct Box<T>\n    value: T\nvar b: Box<int, int>") == Codes{"S057"});
    CHECK(diagnostic_codes("struct P\n    x: int\nvar p: P<int>") == Codes{"S057"});
    // A use that cannot be deduced.
    CHECK(diagnostic_codes("func make<T>(): T[]\n    var empty: T[] = []\n    return empty\nvar x = make()")
        == Codes{"S058"});
    // Deduction happens, and then the arguments are still checked against the instance.
    CHECK(diagnostic_codes("func both<T>(a: T, b: T): T\n    return a\nprintln(both(1, \"x\"))")
        == Codes{"S010"});
    // A template name cannot be declared twice, in any combination.
    CHECK(diagnostic_codes("struct Box<T>\n    value: T\nstruct Box\n    x: int") == Codes{"S028"});
    CHECK(diagnostic_codes("func f<T>(x: T)\n    pass\nfunc f(x: int)\n    pass") == Codes{"S003"});
    // An unknown type among the arguments.
    CHECK(diagnostic_codes("struct Box<T>\n    value: T\nvar b: Box<Missing>") == Codes{"S031"});
}

TEST_CASE("generics: Pashto programs use them the same way")
{
    const CompileResult result = compile(SourceFile("p.th",
        "# ژبه: ps\n"
        "جوړښت جوړه<الف، ب>\n"
        "    لومړۍ_برخه: الف\n"
        "    دویمه_برخه: ب\n"
        "دنده لومړۍ_یې<الف، ب>(جوړه_: جوړه<الف، ب>): الف\n"
        "    راګرځه جوړه_.لومړۍ_برخه\n"
        "کرښه چاپ(لومړۍ_یې(جوړه(لومړۍ_برخه: ۱، دویمه_برخه: «یو»)))\n"));
    CHECK(result.succeeded());
}
