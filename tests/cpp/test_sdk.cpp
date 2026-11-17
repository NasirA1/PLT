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
                     "var s: stack.Stack<int> = stack.new()\n"
                     "println(stack.is_empty(s))\n"
                     "stack.push(s, 1)\nstack.push(s, 2)\nstack.push(s, 3)\n"
                     "println(stack.pop(s), \" \", stack.peek(s), \" \", stack.size(s), \" \", stack.is_empty(s))\n"),
        "true\n3 2 2 false\n");
}

TEST_CASE("sdk: the queue is first in, first out, and reclaims served space")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var q: queue.Queue<int> = queue.new()\n"
                     "println(queue.is_empty(q))\n"
                     "for var i = 1, i <= 30, i = i + 1\n"
                     "    queue.add(q, i)\n"
                     "var first_25 = 0\n"
                     "for var i = 0, i < 25, i = i + 1\n"
                     "    first_25 = queue.take(q)\n"
                     "println(first_25, \" \", queue.peek(q), \" \", queue.size(q), \" \", queue.is_empty(q))\n"
                     "println(len(q.items) <= 10)\n"), // the served space really was reclaimed
        "true\n25 26 5 false\ntrue\n");
}

TEST_CASE("sdk: the linked list adds at both ends, removes and walks in order")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var l: linked_list.LinkedList<int> = linked_list.new()\n"
                     "linked_list.add_last(l, 2)\nlinked_list.add_first(l, 1)\nlinked_list.add_last(l, 3)\n"
                     "println(linked_list.to_array(l), \" \", linked_list.size(l))\n"
                     "println(linked_list.has(l, 3), \" \", linked_list.has(l, 9))\n"
                     "println(linked_list.remove_value(l, 2), \" \", linked_list.remove_value(l, 9), \" \", linked_list.to_array(l))\n"
                     "println(linked_list.remove_first(l), \" \", linked_list.to_array(l), \" \", linked_list.size(l))\n"),
        "[1, 2, 3] 3\ntrue false\ntrue false [1, 3]\n1 [3] 1\n");
}

TEST_CASE("sdk: the binary search tree keeps order, ignores duplicates and removes correctly")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var t: tree.BinaryTree<int> = tree.new()\n"
                     "for n in [5, 3, 8, 1, 4, 7, 9, 5, 2, 6]\n"
                     "    tree.insert(t, n)\n"
                     "println(tree.sorted(t), \" \", tree.size(t), \" \", tree.height(t))\n"
                     "println(tree.has(t, 7), \" \", tree.has(t, 10), \" \", tree.smallest(t), \" \", tree.largest(t))\n"
                     // the root with two children, a missing value, a leaf, a node with one child
                     "println(tree.remove(t, 5), \" \", tree.remove(t, 42), \" \", tree.sorted(t))\n"
                     "println(tree.remove(t, 1), \" \", tree.remove(t, 9), \" \", tree.sorted(t), \" \", tree.size(t))\n"),
        "[1, 2, 3, 4, 5, 6, 7, 8, 9] 9 4\n"
        "true false 1 9\n"
        "true false [1, 2, 3, 4, 6, 7, 8, 9]\n"
        "true true [2, 3, 4, 6, 7, 8] 6\n");
}

TEST_CASE("sdk: the heap always gives the smallest value first")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var h: heap.Heap<int> = heap.new()\n"
                     "for n in [9, 4, 7, 1, 3, 8, 1]\n"
                     "    heap.add(h, n)\n"
                     "var taken: int[] = []\n"
                     "while not heap.is_empty(h)\n"
                     "    push(taken, heap.take(h))\n"
                     "println(taken, \" \", heap.size(h))\n"),
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
                     "متغیر د: ډېرۍ.ډېرۍ<عدد> = ډېرۍ.نوې()\n"
                     "ډېرۍ.ورزیات_کړه(د، ۱)\nډېرۍ.ورزیات_کړه(د، ۲)\n"
                     "کرښه چاپ(ډېرۍ.واخله(د)، \" \"، ډېرۍ.کچه(د))\n"
                     "متغیر ل: لیکه.لیکه<عدد> = لیکه.نوې()\n"
                     "لیکه.ورزیات_کړه(ل، ۱)\nلیکه.ورزیات_کړه(ل، ۲)\n"
                     "کرښه چاپ(لیکه.واخله(ل)، \" \"، لیکه.وګوره(ل))\n"
                     "متغیر ځ: ځنځیر.ځنځیر<عدد> = ځنځیر.نوی()\n"
                     "ځنځیر.پای_ته_ورزیات_کړه(ځ، ۲)\nځنځیر.سر_ته_ورزیات_کړه(ځ، ۱)\n"
                     "کرښه چاپ(ځنځیر.لړۍ_کړه(ځ)، \" \"، ځنځیر.لري(ځ، ۲)، \" \"، ځنځیر.لیرې_کړه(ځ، ۲)، \" \"، ځنځیر.کچه(ځ))\n"
                     "متغیر و: ونه.ونه<عدد> = ونه.نوې()\n"
                     "تکرار ن په [۵، ۳، ۸، ۱]\n"
                     "    ونه.ورزیات_کړه(و، ن)\n"
                     "کرښه چاپ(ونه.په_ترتیب(و)، \" \"، ونه.لوړوالی(و)، \" \"، ونه.لیرې_کړه(و، ۳)، \" \"، ونه.په_ترتیب(و))\n"
                     "متغیر م: لومړیتوب_لیکه.لومړیتوب_لیکه<عدد> = لومړیتوب_لیکه.نوې()\n"
                     "تکرار ن په [۴، ۱، ۳]\n"
                     "    لومړیتوب_لیکه.ورزیات_کړه(م، ن)\n"
                     "کرښه چاپ(لومړیتوب_لیکه.واخله(م)، \" \"، لومړیتوب_لیکه.وګوره(م))\n"
                     "متغیر لړ = [۵، ۲، ۹، ۱]\n"
                     "ترتیب.چټک(لړ)\n"
                     "کرښه چاپ(لړ، \" \"، لټون.نیمایي(لړ، ۹)، \" \"، ترتیب.شوې_ده(لړ))\n"),
        "۲ ۱\n۱ ۲\n[۱، ۲] رښتیا رښتیا ۱\n[۱، ۳، ۵، ۸] ۳ رښتیا [۱، ۵، ۸]\n۱ ۳\n[۱، ۲، ۵، ۹] ۳ رښتیا\n");
}

TEST_CASE("sdk: the set keeps every value once, in first-added order")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var seen: set.Set<string> = set.new()\n"
                     "println(set.is_empty(seen))\n"
                     "for color in [\"red\", \"blue\", \"red\", \"green\", \"blue\"]\n"
                     "    set.add(seen, color)\n"
                     "println(set.values(seen), \" \", set.size(seen))\n"
                     "println(set.has(seen, \"blue\"), \" \", set.has(seen, \"pink\"))\n"
                     "println(set.remove(seen, \"blue\"), \" \", set.remove(seen, \"blue\"), \" \", set.values(seen))\n"),
        "true\n[\"red\", \"blue\", \"green\"] 3\ntrue false\ntrue false [\"red\", \"green\"]\n");
    // Union, intersection and difference.
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var a = set.Set(entries: {1: true, 2: true, 3: true})\n"
                     "var b = set.Set(entries: {3: true, 4: true})\n"
                     "println(set.values(set.union(a, b)), \" \", set.values(set.intersection(a, b)), \" \",\n"
                     "        set.values(set.difference(a, b)))\n"),
        "[1, 2, 3, 4] [3] [1, 2]\n");
}

TEST_CASE("sdk: the ordered set keeps unique values sorted")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var scores: ordered_set.OrderedSet<int> = ordered_set.new()\n"
                     "println(ordered_set.is_empty(scores))\n"
                     "for n in [70, 40, 90, 70, 40]\n"
                     "    ordered_set.add(scores, n)\n"
                     "println(ordered_set.values(scores), \" \", ordered_set.size(scores))\n"
                     "println(ordered_set.smallest(scores), \" \", ordered_set.largest(scores))\n"
                     "println(ordered_set.has(scores, 40), \" \", ordered_set.has(scores, 41))\n"
                     "println(ordered_set.remove(scores, 70), \" \", ordered_set.remove(scores, 70), \" \",\n"
                     "        ordered_set.values(scores))\n"),
        "true\n[40, 70, 90] 3\n40 90\ntrue false\ntrue false [40, 90]\n");
    // The same through the Pashto names.
    CHECK_EQ(run_sdk("# ژبه: ps\n"
                     "راوړه \"datastructures\"\n"
                     "متغیر ټ: ټولګه.ټولګه<عدد> = ټولګه.نوې()\n"
                     "تکرار ن په [۲، ۱، ۲]\n"
                     "    ټولګه.ورزیات_کړه(ټ، ن)\n"
                     "کرښه چاپ(ټولګه.ارزښتونه(ټ)، \" \"، ټولګه.لري(ټ، ۱)، \" \"، ټولګه.لیرې_کړه(ټ، ۹))\n"
                     "متغیر ت: ترتیب_شوې_ټولګه.ترتیب_شوې_ټولګه<عدد> = ترتیب_شوې_ټولګه.نوې()\n"
                     "تکرار ن په [۳، ۱، ۲، ۳]\n"
                     "    ترتیب_شوې_ټولګه.ورزیات_کړه(ت، ن)\n"
                     "کرښه چاپ(ترتیب_شوې_ټولګه.ارزښتونه(ت)، \" \"، ترتیب_شوې_ټولګه.کمینه(ت))\n"),
        "[۲، ۱] رښتیا درواغ\n[۱، ۲، ۳] ۱\n");
}

TEST_CASE("sdk: the deque serves both ends in O(1)")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var d: deque.Deque<int> = deque.new()\n"
                     "println(deque.is_empty(d))\n"
                     "deque.add_last(d, 2)\ndeque.add_first(d, 1)\ndeque.add_last(d, 3)\n"
                     "println(deque.to_array(d), \" \", deque.size(d))\n"
                     "println(deque.take_first(d), \" \", deque.take_last(d), \" \",\n"
                     "        deque.peek_first(d), \" \", deque.peek_last(d), \" \", deque.to_array(d))\n"
                     // Draining across the refill boundary keeps the order.
                     "var e: deque.Deque<int> = deque.new()\n"
                     "for var i = 1, i <= 5, i = i + 1\n"
                     "    deque.add_last(e, i)\n"
                     "var out: int[] = []\n"
                     "while not deque.is_empty(e)\n"
                     "    push(out, deque.take_last(e))\n"
                     "println(out)\n"),
        "true\n[1, 2, 3] 3\n1 3 2 2 [2]\n[5, 4, 3, 2, 1]\n");
}

TEST_CASE("sdk: the doubly linked list walks both ways and removes anywhere")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var l: doubly_linked_list.DoublyLinkedList<string> = doubly_linked_list.new()\n"
                     "doubly_linked_list.add_last(l, \"b\")\ndoubly_linked_list.add_first(l, \"a\")\ndoubly_linked_list.add_last(l, \"c\")\n"
                     "println(doubly_linked_list.to_array(l), \" \", doubly_linked_list.to_array_reversed(l), \" \", doubly_linked_list.size(l))\n"
                     "println(doubly_linked_list.take_first(l), \" \", doubly_linked_list.take_last(l), \" \", doubly_linked_list.to_array(l))\n"
                     // removing the middle, the head, the tail and a missing value
                     "for value in [\"x\", \"y\", \"z\"]\n"
                     "    doubly_linked_list.add_last(l, value)\n"
                     "println(doubly_linked_list.remove_value(l, \"y\"), \" \", doubly_linked_list.remove_value(l, \"q\"), \" \", doubly_linked_list.to_array(l))\n"
                     "println(doubly_linked_list.remove_value(l, \"b\"), \" \", doubly_linked_list.to_array(l), \" \", doubly_linked_list.to_array_reversed(l))\n"),
        "[\"a\", \"b\", \"c\"] [\"c\", \"b\", \"a\"] 3\na c [\"b\"]\ntrue false [\"b\", \"x\", \"z\"]\ntrue [\"x\", \"z\"] [\"z\", \"x\"]\n");
}

TEST_CASE("sdk: the graph searches breadth first, depth first and finds shortest paths")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var g: graph.Graph<string> = graph.new()\n"
                     "graph.connect(g, \"a\", \"b\")\n"
                     "graph.connect(g, \"b\", \"c\")\n"
                     "graph.connect(g, \"a\", \"d\")\n"
                     "graph.connect(g, \"d\", \"c\")\n"
                     "graph.add_edge(g, \"c\", \"e\")\n"
                     "graph.add_vertex(g, \"lonely\")\n"
                     "println(graph.vertices(g), \" \", graph.neighbors(g, \"a\"), \" \", graph.neighbors(g, \"?\"))\n"
                     "println(graph.bfs(g, \"a\"))\n"
                     "println(graph.dfs(g, \"a\"))\n"
                     "println(graph.shortest_path(g, \"a\", \"e\"), \" \", graph.shortest_path(g, \"a\", \"a\"))\n"
                     // e has no outgoing edges: the direction matters
                     "println(graph.has_path(g, \"e\", \"a\"), \" \", graph.has_path(g, \"a\", \"lonely\"))\n"),
        "[\"a\", \"b\", \"c\", \"d\", \"e\", \"lonely\"] [\"b\", \"d\"] []\n"
        "[\"a\", \"b\", \"d\", \"c\", \"e\"]\n"
        "[\"a\", \"b\", \"c\", \"d\", \"e\"]\n"
        "[\"a\", \"b\", \"c\", \"e\"] [\"a\"]\n"
        "false false\n");
}

TEST_CASE("sdk: the trie answers word and prefix questions")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var t = trie.new()\n"
                     "for word in [\"car\", \"cart\", \"cat\", \"dog\", \"car\"]\n"
                     "    trie.add(t, word)\n"
                     "println(trie.words(t), \" \", trie.size(t))\n"
                     "println(trie.has(t, \"car\"), \" \", trie.has(t, \"ca\"), \" \", trie.has(t, \"carts\"))\n"
                     "println(trie.has_prefix(t, \"ca\"), \" \", trie.has_prefix(t, \"\"), \" \", trie.has_prefix(t, \"x\"))\n"),
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
                     "متغیر د: دوسرې_لیکه.دوسرې_لیکه<عدد> = دوسرې_لیکه.نوې()\n"
                     "دوسرې_لیکه.پای_ته_ورزیات_کړه(د، ۲)\n"
                     "دوسرې_لیکه.سر_ته_ورزیات_کړه(د، ۱)\n"
                     "کرښه چاپ(دوسرې_لیکه.لړۍ_کړه(د)، \" \"، دوسرې_لیکه.له_پایه_واخله(د))\n"
                     "متغیر ځ: دوه_اړخیز_ځنځیر.دوه_اړخیز_ځنځیر<عدد> = دوه_اړخیز_ځنځیر.نوی()\n"
                     "دوه_اړخیز_ځنځیر.پای_ته_ورزیات_کړه(ځ، ۱)\n"
                     "دوه_اړخیز_ځنځیر.پای_ته_ورزیات_کړه(ځ، ۲)\n"
                     "کرښه چاپ(دوه_اړخیز_ځنځیر.سرچپه_لړۍ_کړه(ځ))\n"
                     "متغیر ګ: ګراف.ګراف<متن> = ګراف.نوی()\n"
                     "ګراف.ونښلوه(ګ، \"ا\"، \"ب\")\n"
                     "ګراف.ونښلوه(ګ، \"ب\"، \"ج\")\n"
                     "کرښه چاپ(ګراف.لنډه_لار(ګ، \"ا\"، \"ج\")، \" \"، ګراف.پلن_لټون(ګ، \"ج\"))\n"
                     "متغیر و = مختاړې_ونه.نوې()\n"
                     "مختاړې_ونه.ورزیات_کړه(و، \"کور\")\n"
                     "مختاړې_ونه.ورزیات_کړه(و، \"کار\")\n"
                     "کرښه چاپ(مختاړې_ونه.کچه(و)، \" \"، مختاړې_ونه.مختاړی_لري(و، \"ک\"))\n"
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
            if (line.starts_with(func_word + " "))
            {
                ++functions;
            }
            if (line.starts_with(struct_word + " "))
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
