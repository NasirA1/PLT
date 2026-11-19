// Tests marked with annotations: finding them, keeping them out of production code, and writing
// the calls that run them (see testing.hpp).
#include "thunder/testing.hpp"

#include "thunder/compiler.hpp"
#include "thunder/language_pack.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <utility>

namespace thunder
{
    namespace
    {
        const Annotation* find_annotation(const std::vector<Annotation>& annotations, std::string_view name)
        {
            for (const Annotation& annotation : annotations)
            {
                if (annotation.name == name)
                {
                    return &annotation;
                }
            }
            return nullptr;
        }

        bool struct_declares_tests(const StructDeclStmt& decl)
        {
            if (find_annotation(decl.annotations, "test") != nullptr)
            {
                return true;
            }
            return std::ranges::any_of(decl.methods,
                [](const auto& method) { return find_annotation(method->annotations, "test") != nullptr; });
        }

        bool item_declares_tests(const Stmt& item)
        {
            switch (item.kind)
            {
            case StmtKind::FuncDecl:
                return find_annotation(static_cast<const FuncDeclStmt&>(item).annotations, "test") != nullptr;
            case StmtKind::StructDecl:
                return struct_declares_tests(static_cast<const StructDeclStmt&>(item));
            case StmtKind::Module:
            {
                const auto& module = static_cast<const ModuleDeclStmt&>(item);
                return find_annotation(module.annotations, "test") != nullptr
                    || std::ranges::any_of(module.members, [](const StmtPtr& member) { return item_declares_tests(*member); });
            }
            default:
                return false;
            }
        }

        // Calls 'visit' with every annotation of the item (and of its members).
        template <typename Visit>
        void each_annotation(const Stmt& item, Visit&& visit)
        {
            switch (item.kind)
            {
            case StmtKind::FuncDecl:
                for (const Annotation& annotation : static_cast<const FuncDeclStmt&>(item).annotations)
                {
                    visit(annotation);
                }
                break;
            case StmtKind::StructDecl:
            {
                const auto& decl = static_cast<const StructDeclStmt&>(item);
                for (const Annotation& annotation : decl.annotations)
                {
                    visit(annotation);
                }
                for (const auto& method : decl.methods)
                {
                    each_annotation(*method, visit);
                }
                break;
            }
            case StmtKind::Module:
            {
                const auto& module = static_cast<const ModuleDeclStmt&>(item);
                for (const Annotation& annotation : module.annotations)
                {
                    visit(annotation);
                }
                for (const StmtPtr& member : module.members)
                {
                    each_annotation(*member, visit);
                }
                break;
            }
            default:
                break;
            }
        }

        bool imports_testing(const ImportStmt& import)
        {
            return import.is_file && !import.is_native && (import.target == "testing" || import.target == "testing.th");
        }

        // Builds the syntax the compiler writes for the tests. Every node carries the span of
        // the annotation it comes from, so the debugger and the traces point at the test.
        class Writer
        {
        public:
            explicit Writer(const LanguagePack& language)
                : language_(language)
            {
            }

            // "a", "a.b", "a.b.c": a name, or members of a module.
            static ExprPtr reference(const std::vector<std::string>& path, const Span& span)
            {
                ExprPtr expr = std::make_unique<NameExpr>(span, path.front());
                for (std::size_t i = 1; i < path.size(); ++i)
                {
                    expr = std::make_unique<FieldExpr>(span, std::move(expr), path[i], span);
                }
                return expr;
            }

            static std::unique_ptr<CallExpr> call(ExprPtr callee, std::vector<ExprPtr> arguments, const Span& span)
            {
                std::unique_ptr<CallExpr> call;
                if (callee->kind == ExprKind::Name)
                {
                    call = std::make_unique<CallExpr>(span, static_cast<NameExpr&>(*callee).name, span);
                }
                else
                {
                    call = std::make_unique<CallExpr>(span, std::string(), span);
                    call->callee_expr = std::move(callee);
                }
                call->arguments = std::move(arguments);
                call->argument_names.assign(call->arguments.size(), std::string());
                return call;
            }

            // test.run("suite", "name", body)
            StmtPtr run(const std::string& suite, const std::string& name, ExprPtr body, const Span& span) const
            {
                std::vector<ExprPtr> arguments;
                arguments.push_back(std::make_unique<StringLiteralExpr>(span, suite));
                arguments.push_back(std::make_unique<StringLiteralExpr>(span, name));
                arguments.push_back(std::move(body));
                return std::make_unique<ExprStmt>(span, call(testing_member("run", span), std::move(arguments), span));
            }

            // test.finish()
            StmtPtr finish(const Span& span) const
            {
                return std::make_unique<ExprStmt>(span, call(testing_member("finish", span), {}, span));
            }

            // func()
            //     var (suite) = Suite()
            //     (suite).method()
            //     (suite).close()          (when the suite has a close() method)
            // A fresh suite for every test: its constructor prepares, its close() tidies up.
            ExprPtr method_body(const std::vector<std::string>& suite_path, const std::string& method,
                const std::string& closer, const Span& span) const
            {
                const std::string local = "(" + language_.term("suite") + ")";
                auto lambda = std::make_unique<LambdaExpr>(span);
                lambda->body = std::make_unique<BlockStmt>(span);
                auto instance = std::make_unique<VarDeclStmt>(span, false, local, span);
                instance->initializer = call(reference(suite_path, span), {}, span);
                lambda->body->statements.push_back(std::move(instance));
                lambda->body->statements.push_back(
                    std::make_unique<ExprStmt>(span, call(reference({local, method}, span), {}, span)));
                if (!closer.empty())
                {
                    lambda->body->statements.push_back(
                        std::make_unique<ExprStmt>(span, call(reference({local, closer}, span), {}, span)));
                }
                return lambda;
            }

        private:
            ExprPtr testing_member(std::string_view english, const Span& span) const
            {
                return reference({fold_letters(testing_name(language_, "test")), fold_letters(testing_name(language_, english))},
                    span);
            }

            const LanguagePack& language_;
        };

        class Expander
        {
        public:
            Expander(CompileResult& result, std::string_view source)
                : result_(result)
                , source_(source)
                , language_(*result.language)
                , diagnostics_(result.diagnostics)
                , writer_(language_)
            {
            }

            std::vector<TestCase> expand()
            {
                ProgramNode& program = *result_.ast;
                // A file with tests is run, never used: importing one is refused, once, where
                // it is imported.
                for (const ModuleUnit& unit : result_.modules)
                {
                    if (unit.ast != nullptr && declares_tests(*unit.ast))
                    {
                        test_files_.insert(unit.ast.get());
                        const ImportStmt* import = importer_of(unit.ast.get());
                        diagnostics_.report(DiagId::TestsImported,
                            import != nullptr ? import->target_span : unit.ast->span, unit.source->name());
                    }
                }
                if (!declares_tests(program))
                {
                    refuse_test_code();
                    return {};
                }
                refuse_program_code(program);
                // "@mock" takes no description, and a test's description names it: not "".
                for (const StmtPtr& item : program.items)
                {
                    each_annotation(*item, [&](const Annotation& annotation)
                    {
                        if (annotation.name == "mock" && annotation.argument)
                        {
                            diagnostics_.report(DiagId::AnnotationArgument, annotation.argument_span,
                                language_.annotation_spelling("mock"), language_.term("no arguments"));
                        }
                        else if (annotation.name == "test" && annotation.argument && annotation.argument->empty())
                        {
                            diagnostics_.report(DiagId::AnnotationArgument, annotation.argument_span,
                                language_.annotation_spelling("test"), language_.term("a name that is not empty"));
                        }
                    });
                }
                for (const StmtPtr& item : program.items)
                {
                    collect(*item, {});
                }
                if (!tests_.empty() || !generated_.empty())
                {
                    Span end = program.span;
                    end.start = end.end;
                    end.offset += end.length;
                    end.length = 0;
                    generated_.push_back(writer_.finish(end));
                }
                for (StmtPtr& statement : generated_)
                {
                    program.items.push_back(std::move(statement));
                }
                return std::move(tests_);
            }

        private:
            // A production program: no testing module, no mocks, anywhere in it.
            void refuse_test_code()
            {
                const auto check = [&](const ProgramNode& file)
                {
                    for (const StmtPtr& item : file.items)
                    {
                        if (item->kind == StmtKind::Import && imports_testing(static_cast<const ImportStmt&>(*item)))
                        {
                            const auto& import = static_cast<const ImportStmt&>(*item);
                            diagnostics_.report(DiagId::TestCodeInProduction, import.span,
                                std::format("'{} \"{}\"'", language_.spelling(TokenKind::KwImport), import.target));
                        }
                        each_annotation(*item, [&](const Annotation& annotation)
                        {
                            if (annotation.name == "mock")
                            {
                                diagnostics_.report(DiagId::TestCodeInProduction, annotation.span,
                                    std::format("'@{}'", language_.annotation_spelling("mock")));
                            }
                        });
                    }
                };
                check(*result_.ast);
                for (const ModuleUnit& unit : result_.modules)
                {
                    if (unit.ast != nullptr && !test_files_.contains(unit.ast.get()))
                    {
                        check(*unit.ast);
                    }
                }
            }

            // The import that loaded 'module' (the first one, in the program or another module).
            const ImportStmt* importer_of(const ProgramNode* module) const
            {
                const auto find = [&](const ProgramNode& file) -> const ImportStmt*
                {
                    for (const StmtPtr& item : file.items)
                    {
                        if (item->kind == StmtKind::Import && static_cast<const ImportStmt&>(*item).module == module)
                        {
                            return static_cast<const ImportStmt*>(item.get());
                        }
                    }
                    return nullptr;
                };
                if (const ImportStmt* import = find(*result_.ast))
                {
                    return import;
                }
                for (const ModuleUnit& unit : result_.modules)
                {
                    if (unit.ast != nullptr)
                    {
                        if (const ImportStmt* import = find(*unit.ast))
                        {
                            return import;
                        }
                    }
                }
                return nullptr;
            }

            // A test file: declarations only - no statements and no main() of its own.
            void refuse_program_code(const ProgramNode& program)
            {
                for (const StmtPtr& item : program.items)
                {
                    switch (item->kind)
                    {
                    case StmtKind::FuncDecl:
                    {
                        const auto& func = static_cast<const FuncDeclStmt&>(*item);
                        if (language_.is_main_name(func.name))
                        {
                            diagnostics_.report(DiagId::TestFileStatement, func.name_span,
                                std::format("{} '{}'", language_.term("the function"), func.name));
                        }
                        break;
                    }
                    case StmtKind::StructDecl:
                    case StmtKind::Interface:
                    case StmtKind::EnumDecl:
                    case StmtKind::Module:
                    case StmtKind::Import:
                    case StmtKind::VarDecl:
                    case StmtKind::Pass:
                        break;
                    default:
                        diagnostics_.report(DiagId::TestFileStatement, item->span, language_.term("this statement"));
                        break;
                    }
                }
            }

            static bool test_shaped(const FuncDeclStmt& func)
            {
                return func.params.empty() && !func.return_type && !func.is_native && func.body != nullptr;
            }

            // An explicitly marked function or method: it must be able to run as a test.
            bool check_marked(const FuncDeclStmt& func, const Annotation& annotation)
            {
                if (func.is_native)
                {
                    diagnostics_.report(DiagId::AnnotationTarget, annotation.span, language_.annotation_spelling("test"),
                        std::format("'{} {}'", language_.spelling(TokenKind::KwNative), language_.spelling(TokenKind::KwFunc)));
                    return false;
                }
                if (!func.type_params.empty())
                {
                    diagnostics_.report(DiagId::TestGeneric, annotation.span, language_.term("function"));
                    return false;
                }
                if (!test_shaped(func))
                {
                    diagnostics_.report(DiagId::TestShape, func.name_span, func.name);
                    return false;
                }
                return true;
            }

            void refuse_mock(const std::vector<Annotation>& annotations, std::string_view what)
            {
                if (const Annotation* mock = find_annotation(annotations, "mock"))
                {
                    diagnostics_.report(DiagId::AnnotationTarget, mock->span, language_.annotation_spelling("mock"),
                        std::string(what));
                }
            }

            void add(const std::string& suite, const std::string& name, const std::string& function, const Span& span,
                ExprPtr body)
            {
                if (!seen_.insert({suite, name}).second)
                {
                    diagnostics_.report(DiagId::DuplicateTest, span, suite, name);
                    return;
                }
                tests_.push_back(TestCase{suite, name, function, span});
                generated_.push_back(writer_.run(suite, name, std::move(body), span));
            }

            static std::string joined(const std::vector<std::string>& path)
            {
                std::string text;
                for (const std::string& part : path)
                {
                    text += (text.empty() ? "" : ".") + part;
                }
                return text;
            }

            // 'module' is the path of the enclosing module ({} at the top level); a module's
            // suite name is passed to its functions.
            void collect(const Stmt& item, const std::vector<std::string>& module, const std::string* module_suite = nullptr,
                bool module_marked = false)
            {
                switch (item.kind)
                {
                case StmtKind::FuncDecl:
                {
                    const auto& func = static_cast<const FuncDeclStmt&>(item);
                    refuse_mock(func.annotations, language_.term("a function"));
                    const Annotation* marked = find_annotation(func.annotations, "test");
                    const bool implied = marked == nullptr && module_marked && !func.name.starts_with('_')
                        && func.type_params.empty() && test_shaped(func);
                    if (marked != nullptr && !check_marked(func, *marked))
                    {
                        return;
                    }
                    if (marked == nullptr && !implied)
                    {
                        return;
                    }
                    std::vector<std::string> path = module;
                    path.push_back(func.name);
                    const std::string name = marked != nullptr && marked->argument && !marked->argument->empty()
                        ? *marked->argument
                        : test_display_name(written(func.name, func.name_span));
                    const Span span = marked != nullptr ? marked->span : func.name_span;
                    add(module_suite != nullptr ? *module_suite : std::string(), name, joined(path), span,
                        Writer::reference(path, span));
                    return;
                }
                case StmtKind::StructDecl:
                    collect_suite(static_cast<const StructDeclStmt&>(item), module);
                    return;
                case StmtKind::Module:
                {
                    const auto& decl = static_cast<const ModuleDeclStmt&>(item);
                    refuse_mock(decl.annotations, std::format("'{}'", language_.spelling(TokenKind::KwModule)));
                    const Annotation* marked = find_annotation(decl.annotations, "test");
                    const std::string suite = marked != nullptr && marked->argument ? *marked->argument
                                                                                    : written(decl.name, decl.name_span);
                    std::vector<std::string> path = module;
                    path.push_back(decl.name);
                    for (const StmtPtr& member : decl.members)
                    {
                        collect(*member, path, &suite, marked != nullptr);
                    }
                    return;
                }
                default:
                    return;
                }
            }

            // "@test struct StackTests": a suite. Each test runs on a new instance, made by the
            // constructor (without arguments) and closed by close() when there is one.
            void collect_suite(const StructDeclStmt& decl, const std::vector<std::string>& module)
            {
                const Annotation* marked = find_annotation(decl.annotations, "test");
                if (!struct_declares_tests(decl))
                {
                    return;
                }
                const Span where = marked != nullptr ? marked->span : decl.name_span;
                if (decl.is_mixin)
                {
                    diagnostics_.report(DiagId::AnnotationTarget, where, language_.annotation_spelling("test"),
                        std::format("'{}'", language_.spelling(TokenKind::KwMixin)));
                    return;
                }
                if (find_annotation(decl.annotations, "mock") != nullptr)
                {
                    diagnostics_.report(DiagId::AnnotationTarget, where, language_.annotation_spelling("test"),
                        std::format("'@{}'", language_.annotation_spelling("mock")));
                    return;
                }
                if (!decl.type_params.empty())
                {
                    diagnostics_.report(DiagId::TestGeneric, where, language_.term("struct"));
                    return;
                }
                std::string closer;
                for (const auto& method : decl.methods)
                {
                    if (method->is_constructor && !method->params.empty())
                    {
                        diagnostics_.report(DiagId::SuiteConstructor, method->name_span, decl.name);
                        return;
                    }
                    if (!method->is_constructor && method->params.empty()
                        && (method->name == fold_letters(language_.term("close")) || method->name == "close"))
                    {
                        closer = method->name;
                    }
                }
                const std::string suite = marked != nullptr && marked->argument ? *marked->argument
                                                                                : written(decl.name, decl.name_span);
                std::vector<std::string> path = module;
                path.push_back(decl.name);
                for (const auto& method : decl.methods)
                {
                    if (method->is_constructor)
                    {
                        continue;
                    }
                    refuse_mock(method->annotations, language_.term("a method"));
                    const Annotation* method_marked = find_annotation(method->annotations, "test");
                    const bool implied = method_marked == nullptr && marked != nullptr && !method->name.starts_with('_')
                        && method->name != closer && method->type_params.empty() && test_shaped(*method);
                    if (method_marked != nullptr && !check_marked(*method, *method_marked))
                    {
                        continue;
                    }
                    if (method_marked == nullptr && !implied)
                    {
                        continue;
                    }
                    const std::string name = method_marked != nullptr && method_marked->argument
                        ? *method_marked->argument
                        : test_display_name(written(method->name, method->name_span));
                    const Span span = method_marked != nullptr ? method_marked->span : method->name_span;
                    add(suite, name, joined(path) + "." + method->name, span,
                        writer_.method_body(path, method->name, closer, span));
                }
            }

            // A declared name as the program wrote it (identifiers are kept with their letters
            // folded; the transcript shows the author's spelling).
            std::string written(const std::string& name, const Span& span) const
            {
                if (span.file == 0 && span.offset + span.length <= source_.size() && span.length > 0)
                {
                    const std::string_view text = source_.substr(span.offset, span.length);
                    if (fold_letters(text) == name)
                    {
                        return std::string(text);
                    }
                }
                return name;
            }

            CompileResult& result_;
            std::string_view source_;
            const LanguagePack& language_;
            DiagnosticBag& diagnostics_;
            Writer writer_;
            std::vector<TestCase> tests_;
            std::vector<StmtPtr> generated_;
            std::set<std::pair<std::string, std::string>> seen_;
            std::set<const ProgramNode*> test_files_;
        };
    }

    bool declares_tests(const ProgramNode& program)
    {
        return std::ranges::any_of(program.items, [](const StmtPtr& item) { return item_declares_tests(*item); });
    }

    void import_testing(ProgramNode& program)
    {
        for (const StmtPtr& item : program.items)
        {
            if (item->kind == StmtKind::Import && imports_testing(static_cast<const ImportStmt&>(*item)))
            {
                return;
            }
        }
        Span start = program.span;
        start.end = start.start;
        start.length = 0;
        program.items.insert(program.items.begin(), std::make_unique<ImportStmt>(start, "testing", start, true));
    }

    std::string testing_name(const LanguagePack& language, std::string_view english)
    {
        const std::string key = "testing." + std::string(english);
        const std::string term = language.term(key);
        return term == key ? std::string(english) : term;
    }

    std::vector<TestCase> expand_tests(CompileResult& result, std::string_view source)
    {
        if (result.ast == nullptr)
        {
            return {};
        }
        Expander expander(result, source);
        return expander.expand();
    }

    std::string test_display_name(std::string_view declared)
    {
        std::string name(declared);
        std::ranges::replace(name, '_', ' ');
        const auto first = name.find_first_not_of(' ');
        if (first == std::string::npos)
        {
            return std::string(declared);
        }
        const auto last = name.find_last_not_of(' ');
        return name.substr(first, last - first + 1);
    }
}
