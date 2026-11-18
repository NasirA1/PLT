// The SDK's pure-Thunder modules (lib/thunder/<language>/): the data structures and the
// algorithms, in both languages. The modules are ordinary Thunder compiled with the SDK
// module path, exactly as a user program finds them.

#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"
#include "thunder/report.hpp"
#include "thunder/vm.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace thunder;

namespace
{
    // Compiles and runs 'text' with the SDK's modules in reach, returning the output.
    // Compile errors and runtime errors fail the calling test via CHECK on the result.
    std::string run_sdk(const std::string& text)
    {
        const SourceFile source("program.th", text);
        CompileOptions options;
        options.sdk_module_path = (fs::path(THUNDER_SOURCE_DIR) / "lib" / "thunder").generic_string();
        const CompileResult result = compile(source, english_pack(), options);
        std::string messages;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            messages += format_short(diagnostic, source.name(), *result.language) + "\n";
        }
        CHECK_EQ(messages, "");
        REQUIRE(result.program.has_value());
        std::ostringstream output;
        Vm vm(*result.program, output);
        const ExecResult exec = vm.run();
        CHECK_EQ(exec.error ? format_runtime_error(*exec.error, source.name(), *result.language) : "", "");
        return output.str();
    }
    // The diagnostic codes a program draws, compiled with the SDK's modules in reach.
    std::vector<std::string> diagnostic_codes_sdk(const std::string& text)
    {
        const SourceFile source("program.th", text);
        CompileOptions options;
        options.sdk_module_path = (fs::path(THUNDER_SOURCE_DIR) / "lib" / "thunder").generic_string();
        const CompileResult result = compile(source, english_pack(), options);
        std::vector<std::string> codes;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            codes.push_back(diagnostic.code);
        }
        return codes;
    }
}

namespace
{
    // Like run_sdk, with the program's command-line arguments and its exit status.
    struct SdkRun
    {
        std::string output;
        int exit_code = 0;
    };

    SdkRun run_sdk_with(const std::string& text, std::vector<std::string> arguments)
    {
        const SourceFile source("program.th", text);
        CompileOptions options;
        options.sdk_module_path = (fs::path(THUNDER_SOURCE_DIR) / "lib" / "thunder").generic_string();
        const CompileResult result = compile(source, english_pack(), options);
        std::string messages;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            messages += format_short(diagnostic, source.name(), *result.language) + "\n";
        }
        CHECK_EQ(messages, "");
        REQUIRE(result.program.has_value());
        std::ostringstream output;
        Vm vm(*result.program, output);
        vm.set_arguments(std::move(arguments));
        const ExecResult exec = vm.run();
        CHECK_EQ(exec.error ? format_runtime_error(*exec.error, source.name(), *result.language) : "", "");
        return {output.str(), exec.exit_code};
    }
}

TEST_CASE("sdk: the testing module reports every test, counts them, and fails the program when one fails")
{
    const std::string program =
        "import \"testing\"\n"
        "func adds()\n    expect.equal(2 + 2, 4)\n    expect.is_true(true)\n"
        "func lists()\n    expect.equal([1, 2], [1, 2])\n    expect.not_equal([1], [2])\n    expect.near(0.1 + 0.2, 0.3, 0.001)\n"
        "func wrong()\n    expect.equal(\"a\", \"b\")\n    expect.is_false(true)\n    test.fail(\"and a message of my own\")\n"
        "test.run(\"adds\", adds)\ntest.run(\"lists\", lists)\ntest.run(\"wrong\", wrong)\ntest.finish()\n";
    const SdkRun all = run_sdk_with(program, {});
    CHECK_EQ(all.output,
        "✓ adds\n✓ lists\n"
        "✗ wrong: expected b, got a\n✗ wrong: expected false, got true\n✗ wrong: and a message of my own\n"
        "2 passed, 1 failed\n");
    CHECK_EQ(all.exit_code, 1);
    // Only the tests named on the command line run; everything passing is exit status 0.
    const SdkRun some = run_sdk_with(program, {"lists", "adds"});
    CHECK_EQ(some.output, "✓ adds\n✓ lists\n2 passed, 0 failed\n");
    CHECK_EQ(some.exit_code, 0);
}

TEST_CASE("sdk: the Pashto testing module behaves exactly like the English one")
{
    const std::string program =
        "# ژبه: ps\n"
        "راوړه \"testing\"\n"
        "دنده جمع()\n    تمه.برابر(۲ + ۲، ۴)\n    تمه.رښتیا_وي(رښتیا)\n"
        "دنده ناسمه()\n    تمه.برابر(\"الف\"، \"ب\")\n    تمه.نږدې(۱.۰، ۲.۰، ۰.۵)\n"
        "ازموینه.وچلوه(\"جمع\"، جمع)\nازموینه.وچلوه(\"ناسمه\"، ناسمه)\nازموینه.پای_ته_ورسوه()\n";
    const SdkRun all = run_sdk_with(program, {});
    CHECK_EQ(all.output,
        "✓ جمع\n"
        "✗ ناسمه: تمه ب وه، الف ترلاسه شو\n✗ ناسمه: تمه ۲٫۰ وه (په ۰٫۵ کې)، ۱٫۰ ترلاسه شو\n"
        "۱ تېرې شوې، ۱ ناکامې\n");
    CHECK_EQ(all.exit_code, 1);
    const SdkRun some = run_sdk_with(program, {"جمع"});
    CHECK_EQ(some.output, "✓ جمع\n۱ تېرې شوې، ۰ ناکامې\n");
    CHECK_EQ(some.exit_code, 0);
}

TEST_CASE("sdk: the stack is last in, first out")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var s: Stack<int> = Stack()\n"
                     "println(s.is_empty())\n"
                     "s.push(1)\ns.push(2)\ns.push(3)\n"
                     "println(s.pop(), \" \", s.peek(), \" \", s.size(), \" \", s.is_empty())\n"),
        "true\n3 2 2 false\n");
}

TEST_CASE("sdk: the queue is first in, first out, and reclaims served space")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var q: Queue<int> = Queue()\n"
                     "println(q.is_empty())\n"
                     "for var i = 1, i <= 30, i = i + 1\n"
                     "    q.add(i)\n"
                     "var first_25 = 0\n"
                     "for var i = 0, i < 25, i = i + 1\n"
                     "    first_25 = q.take()\n"
                     "println(first_25, \" \", q.peek(), \" \", q.size(), \" \", q.is_empty())\n"),
        "true\n25 26 5 false\n");
    // The internals (q.items_) are private now: reading them from here is an error.
    CHECK_EQ(diagnostic_codes_sdk("import \"datastructures\"\n"
                                  "var q: Queue<int> = Queue()\nprintln(len(q.items_))\n"),
        std::vector<std::string>{"S076"});
}

TEST_CASE("sdk: the linked list adds at both ends, removes and walks in order")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var l: LinkedList<int> = LinkedList()\n"
                     "l.add_last(2)\nl.add_first(1)\nl.add_last(3)\n"
                     "println(l.to_array(), \" \", l.size())\n"
                     "println(l.has(3), \" \", l.has(9))\n"
                     "println(l.remove_value(2), \" \", l.remove_value(9), \" \", l.to_array())\n"
                     "println(l.remove_first(), \" \", l.to_array(), \" \", l.size())\n"),
        "[1, 2, 3] 3\ntrue false\ntrue false [1, 3]\n1 [3] 1\n");
}

TEST_CASE("sdk: the binary search tree keeps order, ignores duplicates and removes correctly")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var t: BinaryTree<int> = BinaryTree()\n"
                     "for n in [5, 3, 8, 1, 4, 7, 9, 5, 2, 6]\n"
                     "    t.insert(n)\n"
                     "println(t.sorted(), \" \", t.size(), \" \", t.height())\n"
                     "println(t.has(7), \" \", t.has(10), \" \", t.smallest(), \" \", t.largest())\n"
                     // the root with two children, a missing value, a leaf, a node with one child
                     "println(t.remove(5), \" \", t.remove(42), \" \", t.sorted())\n"
                     "println(t.remove(1), \" \", t.remove(9), \" \", t.sorted(), \" \", t.size())\n"),
        "[1, 2, 3, 4, 5, 6, 7, 8, 9] 9 4\n"
        "true false 1 9\n"
        "true false [1, 2, 3, 4, 6, 7, 8, 9]\n"
        "true true [2, 3, 4, 6, 7, 8] 6\n");
}

TEST_CASE("sdk: the heap always gives the smallest value first")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var h: Heap<int> = Heap()\n"
                     "for n in [9, 4, 7, 1, 3, 8, 1]\n"
                     "    h.add(n)\n"
                     "var taken: int[] = []\n"
                     "while not h.is_empty()\n"
                     "    push(taken, h.take())\n"
                     "println(taken, \" \", h.size())\n"),
        "[1, 1, 3, 4, 7, 8, 9] 0\n");
}

TEST_CASE("sdk: every sort sorts, including the edge cases")
{
    // Each sort over the awkward shapes: empty, one element, duplicates, sorted, reversed.
    const char* sorts[] = {"sorting.bubble", "sorting.selection", "sorting.insertion", "sorting.quick", "sorting.merge"};
    for (const char* sort_name : sorts)
    {
        const std::string name(sort_name);
        CHECK_EQ(run_sdk("import \"algorithms\"\n"
                         "var empty: int[] = []\n" + name + "(empty)\n"
                         "var one = [7]\n" + name + "(one)\n"
                         "var dup = [5, 2, 9, 1, 5, 6, 2, 2]\n" + name + "(dup)\n"
                         "var sorted_already = [1, 2, 3, 4]\n" + name + "(sorted_already)\n"
                         "var reversed = [9, 7, 5, 3, 1]\n" + name + "(reversed)\n"
                         "println(empty, one, dup, sorted_already, reversed)\n"),
            "[][7][1, 2, 2, 2, 5, 5, 6, 9][1, 2, 3, 4][1, 3, 5, 7, 9]\n");
    }
}

TEST_CASE("sdk: the searches find what is there and -1 for what is not")
{
    CHECK_EQ(run_sdk("import \"algorithms\"\n"
                     "var values = [1, 3, 5, 7, 9, 11]\n"
                     "println(searching.linear(values, 7), \" \", searching.linear(values, 2))\n"
                     "println(searching.binary(values, 1), \" \", searching.binary(values, 11), \" \",\n"
                     "        searching.binary(values, 7), \" \", searching.binary(values, 2))\n"
                     "var empty: int[] = []\n"
                     "println(searching.linear(empty, 1), \" \", searching.binary(empty, 1))\n"),
        "3 -1\n0 5 3 -1\n-1 -1\n");
}

TEST_CASE("sdk: sorting.is_sorted, index_of_smallest and a repeatable shuffle")
{
    CHECK_EQ(run_sdk("import \"algorithms\"\n"
                     "var none: int[] = []\n"
                     "println(sorting.is_sorted([1, 2, 2, 3]), \" \", sorting.is_sorted([2, 1]), \" \", sorting.is_sorted(none))\n"
                     "println(searching.index_of_smallest([4, 1, 3]), \" \", searching.index_of_smallest(none))\n"
                     "random.seed(1)\n"
                     "var values = [1, 2, 3, 4, 5, 6, 7, 8]\n"
                     "sorting.shuffle(values)\n"
                     "var shuffled = copy(values)\n"
                     "sorting.quick(values)\n"
                     "println(len(shuffled), \" \", values)\n"),
        "true false true\n1 -1\n8 [1, 2, 3, 4, 5, 6, 7, 8]\n");
}

TEST_CASE("sdk: the Pashto modules behave exactly like the English ones")
{
    // The same scenarios through the Pashto names; the outputs are the Pashto spellings.
    CHECK_EQ(run_sdk("# ژبه: ps\n"
                     "راوړه \"datastructures\"\n"
                     "راوړه \"algorithms\"\n"
                     "متغیر د: ډېرۍ<عدد> = ډېرۍ()\n"
                     "د.ورزیات_کړه(۱)\nد.ورزیات_کړه(۲)\n"
                     "کرښه چاپ(د.واخله()، \" \"، د.کچه())\n"
                     "متغیر ل: لیکه<عدد> = لیکه()\n"
                     "ل.ورزیات_کړه(۱)\nل.ورزیات_کړه(۲)\n"
                     "کرښه چاپ(ل.واخله()، \" \"، ل.وګوره())\n"
                     "متغیر ځ: ځنځیر<عدد> = ځنځیر()\n"
                     "ځ.پای_ته_ورزیات_کړه(۲)\nځ.سر_ته_ورزیات_کړه(۱)\n"
                     "کرښه چاپ(ځ.لړۍ_کړه()، \" \"، ځ.لري(۲)، \" \"، ځ.لیرې_کړه(۲)، \" \"، ځ.کچه())\n"
                     "متغیر و: ونه<عدد> = ونه()\n"
                     "تکرار ن په [۵، ۳، ۸، ۱]\n"
                     "    و.ورزیات_کړه(ن)\n"
                     "کرښه چاپ(و.په_ترتیب()، \" \"، و.لوړوالی()، \" \"، و.لیرې_کړه(۳)، \" \"، و.په_ترتیب())\n"
                     "متغیر م: لومړیتوب_لیکه<عدد> = لومړیتوب_لیکه()\n"
                     "تکرار ن په [۴، ۱، ۳]\n"
                     "    م.ورزیات_کړه(ن)\n"
                     "کرښه چاپ(م.واخله()، \" \"، م.وګوره())\n"
                     "متغیر لړ = [۵، ۲، ۹، ۱]\n"
                     "ترتیب.چټک(لړ)\n"
                     "کرښه چاپ(لړ، \" \"، لټون.نیمایي(لړ، ۹)، \" \"، ترتیب.شوې_ده(لړ))\n"),
        "۲ ۱\n۱ ۲\n[۱، ۲] رښتیا رښتیا ۱\n[۱، ۳، ۵، ۸] ۳ رښتیا [۱، ۵، ۸]\n۱ ۳\n[۱، ۲، ۵، ۹] ۳ رښتیا\n");
}

TEST_CASE("sdk: the set keeps every value once, in first-added order")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var seen: Set<string> = Set()\n"
                     "println(seen.is_empty())\n"
                     "for color in [\"red\", \"blue\", \"red\", \"green\", \"blue\"]\n"
                     "    seen.add(color)\n"
                     "println(seen.values(), \" \", seen.size())\n"
                     "println(seen.has(\"blue\"), \" \", seen.has(\"pink\"))\n"
                     "println(seen.remove(\"blue\"), \" \", seen.remove(\"blue\"), \" \", seen.values())\n"),
        "true\n[\"red\", \"blue\", \"green\"] 3\ntrue false\ntrue false [\"red\", \"green\"]\n");
    // Union, intersection and difference.
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var a: Set<int> = Set()\n"
                     "for n in [1, 2, 3]\n    a.add(n)\n"
                     "var b: Set<int> = Set()\n"
                     "for n in [3, 4]\n    b.add(n)\n"
                     "println(a.union(b).values(), \" \", a.intersection(b).values(), \" \",\n"
                     "        a.difference(b).values())\n"),
        "[1, 2, 3, 4] [3] [1, 2]\n");
}

TEST_CASE("sdk: the ordered set keeps unique values sorted")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var scores: OrderedSet<int> = OrderedSet()\n"
                     "println(scores.is_empty())\n"
                     "for n in [70, 40, 90, 70, 40]\n"
                     "    scores.add(n)\n"
                     "println(scores.values(), \" \", scores.size())\n"
                     "println(scores.smallest(), \" \", scores.largest())\n"
                     "println(scores.has(40), \" \", scores.has(41))\n"
                     "println(scores.remove(70), \" \", scores.remove(70), \" \",\n"
                     "        scores.values())\n"),
        "true\n[40, 70, 90] 3\n40 90\ntrue false\ntrue false [40, 90]\n");
    // The same through the Pashto names.
    CHECK_EQ(run_sdk("# ژبه: ps\n"
                     "راوړه \"datastructures\"\n"
                     "متغیر ټ: ټولګه<عدد> = ټولګه()\n"
                     "تکرار ن په [۲، ۱، ۲]\n"
                     "    ټ.ورزیات_کړه(ن)\n"
                     "کرښه چاپ(ټ.ارزښتونه()، \" \"، ټ.لري(۱)، \" \"، ټ.لیرې_کړه(۹))\n"
                     "متغیر ت: ترتیب_شوې_ټولګه<عدد> = ترتیب_شوې_ټولګه()\n"
                     "تکرار ن په [۳، ۱، ۲، ۳]\n"
                     "    ت.ورزیات_کړه(ن)\n"
                     "کرښه چاپ(ت.ارزښتونه()، \" \"، ت.کمینه())\n"),
        "[۲، ۱] رښتیا درواغ\n[۱، ۲، ۳] ۱\n");
}

TEST_CASE("sdk: the deque serves both ends in O(1)")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var d: Deque<int> = Deque()\n"
                     "println(d.is_empty())\n"
                     "d.add_last(2)\nd.add_first(1)\nd.add_last(3)\n"
                     "println(d.to_array(), \" \", d.size())\n"
                     "println(d.take_first(), \" \", d.take_last(), \" \",\n"
                     "        d.peek_first(), \" \", d.peek_last(), \" \", d.to_array())\n"
                     // Draining across the refill boundary keeps the order.
                     "var e: Deque<int> = Deque()\n"
                     "for var i = 1, i <= 5, i = i + 1\n"
                     "    e.add_last(i)\n"
                     "var out: int[] = []\n"
                     "while not e.is_empty()\n"
                     "    push(out, e.take_last())\n"
                     "println(out)\n"),
        "true\n[1, 2, 3] 3\n1 3 2 2 [2]\n[5, 4, 3, 2, 1]\n");
}

TEST_CASE("sdk: the doubly linked list walks both ways and removes anywhere")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var l: DoublyLinkedList<string> = DoublyLinkedList()\n"
                     "l.add_last(\"b\")\nl.add_first(\"a\")\nl.add_last(\"c\")\n"
                     "println(l.to_array(), \" \", l.to_array_reversed(), \" \", l.size())\n"
                     "println(l.take_first(), \" \", l.take_last(), \" \", l.to_array())\n"
                     // removing the middle, the head, the tail and a missing value
                     "for value in [\"x\", \"y\", \"z\"]\n"
                     "    l.add_last(value)\n"
                     "println(l.remove_value(\"y\"), \" \", l.remove_value(\"q\"), \" \", l.to_array())\n"
                     "println(l.remove_value(\"b\"), \" \", l.to_array(), \" \", l.to_array_reversed())\n"),
        "[\"a\", \"b\", \"c\"] [\"c\", \"b\", \"a\"] 3\na c [\"b\"]\ntrue false [\"b\", \"x\", \"z\"]\ntrue [\"x\", \"z\"] [\"z\", \"x\"]\n");
}

TEST_CASE("sdk: the graph searches breadth first, depth first and finds shortest paths")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var g: Graph<string> = Graph()\n"
                     "g.connect(\"a\", \"b\")\n"
                     "g.connect(\"b\", \"c\")\n"
                     "g.connect(\"a\", \"d\")\n"
                     "g.connect(\"d\", \"c\")\n"
                     "g.add_edge(\"c\", \"e\")\n"
                     "g.add_vertex(\"lonely\")\n"
                     "println(g.vertices(), \" \", g.neighbors(\"a\"), \" \", g.neighbors(\"?\"))\n"
                     "println(g.bfs(\"a\"))\n"
                     "println(g.dfs(\"a\"))\n"
                     "println(g.shortest_path(\"a\", \"e\"), \" \", g.shortest_path(\"a\", \"a\"))\n"
                     // e has no outgoing edges: the direction matters
                     "println(g.has_path(\"e\", \"a\"), \" \", g.has_path(\"a\", \"lonely\"))\n"),
        "[\"a\", \"b\", \"c\", \"d\", \"e\", \"lonely\"] [\"b\", \"d\"] []\n"
        "[\"a\", \"b\", \"d\", \"c\", \"e\"]\n"
        "[\"a\", \"b\", \"c\", \"d\", \"e\"]\n"
        "[\"a\", \"b\", \"c\", \"e\"] [\"a\"]\n"
        "false false\n");
}

TEST_CASE("sdk: the trie answers word and prefix questions")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var t = Trie()\n"
                     "for word in [\"car\", \"cart\", \"cat\", \"dog\", \"car\"]\n"
                     "    t.add(word)\n"
                     "println(t.words(), \" \", t.size())\n"
                     "println(t.has(\"car\"), \" \", t.has(\"ca\"), \" \", t.has(\"carts\"))\n"
                     "println(t.has_prefix(\"ca\"), \" \", t.has_prefix(\"\"), \" \", t.has_prefix(\"x\"))\n"),
        "[\"car\", \"cart\", \"cat\", \"dog\"] 4\ntrue false false\ntrue true false\n");
}

TEST_CASE("sdk: the text algorithms search, compare and measure")
{
    CHECK_EQ(run_sdk("import \"algorithms\"\n"
                     "println(text.find(\"hello world\", \"world\"), \" \", text.find(\"abc\", \"z\"), \" \",\n"
                     "        text.find(\"abc\", \"\"), \" \", text.find(\"ab\", \"abc\"))\n"
                     "println(text.find_all(\"aaaa\", \"aa\"), \" \", text.find_all(\"abc\", \"\"))\n"
                     "println(text.is_palindrome(\"level\"), \" \", text.is_palindrome(\"۱۲۳۲۱\"), \" \",\n"
                     "        text.is_palindrome(\"ab\"), \" \", text.is_palindrome(\"\"))\n"
                     "println(text.longest_common_prefix([\"flower\", \"flow\", \"flight\"]), \" \",\n"
                     "        text.longest_common_prefix([\"a\", \"b\"]) == \"\")\n"
                     "println(text.edit_distance(\"kitten\", \"sitting\"), \" \", text.edit_distance(\"\", \"abc\"), \" \",\n"
                     "        text.edit_distance(\"same\", \"same\"))\n"),
        "6 -1 0 -1\n[0, 1, 2] []\ntrue true false true\nfl true\n3 3 0\n");
}

TEST_CASE("sdk: the new structures speak Pashto too")
{
    CHECK_EQ(run_sdk("# ژبه: ps\n"
                     "راوړه \"datastructures\"\n"
                     "راوړه \"algorithms\"\n"
                     "متغیر د: دوسرې_لیکه<عدد> = دوسرې_لیکه()\n"
                     "د.پای_ته_ورزیات_کړه(۲)\n"
                     "د.سر_ته_ورزیات_کړه(۱)\n"
                     "کرښه چاپ(د.لړۍ_کړه()، \" \"، د.له_پایه_واخله())\n"
                     "متغیر ځ: دوه_اړخیز_ځنځیر<عدد> = دوه_اړخیز_ځنځیر()\n"
                     "ځ.پای_ته_ورزیات_کړه(۱)\n"
                     "ځ.پای_ته_ورزیات_کړه(۲)\n"
                     "کرښه چاپ(ځ.سرچپه_لړۍ_کړه())\n"
                     "متغیر ګ: ګراف<متن> = ګراف()\n"
                     "ګ.ونښلوه(\"ا\"، \"ب\")\n"
                     "ګ.ونښلوه(\"ب\"، \"ج\")\n"
                     "کرښه چاپ(ګ.لنډه_لار(\"ا\"، \"ج\")، \" \"، ګ.پلن_لټون(\"ج\"))\n"
                     "متغیر و = مختاړې_ونه()\n"
                     "و.ورزیات_کړه(\"کور\")\n"
                     "و.ورزیات_کړه(\"کار\")\n"
                     "کرښه چاپ(و.کچه()، \" \"، و.مختاړی_لري(\"ک\"))\n"
                     "کرښه چاپ(متنونه.ومومه(\"سلام نړۍ\"، \"نړۍ\")، \" \"، متنونه.سرچپه_ورته_ده(\"۱۲۳۲۱\")، \" \"،\n"
                     "        متنونه.د_بدلون_واټن(\"کور\"، \"کار\"))\n"),
        "[۱، ۲] ۲\n[۲، ۱]\n[«ا»، «ب»، «ج»] [«ج»، «ب»، «ا»]\n۲ رښتیا\n۵ رښتیا ۱\n");
}

TEST_CASE("sdk: every English structure and algorithm has a Pashto counterpart")
{
    // The modules are generic templates, so nothing is compiled until they are used; the
    // honest parity check is the declarations themselves: the same number of functions and
    // structs in both languages (a function added to one and forgotten in the other fails).
    const auto count = [](const fs::path& path, const std::string& func_word, const std::string& struct_word)
    {
        std::ifstream file(path, std::ios::binary);
        std::string line;
        std::size_t functions = 0;
        std::size_t structs = 0;
        while (std::getline(file, line))
        {
            const std::size_t indent = line.find_first_not_of(' ');
            const std::string_view body =
                indent == std::string::npos ? std::string_view() : std::string_view(line).substr(indent);
            if (body.starts_with(func_word + " "))
            {
                ++functions; // functions and methods alike (methods are indented in their struct)
            }
            if (body.starts_with(struct_word + " "))
            {
                ++structs;
            }
        }
        return std::pair{functions, structs};
    };
    const fs::path sdk = fs::path(THUNDER_SOURCE_DIR) / "lib" / "thunder";
    for (const std::string& module : {std::string("datastructures"), std::string("algorithms")})
    {
        const auto english = count(sdk / "en" / (module + ".th"), "func", "struct");
        const auto pashto = count(sdk / "ps" / (module + ".th"), "دنده", "جوړښت");
        CHECK_EQ(english.first, pashto.first);
        CHECK_EQ(english.second, pashto.second);
    }
}
