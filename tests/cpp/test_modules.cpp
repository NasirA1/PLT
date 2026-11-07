// Modules: import "file" loads another Thunder file; its declarations can then be used directly.
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/compiler.hpp"
#include "thunder/os.hpp"
#include "thunder/report.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace thunder;
using Codes = std::vector<std::string>;

namespace
{
    using Files = std::map<std::string, std::string>;

    // Compiles "main.th" with the given in-memory files.
    CompileResult compile_files(const std::string& main, const Files& files, std::vector<std::string> module_paths = {},
        const LanguagePack& language = english_pack())
    {
        CompileOptions options;
        options.module_paths = std::move(module_paths);
        options.read_file = [files](const std::string& path) -> std::optional<std::string>
        {
            const auto it = files.find(path);
            return it == files.end() ? std::nullopt : std::optional(it->second);
        };
        return compile(SourceFile("main.th", main), language, options);
    }

    Codes codes(const CompileResult& result)
    {
        Codes out;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            out.push_back(diagnostic.code);
        }
        return out;
    }

    std::string run_files(const std::string& main, const Files& files, std::vector<std::string> module_paths = {})
    {
        const CompileResult result = compile_files(main, files, std::move(module_paths));
        if (!result.program)
        {
            std::string out = "COMPILE ERROR";
            for (const Diagnostic& diagnostic : result.diagnostics.items())
            {
                out += " " + result.file_name(diagnostic.span.file) + ":" + diagnostic.code;
            }
            return out;
        }
        std::ostringstream output;
        Vm vm(*result.program, output);
        const ExecResult exec = vm.run();
        std::string out = output.str();
        if (exec.error)
        {
            out += "ERROR " + format_runtime_error(*exec.error, result.source_name);
        }
        return out;
    }

    const Files geometry{
        {"lib/geometry.th", "import \"util\"\nconst sides = 4\nstruct Rect\n    w: int\n    h: int\n"
                            "func area(r: Rect): int\n    return times(r.w, r.h)\n"},
        {"lib/util.th", "var calls = 0\nfunc times(a: int, b: int): int\n    calls = calls + 1\n    return a * b\n"},
    };
}

TEST_CASE("modules: an imported file's functions, structs, constants and variables can be used")
{
    CHECK_EQ(run_files("import \"lib/geometry\"\nvar r = Rect(3, 4)\nprintln(area(r), \" \", sides, \" \", times(2, 5), \" \", calls)",
                 geometry),
        "12 4 10 2\n");
    // ".th" may be written, and a module is loaded once however often it is imported (diamond and
    // circular imports are harmless).
    Files cyclic = geometry;
    cyclic["lib/util.th"] = "import \"geometry.th\"\n" + geometry.at("lib/util.th");
    CHECK_EQ(run_files("import \"lib/geometry.th\"\nimport \"lib/util\"\nimport \"lib/geometry\"\nprintln(area(Rect(2, 2)))", cyclic),
        "4\n");
    const CompileResult result = compile_files("import \"lib/geometry\"\nimport \"lib/util\"\nprintln(1)", geometry);
    REQUIRE(result.succeeded());
    REQUIRE(result.modules.size() == 2);
    CHECK_EQ(result.modules[0].source->name(), "lib/geometry.th");
    CHECK_EQ(result.modules[1].source->name(), "lib/util.th"); // found next to geometry.th
    CHECK_EQ(result.modules[1].source->id(), 2u);
    CHECK_EQ(result.file_name(2), "lib/util.th");
    CHECK_EQ(result.file_name(0), "main.th");
    CHECK((result.program->source_files == std::vector<std::string>{"main.th", "lib/geometry.th", "lib/util.th"}));
    // A module's globals are initialised before the importing program's statements run.
    CHECK_EQ(run_files("println(calls)\nimport \"lib/util\"", geometry), "0\n");
}

TEST_CASE("modules: module directories are searched after the importing file's directory")
{
    const Files files{{"/libs/text_tools.th", "func shout(s: string): string\n    return to_upper(s) + \"!\"\n"}};
    CHECK_EQ(run_files("import \"text_tools\"\nprintln(shout(\"hi\"))", files, {"/libs"}), "HI!\n");
    const CompileResult result = compile_files("import \"text_tools\"", files, {"/libs"});
    REQUIRE(result.modules.size() == 1);
    CHECK_EQ(result.modules[0].source->name(), "text_tools.th");
    CHECK_EQ(result.modules[0].path, "/libs/text_tools.th");
}

TEST_CASE("modules: errors")
{
    // Not found: the message lists where the compiler looked.
    const CompileResult missing = compile_files("import \"nowhere\"", {}, {"/libs"});
    CHECK(codes(missing) == Codes{"S045"});
    CHECK_EQ(missing.diagnostics.items()[0].message, "module 'nowhere' not found (looked for nowhere.th, /libs/nowhere.th)");
    CHECK(missing.timings[2].ran && !missing.timings[2].succeeded);

    // A module only declares things.
    const Files statements{{"m.th", "println(1)\nvar ok = 1\nfunc main()\n    pass\nif true\n    pass\npass"}};
    const CompileResult declared = compile_files("import \"m\"", statements);
    CHECK(codes(declared) == (Codes{"S047", "S048", "S047"}));
    CHECK_EQ(declared.diagnostics.items()[0].span.file, 1u);

    // Errors in a module are reported in the module's file, with its own line numbers.
    const CompileResult broken = compile_files("var x = 1\nimport \"bad\"", {{"bad.th", "func f(): int\n    return \"s\"\n"}});
    REQUIRE(codes(broken) == Codes{"S027"});
    const Diagnostic& diagnostic = broken.diagnostics.items()[0];
    CHECK_EQ(diagnostic.span.file, 1u);
    CHECK_EQ(diagnostic.span.start.line, 2u);
    const SourceFile main_source("main.th", "");
    CHECK_CONTAINS(format_with_source(diagnostic, broken.source_of(1, main_source)), "bad.th:2:12: semantic error [S027]");
    CHECK_CONTAINS(format_with_source(diagnostic, broken.source_of(1, main_source)), "    return \"s\"");
    CHECK_EQ(&broken.source_of(0, main_source), &main_source);
    CHECK_EQ(&broken.source_of(9, main_source), &main_source);
    CHECK_EQ(broken.file_name(9), "main.th");

    // Lexical and syntax errors in a module stop compilation before semantic analysis.
    CHECK(codes(compile_files("import \"bad\"\nvar s: int = \"x\"", {{"bad.th", "var a = 1 +\n"}})) == Codes{"P002"});
    CHECK(codes(compile_files("import \"bad\"", {{"bad.th", "var a = @\n"}})) == Codes{"L001"});

    // Names clash across files as they would within one file.
    CHECK(codes(compile_files("import \"m\"\nfunc f()\n    pass", {{"m.th", "func f()\n    pass"}})) == Codes{"S003"});
    CHECK(codes(compile_files("import \"m\"\nvar g = 2", {{"m.th", "var g = 1"}})) == Codes{"S002"});

    // A module must be written in the same language as the program that imports it.
    const CompileResult mixed = compile_files("import \"m\"", {{"m.th", "// ژبه: ps\nمتغیر a = ۱\n"}});
    CHECK(codes(mixed) == Codes{"S046"});
    CHECK_CONTAINS(mixed.diagnostics.items()[0].message, "پښتو");
    // A module without a language directive is read in the importing program's language.
    const CompileResult pashto = compile_files("راوړه \"m\"\nکرښه چاپ(a)", {{"m.th", "متغیر a = ۱\n"}}, {}, pashto_pack());
    CHECK(pashto.succeeded());
}

TEST_CASE("modules: runtime errors and the debugger show the module's file")
{
    const Files files{{"lib/m.th", "func risky(n: int): int\n    return 10 / n\n"}};
    CHECK_EQ(run_files("import \"lib/m\"\nprintln(risky(0))", files),
        "ERROR lib/m.th:2:15: runtime error [R001]: division by zero\n    in risky [lib/m.th] (line 2)\n    in <main> (line 2)");

    const CompileResult result = compile_files("import \"lib/m\"\nprintln(risky(5))", files);
    REQUIRE(result.succeeded());
    const bytecode::Function& risky = result.program->functions[1];
    CHECK_EQ(risky.file_at(0), 1u);
    CHECK_EQ(result.program->functions[0].file_at(0), 0u);

    // The file ids survive serialisation; a line table may not refer to a file that is not listed.
    const auto bytes = bytecode::serialize(*result.program);
    const auto loaded = bytecode::deserialize(bytes);
    REQUIRE(loaded.has_value());
    CHECK(loaded->source_files == result.program->source_files);
    CHECK_EQ(loaded->functions[1].file_at(0), 1u);
    CHECK_EQ(loaded->file_name(1), "lib/m.th");
    CHECK_EQ(loaded->file_name(7), loaded->source_name);
    bytecode::Program corrupt = *result.program;
    corrupt.source_files.pop_back();
    CHECK_CONTAINS(*bytecode::verify(corrupt), "source file #1 does not exist");

    // Reports mark spans in modules with their file id and list the modules.
    const std::string json = compile_report_json(result, SourceFile("main.th", "import \"lib/m\"\nprintln(risky(5))"));
    CHECK_CONTAINS(json, "\"modules\":[{\"file\":1,\"name\":\"lib/m.th\",\"path\":\"lib/m.th\"}]");
    CHECK_CONTAINS(json, "\"file\":1");

    std::ostringstream output;
    Vm vm(*result.program, output);
    TraceRecorder recorder(1000);
    const ExecResult exec = vm.run(&recorder);
    CHECK_CONTAINS(recorder.finish(exec), "\"file\":1,\"file_name\":\"lib/m.th\"");
}

TEST_CASE("modules: files on disk")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "thunder_module_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "lib");
    std::ofstream(dir / "lib" / "greet.th") << "func greet(name: string): string\n    return \"Hello, \" + name\n";
    const std::string main_path = (dir / "main.th").generic_string();
    const CompileResult result = compile(SourceFile(main_path, "import \"lib/greet\"\nprintln(greet(\"Ada\"))"));
    REQUIRE(result.succeeded());
    std::ostringstream output;
    Vm vm(*result.program, output);
    vm.run();
    CHECK_EQ(output.str(), "Hello, Ada\n");
    // File names may be in any script.
    std::ofstream(os::utf8_path((dir / "lib").generic_string() + "/هندسه.th")) << "const sides = 4\n";
    const CompileResult pashto_name = compile(SourceFile(main_path, "import \"lib/هندسه\"\nprintln(sides)"));
    CHECK(pashto_name.succeeded());
    REQUIRE(pashto_name.modules.size() == 1);
    CHECK_EQ(pashto_name.modules[0].source->name(), "lib/هندسه.th");
    // A directory is not a module.
    fs::create_directories(dir / "folder.th");
    CHECK(codes(compile(SourceFile(main_path, "import \"folder\""))) == Codes{"S045"});
    fs::remove_all(dir);
}

TEST_CASE("modules: the syntax tree shows imports")
{
    CHECK_EQ(test::parse_program("import \"lib/a\"\nimport os"), "(Program (Import \"lib/a\") (Import os))");
}
