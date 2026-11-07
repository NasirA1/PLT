// thunderc - the Thunder compiler.
//
// Compiles a Thunder source file (.th) to bytecode (.thc) and can print what each phase
// of the compiler produced.

#include "thunder/cli/cli_common.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/compiler.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/report.hpp"
#include "thunder/translate.hpp"

#include <iostream>
#include <print>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace thunder;

    constexpr std::string_view usage = R"(Usage: thunderc [options] <source.th>

Compiles a Thunder program to bytecode for the Thunder virtual machine (thundervm).

Options:
  -o, --output <file>  write the bytecode to <file> (default: the source name with .thc)
  --no-output          check the program without writing a bytecode file
  --emit <stage>       print what a compiler phase produced; may be repeated:
                         tokens   the token stream (lexical analysis)
                         ast      the abstract syntax tree (syntax analysis)
                         symbols  scopes and symbols (semantic analysis)
                         asm      the bytecode listing (code generation)
                         hex      a hex dump of the .thc file
                         all      all of the above
  --json               print a JSON report of every phase (used by the IDE)
  --pretty             indent JSON output
  -I, --module-path <dir>
                       also look for imported files (import "name") in <dir>; may be repeated
                       (they are first looked for next to the file that imports them)
  --lang <code>        language of the program: en (English, default) or ps (Pashto);
                       a first-line directive such as "// lang: ps" takes precedence
  --translate <code>   print the program translated into another language (or write it with -o)
  --language-info      print keywords, built-ins and the instruction set as JSON
  -q, --quiet          do not print a success message
  --version            print the version
  -h, --help           show this help

Exit status: 0 success, 1 compile errors, 2 usage or file errors, 3 internal error.
)";

    struct Options
    {
        std::string input;
        std::string output;
        bool write_output = true;
        std::set<std::string> emit;
        bool json = false;
        bool pretty = false;
        bool quiet = false;
        bool language_info = false;
        std::string language = "en";
        std::string translate_to;
        std::vector<std::string> module_paths;
    };

    int fail_usage(const std::string& message)
    {
        std::println(std::cerr, "thunderc: {}", message);
        std::println(std::cerr, "Try 'thunderc --help' for more information.");
        return cli::exit_usage;
    }

    std::vector<cli::OptionSpec> option_table(Options& options, bool& help, bool& version)
    {
        const auto flag = [](bool& target)
        {
            return [&target](const std::string&) -> std::optional<std::string>
            {
                target = true;
                return std::nullopt;
            };
        };
        return {
            {"--help", "-h", false, flag(help)},
            {"--version", "", false, flag(version)},
            {"--output", "-o", true, [&](const std::string& value) -> std::optional<std::string>
                {
                    options.output = value;
                    return std::nullopt;
                }},
            {"--no-output", "", false, [&](const std::string&) -> std::optional<std::string>
                {
                    options.write_output = false;
                    return std::nullopt;
                }},
            {"--emit", "", true, [&](const std::string& value) -> std::optional<std::string>
                {
                    static const std::set<std::string> stages{"tokens", "ast", "symbols", "asm", "hex", "all"};
                    if (!stages.contains(value))
                    {
                        return "--emit expects one of: tokens, ast, symbols, asm, hex, all";
                    }
                    options.emit.insert(value);
                    return std::nullopt;
                }},
            {"--json", "", false, flag(options.json)},
            {"--pretty", "", false, flag(options.pretty)},
            {"--quiet", "-q", false, flag(options.quiet)},
            {"--lang", "", true, [&](const std::string& value) -> std::optional<std::string>
                {
                    options.language = value;
                    return std::nullopt;
                }},
            {"--language-info", "", false, flag(options.language_info)},
            {"--module-path", "-I", true, [&](const std::string& value) -> std::optional<std::string>
                {
                    options.module_paths.push_back(value);
                    return std::nullopt;
                }},
            {"--translate", "", true, [&](const std::string& value) -> std::optional<std::string>
                {
                    options.translate_to = value;
                    return std::nullopt;
                }},
        };
    }

    // Prints the artefacts requested with --emit and the diagnostics, in human-readable form.
    void print_text_report(const Options& options, const SourceFile& source, const CompileResult& result,
        const std::vector<std::uint8_t>& bytes, const std::vector<bytecode::Section>& sections,
        const std::string& output_path)
    {
        const auto wants = [&](const char* stage) { return options.emit.contains(stage) || options.emit.contains("all"); };
        if (wants("tokens"))
        {
            std::print("== tokens ==\n{}\n", tokens_text(result));
        }
        if (wants("ast") && result.ast)
        {
            std::print("== abstract syntax tree ==\n{}\n", to_tree_text(*result.ast, result.language));
        }
        if (wants("symbols") && result.model)
        {
            std::print("== symbols ==\n{}\n", symbols_text(*result.model, *result.language));
        }
        if (wants("asm") && result.program)
        {
            std::print("== bytecode ==\n{}\n", bytecode::disassemble_text(*result.program));
        }
        if (wants("hex") && result.program)
        {
            std::print("== binary ({} bytes) ==\n{}\n", bytes.size(), hex_dump_text(bytes, sections));
        }

        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            // A diagnostic may be in an imported module: show that file's line.
            std::println(std::cerr, "{}",
                format_with_source(diagnostic, result.source_of(diagnostic.span.file, source), *result.language));
        }
        if (!result.internal_error.empty())
        {
            std::println(std::cerr, "thunderc: internal compiler error: {}", result.internal_error);
        }
        else if (!result.succeeded())
        {
            std::println(std::cerr, "{} error(s), {} warning(s); compilation failed.", result.diagnostics.error_count(),
                result.diagnostics.warning_count());
        }
        else if (!options.quiet && output_path.empty())
        {
            std::println("{}: no errors ({} warning(s)).", source.name(), result.diagnostics.warning_count());
        }
        else if (!options.quiet)
        {
            std::println("Compiled {} -> {} ({} bytes, {} warning(s)).", source.name(), output_path, bytes.size(),
                result.diagnostics.warning_count());
        }
    }
}

namespace
{
    int translate_program(const Options& options, const SourceFile& source, const LanguagePack& from)
    {
        const LanguagePack* to = find_language_pack(options.translate_to);
        if (to == nullptr)
        {
            return fail_usage("unknown language '" + options.translate_to + "'");
        }
        const TranslationResult result = translate(source, from, *to);
        if (!result.ok)
        {
            for (const Diagnostic& diagnostic : result.diagnostics.items())
            {
                std::println(std::cerr, "{}", format_with_source(diagnostic, source, result.diagnostics.language()));
            }
            return cli::exit_program_error;
        }
        for (const std::string& name : result.renamed)
        {
            std::println(std::cerr, "thunderc: renamed '{}' to '{}_' because it is reserved in {}", name, name,
                to->display_name);
        }
        if (options.output.empty())
        {
            std::print("{}", result.text);
        }
        else if (!cli::write_file(options.output, result.text))
        {
            return fail_usage("cannot write '" + options.output + "'");
        }
        return cli::exit_ok;
    }
}

int main(int argc, char* argv[])
{
    cli::prepare_console();

    Options options;
    bool help = false;
    bool version = false;
    std::vector<std::string> positional;
    if (auto error = cli::parse_arguments(std::vector<std::string>(argv + 1, argv + argc),
            option_table(options, help, version), positional))
    {
        return fail_usage(*error);
    }
    if (help)
    {
        std::print("{}", usage);
        return cli::exit_ok;
    }
    if (version)
    {
        std::println("thunderc {}", THUNDER_VERSION);
        return cli::exit_ok;
    }
    if (positional.size() > 1)
    {
        return fail_usage("only one source file can be compiled at a time");
    }
    if (!positional.empty())
    {
        options.input = positional.front();
    }

    const LanguagePack* language = find_language_pack(options.language);
    if (language == nullptr)
    {
        return fail_usage("unknown language '" + options.language + "'");
    }
    if (options.language_info)
    {
        std::println("{}", language_info_json(*language, options.pretty));
        return cli::exit_ok;
    }
    if (options.input.empty())
    {
        return fail_usage("no source file given");
    }

    const auto text = cli::read_file(options.input);
    if (!text)
    {
        return fail_usage("cannot read '" + options.input + "'");
    }
    const SourceFile source(options.input == "-" ? "<stdin>" : options.input, *text);
    if (!options.translate_to.empty())
    {
        return translate_program(options, source, *language);
    }
    CompileOptions compile_options;
    compile_options.module_paths = options.module_paths;
    const CompileResult result = compile(source, *language, compile_options);

    // Write the bytecode file first so the JSON report can say where it went.
    std::vector<bytecode::Section> sections;
    std::vector<std::uint8_t> bytes;
    std::string output_path;
    if (result.program)
    {
        bytes = bytecode::serialize(*result.program, &sections);
        if (options.write_output)
        {
            output_path = !options.output.empty() ? options.output
                                                  : cli::replace_extension(options.input == "-" ? "out" : options.input, ".thc");
            const std::string_view data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (!cli::write_file(output_path, data))
            {
                return fail_usage("cannot write '" + output_path + "'");
            }
        }
    }

    if (options.json)
    {
        std::println("{}", compile_report_json(result, source, options.pretty));
    }
    else
    {
        print_text_report(options, source, result, bytes, sections, output_path);
    }

    if (!result.internal_error.empty())
    {
        return cli::exit_internal;
    }
    return result.succeeded() ? cli::exit_ok : cli::exit_program_error;
}
