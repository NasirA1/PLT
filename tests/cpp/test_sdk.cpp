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

TEST_CASE("sdk: the stack is last in, first out")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var s: Stack<int> = new_stack()\n"
                     "println(stack_is_empty(s))\n"
                     "stack_push(s, 1)\nstack_push(s, 2)\nstack_push(s, 3)\n"
                     "println(stack_pop(s), \" \", stack_peek(s), \" \", stack_size(s), \" \", stack_is_empty(s))\n"),
        "true\n3 2 2 false\n");
}

TEST_CASE("sdk: the queue is first in, first out, and reclaims served space")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var q: Queue<int> = new_queue()\n"
                     "println(queue_is_empty(q))\n"
                     "for var i = 1, i <= 30, i = i + 1\n"
                     "    queue_add(q, i)\n"
                     "var first_25 = 0\n"
                     "for var i = 0, i < 25, i = i + 1\n"
                     "    first_25 = queue_take(q)\n"
                     "println(first_25, \" \", queue_peek(q), \" \", queue_size(q), \" \", queue_is_empty(q))\n"
                     "println(len(q.items) <= 10)\n"), // the served space really was reclaimed
        "true\n25 26 5 false\ntrue\n");
}

TEST_CASE("sdk: the linked list adds at both ends, removes and walks in order")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var l: LinkedList<int> = new_linked_list()\n"
                     "list_add_last(l, 2)\nlist_add_first(l, 1)\nlist_add_last(l, 3)\n"
                     "println(list_to_array(l), \" \", list_size(l))\n"
                     "println(list_has(l, 3), \" \", list_has(l, 9))\n"
                     "println(list_remove_value(l, 2), \" \", list_remove_value(l, 9), \" \", list_to_array(l))\n"
                     "println(list_remove_first(l), \" \", list_to_array(l), \" \", list_size(l))\n"),
        "[1, 2, 3] 3\ntrue false\ntrue false [1, 3]\n1 [3] 1\n");
}

TEST_CASE("sdk: the binary search tree keeps order, ignores duplicates and removes correctly")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var t: BinaryTree<int> = new_tree()\n"
                     "for n in [5, 3, 8, 1, 4, 7, 9, 5, 2, 6]\n"
                     "    tree_insert(t, n)\n"
                     "println(tree_sorted(t), \" \", tree_size(t), \" \", tree_height(t))\n"
                     "println(tree_has(t, 7), \" \", tree_has(t, 10), \" \", tree_smallest(t), \" \", tree_largest(t))\n"
                     // the root with two children, a missing value, a leaf, a node with one child
                     "println(tree_remove(t, 5), \" \", tree_remove(t, 42), \" \", tree_sorted(t))\n"
                     "println(tree_remove(t, 1), \" \", tree_remove(t, 9), \" \", tree_sorted(t), \" \", tree_size(t))\n"),
        "[1, 2, 3, 4, 5, 6, 7, 8, 9] 9 4\n"
        "true false 1 9\n"
        "true false [1, 2, 3, 4, 6, 7, 8, 9]\n"
        "true true [2, 3, 4, 6, 7, 8] 6\n");
}

TEST_CASE("sdk: the heap always gives the smallest value first")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var h: Heap<int> = new_heap()\n"
                     "for n in [9, 4, 7, 1, 3, 8, 1]\n"
                     "    heap_add(h, n)\n"
                     "var taken: int[] = []\n"
                     "while not heap_is_empty(h)\n"
                     "    push(taken, heap_take(h))\n"
                     "println(taken, \" \", heap_size(h))\n"),
        "[1, 1, 3, 4, 7, 8, 9] 0\n");
}

TEST_CASE("sdk: every sort sorts, including the edge cases")
{
    // Each sort over the awkward shapes: empty, one element, duplicates, sorted, reversed.
    const char* sorts[] = {"bubble_sort", "selection_sort", "insertion_sort", "quick_sort", "merge_sort"};
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
                     "println(linear_search(values, 7), \" \", linear_search(values, 2))\n"
                     "println(binary_search(values, 1), \" \", binary_search(values, 11), \" \",\n"
                     "        binary_search(values, 7), \" \", binary_search(values, 2))\n"
                     "var empty: int[] = []\n"
                     "println(linear_search(empty, 1), \" \", binary_search(empty, 1))\n"),
        "3 -1\n0 5 3 -1\n-1 -1\n");
}

TEST_CASE("sdk: is_sorted, index_of_smallest and a repeatable shuffle")
{
    CHECK_EQ(run_sdk("import \"algorithms\"\n"
                     "var none: int[] = []\n"
                     "println(is_sorted([1, 2, 2, 3]), \" \", is_sorted([2, 1]), \" \", is_sorted(none))\n"
                     "println(index_of_smallest([4, 1, 3]), \" \", index_of_smallest(none))\n"
                     "seed_random(1)\n"
                     "var values = [1, 2, 3, 4, 5, 6, 7, 8]\n"
                     "shuffle(values)\n"
                     "var shuffled = copy(values)\n"
                     "quick_sort(values)\n"
                     "println(len(shuffled), \" \", values)\n"),
        "true false true\n1 -1\n8 [1, 2, 3, 4, 5, 6, 7, 8]\n");
}

TEST_CASE("sdk: the Pashto modules behave exactly like the English ones")
{
    // The same scenarios through the Pashto names; the outputs are the Pashto spellings.
    CHECK_EQ(run_sdk("# ژبه: ps\n"
                     "راوړه \"datastructures\"\n"
                     "راوړه \"algorithms\"\n"
                     "متغیر د: ډېرۍ<عدد> = نوې_ډېرۍ()\n"
                     "ډېرۍ_ته_ورزیات_کړه(د، ۱)\nډېرۍ_ته_ورزیات_کړه(د، ۲)\n"
                     "کرښه چاپ(له_ډېرۍ_واخله(د)، \" \"، د_ډېرۍ_کچه(د))\n"
                     "متغیر ل: لیکه<عدد> = نوې_لیکه()\n"
                     "لیکې_ته_ورزیات_کړه(ل، ۱)\nلیکې_ته_ورزیات_کړه(ل، ۲)\n"
                     "کرښه چاپ(له_لیکې_واخله(ل)، \" \"، لیکه_وګوره(ل))\n"
                     "متغیر ځ: ځنځیر<عدد> = نوی_ځنځیر()\n"
                     "ځنځیر_پای_ته_ورزیات_کړه(ځ، ۲)\nځنځیر_سر_ته_ورزیات_کړه(ځ، ۱)\n"
                     "کرښه چاپ(ځنځیر_لړۍ_کړه(ځ)، \" \"، ځنځیر_لري(ځ، ۲)، \" \"، له_ځنځیره_لیرې_کړه(ځ، ۲)، \" \"، د_ځنځیر_کچه(ځ))\n"
                     "متغیر و: ونه<عدد> = نوې_ونه()\n"
                     "لپاره ن په [۵، ۳، ۸، ۱]\n"
                     "    ونې_ته_ورزیات_کړه(و، ن)\n"
                     "کرښه چاپ(ونه_په_ترتیب(و)، \" \"، د_ونې_لوړوالی(و)، \" \"، له_ونې_لیرې_کړه(و، ۳)، \" \"، ونه_په_ترتیب(و))\n"
                     "متغیر م: لومړیتوب_لیکه<عدد> = نوې_لومړیتوب_لیکه()\n"
                     "لپاره ن په [۴، ۱، ۳]\n"
                     "    لومړیتوب_لیکې_ته_ورزیات_کړه(م، ن)\n"
                     "کرښه چاپ(له_لومړیتوب_لیکې_واخله(م)، \" \"، لومړیتوب_لیکه_وګوره(م))\n"
                     "متغیر لړ = [۵، ۲، ۹، ۱]\n"
                     "چټک_ترتیب(لړ)\n"
                     "کرښه چاپ(لړ، \" \"، نیمایي_لټون(لړ، ۹)، \" \"، ترتیب_شوې_ده(لړ))\n"),
        "۲ ۱\n۱ ۲\n[۱، ۲] رښتیا رښتیا ۱\n[۱، ۳، ۵، ۸] ۳ رښتیا [۱، ۵، ۸]\n۱ ۳\n[۱، ۲، ۵، ۹] ۳ رښتیا\n");
}

TEST_CASE("sdk: the set keeps every value once, in first-added order")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var seen: Set<string> = new_set()\n"
                     "println(set_is_empty(seen))\n"
                     "for color in [\"red\", \"blue\", \"red\", \"green\", \"blue\"]\n"
                     "    set_add(seen, color)\n"
                     "println(set_values(seen), \" \", set_size(seen))\n"
                     "println(set_has(seen, \"blue\"), \" \", set_has(seen, \"pink\"))\n"
                     "println(set_remove(seen, \"blue\"), \" \", set_remove(seen, \"blue\"), \" \", set_values(seen))\n"),
        "true\n[\"red\", \"blue\", \"green\"] 3\ntrue false\ntrue false [\"red\", \"green\"]\n");
    // Union, intersection and difference.
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var a = Set(entries: {1: true, 2: true, 3: true})\n"
                     "var b = Set(entries: {3: true, 4: true})\n"
                     "println(set_values(set_union(a, b)), \" \", set_values(set_intersection(a, b)), \" \",\n"
                     "        set_values(set_difference(a, b)))\n"),
        "[1, 2, 3, 4] [3] [1, 2]\n");
}

TEST_CASE("sdk: the ordered set keeps unique values sorted")
{
    CHECK_EQ(run_sdk("import \"datastructures\"\n"
                     "var scores: OrderedSet<int> = new_ordered_set()\n"
                     "println(ordered_set_is_empty(scores))\n"
                     "for n in [70, 40, 90, 70, 40]\n"
                     "    ordered_set_add(scores, n)\n"
                     "println(ordered_set_values(scores), \" \", ordered_set_size(scores))\n"
                     "println(ordered_set_smallest(scores), \" \", ordered_set_largest(scores))\n"
                     "println(ordered_set_has(scores, 40), \" \", ordered_set_has(scores, 41))\n"
                     "println(ordered_set_remove(scores, 70), \" \", ordered_set_remove(scores, 70), \" \",\n"
                     "        ordered_set_values(scores))\n"),
        "true\n[40, 70, 90] 3\n40 90\ntrue false\ntrue false [40, 90]\n");
    // The same through the Pashto names.
    CHECK_EQ(run_sdk("# ژبه: ps\n"
                     "راوړه \"datastructures\"\n"
                     "متغیر ټ: ټولګه<عدد> = نوې_ټولګه()\n"
                     "لپاره ن په [۲، ۱، ۲]\n"
                     "    ټولګې_ته_ورزیات_کړه(ټ، ن)\n"
                     "کرښه چاپ(د_ټولګې_ارزښتونه(ټ)، \" \"، ټولګه_لري(ټ، ۱)، \" \"، له_ټولګې_لیرې_کړه(ټ، ۹))\n"
                     "متغیر ت: ترتیب_شوې_ټولګه<عدد> = نوې_ترتیب_شوې_ټولګه()\n"
                     "لپاره ن په [۳، ۱، ۲، ۳]\n"
                     "    ترتیب_شوې_ټولګې_ته_ورزیات_کړه(ت، ن)\n"
                     "کرښه چاپ(د_ترتیب_شوې_ټولګې_ارزښتونه(ت)، \" \"، د_ترتیب_شوې_ټولګې_کمینه(ت))\n"),
        "[۲، ۱] رښتیا درواغ\n[۱، ۲، ۳] ۱\n");
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
