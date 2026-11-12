#include "thunder/compiler.hpp"

#include "thunder/codegen.hpp"
#include "thunder/lexer.hpp"
#include "thunder/os.hpp"
#include "thunder/parser.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

namespace thunder
{
    namespace
    {
        class PhaseTimer
        {
        public:
            PhaseTimer(CompileResult& result, const char* name)
                : result_(result)
                , start_(std::chrono::steady_clock::now())
            {
                result_.timings.push_back(PhaseTiming{name, 0, true, false});
            }

            ~PhaseTimer()
            {
                const auto elapsed = std::chrono::steady_clock::now() - start_;
                result_.timings.back().microseconds = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
            }

            PhaseTimer(const PhaseTimer&) = delete;
            PhaseTimer& operator=(const PhaseTimer&) = delete;

            void succeeded(bool ok) { result_.timings.back().succeeded = ok; }

        private:
            CompileResult& result_;
            std::chrono::steady_clock::time_point start_;
        };

        constexpr const char* phase_names[] = {
            "lexical analysis",
            "syntax analysis",
            "semantic analysis",
            "code generation",
        };
    }

    namespace
    {
        std::optional<std::string> read_from_disk(const std::string& path)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(os::utf8_path(path), error))
            {
                return std::nullopt;
            }
            std::ifstream file(os::utf8_path(path), std::ios::binary);
            if (!file)
            {
                return std::nullopt;
            }
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        // Loads the files imported by 'program' (and, recursively, by them). Each file is loaded
        // once, however often it is imported, so circular imports are harmless.
        class ModuleLoader
        {
        public:
            ModuleLoader(CompileResult& result, const CompileOptions& options, const LanguagePack& language)
                : result_(result)
                , options_(options)
                , language_(language)
            {
            }

            void load_imports(ProgramNode& program, const std::string& importer_path, const std::string& importer_name)
            {
                namespace fs = std::filesystem;
                for (StmtPtr& item : program.items)
                {
                    if (item->kind != StmtKind::Import || !static_cast<ImportStmt&>(*item).is_file
                        || static_cast<ImportStmt&>(*item).is_native)
                    {
                        continue;
                    }
                    auto& import = static_cast<ImportStmt&>(*item);
                    fs::path relative = os::utf8_path(import.target);
                    if (relative.extension() != ".th")
                    {
                        relative += ".th";
                    }

                    // Next to the importing file first, then in each module directory.
                    std::vector<std::pair<std::string, std::string>> candidates; // (path, display name)
                    const fs::path importer_dir = os::utf8_path(importer_path).parent_path();
                    const fs::path display_dir = os::utf8_path(importer_name).parent_path();
                    candidates.emplace_back(os::path_text((importer_dir / relative).lexically_normal()),
                        os::path_text((display_dir / relative).lexically_normal()));
                    for (const std::string& directory : options_.module_paths)
                    {
                        candidates.emplace_back(os::path_text((os::utf8_path(directory) / relative).lexically_normal()),
                            os::path_text(relative.lexically_normal()));
                    }
                    if (!options_.sdk_module_path.empty())
                    {
                        // The SDK's own modules, in the program's language: lib/thunder/<code>/.
                        candidates.emplace_back(os::path_text((os::utf8_path(options_.sdk_module_path)
                                                                  / language_.code / relative).lexically_normal()),
                            os::path_text(relative.lexically_normal()));
                    }

                    std::optional<std::string> text;
                    std::size_t found = 0;
                    for (; found < candidates.size(); ++found)
                    {
                        text = options_.read_file ? options_.read_file(candidates[found].first)
                                                  : read_from_disk(candidates[found].first);
                        if (text)
                        {
                            break;
                        }
                    }
                    if (!text)
                    {
                        std::string looked;
                        for (const auto& [path, display] : candidates)
                        {
                            looked += (looked.empty() ? "" : ", ") + path;
                        }
                        result_.diagnostics.report(DiagId::ModuleNotFound, import.target_span, import.target, looked);
                        continue;
                    }

                    const auto& [path, display] = candidates[found];
                    std::error_code error;
                    const fs::path canonical = fs::weakly_canonical(os::utf8_path(path), error);
                    const std::string key = error ? path : os::path_text(canonical);
                    import.resolved_path = path;
                    if (const auto loaded = loaded_.find(key); loaded != loaded_.end())
                    {
                        import.module = loaded->second.first;
                        import.module_file = loaded->second.second;
                        continue;
                    }
                    if (result_.modules.size() >= 0xFFFE)
                    {
                        throw std::length_error("program too large: more than 65534 modules");
                    }

                    const auto id = static_cast<std::uint16_t>(result_.modules.size() + 1);
                    ModuleUnit unit;
                    unit.source = std::make_unique<SourceFile>(display, std::move(*text), id);
                    unit.path = path;
                    const LanguagePack& module_language = choose_language(*unit.source, language_);
                    loaded_.emplace(key, std::pair<ProgramNode*, std::uint16_t>{nullptr, id});
                    if (&module_language != &language_)
                    {
                        result_.diagnostics.report(DiagId::ModuleLanguageMismatch, import.target_span, import.target,
                            module_language.native_name, language_.native_name);
                        result_.modules.push_back(std::move(unit));
                        continue;
                    }

                    // As for the program itself, a file with lexical errors is not parsed.
                    const std::size_t errors_before = result_.diagnostics.error_count();
                    Lexer lexer(*unit.source, language_, result_.diagnostics);
                    unit.tokens = lexer.tokenize();
                    if (result_.diagnostics.error_count() != errors_before)
                    {
                        result_.modules.push_back(std::move(unit));
                        continue;
                    }
                    Parser parser(unit.tokens, language_, result_.diagnostics);
                    unit.ast = std::make_unique<ProgramNode>(parser.parse_program());
                    ProgramNode* module = unit.ast.get();
                    loaded_[key].first = module;
                    import.module = module;
                    import.module_file = id;
                    const std::string module_path = unit.path;
                    const std::string module_name = unit.source->name();
                    result_.modules.push_back(std::move(unit));
                    load_imports(*module, module_path, module_name);
                }
            }

        private:
            CompileResult& result_;
            const CompileOptions& options_;
            const LanguagePack& language_;
            std::map<std::string, std::pair<ProgramNode*, std::uint16_t>> loaded_;
        };
    }

    std::string CompileResult::file_name(std::uint16_t file) const
    {
        if (file == 0 || file > modules.size())
        {
            return source_name;
        }
        return modules[file - 1].source->name();
    }

    const SourceFile& CompileResult::source_of(std::uint16_t file, const SourceFile& main_source) const
    {
        if (file == 0 || file > modules.size())
        {
            return main_source;
        }
        return *modules[file - 1].source;
    }

    const LanguagePack& choose_language(const SourceFile& source, const LanguagePack& default_language,
        std::string* unknown)
    {
        if (const auto directive = language_directive(source.text()))
        {
            if (const LanguagePack* pack = find_language_pack(*directive))
            {
                return *pack;
            }
            if (unknown != nullptr)
            {
                *unknown = *directive;
            }
        }
        // Language packs are long-lived singletons (english_pack(), pashto_pack()), never temporaries.
        return default_language; // NOLINT(bugprone-return-const-ref-from-parameter)
    }

    CompileResult compile(const SourceFile& source, const LanguagePack& default_language, const CompileOptions& options)
    {
        std::string unknown_language;
        const LanguagePack& language = choose_language(source, default_language, &unknown_language);
        CompileResult result(language);
        result.source_name = source.name();
        if (!unknown_language.empty())
        {
            const Position start{1, 1};
            result.diagnostics.report(DiagId::UnknownLanguage, Span{0, 0, start, start, 0}, unknown_language,
                language.native_name);
        }
        try
        {
            {
                PhaseTimer timer(result, phase_names[0]);
                Lexer lexer(source, language, result.diagnostics);
                result.tokens = lexer.tokenize();
                timer.succeeded(!result.diagnostics.has_errors());
            }

            if (!result.diagnostics.has_errors())
            {
                PhaseTimer timer(result, phase_names[1]);
                Parser parser(result.tokens, language, result.diagnostics);
                result.ast = std::make_unique<ProgramNode>(parser.parse_program());
                timer.succeeded(!result.diagnostics.has_errors());
            }

            if (result.ast && !result.diagnostics.has_errors())
            {
                PhaseTimer timer(result, phase_names[2]);
                // Resolving an import is name resolution, so the imported files are found (and
                // tokenised and parsed, with their own lexical and syntax errors) at the start of
                // semantic analysis. The program is only analysed if they all loaded cleanly.
                ModuleLoader loader(result, options, language);
                // Modules are named relative to the program's directory ("lib/shapes.th").
                loader.load_imports(*result.ast, source.name(), os::path_text(os::utf8_path(source.name()).filename()));
                if (!result.diagnostics.has_errors())
                {
                    SemanticAnalyzer analyzer(language, result.diagnostics);
                    result.model = std::make_unique<SemanticModel>(analyzer.analyze(*result.ast));
                }
                timer.succeeded(!result.diagnostics.has_errors());
            }

            if (result.model && !result.diagnostics.has_errors())
            {
                PhaseTimer timer(result, phase_names[3]);
                CodeGenerator generator(*result.model, language);
                const std::string& path = source.name();
                const std::size_t slash = path.find_last_of("/\\");
                const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
                bytecode::Program program = generator.generate(*result.ast, name);
                if (!result.modules.empty())
                {
                    program.source_files.push_back(name);
                    for (const ModuleUnit& unit : result.modules)
                    {
                        program.source_files.push_back(unit.source->name());
                    }
                }
                // The generated code must always pass the same checks the VM applies to files.
                if (auto problem = bytecode::verify(program))
                {
                    result.internal_error = "generated bytecode failed verification: " + *problem;
                }
                else
                {
                    result.program = std::move(program);
                    timer.succeeded(true);
                }
            }
        }
        catch (const std::exception& e)
        {
            result.internal_error = e.what();
            result.program.reset();
        }

        result.diagnostics.sort_by_position();

        // Record the phases that did not run so tools can show the whole pipeline.
        for (std::size_t i = result.timings.size(); i < std::size(phase_names); ++i)
        {
            result.timings.push_back(PhaseTiming{phase_names[i], 0, false, false});
        }
        return result;
    }
}
