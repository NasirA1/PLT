#include "thunder/sema.hpp"

#include <algorithm>
#include <format>
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

        // Pass 1: collect every struct and function signature, so they can be used before (above)
        // the place where they are declared.
        collect_structs(program);
        collect_functions(program);

        // Pass 2: check the top-level statements in order; top-level variables become globals.
        model_.global_scope.kind = "global";
        model_.global_scope.span = program.span;
        scopes_.push_back(Scope{{}, &model_.global_scope});
        current_function_ = 0;
        for (StmtPtr& item : program.items)
        {
            if (item->kind != StmtKind::FuncDecl && item->kind != StmtKind::StructDecl)
            {
                check_statement(*item);
            }
        }

        // Pass 3: check function bodies. Every global is known by now, so functions may use any.
        for (StmtPtr& item : program.items)
        {
            if (item->kind == StmtKind::FuncDecl)
            {
                auto& func = static_cast<FuncDeclStmt&>(*item);
                if (func.function_index >= 0)
                {
                    check_function(func);
                }
            }
        }
        scopes_.clear();
        return std::move(model_);
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

    void SemanticAnalyzer::collect_structs(ProgramNode& program)
    {
        // First register every struct name, so fields may refer to structs declared later.
        std::vector<StructDeclStmt*> declarations;
        for (StmtPtr& item : program.items)
        {
            if (item->kind != StmtKind::StructDecl)
            {
                continue;
            }
            auto& decl = static_cast<StructDeclStmt&>(*item);
            if (language_.builtin(decl.name) || struct_indexes_.contains(decl.name))
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
                if (field.type.base == Type::Base::Struct && !self(self, static_cast<std::size_t>(field.type.struct_index)))
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

    void SemanticAnalyzer::collect_functions(ProgramNode& program)
    {
        for (StmtPtr& item : program.items)
        {
            if (item->kind != StmtKind::FuncDecl)
            {
                continue;
            }
            auto& func = static_cast<FuncDeclStmt&>(*item);
            if (language_.builtin(func.name))
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
            if (function_indexes_.contains(func.name))
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

            if (func.name == "main")
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
        case StmtKind::Break:
        case StmtKind::Continue:
            if (loop_depth_ == 0)
            {
                diagnostics_.report(DiagId::OutsideLoop, stmt.span,
                    language_.spelling(stmt.kind == StmtKind::Break ? TokenKind::KwBreak : TokenKind::KwContinue));
            }
            break;
        case StmtKind::Return:
            check_return(static_cast<ReturnStmt&>(stmt));
            break;
        case StmtKind::FuncDecl:
        case StmtKind::StructDecl:
            // Nested functions and structs are rejected by the parser; top-level ones are collected first.
            break;
        }
    }

    void SemanticAnalyzer::check_var_decl(VarDeclStmt& decl)
    {
        std::optional<Type> declared;
        if (decl.declared_type)
        {
            resolve_type(*decl.declared_type);
            declared = decl.declared_type->type;
        }

        Type type = declared.value_or(Type::error());
        if (decl.initializer)
        {
            const Type value = check_expr(*decl.initializer, declared);
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

        const bool top_level = current_function_ == 0 && scopes_.size() == 1;
        decl.symbol = declare(decl.name, top_level ? SymbolKind::Global : SymbolKind::Local, type, decl.is_const,
            decl.name_span);
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

        const Type value = check_expr(*assign.value, target_type.is_error() ? std::nullopt : std::optional(target_type));
        if (!assignable(target_type, value))
        {
            std::string target_text = language_.term("the target");
            if (assign.target->kind == ExprKind::Name)
            {
                target_text = static_cast<NameExpr&>(*assign.target).name;
            }
            else if (assign.target->kind == ExprKind::Index)
            {
                target_text = language_.term("the array element");
            }
            else if (assign.target->kind == ExprKind::Field)
            {
                target_text = static_cast<FieldExpr&>(*assign.target).field;
            }
            diagnostics_.report(DiagId::AssignTypeMismatch, assign.value->span, type_name(value), target_text,
                type_name(target_type));
        }
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
        const Type value = check_expr(*ret.value, function.return_type);
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

            const Type first = check_expr(*array.elements.front(), element_hint);
            bool ok = !first.is_error();
            for (std::size_t i = 1; i < array.elements.size(); ++i)
            {
                const Type element = check_expr(*array.elements[i], first.is_error() ? element_hint : first);
                if (!first.is_error() && !element.is_error() && element != first)
                {
                    diagnostics_.report(DiagId::ArrayElementMismatch, array.elements[i]->span, type_name(first),
                        type_name(element));
                    ok = false;
                }
            }
            if (ok)
            {
                type = Type::array_of(first);
            }
            break;
        }
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
            if (unary.op == TokenKind::Minus && operand.is_int())
            {
                type = Type::int_type();
            }
            else if (unary.op == TokenKind::Bang && operand.is_bool())
            {
                type = Type::bool_type();
            }
            else
            {
                diagnostics_.report(DiagId::UnaryOperandMismatch, unary.span, operator_text(unary.op),
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
        }

        expr.type = type;
        return type;
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
            else if (function_indexes_.contains(name.name) || language_.builtin(name.name))
            {
                diagnostics_.report(DiagId::FunctionUsedAsValue, name.span, name.name, name.name);
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

        const bool both_int = left.is_int() && right.is_int();
        const bool both_string = left.is_string() && right.is_string();
        const bool both_bool = left.is_bool() && right.is_bool();

        switch (binary.op)
        {
        case TokenKind::Plus:
            if (both_int || both_string)
            {
                return left;
            }
            break;
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            if (both_int)
            {
                return left;
            }
            break;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            if (both_int || both_string)
            {
                return Type::bool_type();
            }
            break;
        case TokenKind::Equal:
        case TokenKind::NotEqual:
            if (both_int || both_string || both_bool)
            {
                return Type::bool_type();
            }
            break;
        case TokenKind::AndAnd:
        case TokenKind::OrOr:
            if (both_bool)
            {
                return Type::bool_type();
            }
            break;
        default:
            break;
        }

        diagnostics_.report(DiagId::BinaryOperandMismatch, binary.op_span, operator_text(binary.op), type_name(left),
            type_name(right));
        return Type::error();
    }

    Type SemanticAnalyzer::check_call(CallExpr& call, std::optional<Type> expected, bool value_required)
    {
        Type result = Type::error();
        std::string callee_name = call.callee;

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

        if (const auto builtin = language_.builtin(call.callee))
        {
            call.builtin = builtin;
            result = check_builtin(call, *builtin, expected);
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
                const Type argument = check_expr(*call.arguments[i], hint);
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
            if (lookup(call.callee) != nullptr)
            {
                diagnostics_.report(DiagId::NotAFunction, call.callee_span, call.callee);
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
            const Type argument = check_expr(*call.arguments[i], hint);
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

    Type SemanticAnalyzer::check_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected)
    {
        const BuiltinInfo& builtin = builtin_info(id);
        const std::string name = language_.spelling(id);
        const std::size_t count = call.arguments.size();
        if (builtin.arity != variadic && count != static_cast<std::size_t>(builtin.arity))
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, name, builtin.arity, count);
            for (ExprPtr& argument : call.arguments)
            {
                check_expr(*argument);
            }
            return Type::error();
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
            if (!argument.is_error() && !argument.is_string() && !argument.is_array())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
                return Type::error();
            }
            return Type::int_type();
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
        case BuiltinId::ToInt:
        {
            const Type argument = check_expr(*call.arguments[0]);
            if (!argument.is_error() && !argument.is_string())
            {
                diagnostics_.report(DiagId::ArgumentType, call.arguments[0]->span, 1, name, type_name(Type::string_type()),
                    type_name(argument));
                return Type::error();
            }
            return Type::int_type();
        }
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
        }
        return Type::error();
    }

    // ------------------------------------------------------------------ helpers

    bool SemanticAnalyzer::assignable(const Type& target, const Type& value)
    {
        return target.is_error() || value.is_error() || target == value;
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
