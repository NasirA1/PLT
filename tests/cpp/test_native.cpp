// The C++ native interface: import native, native func, calls out and callbacks back in.
// The module under test is lib/native/thunder_demo.cpp, built into <build>/bin.
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"
#include "thunder/os.hpp"
#include "thunder/vm.hpp"

#include <filesystem>
#include <sstream>

using namespace thunder;
using test::diagnostic_codes;
using Codes = std::vector<std::string>;

namespace
{
    // The directory holding libthunder_demo.so: the one this test executable is in.
    std::string demo_directory()
    {
        return os::path_text(os::utf8_path(os::executable_path()).parent_path());
    }

    struct NativeRun
    {
        std::string output;
        std::string error;   // the runtime error code, "" if there was none
        std::string message; // its message
    };

    NativeRun run_native(const std::string& body, bool allow = true)
    {
        const SourceFile source("n.th", "import native \"thunder_demo\"\n" + body);
        const CompileResult compiled = compile(source);
        NativeRun run;
        if (!compiled.program)
        {
            run.error = "COMPILE";
            for (const Diagnostic& diagnostic : compiled.diagnostics.items())
            {
                run.error += " " + diagnostic.code;
            }
            return run;
        }
        std::ostringstream out;
        VmOptions options;
        options.native_paths = {demo_directory()};
        options.allow_native = allow;
        Vm vm(*compiled.program, out, options);
        const ExecResult result = vm.run();
        run.output = out.str();
        if (result.error)
        {
            run.error = result.error->code;
            run.message = result.error->message;
        }
        return run;
    }
}

TEST_CASE("native: the demo module is where the tests expect it")
{
    REQUIRE(std::filesystem::exists(
        std::filesystem::path(demo_directory()) / os::library_file_name("thunder_demo")));
}

TEST_CASE("native: calling C++ from Thunder")
{
    CHECK_EQ(run_native("native func hypot(x: float, y: float): float\nprintln(hypot(3, 4))").output, "5.0\n");
    CHECK_EQ(run_native("native func shout(text: string): string\nprintln(shout(\"hi\"))").output, "HI!\n");
    CHECK_EQ(run_native("native func checksum(text: string): int\nprintln(checksum(\"\") == 0)").output, "true\n");
    CHECK_EQ(run_native("native func divide(a: int, b: int): int\nprintln(divide(27, 4))").output, "6\n");
    // A declaration may stand after the code that uses it, like any other function.
    CHECK_EQ(run_native("println(divide(9, 2))\nnative func divide(a: int, b: int): int").output, "4\n");
}

TEST_CASE("native: arguments are type checked like any other call")
{
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: int): int\nprintln(f(\"text\"))") == Codes{"S010"});
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: int): int\nprintln(f(1, 2))") == Codes{"S009"});
    // int widens to a float parameter, as everywhere else.
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: float): float\nprintln(f(1))").empty());
    // Only the simple types and callbacks may cross the boundary.
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: int[])") == Codes{"S053"});
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(): int[]") == Codes{"S053"});
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: func(int[]))") == Codes{"S053"});
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: func(int): bool)").empty());
    // "native func" without a library, and a name that is already taken.
    CHECK(diagnostic_codes("native func f(a: int)") == Codes{"S054"});
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: int)\nfunc f(a: int)\n    pass") == Codes{"S003"});
    CHECK(diagnostic_codes("import native \"x\"\nnative func len(a: int)") == Codes{"S004"});
    // A native function is not a value: there is nothing to pass around.
    CHECK(diagnostic_codes("import native \"x\"\nnative func f(a: int)\nvar g = f") == Codes{"S025"});
}

TEST_CASE("native: C++ calling back into Thunder")
{
    const char* program = R"(
native func each_prime(limit: int, report: func(int))
var found: int[] = []
func show(p: int)
    push(found, p)
each_prime(20, show)
println(found)
)";
    CHECK_EQ(run_native(program).output, "[2, 3, 5, 7, 11, 13, 17, 19]\n");

    const char* twice = R"(
native func apply_twice(value: int, f: func(int): int): int
func triple(n: int): int
    return n * 3
println(apply_twice(7, triple))
)";
    CHECK_EQ(run_native(twice).output, "63\n");
}

TEST_CASE("native: an error in the C++ code stops the program")
{
    const NativeRun run = run_native("native func divide(a: int, b: int): int\nprintln(divide(1, 0))");
    CHECK_EQ(run.error, "R030");
    CHECK_CONTAINS(run.message, "divide() cannot divide by zero");
}

TEST_CASE("native: an error in a callback stops the program")
{
    const char* program = R"(
native func each_prime(limit: int, report: func(int))
func show(p: int)
    println(10 / (p - 3))
each_prime(20, show)
)";
    const NativeRun run = run_native(program);
    CHECK_EQ(run.error, "R001"); // division by zero inside the Thunder callback
    CHECK_EQ(run.output, "-10\n"); // 10 / (2 - 3) printed before 3 made it divide by zero
}

TEST_CASE("native: a missing library or function is a runtime error")
{
    const SourceFile source("n.th", "import native \"no_such_library\"\nnative func f(): int\nprintln(f())");
    const CompileResult compiled = compile(source);
    REQUIRE(compiled.program.has_value());
    std::ostringstream out;
    VmOptions options;
    options.native_paths = {demo_directory()};
    Vm vm(*compiled.program, out, options);
    const ExecResult result = vm.run();
    REQUIRE(result.error.has_value());
    CHECK_EQ(result.error->code, "R028");

    const NativeRun missing = run_native("native func no_such_function(): int\nprintln(no_such_function())");
    CHECK_EQ(missing.error, "R029");
}

TEST_CASE("native: --no-native refuses to load anything")
{
    const NativeRun run = run_native("native func hypot(x: float, y: float): float\nprintln(hypot(3, 4))", false);
    CHECK_EQ(run.error, "R028");
    CHECK_CONTAINS(run.message, "switched off");
}

TEST_CASE("native: the declarations reach the bytecode")
{
    const bytecode::Program program = test::compile_ok(
        "import native \"thunder_demo\"\nnative func hypot(x: float, y: float): float\nprintln(hypot(1, 1))");
    REQUIRE(program.native_libraries.size() == 1u);
    CHECK_EQ(program.native_libraries[0], "thunder_demo");
    REQUIRE(program.natives.size() == 1u);
    CHECK_EQ(program.natives[0].name, "hypot");
    CHECK_EQ(program.natives[0].arity, 2u);
    CHECK(program.natives[0].returns_value);
    CHECK_CONTAINS(bytecode::disassemble_text(program), "CALL_NATIVE");

    // A round trip through the .thc format keeps them.
    const std::vector<std::uint8_t> bytes = bytecode::serialize(program);
    const auto restored = bytecode::deserialize(bytes);
    REQUIRE(restored.has_value());
    CHECK(restored->native_libraries == program.native_libraries);
    CHECK(restored->natives == program.natives);
}

TEST_CASE("native: Pashto spelling")
{
    const SourceFile source("n.th",
        "# ژبه: ps\nراوړه بهرنی \"thunder_demo\"\nبهرنی دنده hypot(س: اعشاري، ص: اعشاري): اعشاري\n"
        "کرښه چاپ(hypot(۳، ۴))\n");
    const CompileResult compiled = compile(source, pashto_pack());
    REQUIRE(compiled.program.has_value());
    std::ostringstream out;
    VmOptions options;
    options.native_paths = {demo_directory()};
    Vm vm(*compiled.program, out, options);
    const ExecResult result = vm.run();
    CHECK(!result.error.has_value());
    CHECK_EQ(out.str(), "۵٫۰\n");
}
