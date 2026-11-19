// Tests marked with annotations: "@test" on a function, a struct or a module, "@mock" on a struct.
//
// A program file with "@test" in it is a test file. It holds tests and what they need (helpers,
// mocks, fixtures) and no program of its own; running it runs its tests. The compiler finds the
// tests and writes, after the declarations, the calls a programmer used to write by hand:
//
//     test.run("suite", "name", body)    for each test, in the order they are declared
//     test.finish()                      the summary and the exit status
//
// The harness itself stays ordinary Thunder (lib/thunder/<language>/testing.th), imported
// automatically. The rules that keep test code out of production code are enforced here too:
// a production program (a file without "@test") may not import the testing module or declare a
// mock, and a file with tests cannot be imported. docs/testing.md describes it all.
#pragma once

#include "thunder/ast.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace thunder
{
    struct CompileResult;
    struct LanguagePack;

    // One test the compiler found: the suite ("" for a test outside any suite), the name shown in
    // the transcript, the declaration it runs ("numbers.largest", "StackTests.pushes") and where
    // its annotation (or, for a test of a marked suite, its declaration) is.
    struct TestCase
    {
        std::string suite;
        std::string name;
        std::string function;
        Span span;
    };

    // True if anything in the program is marked "@test" (a function, a method, a struct, a module).
    bool declares_tests(const ProgramNode& program);

    // Adds 'import "testing"' to a program with tests that does not import it itself.
    void import_testing(ProgramNode& program);

    // The names of the testing module's members the generated code uses, in 'language'
    // (English: "test", "run", "finish", "Mock", "_record").
    std::string testing_name(const LanguagePack& language, std::string_view english);

    // After the imports are loaded: enforces the separation of tests and production code, and
    // appends the statements that run the tests to a test file (result.ast). Returns the tests,
    // in the order they run. 'source' is the program's text (test names keep its spelling).
    std::vector<TestCase> expand_tests(CompileResult& result, std::string_view source);

    // "largest_of_a_list" -> "largest of a list": the name a test is shown under when its
    // annotation gives none.
    std::string test_display_name(std::string_view declared);
}
