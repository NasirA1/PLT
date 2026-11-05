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

    struct CompileResult
    {
        explicit CompileResult(const LanguagePack& lang)
            : language(&lang)
            , diagnostics(lang)
        {
        }

        const LanguagePack* language; // the language the program was compiled as
        std::vector<Token> tokens;
        std::unique_ptr<ProgramNode> ast;     // null if parsing did not run
        std::unique_ptr<SemanticModel> model; // null if semantic analysis did not run
        std::optional<bytecode::Program> program; // set only if compilation succeeded
        DiagnosticBag diagnostics;
        std::vector<PhaseTiming> timings;
        std::string internal_error; // non-empty if the compiler itself failed (a bug)

        bool succeeded() const { return program.has_value(); }
    };

    // Compiles 'source'. Each phase only runs if all earlier phases succeeded, which mirrors how
    // the pipeline is taught: lexical -> syntax -> semantic analysis -> code generation.
    // A directive on the first line ("// lang: ps") overrides 'default_language'.
    CompileResult compile(const SourceFile& source, const LanguagePack& default_language = english_pack());

    // The language a source file should be compiled as: its directive, or 'default_language'.
    // If the directive names an unknown language, 'unknown' receives the name.
    const LanguagePack& choose_language(const SourceFile& source, const LanguagePack& default_language,
        std::string* unknown = nullptr);
}
