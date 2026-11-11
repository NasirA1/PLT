#include "thunder/sema.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/lexer.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace thunder
{
    std::string_view symbol_kind_name(SymbolKind kind)
    {
        switch (kind)
        {
        case SymbolKind::Global:
            return "global";
        case SymbolKind::Local:
            return "local";
        case SymbolKind::Parameter:
            return "parameter";
        }
        return "unknown";
    }

    SemanticAnalyzer::SemanticAnalyzer(const LanguagePack& language, DiagnosticBag& diagnostics)
        : language_(language)
        , diagnostics_(diagnostics)
    {
    }

    SemanticModel SemanticAnalyzer::analyze(ProgramNode& program)
    {
        // Function 0 is the implicit "<main>" function that holds the top-level statements.
        FunctionInfo top_level;
        top_level.name = "<main>";
        top_level.span = program.span;
        model_.functions.push_back(std::move(top_level));

        // Pass 0: gather the program's items and those of the modules it imports (flat: every
        // name a module declares can be used directly by the importing program).
        std::unordered_set<const ProgramNode*> seen{&program};
        flatten(program, false, seen);

        // Pass 1: collect every struct and function signature, so they can be used before (above)
        // the place where they are declared.
        collect_structs();
        collect_functions();

        // Pass 2: check the top-level statements in order; top-level variables become globals.
        model_.global_scope.kind = "global";
        model_.global_scope.span = program.span;
        scopes_.push_back(Scope{{}, &model_.global_scope});
        current_function_ = 0;
        for (Stmt* item : items_)
        {
            if (item->kind != StmtKind::FuncDecl && item->kind != StmtKind::StructDecl)
            {
                check_statement(*item);
            }
        }

        // Pass 3: check function bodies. Every global is known by now, so functions may use any.
        for (Stmt* item : items_)
        {
            if (item->kind == StmtKind::FuncDecl)
            {
                auto& func = static_cast<FuncDeclStmt&>(*item);
                if (!func.is_native && func.function_index >= 0)
                {
                    check_function(func);
                }
            }
        }
        scopes_.clear();
        model_.items.assign(items_.begin(), items_.end());
        return std::move(model_);
    }

    // ------------------------------------------------------------------ modules

    void SemanticAnalyzer::flatten(ProgramNode& program, bool is_module, std::unordered_set<const ProgramNode*>& seen)
    {
        // Imports first, wherever they are written: everything a file imports is declared (and its
        // variables initialised) before the file's own items.
        for (StmtPtr& item : program.items)
        {
            if (item->kind == StmtKind::Import)
            {
                auto& import = static_cast<ImportStmt&>(*item);
                if (import.is_native)
                {
                    // import native "mathx": a C++ library, loaded when the program runs.
                    if (std::ranges::find(model_.native_libraries, import.target) == model_.native_libraries.end())
                    {
                        model_.native_libraries.push_back(import.target);
                    }
                }
                else if (!import.is_file)
                {
                    import.builtin_module = find_builtin_module(import.target);
                    if (import.builtin_module)
                    {
                        model_.imported_modules.insert(*import.builtin_module);
                    }
                    else
                    {
                        diagnostics_.report(DiagId::UnknownBuiltinModule, import.target_span, import.target);
                    }
                }
                else if (import.module != nullptr && seen.insert(import.module).second)
                {
                    flatten(*import.module, true, seen);
                }
            }
        }
        for (StmtPtr& item : program.items)
        {
            if (item->kind == StmtKind::Import)
            {
                continue;
            }
            if (is_module)
            {
                // A module only declares things; importing it must not run any statements.
                const bool declaration = item->kind == StmtKind::FuncDecl || item->kind == StmtKind::StructDecl
                    || item->kind == StmtKind::VarDecl || item->kind == StmtKind::Pass;
                if (!declaration)
                {
                    diagnostics_.report(DiagId::ModuleStatement, item->span);
                    continue;
                }
                if (item->kind == StmtKind::FuncDecl && language_.is_main_name(static_cast<FuncDeclStmt&>(*item).name))
                {
                    diagnostics_.report(DiagId::ModuleDefinesMain, static_cast<FuncDeclStmt&>(*item).name_span);
                    continue;
                }
            }
            items_.push_back(item.get());
        }
    }

    std::optional<BuiltinId> SemanticAnalyzer::builtin_named(std::string_view name) const
    {
        const auto id = language_.builtin(name);
        if (!id)
        {
            return std::nullopt;
        }
        const BuiltinModule module = builtin_info(*id).module;
        if (module != BuiltinModule::None && !model_.imported_modules.contains(module))
        {
            return std::nullopt; // e.g. "run" is an ordinary name unless the program imports os
        }
        return id;
    }

    // ------------------------------------------------------------------ declarations

    int StructInfo::field_index(std::string_view field) const
    {
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            if (fields[i].name == field)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    const StructInfo* SemanticAnalyzer::find_struct(std::string_view name) const
    {
        const auto it = struct_indexes_.find(std::string(name));
        return it == struct_indexes_.end() ? nullptr : model_.structs[static_cast<std::size_t>(it->second)].get();
    }

    bool SemanticAnalyzer::resolve_type(TypeSyntax& syntax)
    {
        if (!syntax.parts.empty())
        {
            // A tuple or map type: resolve the parts, then rebuild the type from them.
            bool ok = true;
            std::vector<Type> types;
            for (TypeSyntax& part : syntax.parts)
            {
                ok = resolve_type(part) && ok;
                types.push_back(part.type);
            }
            const std::uint8_t dimensions = syntax.type.dimensions;
            if (syntax.type.base == Type::Base::Function)
            {
                Type result = types.front();
                syntax.type = Type::function_of(std::vector<Type>(types.begin() + 1, types.end()), std::move(result));
            }
            else if (syntax.type.base == Type::Base::Map)
            {
                if (!types[0].is_error() && !types[0].is_key_type())
                {
                    diagnostics_.report(DiagId::MapKeyType, syntax.parts[0].span, type_name(types[0]));
                    ok = false;
                }
                syntax.type = Type::map_of(types[0], types[1]);
            }
            else
            {
                syntax.type = Type::tuple_of(std::move(types));
            }
            syntax.type.dimensions = dimensions;
            if (!ok)
            {
                syntax.type = Type::error();
            }
            return ok;
        }
        if (syntax.struct_name.empty())
        {
            return true;
        }
        const auto it = struct_indexes_.find(syntax.struct_name);
        if (it == struct_indexes_.end())
        {
            diagnostics_.report(DiagId::UnknownType, syntax.span, syntax.struct_name);
            syntax.type = Type::error();
            return false;
        }
        const std::uint8_t dimensions = syntax.type.dimensions;
        syntax.type = Type::struct_type(it->second, &model_.structs[static_cast<std::size_t>(it->second)]->name);
        syntax.type.dimensions = dimensions;
        return true;
    }

    void SemanticAnalyzer::collect_structs()
    {
        // First register every struct name, so fields may refer to structs declared later.
        std::vector<StructDeclStmt*> declarations;
        for (Stmt* item : items_)
        {
            if (item->kind != StmtKind::StructDecl)
            {
                continue;
            }
            auto& decl = static_cast<StructDeclStmt&>(*item);
            if (builtin_named(decl.name) || struct_indexes_.contains(decl.name))
            {
                diagnostics_.report(DiagId::TypeRedefined, decl.name_span, decl.name);
                continue;
            }
            auto info = std::make_unique<StructInfo>();
            info->name = decl.name;
            info->decl = &decl;
            info->span = decl.span;
            decl.struct_index = static_cast<int>(model_.structs.size());
            struct_indexes_.emplace(decl.name, decl.struct_index);
            model_.structs.push_back(std::move(info));
            declarations.push_back(&decl);
        }

        for (StructDeclStmt* decl : declarations)
        {
            StructInfo& info = *model_.structs[static_cast<std::size_t>(decl->struct_index)];
            for (FieldDecl& field : decl->fields)
            {
                if (info.field_index(field.name) >= 0)
                {
                    diagnostics_.report(DiagId::DuplicateField, field.name_span, decl->name, field.name);
                    continue;
                }
                resolve_type(field.type);
                info.fields.push_back(FieldInfo{field.name, field.type.type, field.name_span});
            }
        }
        check_recursive_structs();
    }

    void SemanticAnalyzer::check_recursive_structs()
    {
        // A struct may not contain itself, directly or through other structs or arrays: every
        // value must be finite, and reference counting can then never leak a cycle.
        enum class Mark
        {
            Unvisited,
            InProgress,
            Done
        };
        std::vector<Mark> marks(model_.structs.size(), Mark::Unvisited);
        const auto visit = [&](auto& self, std::size_t index) -> bool
        {
            if (marks[index] == Mark::InProgress)
            {
                return false;
            }
            if (marks[index] == Mark::Done)
            {
                return true;
            }
            marks[index] = Mark::InProgress;
            StructInfo& info = *model_.structs[index];
            for (FieldInfo& field : info.fields)
            {
                // Structs reached through arrays, tuples and maps count too.
                const auto mentions_cycle = [&](const auto& find, const Type& type) -> bool
                {
                    if (type.base == Type::Base::Function)
                    {
                        return false; // a function value holds no values of its parameter or result types
                    }
                    if (type.base == Type::Base::Struct)
                    {
                        return !self(self, static_cast<std::size_t>(type.struct_index));
                    }
                    for (const Type& part : type.elements())
                    {
                        if (find(find, part))
                        {
                            return true;
                        }
                    }
                    return false;
                };
                if (mentions_cycle(mentions_cycle, field.type))
                {
                    diagnostics_.report(DiagId::RecursiveStruct, field.span, info.name, field.name);
                    field.type = Type::error(); // report each cycle once
                }
            }
            marks[index] = Mark::Done;
            return true;
        };
        for (std::size_t i = 0; i < model_.structs.size(); ++i)
        {
            visit(visit, i);
        }
    }

    void SemanticAnalyzer::collect_functions()
    {
        for (Stmt* item : items_)
        {
            if (item->kind != StmtKind::FuncDecl)
            {
                continue;
            }
            auto& func = static_cast<FuncDeclStmt&>(*item);
            if (builtin_named(func.name))
            {
                diagnostics_.report(DiagId::BuiltinRedefined, func.name_span, func.name);
                continue;
            }
            if (struct_indexes_.contains(func.name))
            {
                diagnostics_.report(DiagId::TypeRedefined, func.name_span, func.name);
                continue;
            }
            for (Param& param : func.params)
            {
                resolve_type(param.type);
            }
            if (func.return_type)
            {
                resolve_type(*func.return_type);
            }
            if (func.is_native)
            {
                collect_native(func);
                continue;
            }
            if (native_indexes_.contains(func.name) || function_indexes_.contains(func.name))
            {
                diagnostics_.report(DiagId::FunctionRedefined, func.name_span, func.name);
                continue;
            }

            FunctionInfo info;
            info.name = func.name;
            info.decl = &func;
            info.span = func.span;
            info.return_type = func.return_type ? func.return_type->type : Type::void_type();
            for (const Param& param : func.params)
            {
                info.parameter_types.push_back(param.type.type);
            }

            func.function_index = static_cast<int>(model_.functions.size());
            function_indexes_.emplace(func.name, func.function_index);
            model_.functions.push_back(std::move(info));

            if (language_.is_main_name(func.name))
            {
                if (!func.params.empty() || func.return_type)
                {
                    diagnostics_.report(DiagId::BadMainSignature, func.name_span);
                }
                else
                {
                    model_.main_function = func.function_index;
                }
            }
        }
    }

    std::optional<std::uint8_t> native_kind(const Type& type, bool allow_function)
    {
        if (type.is_int())
        {
            return 1; // THUNDER_INT
        }
        if (type.is_float())
        {
            return 2; // THUNDER_FLOAT
        }
        if (type.is_bool())
        {
            return 3; // THUNDER_BOOL
        }
        if (type.is_string())
        {
            return 4; // THUNDER_STRING
        }
        if (allow_function && type.is_function())
        {
            // A callback may only take and return the simple types, for the same reason.
            for (const Type& parameter : type.parameter_types())
            {
                if (!native_kind(parameter, false))
                {
                    return std::nullopt;
                }
            }
            if (!type.result_type().is_void() && !native_kind(type.result_type(), false))
            {
                return std::nullopt;
            }
            return 5; // THUNDER_FUNCTION
        }
        return std::nullopt;
    }

    void SemanticAnalyzer::collect_native(FuncDeclStmt& func)
    {
        if (native_indexes_.contains(func.name) || function_indexes_.contains(func.name))
        {
            diagnostics_.report(DiagId::FunctionRedefined, func.name_span, func.name);
            return;
        }
        if (model_.native_libraries.empty())
        {
            diagnostics_.report(DiagId::NativeNeedsLibrary, func.name_span, func.name);
        }
        if (language_.is_main_name(func.name))
        {
            diagnostics_.report(DiagId::BadMainSignature, func.name_span);
        }
        if (func.params.size() > bytecode::max_native_arity)
        {
            diagnostics_.report(DiagId::NativeTooManyParameters, func.name_span, func.name,
                bytecode::max_native_arity);
            return;
        }

        bool ok = true;
        NativeInfo info;
        info.name = func.name;
        info.decl = &func;
        info.span = func.span;
        info.return_type = func.return_type ? func.return_type->type : Type::void_type();
        for (const Param& param : func.params)
        {
            if (!param.type.type.is_error() && !native_kind(param.type.type, true))
            {
                diagnostics_.report(DiagId::NativeTypeNotSupported, param.type.span, type_name(param.type.type));
                ok = false;
            }
            info.parameter_types.push_back(param.type.type);
        }
        if (!info.return_type.is_void() && !info.return_type.is_error() && !native_kind(info.return_type, false))
        {
            diagnostics_.report(DiagId::NativeTypeNotSupported, func.return_type->span, type_name(info.return_type));
            ok = false;
        }
        if (!ok)
        {
            return;
        }
        func.native_index = static_cast<int>(model_.natives.size());
        native_indexes_.emplace(func.name, func.native_index);
        model_.natives.push_back(std::move(info));
    }

    void SemanticAnalyzer::check_function(FuncDeclStmt& func)
    {
        current_function_ = func.function_index;
        loop_depth_ = 0;

        // Parameters and the outermost block of the body share one scope.
        push_scope("function", func.span, func.name);
        for (Param& param : func.params)
        {
            param.symbol = declare(param.name, SymbolKind::Parameter, param.type.type, false, param.name_span);
        }
        check_statements(func.body->statements);
        pop_scope();

        const FunctionInfo& info = model_.functions[static_cast<std::size_t>(func.function_index)];
        if (!info.return_type.is_void() && !info.return_type.is_error() && !always_returns(*func.body))
        {
            diagnostics_.report(DiagId::NotAllPathsReturn, func.name_span, func.name);
        }
        current_function_ = 0;
    }

    void SemanticAnalyzer::push_scope(std::string kind, const Span& span, std::string name)
    {
        ScopeInfo& parent = *scopes_.back().info;
        parent.children.push_back(ScopeInfo{std::move(kind), std::move(name), span, {}, {}});
        scopes_.push_back(Scope{{}, &parent.children.back()});
    }

    void SemanticAnalyzer::pop_scope()
    {
        for (const Symbol* symbol : scopes_.back().info->symbols)
        {
            if (symbol->kind == SymbolKind::Local && !symbol->used)
            {
                diagnostics_.report(DiagId::UnusedVariable, symbol->span, symbol->name);
            }
        }
        scopes_.pop_back();
    }

    Symbol* SemanticAnalyzer::declare(const std::string& name, SymbolKind kind, Type type, bool is_const,
        const Span& span)
    {
        Scope& scope = scopes_.back();
        if (struct_indexes_.contains(name))
        {
            diagnostics_.report(DiagId::TypeRedefined, span, name);
        }
        else if (scope.names.contains(name))
        {
            diagnostics_.report(DiagId::AlreadyDeclared, span, name);
        }

        auto symbol = std::make_unique<Symbol>();
        symbol->name = name;
        symbol->kind = kind;
        symbol->type = type;
        symbol->is_const = is_const;
        symbol->span = span;
        symbol->function_index = current_function_;

        if (kind == SymbolKind::Global)
        {
            symbol->slot = static_cast<std::uint16_t>(model_.globals.size());
            model_.globals.push_back(symbol.get());
        }
        else
        {
            FunctionInfo& function = model_.functions[static_cast<std::size_t>(current_function_)];
            symbol->slot = function.local_count++;
            function.locals.push_back(symbol.get());
        }

        Symbol* raw = symbol.get();
        scope.names[name] = raw;
        scope.info->symbols.push_back(raw);
        model_.symbols.push_back(std::move(symbol));
        return raw;
    }

    Symbol* SemanticAnalyzer::lookup(const std::string& name)
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            const auto found = it->names.find(name);
            if (found != it->names.end())
            {
                return found->second;
            }
        }
        return nullptr;
    }

    // ------------------------------------------------------------------ statements

    void SemanticAnalyzer::check_statements(std::vector<StmtPtr>& statements)
    {
        bool reported_unreachable = false;
        bool terminated = false;
        for (StmtPtr& stmt : statements)
        {
            if (terminated && !reported_unreachable)
            {
                diagnostics_.report(DiagId::UnreachableCode, stmt->span);
                reported_unreachable = true;
            }
            check_statement(*stmt);
            terminated = terminated || terminates(*stmt);
        }
    }

    void SemanticAnalyzer::check_statement(Stmt& stmt)
    {
        switch (stmt.kind)
        {
        case StmtKind::VarDecl:
            check_var_decl(static_cast<VarDeclStmt&>(stmt));
            break;
        case StmtKind::Assign:
            check_assign(static_cast<AssignStmt&>(stmt));
            break;
        case StmtKind::ExprStmt:
            check_expr(*static_cast<ExprStmt&>(stmt).expr, std::nullopt, false);
            break;
        case StmtKind::Block:
            push_scope("block", stmt.span);
            check_statements(static_cast<BlockStmt&>(stmt).statements);
            pop_scope();
            break;
        case StmtKind::If:
        {
            auto& branch = static_cast<IfStmt&>(stmt);
            check_condition(*branch.condition);
            check_statement(*branch.then_branch);
            if (branch.else_branch)
            {
                check_statement(*branch.else_branch);
            }
            break;
        }
        case StmtKind::While:
        {
            auto& loop = static_cast<WhileStmt&>(stmt);
            check_condition(*loop.condition);
            ++loop_depth_;
            check_statement(*loop.body);
            --loop_depth_;
            break;
        }
        case StmtKind::For:
        {
            auto& loop = static_cast<ForStmt&>(stmt);
            push_scope("for", stmt.span);
            if (loop.init)
            {
                check_statement(*loop.init);
            }
            if (loop.condition)
            {
                check_condition(*loop.condition);
            }
            if (loop.step)
            {
                check_statement(*loop.step);
            }
            ++loop_depth_;
            check_statement(*loop.body);
            --loop_depth_;
            pop_scope();
            break;
        }
        case StmtKind::ForEach:
            check_for_each(static_cast<ForEachStmt&>(stmt));
            break;
        case StmtKind::Break:
        case StmtKind::Continue:
            if (loop_depth_ == 0)
            {
                diagnostics_.report(DiagId::OutsideLoop, stmt.span,
                    language_.spelling(stmt.kind == StmtKind::Break ? TokenKind::KwBreak : TokenKind::KwContinue));
            }
            break;
        case StmtKind::Pass:
            break;
        case StmtKind::Return:
            check_return(static_cast<ReturnStmt&>(stmt));
            break;
        case StmtKind::FuncDecl:
        case StmtKind::StructDecl:
        case StmtKind::Import:
            // Nested functions, structs and imports are rejected by the parser; top-level ones are
            // handled before the statements are checked.
            break;
        }
    }

    void SemanticAnalyzer::check_var_decl(VarDeclStmt& decl)
    {
        const bool top_level = current_function_ == 0 && scopes_.size() == 1;
        const SymbolKind kind = top_level ? SymbolKind::Global : SymbolKind::Local;
        if (!decl.names.empty())
        {
            // var (a, b) = pair: every name gets the type of its element of the tuple.
            const Type value = check_expr(*decl.initializer);
            std::vector<Type> element_types(decl.names.size(), Type::error());
            if (!value.is_error())
            {
                if (value.is_tuple() && value.elements().size() == decl.names.size())
                {
                    element_types = value.elements();
                }
                else
                {
                    diagnostics_.report(DiagId::DestructureMismatch, decl.initializer->span, type_name(value),
                        decl.names.size());
                }
            }
            for (std::size_t i = 0; i < decl.names.size(); ++i)
            {
                decl.symbols.push_back(declare(decl.names[i], kind, element_types[i], decl.is_const, decl.name_spans[i]));
            }
            return;
        }

        std::optional<Type> declared;
        if (decl.declared_type)
        {
            resolve_type(*decl.declared_type);
            declared = decl.declared_type->type;
        }

        Type type = declared.value_or(Type::error());
        if (decl.initializer)
        {
            Type value = check_expr(*decl.initializer, declared);
            if (declared)
            {
                value = coerce(*decl.initializer, *declared, value);
            }
            if (!declared)
            {
                type = value;
            }
            else if (!assignable(*declared, value))
            {
                diagnostics_.report(DiagId::AssignTypeMismatch, decl.initializer->span, type_name(value),
                    decl.name, type_name(*declared));
            }
        }

        decl.symbol = declare(decl.name, kind, type, decl.is_const, decl.name_span);
    }

    void SemanticAnalyzer::check_assign(AssignStmt& assign)
    {
        Type target_type = Type::error();
        if (assign.target->kind == ExprKind::Name)
        {
            auto& name = static_cast<NameExpr&>(*assign.target);
            Symbol* symbol = lookup(name.name);
            if (symbol == nullptr)
            {
                target_type = check_name(name); // reports the error
                if (name.function_index >= 0)
                {
                    diagnostics_.report(DiagId::AssignToConstant, name.span, name.name); // functions are constants
                    target_type = Type::error();
                }
            }
            else
            {
                name.symbol = symbol;
                name.type = symbol->type;
                target_type = symbol->type;
                if (symbol->is_const)
                {
                    diagnostics_.report(DiagId::AssignToConstant, name.span, name.name);
                }
            }
        }
        else if (assign.target->kind == ExprKind::Field)
        {
            target_type = check_expr(*assign.target);
        }
        else if (assign.target->kind == ExprKind::Index)
        {
            auto& index = static_cast<IndexExpr&>(*assign.target);
            target_type = check_expr(index);
            if (index.target->type.is_string())
            {
                diagnostics_.report(DiagId::StringImmutable, index.span);
                target_type = Type::error();
            }
        }
        else
        {
            check_expr(*assign.target); // the parser already reported the invalid target
        }

        Type value = check_expr(*assign.value, target_type.is_error() ? std::nullopt : std::optional(target_type));
        if (assign.compound_op)
        {
            check_compound_assign(assign, target_type, value);
            return;
        }
        value = coerce(*assign.value, target_type, value);
        if (!assignable(target_type, value))
        {
            std::string target_text = language_.term("the target");
            if (assign.target->kind == ExprKind::Name)
            {
                target_text = static_cast<NameExpr&>(*assign.target).name;
            }
            else if (assign.target->kind == ExprKind::Index)
            {
                const bool map = static_cast<IndexExpr&>(*assign.target).target->type.is_map();
                target_text = language_.term(map ? "the map entry" : "the array element");
            }
            else if (assign.target->kind == ExprKind::Field)
            {
                target_text = static_cast<FieldExpr&>(*assign.target).field;
            }
            diagnostics_.report(DiagId::AssignTypeMismatch, assign.value->span, type_name(value), target_text,
                type_name(target_type));
        }
    }

    void SemanticAnalyzer::check_compound_assign(AssignStmt& assign, const Type& target_type, Type value)
    {
        if (target_type.is_error() || value.is_error())
        {
            return;
        }
        value = coerce(*assign.value, target_type, value);
        const TokenKind op = *Lexer::compound_operator(*assign.compound_op);
        const bool numbers = target_type.is_number() && target_type == value;
        const bool strings = target_type.is_string() && value.is_string();
        const bool ok = op == TokenKind::Plus ? (numbers || strings)
            : op == TokenKind::Percent         ? (target_type.is_int() && value.is_int())
                                               : numbers;
        if (!ok)
        {
            diagnostics_.report(DiagId::BinaryOperandMismatch, assign.span, language_.spelling(*assign.compound_op),
                type_name(target_type), type_name(value));
        }
    }

    void SemanticAnalyzer::check_for_each(ForEachStmt& loop)
    {
        push_scope("for", loop.span);
        const Type collection = check_expr(*loop.collection);
        // Arrays give their elements, strings their characters and maps their keys (use items(m)
        // for the pairs).
        Type element = Type::error();
        if (collection.is_array())
        {
            element = collection.element();
        }
        else if (collection.is_string())
        {
            element = Type::string_type();
        }
        else if (collection.is_map())
        {
            element = collection.key_type();
            loop.over_map_keys = true;
        }
        else if (!collection.is_error())
        {
            diagnostics_.report(DiagId::NotIterable, loop.collection->span, type_name(collection));
        }

        std::vector<Type> names(loop.names.size(), Type::error());
        if (loop.names.size() == 1)
        {
            names[0] = element;
        }
        else if (!element.is_error())
        {
            if (element.is_tuple() && element.elements().size() == loop.names.size())
            {
                names = element.elements();
            }
            else
            {
                diagnostics_.report(DiagId::DestructureMismatch, loop.collection->span, type_name(element),
                    loop.names.size());
            }
        }
        for (std::size_t i = 0; i < loop.names.size(); ++i)
        {
            loop.symbols.push_back(declare(loop.names[i], SymbolKind::Local, names[i], false, loop.name_spans[i]));
        }

        // Two hidden locals hold the sequence and the position in it. Their names cannot clash with
        // a program's own names, which never contain brackets.
        const Type sequence = loop.over_map_keys ? Type::array_of(element) : collection;
        Symbol* sequence_symbol =
            declare("(" + language_.term("sequence") + ")", SymbolKind::Local, sequence, false, loop.span);
        Symbol* index_symbol =
            declare("(" + language_.term("position") + ")", SymbolKind::Local, Type::int_type(), false, loop.span);
        sequence_symbol->used = true;
        index_symbol->used = true;
        loop.sequence_symbol = sequence_symbol;
        loop.index_symbol = index_symbol;

        ++loop_depth_;
        check_statement(*loop.body);
        --loop_depth_;
        pop_scope();
    }

    void SemanticAnalyzer::check_return(ReturnStmt& ret)
    {
        if (current_function_ == 0)
        {
            diagnostics_.report(DiagId::ReturnOutsideFunction, ret.span);
            if (ret.value)
            {
                check_expr(*ret.value);
            }
            return;
        }

        const FunctionInfo& function = model_.functions[static_cast<std::size_t>(current_function_)];
        if (function.return_type.is_void())
        {
            if (ret.value)
            {
                check_expr(*ret.value, std::nullopt, false);
                diagnostics_.report(DiagId::VoidReturnsValue, ret.value->span, function.name);
            }
            return;
        }

        if (!ret.value)
        {
            diagnostics_.report(DiagId::MissingReturnValue, ret.span, function.name, type_name(function.return_type));
            return;
        }
        const Type value = coerce(*ret.value, function.return_type, check_expr(*ret.value, function.return_type));
        if (!assignable(function.return_type, value))
        {
            diagnostics_.report(DiagId::ReturnTypeMismatch, ret.value->span, function.name,
                type_name(function.return_type), type_name(value));
        }
    }

    void SemanticAnalyzer::check_condition(Expr& condition)
    {
        const Type type = check_expr(condition);
        if (!type.is_error() && !type.is_bool())
        {
            diagnostics_.report(DiagId::ConditionNotBool, condition.span, type_name(type));
        }
    }

    // ------------------------------------------------------------------ expressions

    Type SemanticAnalyzer::check_expr(Expr& expr, std::optional<Type> expected, bool value_required)
    {
        Type type = Type::error();
        switch (expr.kind)
        {
        case ExprKind::IntLiteral:
            type = Type::int_type();
            break;
        case ExprKind::FloatLiteral:
            type = Type::float_type();
            break;
        case ExprKind::BoolLiteral:
            type = Type::bool_type();
            break;
        case ExprKind::StringLiteral:
            type = Type::string_type();
            break;
        case ExprKind::ArrayLiteral:
        {
            auto& array = static_cast<ArrayLiteralExpr&>(expr);
            const std::optional<Type> element_hint =
                expected && expected->is_array() ? std::optional(expected->element()) : std::nullopt;
            if (array.elements.empty())
            {
                if (element_hint)
                {
                    type = *expected;
                }
                else
                {
                    diagnostics_.report(DiagId::EmptyArrayNeedsType, array.span);
                }
                break;
            }

            Type first = check_expr(*array.elements.front(), element_hint);
            bool ok = !first.is_error();
            std::vector<Type> element_types{first};
            bool any_float = first.is_float();
            bool all_numbers = first.is_number();
            for (std::size_t i = 1; i < array.elements.size(); ++i)
            {
                const Type element = check_expr(*array.elements[i], first.is_error() ? element_hint : first);
                element_types.push_back(element);
                any_float = any_float || element.is_float();
                all_numbers = all_numbers && element.is_number();
            }
            // [1, 2.5] is an array of decimals: whole numbers among the elements are widened.
            if (all_numbers && any_float)
            {
                for (std::size_t i = 0; i < array.elements.size(); ++i)
                {
                    element_types[i] = coerce(*array.elements[i], Type::float_type(), element_types[i]);
                }
                first = Type::float_type();
            }
            for (std::size_t i = 1; i < array.elements.size(); ++i)
            {
                if (!first.is_error() && !element_types[i].is_error() && element_types[i] != first)
                {
                    diagnostics_.report(DiagId::ArrayElementMismatch, array.elements[i]->span, type_name(first),
                        type_name(element_types[i]));
                    ok = false;
                }
            }
            if (ok)
            {
                type = Type::array_of(first);
            }
            break;
        }
        case ExprKind::TupleLiteral:
        {
            auto& tuple = static_cast<TupleLiteralExpr&>(expr);
            const std::vector<Type>* hints = expected && expected->is_tuple()
                    && expected->elements().size() == tuple.elements.size()
                ? &expected->elements()
                : nullptr;
            std::vector<Type> element_types;
            bool ok = true;
            for (std::size_t i = 0; i < tuple.elements.size(); ++i)
            {
                Type element = check_expr(*tuple.elements[i], hints ? std::optional((*hints)[i]) : std::nullopt);
                if (hints != nullptr)
                {
                    element = coerce(*tuple.elements[i], (*hints)[i], element);
                }
                ok = ok && !element.is_error();
                element_types.push_back(element);
            }
            if (ok)
            {
                type = Type::tuple_of(std::move(element_types));
            }
            break;
        }
        case ExprKind::MapLiteral:
            type = check_map_literal(static_cast<MapLiteralExpr&>(expr), expected);
            break;
        case ExprKind::Name:
            type = check_name(static_cast<NameExpr&>(expr));
            break;
        case ExprKind::Unary:
        {
            auto& unary = static_cast<UnaryExpr&>(expr);
            const Type operand = check_expr(*unary.operand);
            if (operand.is_error())
            {
                break;
            }
            if (unary.op == TokenKind::Minus && operand.is_number())
            {
                type = operand;
            }
            else if (unary.op == TokenKind::KwNot && operand.is_bool())
            {
                type = Type::bool_type();
            }
            else if (unary.op == TokenKind::Tilde && operand.is_int())
            {
                type = operand;
            }
            else
            {
                diagnostics_.report(DiagId::UnaryOperandMismatch, unary.span, language_.spelling(unary.op),
                    type_name(operand));
            }
            break;
        }
        case ExprKind::Binary:
            type = check_binary(static_cast<BinaryExpr&>(expr));
            break;
        case ExprKind::Call:
            type = check_call(static_cast<CallExpr&>(expr), expected, value_required);
            break;
        case ExprKind::Index:
        {
            auto& index = static_cast<IndexExpr&>(expr);
            const Type target = check_expr(*index.target);
            if (target.is_map())
            {
                // m[key]: the key must have the map's key type.
                const Type key = check_expr(*index.index, target.key_type());
                if (!key.is_error() && key != target.key_type())
                {
                    diagnostics_.report(DiagId::MapKeyMismatch, index.index->span, type_name(target),
                        type_name(target.key_type()), type_name(key));
                }
                type = target.value_type();
                break;
            }
            const Type position = check_expr(*index.index);
            if (!position.is_error() && !position.is_int())
            {
                diagnostics_.report(DiagId::IndexNotInt, index.index->span, type_name(position));
            }
            if (target.is_error())
            {
                break;
            }
            if (target.is_array())
            {
                type = target.element();
            }
            else if (target.is_string())
            {
                type = Type::string_type();
            }
            else
            {
                diagnostics_.report(DiagId::NotIndexable, index.target->span, type_name(target));
            }
            break;
        }
        case ExprKind::Field:
            type = check_field(static_cast<FieldExpr&>(expr));
            break;
        case ExprKind::TupleIndex:
        {
            auto& element = static_cast<TupleIndexExpr&>(expr);
            const Type target = check_expr(*element.target);
            if (target.is_error())
            {
                break;
            }
            if (!target.is_tuple())
            {
                diagnostics_.report(DiagId::NotATuple, element.index_span, element.index, type_name(target));
            }
            else if (element.index < 0 || static_cast<std::size_t>(element.index) >= target.elements().size())
            {
                diagnostics_.report(DiagId::TupleIndexOutOfRange, element.index_span, type_name(target), element.index);
            }
            else
            {
                type = target.elements()[static_cast<std::size_t>(element.index)];
            }
            break;
        }
        }

        expr.type = type;
        return type;
    }

    Type SemanticAnalyzer::check_map_literal(MapLiteralExpr& map, std::optional<Type> expected)
    {
        const bool hinted = expected && expected->is_map();
        if (map.keys.empty())
        {
            if (hinted)
            {
                return *expected;
            }
            if (!expected || !expected->is_error()) // (an invalid annotation was already reported)
            {
                diagnostics_.report(DiagId::EmptyMapNeedsType, map.span);
            }
            return Type::error();
        }
        bool ok = true;
        Type key_type = Type::error();
        Type value_type = Type::error();
        for (std::size_t i = 0; i < map.keys.size(); ++i)
        {
            const std::optional<Type> key_hint = hinted ? std::optional(expected->key_type())
                                                        : (key_type.is_error() ? std::nullopt : std::optional(key_type));
            const Type key = check_expr(*map.keys[i], key_hint);
            if (key.is_error())
            {
                ok = false;
            }
            else if (!key.is_key_type())
            {
                diagnostics_.report(DiagId::MapKeyType, map.keys[i]->span, type_name(key));
                ok = false;
            }
            else if (key_type.is_error())
            {
                key_type = key;
            }
            else if (key != key_type)
            {
                diagnostics_.report(DiagId::MapElementMismatch, map.keys[i]->span, language_.term("keys"),
                    type_name(key_type), type_name(key));
                ok = false;
            }

            const std::optional<Type> value_hint = hinted ? std::optional(expected->value_type())
                                                          : (value_type.is_error() ? std::nullopt : std::optional(value_type));
            Type value = check_expr(*map.values[i], value_hint);
            // {"a": 1.5, "b": 2} is a map of decimals: whole-number values are widened, and an
            // earlier whole-number value is widened as soon as a decimal appears.
            if (value_type.is_float())
            {
                value = coerce(*map.values[i], Type::float_type(), value);
            }
            else if (value.is_float() && value_type.is_int())
            {
                for (std::size_t earlier = 0; earlier < i; ++earlier)
                {
                    coerce(*map.values[earlier], Type::float_type(), Type::int_type());
                }
                value_type = Type::float_type();
            }
            if (value.is_error())
            {
                ok = false;
            }
            else if (value_type.is_error())
            {
                value_type = value;
            }
            else if (value != value_type)
            {
                diagnostics_.report(DiagId::MapElementMismatch, map.values[i]->span, language_.term("values"),
                    type_name(value_type), type_name(value));
                ok = false;
            }
        }
        return ok ? Type::map_of(key_type, value_type) : Type::error();
    }

    Type SemanticAnalyzer::check_field(FieldExpr& field)
    {
        const Type target = check_expr(*field.target);
        if (target.is_error())
        {
            return Type::error();
        }
        if (!target.is_struct())
        {
            diagnostics_.report(DiagId::NotAStruct, field.field_span, type_name(target));
            return Type::error();
        }
        const StructInfo& info = *model_.structs[static_cast<std::size_t>(target.struct_index)];
        field.field_index = info.field_index(field.field);
        if (field.field_index < 0)
        {
            diagnostics_.report(DiagId::NoSuchField, field.field_span, type_name(target), field.field);
            return Type::error();
        }
        return info.fields[static_cast<std::size_t>(field.field_index)].type;
    }

    Type SemanticAnalyzer::check_name(NameExpr& name)
    {
        Symbol* symbol = lookup(name.name);
        if (symbol == nullptr)
        {
            if (struct_indexes_.contains(name.name))
            {
                diagnostics_.report(DiagId::StructUsedAsValue, name.span, name.name, name.name);
            }
            else if (const auto found = function_indexes_.find(name.name); found != function_indexes_.end())
            {
                // A function used as a value, e.g. map(numbers, double): a reference to the function.
                const FunctionInfo& function = model_.functions[static_cast<std::size_t>(found->second)];
                name.function_index = found->second;
                return Type::function_of(function.parameter_types, function.return_type);
            }
            else if (native_indexes_.contains(name.name) || builtin_named(name.name))
            {
                // Native functions have no Thunder function value: there is nothing to call back.
                diagnostics_.report(DiagId::FunctionUsedAsValue, name.span, name.name, name.name);
            }
            else if (const auto gated = language_.builtin(name.name))
            {
                diagnostics_.report(DiagId::BuiltinNeedsImport, name.span, name.name,
                    language_.spelling(builtin_info(*gated).module));
            }
            else
            {
                diagnostics_.report(DiagId::UndeclaredName, name.span, name.name);
            }
            return Type::error();
        }
        symbol->used = true;
        name.symbol = symbol;
        return symbol->type;
    }

    Type SemanticAnalyzer::check_binary(BinaryExpr& binary)
    {
        const Type left = check_expr(*binary.left);
        const Type right = check_expr(*binary.right);
        if (left.is_error() || right.is_error())
        {
            return Type::error();
        }

        // An int mixed with a float is widened, so 1 + 0.5 and n / 2.0 work.
        Type left_type = left;
        Type right_type = right;
        if (left.is_int() && right.is_float())
        {
            left_type = coerce(*binary.left, Type::float_type(), left);
        }
        else if (left.is_float() && right.is_int())
        {
            right_type = coerce(*binary.right, Type::float_type(), right);
        }

        const bool both_number = left_type.is_number() && left_type == right_type;
        const bool both_int = left_type.is_int() && right_type.is_int();
        const bool both_string = left_type.is_string() && right_type.is_string();
        const bool both_bool = left_type.is_bool() && right_type.is_bool();

        switch (binary.op)
        {
        case TokenKind::Plus:
            if (both_number || both_string)
            {
                return left_type;
            }
            break;
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
            if (both_number)
            {
                return left_type;
            }
            break;
        case TokenKind::Percent:
            if (both_int)
            {
                return left_type; // the remainder is only defined for whole numbers
            }
            break;
        case TokenKind::Amp:
        case TokenKind::Pipe:
        case TokenKind::Caret:
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
            if (both_int)
            {
                return left_type; // bits belong to whole numbers only
            }
            break;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            if (both_number || both_string)
            {
                return Type::bool_type();
            }
            break;
        case TokenKind::Equal:
        case TokenKind::NotEqual:
            if (both_number || both_string || both_bool || (left_type.is_tuple() && left_type == right_type))
            {
                return Type::bool_type(); // tuples compare element by element
            }
            break;
        case TokenKind::KwAnd:
        case TokenKind::KwOr:
            if (both_bool)
            {
                return Type::bool_type();
            }
            break;
        default:
            break;
        }

        diagnostics_.report(DiagId::BinaryOperandMismatch, binary.op_span, language_.spelling(binary.op),
            type_name(left_type), type_name(right_type));
        return Type::error();
    }

    Type SemanticAnalyzer::check_call(CallExpr& call, std::optional<Type> expected, bool value_required)
    {
        Type result = Type::error();
        std::string callee_name = call.callee;

        if (call.callee_expr)
        {
            // handlers[i](x): the callee is an expression that must produce a function value.
            const Type callee = check_expr(*call.callee_expr);
            call.callee = type_name(callee); // names the callee in messages
            if (callee.is_error() || !callee.is_function())
            {
                if (!callee.is_error())
                {
                    diagnostics_.report(DiagId::NotAFunction, call.callee_expr->span, call.callee);
                }
                check_remaining(call, 0);
                return Type::error();
            }
            if (call.has_named_arguments())
            {
                diagnostics_.report(DiagId::NamedArgumentsNotAllowed, call.span);
            }
            const std::vector<Type> parameters = callee.parameter_types();
            if (call.arguments.size() != parameters.size())
            {
                diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, parameters.size(), call.arguments.size());
            }
            check_arguments(call, parameters);
            result = callee.result_type();
            if (result.is_void() && value_required)
            {
                diagnostics_.report(DiagId::VoidValue, call.span, call.callee);
                return Type::error();
            }
            return result;
        }

        if (const StructInfo* info = find_struct(call.callee))
        {
            return check_construct(call, *info);
        }
        if (call.has_named_arguments())
        {
            diagnostics_.report(DiagId::NamedArgumentsNotAllowed, call.span);
            for (ExprPtr& argument : call.arguments)
            {
                check_expr(*argument);
            }
            return Type::error();
        }

        Symbol* variable = lookup(call.callee);
        if (const auto builtin = builtin_named(call.callee))
        {
            call.builtin = builtin;
            result = check_builtin(call, *builtin, expected);
        }
        else if (variable != nullptr && variable->type.is_function())
        {
            result = check_value_call(call, *variable); // f(x) where f holds a function value
        }
        else if (const auto native = native_indexes_.find(call.callee); native != native_indexes_.end())
        {
            // A function whose body is in a C++ library: the arguments are checked here, the
            // call happens through CALL_NATIVE (see docs/native.md).
            call.native_index = native->second;
            const NativeInfo& info = model_.natives[static_cast<std::size_t>(native->second)];
            if (call.arguments.size() != info.parameter_types.size())
            {
                diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, info.parameter_types.size(),
                    call.arguments.size());
            }
            check_arguments(call, info.parameter_types);
            result = info.return_type;
        }
        else if (const auto found = function_indexes_.find(call.callee); found != function_indexes_.end())
        {
            call.function_index = found->second;
            const FunctionInfo& function = model_.functions[static_cast<std::size_t>(found->second)];
            const std::size_t expected_count = function.parameter_types.size();
            if (call.arguments.size() != expected_count)
            {
                diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, expected_count,
                    call.arguments.size());
            }
            for (std::size_t i = 0; i < call.arguments.size(); ++i)
            {
                const std::optional<Type> hint =
                    i < expected_count ? std::optional(function.parameter_types[i]) : std::nullopt;
                Type argument = check_expr(*call.arguments[i], hint);
                if (hint)
                {
                    argument = coerce(*call.arguments[i], *hint, argument);
                }
                if (hint && !assignable(*hint, argument))
                {
                    diagnostics_.report(DiagId::ArgumentType, call.arguments[i]->span, i + 1, call.callee,
                        type_name(*hint), type_name(argument));
                }
            }
            result = function.return_type;
        }
        else
        {
            for (ExprPtr& argument : call.arguments)
            {
                check_expr(*argument);
            }
            if (variable != nullptr)
            {
                variable->used = true;
                diagnostics_.report(DiagId::NotAFunction, call.callee_span, call.callee);
            }
            else if (const auto gated = language_.builtin(call.callee))
            {
                diagnostics_.report(DiagId::BuiltinNeedsImport, call.callee_span, call.callee,
                    language_.spelling(builtin_info(*gated).module));
            }
            else
            {
                diagnostics_.report(DiagId::UndeclaredName, call.callee_span, call.callee);
            }
            return Type::error();
        }

        if (result.is_void() && value_required)
        {
            diagnostics_.report(DiagId::VoidValue, call.span, callee_name);
            return Type::error();
        }
        return result;
    }

    Type SemanticAnalyzer::check_value_call(CallExpr& call, Symbol& symbol)
    {
        symbol.used = true;
        call.callee_symbol = &symbol;
        const std::vector<Type> parameters = symbol.type.parameter_types();
        if (call.arguments.size() != parameters.size())
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, parameters.size(), call.arguments.size());
        }
        check_arguments(call, parameters);
        return symbol.type.result_type();
    }

    bool SemanticAnalyzer::check_arguments(CallExpr& call, const std::vector<Type>& types, std::size_t first)
    {
        bool ok = true;
        for (std::size_t i = first; i < call.arguments.size(); ++i)
        {
            const std::optional<Type> hint = i < types.size() ? std::optional(types[i]) : std::nullopt;
            Type argument = check_expr(*call.arguments[i], hint);
            if (hint)
            {
                argument = coerce(*call.arguments[i], *hint, argument);
            }
            if (hint && !assignable(*hint, argument))
            {
                diagnostics_.report(DiagId::ArgumentType, call.arguments[i]->span, i + 1, call.callee, type_name(*hint),
                    type_name(argument));
                ok = false;
            }
            ok = ok && !argument.is_error();
        }
        return ok;
    }

    Type SemanticAnalyzer::check_construct(CallExpr& call, const StructInfo& info)
    {
        call.struct_index = info.decl->struct_index;
        const Type result = Type::struct_type(call.struct_index, &info.name);
        const bool named = call.has_named_arguments();
        bool ok = true;

        for (std::size_t i = 0; i < call.arguments.size(); ++i)
        {
            const std::string& name = i < call.argument_names.size() ? call.argument_names[i] : std::string();
            int field_index = -1;
            if (named && name.empty())
            {
                diagnostics_.report(DiagId::MixedArguments, call.arguments[i]->span, info.name);
                ok = false;
            }
            else if (named)
            {
                field_index = info.field_index(name);
                if (field_index < 0)
                {
                    diagnostics_.report(DiagId::NoSuchField, call.arguments[i]->span, info.name, name);
                    ok = false;
                }
                else if (std::ranges::find(call.argument_fields, field_index) != call.argument_fields.end())
                {
                    diagnostics_.report(DiagId::FieldGivenTwice, call.arguments[i]->span, name);
                    ok = false;
                }
            }
            else if (i < info.fields.size())
            {
                field_index = static_cast<int>(i);
            }
            call.argument_fields.push_back(field_index);

            const std::optional<Type> hint =
                field_index >= 0 ? std::optional(info.fields[static_cast<std::size_t>(field_index)].type) : std::nullopt;
            Type argument = check_expr(*call.arguments[i], hint);
            if (hint)
            {
                argument = coerce(*call.arguments[i], *hint, argument);
            }
            if (hint && !assignable(*hint, argument))
            {
                diagnostics_.report(DiagId::AssignTypeMismatch, call.arguments[i]->span, type_name(argument),
                    info.name + "." + info.fields[static_cast<std::size_t>(field_index)].name, type_name(*hint));
                ok = false;
            }
        }

        // Positional arguments must give every field; named arguments may leave fields at their defaults.
        if (!named && call.arguments.size() != info.fields.size())
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, info.name, info.fields.size(), call.arguments.size());
            ok = false;
        }
        return ok ? result : Type::error();
    }

    namespace
    {
        struct Signature
        {
            std::vector<Type> parameters;
            Type result;
        };

        // The built-ins whose parameter and result types never vary (the others are generic).
        std::optional<Signature> fixed_signature(BuiltinId id)
        {
            const Type i = Type::int_type();
            const Type f = Type::float_type();
            const Type b = Type::bool_type();
            const Type s = Type::string_type();
            const Type v = Type::void_type();
            const Type strings = Type::array_of(s);
            switch (id)
            {
            case BuiltinId::Input:
                return Signature{{s}, s};
            case BuiltinId::EndOfInput:
                return Signature{{}, b};
            case BuiltinId::Substring:
                return Signature{{s, i, i}, s};
            case BuiltinId::Trim:
            case BuiltinId::ToUpper:
            case BuiltinId::ToLower:
                return Signature{{s}, s};
            case BuiltinId::Split:
                return Signature{{s, s}, strings};
            case BuiltinId::Join:
                return Signature{{strings, s}, s};
            case BuiltinId::Replace:
                return Signature{{s, s, s}, s};
            case BuiltinId::StartsWith:
            case BuiltinId::EndsWith:
                return Signature{{s, s}, b};
            case BuiltinId::Repeat:
                return Signature{{s, i}, s};
            case BuiltinId::CharCode:
                return Signature{{s}, i};
            case BuiltinId::FromCharCode:
                return Signature{{i}, s};
            case BuiltinId::IsInt:
            case BuiltinId::IsNumber:
                return Signature{{s}, b};
            case BuiltinId::Round:
            case BuiltinId::Floor:
            case BuiltinId::Ceil:
                return Signature{{f}, i};
            case BuiltinId::Sqrt:
            case BuiltinId::Exp:
            case BuiltinId::Log:
            case BuiltinId::Sin:
            case BuiltinId::Cos:
            case BuiltinId::Tan:
                return Signature{{f}, f};
            case BuiltinId::Pow:
                return Signature{{f, f}, f};
            case BuiltinId::Pi:
            case BuiltinId::Random:
                return Signature{{}, f};
            case BuiltinId::Range:
                return Signature{{i, i}, Type::array_of(i)};
            case BuiltinId::Args:
                return Signature{{}, strings};
            case BuiltinId::GetEnv:
            case BuiltinId::ReadFile:
                return Signature{{s}, s};
            case BuiltinId::Run:
                return Signature{{s}, i};
            case BuiltinId::RunOutput:
                return Signature{{s}, Type::tuple_of({i, s})};
            case BuiltinId::WriteFile:
            case BuiltinId::AppendFile:
                return Signature{{s, s}, v};
            case BuiltinId::FileExists:
                return Signature{{s}, b};
            case BuiltinId::RemoveFile:
                return Signature{{s}, v};
            case BuiltinId::ListDir:
                return Signature{{s}, strings};
            case BuiltinId::CurrentDir:
            case BuiltinId::DateTime:
            case BuiltinId::Platform:
                return Signature{{}, s};
            case BuiltinId::Time:
            case BuiltinId::ClockMs:
                return Signature{{}, i};
            case BuiltinId::Sleep:
            case BuiltinId::Exit:
            case BuiltinId::SeedRandom:
            case BuiltinId::FrameRate:
                return Signature{{i}, v};
            case BuiltinId::TextSize:
            case BuiltinId::LineWidth:
                return Signature{{f}, v};
            case BuiltinId::RandomInt:
                return Signature{{i, i}, i};
            case BuiltinId::Canvas:
                return Signature{{i, i}, v};
            case BuiltinId::Clear:
                return Signature{{s}, v};
            // Coordinates and sizes are decimals; whole numbers are widened, so every existing
            // program still compiles and circle(x, y, 2.5, c) now works too.
            case BuiltinId::Pixel:
                return Signature{{f, f, s}, v};
            case BuiltinId::Line:
            case BuiltinId::Rect:
            case BuiltinId::RectOutline:
                return Signature{{f, f, f, f, s}, v};
            case BuiltinId::Circle:
            case BuiltinId::CircleOutline:
                return Signature{{f, f, f, s}, v};
            case BuiltinId::Text:
                return Signature{{f, f, s, s}, v};
            case BuiltinId::Rgb:
                return Signature{{i, i, i}, s};
            case BuiltinId::Frame:
            case BuiltinId::Show:
                return Signature{{}, v};
            case BuiltinId::KeyPressed:
                return Signature{{}, s};
            case BuiltinId::KeyDown:
                return Signature{{s}, b};
            case BuiltinId::MouseX:
            case BuiltinId::MouseY:
                return Signature{{}, i};
            case BuiltinId::MouseDown:
                return Signature{{}, b};
            default:
                return std::nullopt;
            }
        }

        // Types whose values can be compared with == (as the operator allows).
        bool comparable(const Type& type)
        {
            return type.is_number() || type.is_bool() || type.is_string() || type.is_tuple();
        }
    }

    Type SemanticAnalyzer::check_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected)
    {
        const BuiltinInfo& builtin = builtin_info(id);
        const std::string name = language_.spelling(id);
        const std::size_t count = call.arguments.size();
        if (count < static_cast<std::size_t>(builtin.min_arity)
            || (builtin.arity != variadic && count > static_cast<std::size_t>(builtin.arity)))
        {
            const std::string wanted = builtin.min_arity == builtin.arity
                ? std::to_string(builtin.arity)
                : language_.display.localize_digits(std::format("{}-{}", builtin.min_arity, builtin.arity));
            diagnostics_.report(DiagId::ArgumentCount, call.span, name, wanted, count);
            check_remaining(call, 0);
            return Type::error();
        }

        if (const std::optional<Signature> signature = fixed_signature(id))
        {
            const bool ok = check_arguments(call, signature->parameters);
            return ok ? signature->result : Type::error();
        }

        switch (id)
        {
        case BuiltinId::Print:
        case BuiltinId::Println:
            for (ExprPtr& argument : call.arguments)
            {
                check_expr(*argument);
            }
            return Type::void_type();
        case BuiltinId::Len:
        {
            const Type argument = check_expr(*call.arguments[0]);
            if (!argument.is_error() && !argument.is_string() && !argument.is_array() && !argument.is_tuple()
                && !argument.is_map())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
                return Type::error();
            }
            return Type::int_type();
        }
        case BuiltinId::Has:
        case BuiltinId::Remove:
        case BuiltinId::Keys:
        case BuiltinId::Values:
        case BuiltinId::Items:
        case BuiltinId::Get:
        {
            const Type map = check_expr(*call.arguments[0]);
            if (map.is_error())
            {
                check_remaining(call, 1);
                return Type::error();
            }
            if (!map.is_map())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(map));
                check_remaining(call, 1);
                return Type::error();
            }
            if (id == BuiltinId::Keys)
            {
                return Type::array_of(map.key_type());
            }
            if (id == BuiltinId::Values)
            {
                return Type::array_of(map.value_type());
            }
            if (id == BuiltinId::Items)
            {
                return Type::array_of(Type::tuple_of({map.key_type(), map.value_type()}));
            }
            const Type key = check_expr(*call.arguments[1], map.key_type());
            if (!key.is_error() && key != map.key_type())
            {
                diagnostics_.report(DiagId::MapKeyMismatch, call.arguments[1]->span, type_name(map),
                    type_name(map.key_type()), type_name(key));
                check_remaining(call, 2);
                return Type::error();
            }
            if (id == BuiltinId::Get)
            {
                return check_arguments(call, {map, map.key_type(), map.value_type()}, 2) ? map.value_type()
                                                                                         : Type::error();
            }
            return id == BuiltinId::Has ? Type::bool_type() : Type::void_type();
        }
        case BuiltinId::ToInt:
        case BuiltinId::ToFloat:
        {
            // Both read a number from text, and both also accept a number (to_int(2.9) is 2).
            const Type argument = check_expr(*call.arguments[0]);
            if (!argument.is_error() && !argument.is_string() && !argument.is_number())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
                return Type::error();
            }
            return id == BuiltinId::ToFloat ? Type::float_type() : Type::int_type();
        }
        case BuiltinId::Abs:
        {
            const Type argument = check_expr(*call.arguments[0], expected);
            if (argument.is_error())
            {
                return Type::error();
            }
            if (!argument.is_number())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
                return Type::error();
            }
            return argument; // abs of an int is an int, abs of a decimal is a decimal
        }
        case BuiltinId::Min:
        case BuiltinId::Max:
        {
            Type first = check_expr(*call.arguments[0], expected);
            Type second = check_expr(*call.arguments[1], first.is_error() ? expected : std::optional(first));
            if (first.is_error() || second.is_error())
            {
                return Type::error();
            }
            if (first.is_int() && second.is_float())
            {
                first = coerce(*call.arguments[0], Type::float_type(), first);
            }
            else if (first.is_float() && second.is_int())
            {
                second = coerce(*call.arguments[1], Type::float_type(), second);
            }
            if ((first.is_number() || first.is_string()) && first == second)
            {
                return first;
            }
            diagnostics_.report(DiagId::ArgumentType, call.arguments[1]->span, 2, name, type_name(first),
                type_name(second));
            return Type::error();
        }
        case BuiltinId::Sum:
        {
            const auto element = array_argument(call, 0, expected && expected->is_number()
                    ? std::optional(Type::array_of(*expected))
                    : std::nullopt);
            if (!element)
            {
                return Type::error();
            }
            if (!element->is_number())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name,
                    type_name(Type::array_of(*element)));
                return Type::error();
            }
            return *element;
        }
        case BuiltinId::Str:
        case BuiltinId::Inspect:
            check_expr(*call.arguments[0]);
            return Type::string_type();
        case BuiltinId::Dump:
            check_expr(*call.arguments[0]);
            return Type::void_type();
        case BuiltinId::Copy:
            return check_expr(*call.arguments[0], expected);
        case BuiltinId::Array:
        {
            const Type size = check_expr(*call.arguments[0]);
            if (!size.is_error() && !size.is_int())
            {
                diagnostics_.report(DiagId::ArgumentType, call.arguments[0]->span, 1, name, type_name(Type::int_type()),
                    type_name(size));
            }
            const std::optional<Type> hint =
                expected && expected->is_array() ? std::optional(expected->element()) : std::nullopt;
            const Type element = check_expr(*call.arguments[1], hint);
            if (element.is_error() || !size.is_int())
            {
                return Type::error();
            }
            return Type::array_of(element);
        }
        default:
            return check_generic_builtin(call, id, expected);
        }
    }

    void SemanticAnalyzer::check_remaining(CallExpr& call, std::size_t from)
    {
        for (std::size_t i = from; i < call.arguments.size(); ++i)
        {
            check_expr(*call.arguments[i]);
        }
    }

    std::optional<Type> SemanticAnalyzer::array_argument(CallExpr& call, std::size_t index, std::optional<Type> expected)
    {
        const Type argument = check_expr(*call.arguments[index], expected);
        if (argument.is_error())
        {
            return std::nullopt;
        }
        if (!argument.is_array())
        {
            diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[index]->span,
                language_.spelling(*call.builtin), type_name(argument));
            return std::nullopt;
        }
        return argument.element();
    }

    Type SemanticAnalyzer::check_function_argument(CallExpr& call, std::size_t index, const std::vector<Type>& parameters,
        std::optional<Type> result)
    {
        const Type argument = check_expr(*call.arguments[index]);
        if (argument.is_error())
        {
            return Type::error();
        }
        bool fits = argument.is_function() && argument.parameter_types().size() == parameters.size();
        for (std::size_t i = 0; fits && i < parameters.size(); ++i)
        {
            fits = assignable(parameters[i], argument.parameter_types()[i]);
        }
        if (fits)
        {
            fits = result ? assignable(*result, argument.result_type()) : !argument.result_type().is_void();
        }
        if (fits)
        {
            return argument.result_type();
        }
        // Describe the expected type; "?" stands for a result of any type.
        std::string wanted = Type::function_of(parameters, Type::void_type()).to_string(&language_);
        if (!result || !result->is_void())
        {
            wanted += ": " + (result ? type_name(*result) : std::string("?"));
        }
        diagnostics_.report(DiagId::FunctionArgumentMismatch, call.arguments[index]->span, index + 1,
            language_.spelling(*call.builtin), wanted, type_name(argument));
        return Type::error();
    }

    Type SemanticAnalyzer::check_generic_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected)
    {
        const std::string name = language_.spelling(id);
        const Type int_type = Type::int_type();
        switch (id)
        {
        case BuiltinId::Contains:
        case BuiltinId::IndexOf:
        case BuiltinId::Reverse:
        case BuiltinId::Slice:
        {
            // A string (searched for a part) or an array (searched for an element).
            const Type target = check_expr(*call.arguments[0], expected);
            const Type result = id == BuiltinId::Contains ? Type::bool_type() : (id == BuiltinId::IndexOf ? int_type : target);
            if (target.is_error())
            {
                check_remaining(call, 1);
                return Type::error();
            }
            const bool searchable = target.is_string() || (target.is_array() && comparable(target.element()));
            const bool sequence = target.is_string() || target.is_array();
            if (!(id == BuiltinId::Contains || id == BuiltinId::IndexOf ? searchable : sequence))
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(target));
                check_remaining(call, 1);
                return Type::error();
            }
            if (id == BuiltinId::Reverse)
            {
                return target;
            }
            if (id == BuiltinId::Slice)
            {
                return check_arguments(call, {target, int_type, int_type}, 1) ? result : Type::error();
            }
            const Type item = target.is_string() ? target : target.element();
            return check_arguments(call, {target, item}, 1) ? result : Type::error();
        }
        case BuiltinId::First:
        case BuiltinId::Second:
        case BuiltinId::Swap:
        {
            const Type tuple = check_expr(*call.arguments[0]);
            if (tuple.is_error())
            {
                return Type::error();
            }
            if (!tuple.is_tuple() || (id == BuiltinId::Swap && tuple.elements().size() != 2))
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(tuple));
                return Type::error();
            }
            if (id == BuiltinId::Swap)
            {
                return Type::tuple_of({tuple.elements()[1], tuple.elements()[0]});
            }
            return tuple.elements()[id == BuiltinId::First ? 0 : 1];
        }
        case BuiltinId::Zip:
        {
            const auto left = array_argument(call, 0);
            const auto right = array_argument(call, 1);
            return left && right ? Type::array_of(Type::tuple_of({*left, *right})) : Type::error();
        }
        case BuiltinId::Enumerate:
        {
            const auto element = array_argument(call, 0);
            return element ? Type::array_of(Type::tuple_of({int_type, *element})) : Type::error();
        }
        case BuiltinId::Push:
        case BuiltinId::Pop:
        case BuiltinId::Insert:
        case BuiltinId::RemoveAt:
        case BuiltinId::Concat:
        case BuiltinId::Sort:
        {
            const auto element = array_argument(call, 0, id == BuiltinId::Concat ? expected : std::nullopt);
            if (!element)
            {
                check_remaining(call, 1);
                return Type::error();
            }
            const Type array = Type::array_of(*element);
            switch (id)
            {
            case BuiltinId::Push:
                return check_arguments(call, {array, *element}, 1) ? Type::void_type() : Type::error();
            case BuiltinId::Pop:
                return *element;
            case BuiltinId::Insert:
                return check_arguments(call, {array, int_type, *element}, 1) ? Type::void_type() : Type::error();
            case BuiltinId::RemoveAt:
                return check_arguments(call, {array, int_type}, 1) ? *element : Type::error();
            case BuiltinId::Concat:
                return check_arguments(call, {array, array}, 1) ? array : Type::error();
            default: // Sort
                if (!element->is_number() && !element->is_string())
                {
                    diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(array));
                    return Type::error();
                }
                return array;
            }
        }
        case BuiltinId::Map:
        case BuiltinId::Filter:
        {
            const auto element = array_argument(call, 0);
            if (!element)
            {
                check_remaining(call, 1);
                return Type::error();
            }
            if (id == BuiltinId::Map)
            {
                const Type result = check_function_argument(call, 1, {*element}, std::nullopt);
                return result.is_error() ? result : Type::array_of(result);
            }
            const Type keep = check_function_argument(call, 1, {*element}, Type::bool_type());
            return keep.is_error() ? keep : Type::array_of(*element);
        }
        case BuiltinId::Reduce:
        {
            const auto element = array_argument(call, 0);
            const Type initial = check_expr(*call.arguments[1], expected);
            if (!element || initial.is_error())
            {
                check_remaining(call, 2);
                return Type::error();
            }
            const Type result = check_function_argument(call, 2, {initial, *element}, initial);
            return result.is_error() ? result : initial;
        }
        default:
            break;
        }
        throw std::logic_error("semantic analysis: unhandled built-in " + std::string(builtin_info(id).canonical_name));
    }

    // ------------------------------------------------------------------ helpers

    bool SemanticAnalyzer::assignable(const Type& target, const Type& value)
    {
        return target.is_error() || value.is_error() || target == value;
    }

    Type SemanticAnalyzer::coerce(Expr& expr, const Type& target, const Type& actual)
    {
        // Widening an int to a float is the only implicit conversion in Thunder: it never loses
        // information, and it lets students write 1 / 2.0 and circle(x, y, 1.5, ...).
        if (target.is_float() && actual.is_int())
        {
            expr.widen_to_float = true;
            expr.type = Type::float_type();
            return expr.type;
        }
        return actual;
    }

    bool always_returns(const Stmt& stmt)
    {
        switch (stmt.kind)
        {
        case StmtKind::Return:
            return true;
        case StmtKind::Block:
            for (const StmtPtr& child : static_cast<const BlockStmt&>(stmt).statements)
            {
                if (always_returns(*child))
                {
                    return true;
                }
            }
            return false;
        case StmtKind::If:
        {
            const auto& branch = static_cast<const IfStmt&>(stmt);
            return branch.else_branch && always_returns(*branch.then_branch) && always_returns(*branch.else_branch);
        }
        default:
            return false;
        }
    }

    bool terminates(const Stmt& stmt)
    {
        switch (stmt.kind)
        {
        case StmtKind::Return:
        case StmtKind::Break:
        case StmtKind::Continue:
            return true;
        case StmtKind::Block:
            for (const StmtPtr& child : static_cast<const BlockStmt&>(stmt).statements)
            {
                if (terminates(*child))
                {
                    return true;
                }
            }
            return false;
        case StmtKind::If:
        {
            const auto& branch = static_cast<const IfStmt&>(stmt);
            return branch.else_branch && terminates(*branch.then_branch) && terminates(*branch.else_branch);
        }
        default:
            return false;
        }
    }
}
