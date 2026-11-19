// Tests marked with annotations (@test, @mock) and the object-oriented testing SDK
// (lib/thunder/<language>/testing.th): finding the tests, suites as structs and modules, mocks,
// running chosen tests, and keeping test code and production code apart.

#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"
#include "thunder/report.hpp"
#include "thunder/translate.hpp"
#include "thunder/vm.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace thunder;

namespace
{
    using Codes = std::vector<std::string>;

    CompileOptions sdk_options(std::map<std::string, std::string> files = {})
    {
        CompileOptions options;
        options.sdk_module_path = (fs::path(THUNDER_SOURCE_DIR) / "lib" / "thunder").generic_string();
        if (!files.empty())
        {
            // Imported files of the test come from memory; everything else (the SDK) from disk.
            options.read_file = [files](const std::string& path) -> std::optional<std::string>
            {
                const std::string name = fs::path(path).filename().generic_string();
                if (const auto found = files.find(name); found != files.end())
                {
                    return found->second;
                }
                std::error_code error;
                if (!fs::is_regular_file(path, error))
                {
                    return std::nullopt;
                }
                std::ifstream in(path, std::ios::binary);
                return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            };
        }
        return options;
    }

    struct Run
    {
        std::string output;
        int exit_code = 0;
        std::vector<TestCase> tests;
    };

    // Compiles 'text' (with the SDK, and 'files' importable by name), which must succeed, and runs
    // it with 'arguments'.
    Run run_tests(const std::string& text, std::vector<std::string> arguments = {},
        std::map<std::string, std::string> files = {})
    {
        const SourceFile source("program_test.th", text);
        const CompileResult result = compile(source, english_pack(), sdk_options(std::move(files)));
        std::string messages;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            messages += format_short(diagnostic, source.name(), *result.language) + "\n";
        }
        CHECK_EQ(messages, "");
        CHECK_EQ(result.internal_error, "");
        REQUIRE(result.program.has_value());
        std::ostringstream output;
        Vm vm(*result.program, output);
        vm.set_arguments(std::move(arguments));
        const ExecResult exec = vm.run();
        CHECK_EQ(exec.error ? format_runtime_error(*exec.error, source.name(), *result.language) : "", "");
        return {output.str(), exec.exit_code, result.tests};
    }

    Codes codes_of(const std::string& text, std::map<std::string, std::string> files = {})
    {
        const SourceFile source("program.th", text);
        const CompileResult result = compile(source, english_pack(), sdk_options(std::move(files)));
        Codes codes;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            codes.push_back(diagnostic.code);
        }
        return codes;
    }
}

TEST_CASE("testing: functions marked @test run in order, without a line of registration")
{
    const std::string program =
        "@test\nfunc adds()\n    expect.equal(2 + 2, 4)\n    expect.is_true(true)\n"
        "@test(\"lists compare by contents\")\nfunc lists()\n    expect.equal([1, 2], [1, 2])\n"
        "    expect.not_equal([1], [2])\n    expect.near(0.1 + 0.2, 0.3, 0.001)\n"
        "@test\nfunc wrong_on_purpose()\n    expect.equal(\"a\", \"b\")\n    expect.is_false(true)\n"
        "    test.fail(\"and a message of my own\")\n"
        "func helper(): int\n    return 1\n";
    const Run all = run_tests(program);
    CHECK_EQ(all.output,
        "✓ adds\n✓ lists compare by contents\n"
        "✗ wrong on purpose: expected b, got a\n✗ wrong on purpose: expected false, got true\n"
        "✗ wrong on purpose: and a message of my own\n"
        "2 passed, 1 failed\n");
    CHECK_EQ(all.exit_code, 1);
    REQUIRE(all.tests.size() == 3);
    CHECK_EQ(all.tests[1].name, "lists compare by contents");
    CHECK_EQ(all.tests[1].function, "lists");
    CHECK_EQ(all.tests[1].span.start.line, std::uint32_t{5});
    // Only the tests named on the command line run; everything passing is exit status 0.
    const Run some = run_tests(program, {"lists compare by contents", "adds"});
    CHECK_EQ(some.output, "✓ adds\n✓ lists compare by contents\n2 passed, 0 failed\n");
    CHECK_EQ(some.exit_code, 0);
}

TEST_CASE("testing: a struct is a suite - a new instance per test, its constructor and close() around it")
{
    const std::string program =
        "@test(\"a counter\")\nstruct CounterTests\n    _count: int\n\n"
        "    CounterTests()\n        _count = 10\n\n"
        "    func starts_at_ten()\n        expect.equal(_count, 10)\n\n"
        "    func bumps()\n        _count += 1\n        expect.equal(_count, 11)\n\n"
        "    func bumps_again()\n        _count += 1\n        expect.equal(_count, 11)\n\n"   // a fresh instance
        "    func _helper()\n        pass\n\n"                                                   // private: not a test
        "    func needs(n: int)\n        pass\n\n"                                               // parameters: not a test
        "    func answer(): int\n        return 1\n\n"                                           // a result: not a test
        "    func close()\n        println(\"  closed\")\n";
    const Run run = run_tests(program);
    CHECK_EQ(run.output, "a counter\n  closed\n  ✓ starts at ten\n  closed\n  ✓ bumps\n  closed\n  ✓ bumps again\n3 passed, 0 failed\n");
    REQUIRE(run.tests.size() == 3);
    CHECK_EQ(run.tests[0].suite, "a counter");
    CHECK_EQ(run.tests[2].function, "CounterTests.bumps_again");
    // A whole suite, or one test of it, by name.
    CHECK_EQ(run_tests(program, {"a counter/bumps"}).output, "a counter\n  closed\n  ✓ bumps\n1 passed, 0 failed\n");
    // An unmarked struct with one marked method is a suite of that one test.
    CHECK_EQ(run_tests("struct Checks\n    func one()\n        expect.is_true(true)\n"
                       "    @test(\"the second\")\n    func two()\n        expect.is_true(true)\n").output,
        "Checks\n  ✓ the second\n1 passed, 0 failed\n");
}

TEST_CASE("testing: a module is a suite, and suites count apart")
{
    const std::string program =
        "@test\nmodule numbers\n    func adds()\n        expect.equal(1 + 1, 2)\n"
        "    func _private()\n        pass\n    func helper(x: int): int\n        return x\n"
        "module text\n    @test(\"joins\")\n    func joins_words()\n        expect.equal(\"a\" + \"b\", \"ab\")\n"
        "    func not_a_test()\n        pass\n"
        "@test\nfunc outside()\n    expect.equal(numbers.helper(3), 3)\n";
    CHECK_EQ(run_tests(program).output,
        "numbers\n  ✓ adds\ntext\n  ✓ joins\n✓ outside\nnumbers: 1 passed, 0 failed\ntext: 1 passed, 0 failed\n3 passed, 0 failed\n");
    CHECK_EQ(run_tests(program, {"text"}).output, "text\n  ✓ joins\n1 passed, 0 failed\n");
}

TEST_CASE("testing: @mock records the calls of an interface's methods and answers for them")
{
    const std::string program =
        "interface Store\n    func load(key: string): int\n    func save(key: string, value: int)\n"
        "    func name(): string\n"
        "func double_it(store: Store, key: string)\n    store.save(key, store.load(key) * 2)\n"
        "@mock\nstruct FakeStore: Store\n    answer: int\n    func load(key: string): int\n        return answer\n"
        "@mock\nstruct EmptyStore: Store\n"
        "@test\nfunc doubles()\n    var store = FakeStore(answer: 21)\n    double_it(store, \"k\")\n"
        "    expect.equal(store.calls(), [\"load(k)\", \"save(k, 42)\"])\n"
        "    expect.is_true(store.called(\"save\"))\n    expect.is_false(store.called(\"name\"))\n"
        "    expect.equal(store.call_count(\"load\"), 1)\n    expect.equal(store.arguments_of(\"save\"), [[\"k\", \"42\"]])\n"
        "    store.expect_called(\"save\", 1)\n    store.forget_calls()\n    expect.equal(len(store.calls()), 0)\n"
        "@test\nfunc defaults()\n    var store = EmptyStore()\n    expect.equal(store.load(\"x\"), 0)\n"
        "    expect.equal(store.name(), \"\")\n    var through: Store = store\n    through.save(\"y\", 1)\n"
        "    expect.equal(store.calls(), [\"load(x)\", \"name()\", \"save(y, 1)\"])\n"
        "@test\nfunc not_called_enough()\n    var store = EmptyStore()\n    store.expect_called(\"load\", 2)\n";
    CHECK_EQ(run_tests(program).output,
        "✓ doubles\n✓ defaults\n"
        "✗ not called enough: expected load to be called 2 time(s), but it was called 0 time(s)\n"
        "2 passed, 1 failed\n");
    // A mock stands in for interfaces: it must list one.
    CHECK_EQ(codes_of("@test\nfunc t()\n    pass\n@mock\nstruct M\n    x: int\n"), Codes{"S094"});
}

TEST_CASE("testing: the shape of a test and of a suite")
{
    CHECK_EQ(codes_of("@test\nfunc t(x: int)\n    pass\n"), Codes{"S087"});
    CHECK_EQ(codes_of("@test\nfunc t(): int\n    return 1\n"), Codes{"S087"});
    CHECK_EQ(codes_of("@test\nfunc t<T>(x: T)\n    pass\n"), Codes{"S088"});
    CHECK_EQ(codes_of("@test\nstruct S<T>\n    x: T\n    func t()\n        pass\n"), Codes{"S088"});
    CHECK_EQ(codes_of("@test\nstruct S\n    S(n: int)\n        pass\n    func t()\n        pass\n"), Codes{"S090"});
    CHECK_EQ(codes_of("@test(\"same\")\nfunc a()\n    pass\n@test(\"same\")\nfunc b()\n    pass\n"), Codes{"S089"});
    CHECK_EQ(codes_of("@test\nmixin M\n    func t()\n        pass\n@test\nfunc a()\n    pass\n"), Codes{"S085"});
    CHECK_EQ(codes_of("@mock\nfunc a()\n    pass\n@test\nfunc b()\n    pass\n"), Codes{"S085"});
    CHECK_EQ(codes_of("@test\ninterface I\n    func f()\n"), Codes{"S085"});
    // @mock takes no description; a test's description may not be empty.
    CHECK_EQ(codes_of("interface I\n    func f()\n@mock(\"x\")\nstruct M: I\n@test\nfunc a()\n    pass\n"), Codes{"S086"});
    CHECK_EQ(codes_of("@test(\"\")\nfunc a()\n    pass\n"), Codes{"S086"});
    // Unknown annotations, and annotations with nothing to mark.
    CHECK_EQ(codes_of("@frob\nfunc a()\n    pass\n"), Codes{"P021"});
    CHECK_EQ(codes_of("@test\nvar x = 1\n"), Codes{"P022"});
    CHECK_EQ(codes_of("func a()\n    pass\n@test\n"), Codes{"P022"});
}

TEST_CASE("testing: test code stays out of production code, and production code out of test files")
{
    // A program may not use the testing module or declare a mock.
    CHECK_EQ(codes_of("import \"testing\"\nprintln(1)\n"), Codes{"S092"});
    CHECK_EQ(codes_of("interface I\n    func f()\n@mock\nstruct M: I\nprintln(1)\n"), Codes{"S092"});
    // ... nor import a test file (reported once, at the import).
    CHECK_EQ(codes_of("import \"other\"\nprintln(1)\n",
                 {{"other.th", "@test\nfunc a()\n    pass\n@test\nfunc b()\n    pass\n"}}),
        Codes{"S093"});
    // A test file holds tests only: no statements, no main().
    CHECK_EQ(codes_of("@test\nfunc a()\n    pass\nprintln(1)\n"), Codes{"S091"});
    CHECK_EQ(codes_of("@test\nfunc a()\n    pass\nfunc main()\n    pass\n"), Codes{"S091"});
    // Declarations are fine there - and the code under test comes from an import.
    const Run run = run_tests("import \"maths\"\nconst two = 2\n@test\nfunc doubles()\n    expect.equal(maths.twice(1), two)\n",
        {}, {{"maths.th", "module maths\n    func twice(n: int): int\n        return n * 2\n"}});
    CHECK_EQ(run.output, "✓ doubles\n1 passed, 0 failed\n");
    // A helper file of mocks may be imported by a test file (it has no tests of its own).
    CHECK_EQ(run_tests("import \"fakes\"\n@test\nfunc uses_the_fake()\n    var f = Fake()\n    f.go()\n    f.expect_called(\"go\", 1)\n",
                 {}, {{"fakes.th", "interface Goer\n    func go()\n@mock\nstruct Fake: Goer\n"}}).output,
        "✓ uses the fake\n1 passed, 0 failed\n");
}

TEST_CASE("testing: Pashto - @آزموینه in either spelling, @ساختګی, and the same transcript")
{
    const std::string program =
        "# ژبه: ps\n"
        "برسېر ساعت\n    دنده ساعت_یې(): عدد\n"
        "@ساختگی\nجوړښت ساختګی_ساعت: ساعت\n"          // the Persian gaf
        "@آزموينه\nدنده جمع()\n    تمه.برابر(۲ + ۲، ۴)\n    تمه.رښتیا_وي(رښتیا)\n"     // the Arabic yeh
        "@ازموینه(\"ناسمه\")\nدنده ناسمه_ده()\n    تمه.برابر(\"الف\"، \"ب\")\n    تمه.نږدې(۱.۰، ۲.۰، ۰.۵)\n"
        "@آزموینه\nجوړښت د_ساعت_ازموینې\n    دنده صفر_وايي()\n        متغیر س = ساختګی_ساعت()\n"
        "        تمه.برابر(س.ساعت_یې()، ۰)\n        س.تمه_د_بللو(\"ساعت_یې\"، ۱)\n";
    const Run all = run_tests(program);
    CHECK_EQ(all.output,
        "✓ جمع\n"
        "✗ ناسمه: تمه ب وه، الف ترلاسه شو\n✗ ناسمه: تمه ۲٫۰ وه (په ۰٫۵ کې)، ۱٫۰ ترلاسه شو\n"
        "د_ساعت_ازموینې\n  ✓ صفر وايي\n"
        "۲ تېرې شوې، ۱ ناکامې\n");
    CHECK_EQ(all.exit_code, 1);
    CHECK_EQ(run_tests(program, {"جمع"}).output, "✓ جمع\n۱ تېرې شوې، ۰ ناکامې\n");
}

TEST_CASE("testing: annotations translate, and the report lists the tests")
{
    const SourceFile source("t_test.th", "@test(\"adds up\")\nfunc adds()\n    pass\n@mock\nstruct M: I\ninterface I\n    func f()\n");
    const TranslationResult to_pashto = translate(source, english_pack(), pashto_pack());
    REQUIRE(to_pashto.ok);
    CHECK_CONTAINS(to_pashto.text, "@آزموینه(\"adds up\")");
    CHECK_CONTAINS(to_pashto.text, "@ساختګی");
    const TranslationResult back = translate(SourceFile("t_test.th", to_pashto.text), english_pack(), english_pack());
    REQUIRE(back.ok);
    CHECK_CONTAINS(back.text, "@test(\"adds up\")");
    CHECK_CONTAINS(back.text, "@mock");

    const SourceFile tests("t_test.th", "@test\nmodule m\n    func a()\n        pass\n");
    const CompileResult result = compile(tests, english_pack(), sdk_options());
    const std::string json = compile_report_json(result, tests);
    CHECK_CONTAINS(json, "\"tests\":[{\"suite\":\"m\",\"name\":\"a\",\"function\":\"m.a\",\"line\":3}]");
}
