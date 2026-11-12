// The compiler driver: runs each phase in turn and keeps every intermediate result so that
// tools (the command line and the IDE) can show students what each phase produced.
#pragma once

#include "thunder/ast.hpp"
#include "thunder/bytecode.hpp"
#include "thunder/diagnostics.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/sema.hpp"
#include "thunder/source.hpp"
#include "thunder/token.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace thunder
{
    struct PhaseTiming
    {
        std::string phase;       // "lexical analysis", ...
        std::uint64_t microseconds = 0;
        bool ran = false;        // false if an earlier phase failed
        bool succeeded = false;
    };

    struct CompileOptions
    {
        // Directories searched for imported files after the directory of the importing file.
        std::vector<std::string> module_paths;
        // The SDK's Thunder modules (lib/thunder in the sources, copied next to thunderc as
        // lib/thunder): a directory holding one folder per language code, searched last, so
        // that import "sockets" works in any program without a --module-path option.
        std::string sdk_module_path;
        // Reads an imported file; nullopt if it does not exist. The default reads from the disk
        // (tests supply in-memory files).
        std::function<std::optional<std::string>(const std::string& path)> read_file;
    };

    // A module loaded because the program (or another module) imported it.
    struct ModuleUnit
    {
        std::unique_ptr<SourceFile> source; // its id is its position in CompileResult::modules plus one
        std::string path;                   // where it was found (used to resolve its own imports)
        std::vector<Token> tokens;
        std::unique_ptr<ProgramNode> ast;
    };

    struct CompileResult
    {
        explicit CompileResult(const LanguagePack& lang)
            : language(&lang)
            , diagnostics(lang)
        {
        }

        const LanguagePack* language; // the language the program was compiled as
        std::string source_name;      // the program's file name (file id 0)
        std::vector<Token> tokens;    // of the program itself (modules keep their own)
        std::unique_ptr<ProgramNode> ast;     // null if parsing did not run
        std::unique_ptr<SemanticModel> model; // null if semantic analysis did not run
        std::optional<bytecode::Program> program; // set only if compilation succeeded
        DiagnosticBag diagnostics;
        std::vector<PhaseTiming> timings;
        std::string internal_error; // non-empty if the compiler itself failed (a bug)
        std::vector<ModuleUnit> modules; // in the order they were loaded (file ids 1, 2, ...)

        bool succeeded() const { return program.has_value(); }

        // The name of a file by id (Span::file): the program's own name for 0.
        std::string file_name(std::uint16_t file) const;
        // The source of a file by id; 'main_source' for 0 (or an unknown id).
        const SourceFile& source_of(std::uint16_t file, const SourceFile& main_source) const;
    };

    // Compiles 'source'. Each phase only runs if all earlier phases succeeded, which mirrors how
    // the pipeline is taught: lexical -> syntax -> semantic analysis -> code generation.
    // A directive on the first line ("// lang: ps") overrides 'default_language'.
    // Imported files are looked up next to the importing file, then in options.module_paths.
    CompileResult compile(const SourceFile& source, const LanguagePack& default_language = english_pack(),
        const CompileOptions& options = {});

    // The language a source file should be compiled as: its directive, or 'default_language'.
    // If the directive names an unknown language, 'unknown' receives the name.
    const LanguagePack& choose_language(const SourceFile& source, const LanguagePack& default_language,
        std::string* unknown = nullptr);
}
