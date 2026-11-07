// Golden tests: every sample program shipped with the IDE (samples/<language>/*.th) is compiled
// and run, and its complete transcript (diagnostics, output, runtime error) is compared with
// tests/golden/<language>/<name>.expected.
// Set THUNDER_UPDATE_GOLDEN=1 to rewrite the expected files after an intentional change.

#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/graphics.hpp"
#include "thunder/report.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using namespace thunder;

namespace
{
    std::string read_text(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::erase(text, '\r');
        return text;
    }

    // The transcript of one sample. Imports are looked up in the sample's directory; if
    // tests/golden/<language>/<name>.input exists, the program reads it as its input.
    std::string transcript(const fs::path& path)
    {
        const SourceFile source(path.filename().string(), read_text(path));
        CompileOptions options;
        options.module_paths.push_back(path.parent_path().generic_string());
        const CompileResult result = compile(source, english_pack(), options);
        std::string out;
        if (!result.diagnostics.items().empty())
        {
            out += "== diagnostics ==\n";
            for (const Diagnostic& diagnostic : result.diagnostics.items())
            {
                out += format_short(diagnostic, source.name(), *result.language) + "\n";
            }
        }
        if (!result.program)
        {
            return out;
        }

        std::ostringstream output;
        VmOptions vm_options;
        vm_options.max_steps = 200'000; // the infinite-loop sample must stop
        vm_options.max_frames = 20;     // animations and games run for ever in the IDE
        vm_options.random_seed = 1;     // the same "random" numbers on every run
        const fs::path input_path = fs::path(THUNDER_SOURCE_DIR) / "tests" / "golden"
            / path.parent_path().filename() / (path.stem().string() + ".input");
        std::istringstream input(fs::exists(input_path) ? read_text(input_path) : std::string());
        Vm vm(*result.program, output, vm_options, &input);
        const ExecResult exec = vm.run();
        out += "== output ==\n" + output.str();
        if (vm.canvas().used())
        {
            // The final picture, one shape per line (long pictures are shortened).
            if (!out.empty() && out.back() != '\n')
            {
                out += "\n";
            }
            out += "== graphics ==\n";
            std::istringstream shapes(graphics::to_text(vm.canvas()));
            std::string line;
            std::size_t count = 0;
            while (std::getline(shapes, line))
            {
                if (count++ <= 40)
                {
                    out += line + "\n";
                }
            }
            if (count > 41)
            {
                out += "... (" + std::to_string(count - 41) + " more shapes)\n";
            }
            if (exec.frame_limit_reached)
            {
                out += "(stopped after " + std::to_string(vm_options.max_frames) + " frames)\n";
            }
        }
        if (exec.error)
        {
            if (!output.str().empty() && output.str().back() != '\n')
            {
                out += "\n";
            }
            out += "== runtime error ==\n" + format_runtime_error(*exec.error, source.name(), *result.language) + "\n";
        }
        return out;
    }
}

namespace
{
    // samples/en/*.th and samples/ps/*.th, sorted.
    std::vector<fs::path> sample_files()
    {
        std::vector<fs::path> files;
        for (const char* language : {"en", "ps"})
        {
            for (const auto& entry : fs::directory_iterator(fs::path(THUNDER_SOURCE_DIR) / "samples" / language))
            {
                if (entry.path().extension() == ".th")
                {
                    files.push_back(entry.path());
                }
            }
        }
        std::ranges::sort(files);
        return files;
    }
}

TEST_CASE("samples: every sample matches its golden transcript")
{
    const bool update = std::getenv("THUNDER_UPDATE_GOLDEN") != nullptr;
    const std::vector<fs::path> files = sample_files();
    REQUIRE(files.size() >= 25);

    for (const fs::path& file : files)
    {
        const std::string actual = transcript(file);
        const std::string language = file.parent_path().filename().string();
        const fs::path expected_path =
            fs::path(THUNDER_SOURCE_DIR) / "tests" / "golden" / language / (file.stem().string() + ".expected");
        if (update)
        {
            std::ofstream(expected_path, std::ios::binary) << actual;
            continue;
        }
        REQUIRE(fs::exists(expected_path));
        const std::string expected = read_text(expected_path);
        if (actual != expected)
        {
            CHECK_EQ(file.filename().string() + ":\n" + actual, file.filename().string() + ":\n" + expected);
        }
    }
}

TEST_CASE("samples: every sample has no semicolons at the ends of lines")
{
    for (const fs::path& path : sample_files())
    {
        if (path.filename() == "e2_syntax_error.th")
        {
            continue;
        }
        std::istringstream lines(read_text(path));
        std::string line;
        int number = 0;
        while (std::getline(lines, line))
        {
            ++number;
            const std::string code = line.substr(0, line.find("//"));
            const std::size_t last = code.find_last_not_of(" \t");
            if (last != std::string::npos && (code[last] == ';' || code.substr(0, last + 1).ends_with("\u061B")))
            {
                CHECK_EQ(path.filename().string() + ":" + std::to_string(number), std::string("no trailing ';'"));
            }
        }
    }
}

TEST_CASE("samples: every sample is indented with four spaces per level and uses no tabs")
{
    for (const fs::path& path : sample_files())
    {
        std::istringstream lines(read_text(path));
        std::string line;
        int number = 0;
        while (std::getline(lines, line))
        {
            ++number;
            const std::size_t indent = line.find_first_not_of(' ');
            const bool ok = line.find('\t') == std::string::npos && (indent == std::string::npos || indent % 4 == 0);
            if (!ok)
            {
                CHECK_EQ(path.filename().string() + ":" + std::to_string(number) + ": " + line, std::string("4 spaces"));
            }
        }
    }
}
