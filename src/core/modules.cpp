// Modules: named bundles of declarations ("module geometry"), and the built-in modules (math,
// string, random, bytes, os, file, graphics).
//
// Everything here happens before a single type is checked. A module block is taken apart where it
// is found (declare_module): each member is renamed to its qualified name, "geometry.area", and
// joins the program's items like any top-level declaration. Then every reference is written out
// in full (qualify_references):
//
//     geometry.area(r)      the call of geometry.area                (outside the module)
//     area(r)               the call of geometry.area                (inside it, unless a local hides it)
//     math.sqrt(x)          the built-in sqrt of the math module
//     geometry.Shape.circle a member of the enum geometry.Shape
//
// After that the rest of the analyser, the code generator and the virtual machine see ordinary
// names that happen to contain a dot - which no identifier can - so modules cost nothing at run
// time, and the debugger, traces and error messages all show the qualified names.
//
// Rules this file enforces:
//   - a module is declared once, at the top level of a file (modules do not nest)
//   - module names are reserved: no variable, parameter, function or type may be called that
//   - a member whose name starts with '_' is private: only its own module may use it
//   - inside a module, a built-in of the default module still means the built-in, so a module's
//     own "push" is written stack.push there too (built-in names are reserved, as always)

#include "thunder/sema.hpp"

#include <algorithm>

namespace thunder
{
    std::string SemanticAnalyzer::module_part(std::string_view qualified)
    {
        // A generic instance carries its type arguments after the name: "stack.Stack<int>".
        const std::string_view base = qualified.substr(0, qualified.find('<'));
        const std::size_t dot = base.find('.');
        return dot == std::string_view::npos ? std::string() : std::string(base.substr(0, dot));
    }

    std::optional<BuiltinModule> SemanticAnalyzer::visible_builtin_module(std::string_view name,
        std::optional<BuiltinModule>* gated) const
    {
        // The module's name in the program's language, or its English name (accepted everywhere).
        const std::string folded = fold_letters(name);
        std::optional<BuiltinModule> module;
        if (const auto own = language_.modules.find(folded); own != language_.modules.end())
        {
            module = own->second;
        }
        else if (const auto english = english_pack().modules.find(folded); english != english_pack().modules.end())
        {
            module = english->second;
        }
        if (!module)
        {
            return std::nullopt;
        }
        if (module_needs_import(*module) && !model_.imported_modules.contains(*module))
        {
            if (gated != nullptr)
            {
                *gated = module;
            }
            return std::nullopt; // "file" is an ordinary name until the program imports file
        }
        return module;
    }

    bool SemanticAnalyzer::is_module_name(std::string_view name) const
    {
        return user_modules_.contains(std::string(name)) || visible_builtin_module(name, nullptr).has_value();
    }

    void SemanticAnalyzer::declare_module(ModuleDeclStmt& module, bool in_imported_file)
    {
        (void)in_imported_file; // a module block means the same in a program and in an imported file
        if (user_modules_.contains(module.name) || find_builtin_module(module.name))
        {
            diagnostics_.report(DiagId::ModuleRedefined, module.name_span, module.name);
            return;
        }
        user_modules_.emplace(module.name, &module);
        const std::string prefix = module.name + ".";
        for (StmtPtr& member : module.members)
        {
            switch (member->kind)
            {
            case StmtKind::FuncDecl:
            {
                auto& func = static_cast<FuncDeclStmt&>(*member);
                func.name = prefix + func.name;
                module_members_.insert(func.name);
                break;
            }
            case StmtKind::StructDecl:
            {
                auto& decl = static_cast<StructDeclStmt&>(*member);
                decl.name = prefix + decl.name;
                module_members_.insert(decl.name);
                break;
            }
            case StmtKind::EnumDecl:
            {
                auto& decl = static_cast<EnumDeclStmt&>(*member);
                decl.name = prefix + decl.name;
                module_members_.insert(decl.name);
                break;
            }
            case StmtKind::Interface:
            {
                auto& decl = static_cast<InterfaceDeclStmt&>(*member);
                decl.name = prefix + decl.name;
                module_members_.insert(decl.name);
                break;
            }
            case StmtKind::VarDecl:
            {
                auto& decl = static_cast<VarDeclStmt&>(*member);
                if (decl.names.empty())
                {
                    decl.name = prefix + decl.name;
                    module_members_.insert(decl.name);
                }
                for (std::string& name : decl.names)
                {
                    name = prefix + name;
                    module_members_.insert(name);
                }
                break;
            }
            case StmtKind::Pass:
                continue;
            case StmtKind::Module:
                continue; // a nested module: already reported by the parser
            default:
                // A module only declares things: a statement would run at some arbitrary moment.
                diagnostics_.report(DiagId::ModuleStatement, member->span);
                continue;
            }
            module_of_.emplace(member.get(), module.name);
            items_.push_back(member.get());
        }
    }

    // ------------------------------------------------------------------ the qualifier

    struct SemanticAnalyzer::Qualifier
    {
        SemanticAnalyzer& sema;
        std::string module;                                    // the module of the item being walked ("" outside any)
        std::vector<std::unordered_set<std::string>> scopes;   // locals and parameters, innermost last
        std::vector<std::string> type_params;                  // of the generic declaration being walked

        bool is_local(const std::string& name) const
        {
            return std::ranges::any_of(scopes, [&](const auto& scope) { return scope.contains(name); });
        }

        // "area" inside module geometry, when geometry declares an area.
        std::optional<std::string> own_member(const std::string& name) const
        {
            if (module.empty() || name.find('.') != std::string::npos)
            {
                return std::nullopt;
            }
            std::string qualified = module + "." + name;
            if (!sema.module_members_.contains(qualified))
            {
                return std::nullopt;
            }
            return qualified;
        }

        void check_not_module(const std::string& name, const Span& span, const char* what) const
        {
            if (sema.is_module_name(name))
            {
                sema.diagnostics_.report(DiagId::NameIsModule, span, name, sema.language_.term(what));
            }
        }

        void declare_local(const std::string& name, const Span& span)
        {
            check_not_module(name, span, "variable");
            scopes.back().insert(name);
        }

        // "m.member": the qualified name if m names a module, else nullopt (a value's field).
        std::optional<std::string> qualify(const std::string& target, const std::string& member,
            const Span& member_span, const Span& whole) const
        {
            if (target.find('.') != std::string::npos || is_local(target))
            {
                return std::nullopt;
            }
            if (sema.user_modules_.contains(target))
            {
                std::string qualified = target + "." + member;
                if (!sema.module_members_.contains(qualified))
                {
                    sema.diagnostics_.report(DiagId::NoSuchModuleMember, member_span, target, member);
                }
                else if (member.starts_with('_') && module != target)
                {
                    sema.diagnostics_.report(DiagId::PrivateMember, member_span, member, target);
                }
                return qualified;
            }
            std::optional<BuiltinModule> gated;
            const std::optional<BuiltinModule> builtin = sema.visible_builtin_module(target, &gated);
            if (!builtin && !gated)
            {
                return std::nullopt;
            }
            const BuiltinModule which = builtin ? *builtin : *gated;
            const std::string spelled = sema.language_.spelling(which);
            if (gated)
            {
                // os.run without "import os": say so once, then carry on as if it were imported.
                sema.diagnostics_.report(DiagId::BuiltinNeedsImport, whole, target + "." + member, spelled);
            }
            else if (!sema.language_.module_builtin(which, member) && !builtin_enum_of(which, member))
            {
                sema.diagnostics_.report(DiagId::NoSuchModuleMember, member_span, target, member);
            }
            return spelled + "." + member;
        }

        bool builtin_enum_of(BuiltinModule which, const std::string& member) const
        {
            const auto id = sema.language_.builtin_enum(member);
            return id && builtin_enum_info(*id).module == which;
        }

        void type(TypeSyntax& syntax)
        {
            if (!syntax.struct_name.empty())
            {
                const std::size_t dot = syntax.struct_name.find('.');
                if (dot != std::string::npos)
                {
                    const std::string target = syntax.struct_name.substr(0, dot);
                    const std::string member = syntax.struct_name.substr(dot + 1);
                    if (const auto qualified = qualify(target, member, syntax.span, syntax.span))
                    {
                        syntax.struct_name = *qualified;
                    }
                }
                else if (std::ranges::find(type_params, syntax.struct_name) == type_params.end())
                {
                    if (const auto qualified = own_member(syntax.struct_name))
                    {
                        syntax.struct_name = *qualified;
                    }
                }
            }
            for (TypeSyntax& part : syntax.parts)
            {
                type(part);
            }
        }

        void expr(ExprPtr& slot)
        {
            if (!slot)
            {
                return;
            }
            Expr& e = *slot;
            switch (e.kind)
            {
            case ExprKind::Name:
            {
                auto& name = static_cast<NameExpr&>(e);
                if (!is_local(name.name))
                {
                    if (const auto qualified = own_member(name.name))
                    {
                        name.name = *qualified;
                    }
                }
                break;
            }
            case ExprKind::Field:
            {
                auto& field = static_cast<FieldExpr&>(e);
                // m.x where m is a module: the module comes first, even inside a module that has a
                // member called m (Pashto has no capitals, so ډېرۍ.ډېرۍ is the stack module's struct).
                if (field.target->kind != ExprKind::Name
                    || !sema.is_module_name(static_cast<const NameExpr&>(*field.target).name))
                {
                    expr(field.target);
                }
                if (field.target->kind == ExprKind::Name)
                {
                    const auto& target = static_cast<const NameExpr&>(*field.target);
                    if (const auto qualified = qualify(target.name, field.field, field.field_span, field.span))
                    {
                        slot = std::make_unique<NameExpr>(field.span, *qualified); // geometry.area is one name
                    }
                }
                break;
            }
            case ExprKind::Call:
            {
                auto& call = static_cast<CallExpr&>(e);
                if (call.callee_expr)
                {
                    expr(call.callee_expr);
                    if (call.callee_expr->kind == ExprKind::Name)
                    {
                        // geometry.area(r): the callee is a name after all.
                        call.callee = static_cast<const NameExpr&>(*call.callee_expr).name;
                        call.callee_span = call.callee_expr->span;
                        call.callee_expr.reset();
                    }
                }
                else if (!is_local(call.callee) && !sema.language_.builtin(call.callee))
                {
                    if (const auto qualified = own_member(call.callee))
                    {
                        call.callee = *qualified;
                    }
                }
                for (ExprPtr& argument : call.arguments)
                {
                    expr(argument);
                }
                break;
            }
            case ExprKind::Lambda:
            {
                auto& lambda = static_cast<LambdaExpr&>(e);
                for (Param& param : lambda.params)
                {
                    type(param.type);
                }
                if (lambda.return_type)
                {
                    type(*lambda.return_type);
                }
                scopes.emplace_back();
                for (const Param& param : lambda.params)
                {
                    declare_local(param.name, param.name_span);
                }
                stmt(*lambda.body);
                scopes.pop_back();
                break;
            }
            case ExprKind::Unary:
                expr(static_cast<UnaryExpr&>(e).operand);
                break;
            case ExprKind::Binary:
                expr(static_cast<BinaryExpr&>(e).left);
                expr(static_cast<BinaryExpr&>(e).right);
                break;
            case ExprKind::Index:
                expr(static_cast<IndexExpr&>(e).target);
                expr(static_cast<IndexExpr&>(e).index);
                break;
            case ExprKind::TupleIndex:
                expr(static_cast<TupleIndexExpr&>(e).target);
                break;
            case ExprKind::ArrayLiteral:
                for (ExprPtr& element : static_cast<ArrayLiteralExpr&>(e).elements)
                {
                    expr(element);
                }
                break;
            case ExprKind::TupleLiteral:
                for (ExprPtr& element : static_cast<TupleLiteralExpr&>(e).elements)
                {
                    expr(element);
                }
                break;
            case ExprKind::MapLiteral:
                for (ExprPtr& key : static_cast<MapLiteralExpr&>(e).keys)
                {
                    expr(key);
                }
                for (ExprPtr& value : static_cast<MapLiteralExpr&>(e).values)
                {
                    expr(value);
                }
                break;
            case ExprKind::IntLiteral:
            case ExprKind::FloatLiteral:
            case ExprKind::BoolLiteral:
            case ExprKind::NothingLiteral:
            case ExprKind::StringLiteral:
                break;
            }
        }

        void scoped(Stmt& body)
        {
            scopes.emplace_back();
            stmt(body);
            scopes.pop_back();
        }

        void stmt(Stmt& s)
        {
            switch (s.kind)
            {
            case StmtKind::VarDecl:
            {
                auto& decl = static_cast<VarDeclStmt&>(s);
                if (decl.declared_type)
                {
                    type(*decl.declared_type);
                }
                expr(decl.initializer);
                const bool global = scopes.empty();
                if (decl.names.empty())
                {
                    global ? (module.empty() ? check_not_module(decl.name, decl.name_span, "variable") : void())
                           : declare_local(decl.name, decl.name_span);
                }
                for (std::size_t i = 0; i < decl.names.size(); ++i)
                {
                    global ? (module.empty() ? check_not_module(decl.names[i], decl.name_spans[i], "variable") : void())
                           : declare_local(decl.names[i], decl.name_spans[i]);
                }
                break;
            }
            case StmtKind::Assign:
                expr(static_cast<AssignStmt&>(s).target);
                expr(static_cast<AssignStmt&>(s).value);
                break;
            case StmtKind::ExprStmt:
                expr(static_cast<ExprStmt&>(s).expr);
                break;
            case StmtKind::Block:
                scopes.emplace_back();
                for (StmtPtr& child : static_cast<BlockStmt&>(s).statements)
                {
                    stmt(*child);
                }
                scopes.pop_back();
                break;
            case StmtKind::If:
            {
                auto& branch = static_cast<IfStmt&>(s);
                expr(branch.condition);
                stmt(*branch.then_branch);
                if (branch.else_branch)
                {
                    stmt(*branch.else_branch);
                }
                break;
            }
            case StmtKind::While:
                expr(static_cast<WhileStmt&>(s).condition);
                stmt(*static_cast<WhileStmt&>(s).body);
                break;
            case StmtKind::For:
            {
                auto& loop = static_cast<ForStmt&>(s);
                scopes.emplace_back();
                if (loop.init)
                {
                    stmt(*loop.init);
                }
                expr(loop.condition);
                if (loop.step)
                {
                    stmt(*loop.step);
                }
                stmt(*loop.body);
                scopes.pop_back();
                break;
            }
            case StmtKind::ForEach:
            {
                auto& loop = static_cast<ForEachStmt&>(s);
                expr(loop.collection);
                scopes.emplace_back();
                for (std::size_t i = 0; i < loop.names.size(); ++i)
                {
                    declare_local(loop.names[i], loop.name_spans[i]);
                }
                stmt(*loop.body);
                scopes.pop_back();
                break;
            }
            case StmtKind::With:
            {
                auto& with = static_cast<WithStmt&>(s);
                if (with.declared_type)
                {
                    type(*with.declared_type);
                }
                expr(with.resource);
                scopes.emplace_back();
                if (!with.name.empty())
                {
                    declare_local(with.name, with.name_span);
                }
                stmt(*with.body);
                scopes.pop_back();
                break;
            }
            case StmtKind::Return:
                expr(static_cast<ReturnStmt&>(s).value);
                break;
            case StmtKind::FuncDecl:
            {
                auto& func = static_cast<FuncDeclStmt&>(s);
                if (module.empty())
                {
                    check_not_module(func.name, func.name_span, "function");
                }
                type_params = func.type_params;
                for (Param& param : func.params)
                {
                    type(param.type);
                }
                if (func.return_type)
                {
                    type(*func.return_type);
                }
                scopes.emplace_back();
                for (const Param& param : func.params)
                {
                    declare_local(param.name, param.name_span);
                }
                if (func.body)
                {
                    stmt(*func.body);
                }
                scopes.pop_back();
                type_params.clear();
                break;
            }
            case StmtKind::StructDecl:
            {
                auto& decl = static_cast<StructDeclStmt&>(s);
                if (module.empty())
                {
                    check_not_module(decl.name, decl.name_span, "type");
                }
                type_params = decl.type_params;
                // "struct Button: ui.Widget": a base may be a module's interface or mixin, and
                // inside a module an unqualified own member is one of its own.
                for (std::size_t i = 0; i < decl.bases.size(); ++i)
                {
                    const std::size_t dot = decl.bases[i].find('.');
                    if (dot != std::string::npos)
                    {
                        if (const auto qualified = qualify(decl.bases[i].substr(0, dot), decl.bases[i].substr(dot + 1),
                                decl.base_spans[i], decl.base_spans[i]))
                        {
                            decl.bases[i] = *qualified;
                        }
                    }
                    else if (const auto qualified = own_member(decl.bases[i]))
                    {
                        decl.bases[i] = *qualified;
                    }
                }
                for (FieldDecl& field : decl.fields)
                {
                    type(field.type);
                }
                for (auto& method : decl.methods)
                {
                    stmt(*method);
                    type_params = decl.type_params; // stmt() cleared them
                }
                type_params.clear();
                break;
            }
            case StmtKind::Interface:
            {
                auto& decl = static_cast<InterfaceDeclStmt&>(s);
                if (module.empty())
                {
                    check_not_module(decl.name, decl.name_span, "type");
                }
                for (auto& method : decl.methods)
                {
                    for (Param& param : method->params)
                    {
                        type(param.type);
                    }
                    if (method->return_type)
                    {
                        type(*method->return_type);
                    }
                }
                for (FieldDecl& field : decl.fields)
                {
                    type(field.type);
                }
                break;
            }
            case StmtKind::EnumDecl:
            {
                auto& decl = static_cast<EnumDeclStmt&>(s);
                if (module.empty())
                {
                    check_not_module(decl.name, decl.name_span, "type");
                }
                break;
            }
            case StmtKind::Break:
            case StmtKind::Continue:
            case StmtKind::Pass:
            case StmtKind::Import:
            case StmtKind::Module:
                break;
            default:
                break;
            }
        }
    };

    void SemanticAnalyzer::qualify_references()
    {
        Qualifier qualifier{*this, {}, {}, {}};
        for (Stmt* item : items_)
        {
            const auto owner = module_of_.find(item);
            qualifier.module = owner == module_of_.end() ? std::string() : owner->second;
            qualifier.stmt(*item);
        }
    }

    bool SemanticAnalyzer::report_member_needs_module(const std::string& name, const Span& span)
    {
        if (name.find('.') != std::string::npos)
        {
            return false;
        }
        // sqrt -> math.sqrt: a built-in that lives in a module.
        for (const BuiltinModule module : all_builtin_modules())
        {
            if (language_.module_builtin(module, name))
            {
                diagnostics_.report(DiagId::MemberNeedsModule, span, name, language_.spelling(module));
                return true;
            }
        }
        // area -> geometry.area: a member of one of the program's own modules (the first in
        // alphabetical order, should several modules have one).
        std::string best;
        for (const std::string& member : module_members_)
        {
            const std::size_t dot = member.find('.');
            if (member.substr(dot + 1) == name && (best.empty() || member < best))
            {
                best = member;
            }
        }
        if (best.empty())
        {
            return false;
        }
        diagnostics_.report(DiagId::MemberNeedsModule, span, name, best.substr(0, best.find('.')));
        return true;
    }
}
