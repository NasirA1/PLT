#include "thunder/sema.hpp"
#include "thunder/testing.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/lexer.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <unordered_map>
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
        case SymbolKind::Capture:
            return "capture";
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
        // Pass 0b: write every reference to a module member out in full (see modules.cpp).
        qualify_references();

        // Pass 1: collect every interface, struct and function signature, so they can be used
        // before (above) the place where they are declared.
        collect_enums();
        collect_interfaces();
        collect_structs();
        collect_functions();

        // Pass 2: check the top-level statements in order; top-level variables become globals.
        model_.global_scope.kind = "global";
        model_.global_scope.span = program.span;
        scopes_.push_back(Scope{{}, &model_.global_scope});
        current_function_ = 0;
        function_stack_.push_back(FunctionContext{0, nullptr, {}});
        for (Stmt* item : items_)
        {
            if (item->kind != StmtKind::FuncDecl && item->kind != StmtKind::StructDecl && item->kind != StmtKind::EnumDecl
                && item->kind != StmtKind::Interface)
            {
                check_statement(*item);
            }
        }
        function_stack_.pop_back();

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
        // Pass 3b: the bodies of generic instances. Checking one may instantiate more, so the
        // list is drained, not iterated.
        for (std::size_t done = 0; done < pending_functions_.size(); ++done)
        {
            const PendingFunction pending = pending_functions_[done]; // by value: the vector grows
            const TypeBindings saved = std::exchange(bindings_, pending.bindings);
            check_function(*pending.decl);
            bindings_ = saved;
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
            if (item->kind == StmtKind::Module)
            {
                declare_module(static_cast<ModuleDeclStmt&>(*item), is_module);
                continue;
            }
            if (is_module)
            {
                // A module only declares things; importing it must not run any statements.
                const bool declaration = item->kind == StmtKind::FuncDecl || item->kind == StmtKind::StructDecl
                    || item->kind == StmtKind::EnumDecl || item->kind == StmtKind::VarDecl || item->kind == StmtKind::Pass
                    || item->kind == StmtKind::Interface;
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

    // True for any module name, visible or not: "os.run" without "import os" was reported where
    // the name was qualified (S049), so "not declared" would only repeat it.
    bool SemanticAnalyzer::names_a_module(std::string_view name) const
    {
        return !name.empty() && (user_modules_.contains(std::string(name)) || find_builtin_module(name).has_value());
    }

    bool SemanticAnalyzer::already_reported(const std::string& qualified) const
    {
        // A member that does not exist (S070) or of a module not imported (S049) was reported
        // where the name was qualified. A member that exists but is used before its module's
        // variables are declared was not: that is an ordinary "not declared".
        return !module_members_.contains(qualified) && names_a_module(module_part(qualified));
    }

    std::optional<BuiltinId> SemanticAnalyzer::builtin_named(std::string_view name) const
    {
        // "len" (the default module) or a qualified "math.sqrt". Whether a module that needs an
        // import was imported has already been checked where the name was qualified.
        return language_.builtin(name);
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

    int EnumInfo::member_index(std::string_view member) const
    {
        // Folded on both sides: the member list keeps the authored spelling for display, while
        // identifiers arrive from the lexer already folded (ك/گ/ي forms unified).
        const std::string wanted = fold_letters(member);
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            if (fold_letters(members[i]) == wanted)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    const EnumInfo* SemanticAnalyzer::find_enum(std::string_view name) const
    {
        const auto it = enum_indexes_.find(fold_letters(name));
        return it == enum_indexes_.end() ? nullptr : model_.enums[static_cast<std::size_t>(it->second)].get();
    }

    const StructInfo* SemanticAnalyzer::find_struct(std::string_view name) const
    {
        const auto it = struct_indexes_.find(std::string(name));
        return it == struct_indexes_.end() ? nullptr : model_.structs[static_cast<std::size_t>(it->second)].get();
    }

    bool SemanticAnalyzer::resolve_type(TypeSyntax& syntax)
    {
        if (!syntax.struct_name.empty())
        {
            const std::uint8_t dimensions = syntax.type.dimensions;
            // An active type parameter: "T" stands for the concrete type it was bound to.
            if (const auto bound = bindings_.find(syntax.struct_name); bound != bindings_.end())
            {
                if (!syntax.parts.empty())
                {
                    diagnostics_.report(DiagId::TypeArgumentCount, syntax.span, syntax.struct_name, 0,
                        syntax.parts.size());
                    syntax.type = Type::error();
                    return false;
                }
                Type bound_type = bound->second;
                bound_type.dimensions = static_cast<std::uint8_t>(bound_type.dimensions + dimensions);
                syntax.type = bound_type;
                return true;
            }
            // "Stack<int>": an instance of a generic struct.
            if (const auto generic = generic_structs_.find(syntax.struct_name); generic != generic_structs_.end())
            {
                StructDeclStmt& tmpl = *generic->second;
                if (syntax.parts.size() != tmpl.type_params.size())
                {
                    diagnostics_.report(DiagId::TypeArgumentCount, syntax.span, tmpl.name,
                        tmpl.type_params.size(), syntax.parts.size());
                    syntax.type = Type::error();
                    return false;
                }
                bool ok = true;
                std::vector<Type> args;
                for (TypeSyntax& part : syntax.parts)
                {
                    ok = resolve_type(part) && ok;
                    args.push_back(part.type);
                }
                if (!ok)
                {
                    syntax.type = Type::error();
                    return false;
                }
                const int index = instantiate_struct(tmpl, std::move(args), syntax.span);
                if (index < 0)
                {
                    syntax.type = Type::error();
                    return false;
                }
                syntax.type = Type::struct_type(index, &model_.structs[static_cast<std::size_t>(index)]->name);
                syntax.type.dimensions = dimensions;
                return true;
            }
            if (!syntax.parts.empty())
            {
                diagnostics_.report(DiagId::TypeArgumentCount, syntax.span, syntax.struct_name, 0,
                    syntax.parts.size());
                syntax.type = Type::error();
                return false;
            }
            if (const auto en = enum_indexes_.find(syntax.struct_name); en != enum_indexes_.end())
            {
                syntax.type = Type::enum_type(en->second, &model_.enums[static_cast<std::size_t>(en->second)]->name);
                syntax.type.dimensions = dimensions;
                return true;
            }
            if (const auto interface = interface_indexes_.find(syntax.struct_name); interface != interface_indexes_.end())
            {
                syntax.type = Type::interface_type(interface->second,
                    &model_.interfaces[static_cast<std::size_t>(interface->second)]->name);
                syntax.type.dimensions = dimensions;
                return true;
            }
            const auto it = struct_indexes_.find(syntax.struct_name);
            if (it == struct_indexes_.end())
            {
                diagnostics_.report(mixins_.contains(syntax.struct_name) ? DiagId::MixinIsNotType : DiagId::UnknownType,
                    syntax.span, syntax.struct_name);
                syntax.type = Type::error();
                return false;
            }
            syntax.type = Type::struct_type(it->second, &model_.structs[static_cast<std::size_t>(it->second)]->name);
            syntax.type.dimensions = dimensions;
            return true;
        }
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
        return true; // a primitive type, fully resolved by the parser
    }

    void SemanticAnalyzer::collect_structs()
    {
        // First register every struct name, so fields may refer to structs declared later.
        std::vector<StructDeclStmt*> declarations;
        std::vector<StructDeclStmt*> all;
        for (Stmt* item : items_)
        {
            if (item->kind != StmtKind::StructDecl)
            {
                continue;
            }
            auto& decl = static_cast<StructDeclStmt&>(*item);
            if (builtin_named(decl.name) || struct_indexes_.contains(decl.name)
                || generic_structs_.contains(decl.name) || enum_indexes_.contains(decl.name)
                || interface_indexes_.contains(decl.name) || mixins_.contains(decl.name))
            {
                diagnostics_.report(DiagId::TypeRedefined, decl.name_span, decl.name);
                continue;
            }
            if (decl.is_mixin)
            {
                // A mixin is not a type: it is never built, so it has no constructor and no
                // type parameters; its body is checked where a struct includes it.
                if (!decl.type_params.empty())
                {
                    diagnostics_.report(DiagId::MixinGeneric, decl.name_span);
                }
                for (const auto& method : decl.methods)
                {
                    if (method->is_constructor)
                    {
                        diagnostics_.report(DiagId::MixinConstructor, method->name_span, decl.name);
                    }
                }
                // One level only: a mixin may implement interfaces but not include another mixin.
                for (std::size_t i = 0; i < decl.bases.size(); ++i)
                {
                    if (find_interface(decl.bases[i]) == nullptr)
                    {
                        diagnostics_.report(DiagId::MixinHasBases, decl.base_spans[i], decl.name);
                    }
                }
                mixins_.emplace(decl.name, &decl);
                continue;
            }
            all.push_back(&decl);
            if (!decl.type_params.empty())
            {
                // A template: fields are resolved per instantiation. Duplicate fields are
                // reported here, once, whatever the types turn out to be.
                for (std::size_t i = 0; i < decl.fields.size(); ++i)
                {
                    for (std::size_t j = 0; j < i; ++j)
                    {
                        if (decl.fields[i].name == decl.fields[j].name)
                        {
                            diagnostics_.report(DiagId::DuplicateField, decl.fields[i].name_span, decl.name,
                                decl.fields[i].name);
                        }
                    }
                }
                generic_structs_.emplace(decl.name, &decl);
                continue;
            }
            auto info = std::make_unique<StructInfo>();
            info->name = decl.name;
            info->decl = &decl;
            info->span = decl.span;
            decl.struct_index = static_cast<int>(model_.structs.size());
            info->index = decl.struct_index;
            struct_indexes_.emplace(decl.name, decl.struct_index);
            model_.structs.push_back(std::move(info));
            declarations.push_back(&decl);
        }

        // Every struct, enum and interface name is known by now, so an interface's signatures
        // can be resolved - a method of an interface may take and answer with any type.
        resolve_interface_members();

        // Mixins are copied in and the method bodies rewritten (bare member -> member of the
        // hidden receiver) on the syntax tree, before any type resolves: an instance of a
        // generic struct then clones methods that are already complete.
        for (StructDeclStmt* decl : all)
        {
            apply_mixins(*decl);
        }
        for (StructDeclStmt* decl : all)
        {
            rewrite_methods(*decl);
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

        // Methods, constructors and the interfaces each struct implements.
        for (StructDeclStmt* decl : declarations)
        {
            StructInfo& info = *model_.structs[static_cast<std::size_t>(decl->struct_index)];
            for (std::size_t i = 0; i < decl->bases.size(); ++i)
            {
                if (const InterfaceInfo* interface = find_interface(decl->bases[i]))
                {
                    // Implementing an interface implements everything it extends.
                    if (!info.implements(interface->index))
                    {
                        info.interfaces.push_back(interface->index);
                    }
                    for (const int base : interface->bases)
                    {
                        if (!info.implements(base))
                        {
                            info.interfaces.push_back(base);
                        }
                    }
                }
            }
            collect_struct_methods(*decl, info, nullptr);
            check_interface_conformance(info);
        }
    }

    const InterfaceInfo* SemanticAnalyzer::find_interface(std::string_view name) const
    {
        const auto it = interface_indexes_.find(std::string(name));
        return it == interface_indexes_.end() ? nullptr : model_.interfaces[static_cast<std::size_t>(it->second)].get();
    }

    void SemanticAnalyzer::collect_interfaces()
    {
        // Register every name first, so a struct may list an interface declared below it.
        std::vector<InterfaceDeclStmt*> declarations;
        for (Stmt* item : items_)
        {
            if (item->kind != StmtKind::Interface)
            {
                continue;
            }
            auto& decl = static_cast<InterfaceDeclStmt&>(*item);
            if (builtin_named(decl.name) || interface_indexes_.contains(decl.name) || enum_indexes_.contains(decl.name))
            {
                diagnostics_.report(DiagId::TypeRedefined, decl.name_span, decl.name);
                continue;
            }
            auto info = std::make_unique<InterfaceInfo>();
            info->name = decl.name;
            info->decl = &decl;
            info->span = decl.span;
            decl.interface_index = static_cast<int>(model_.interfaces.size());
            info->index = decl.interface_index;
            interface_indexes_.emplace(decl.name, decl.interface_index);
            model_.interfaces.push_back(std::move(info));
            declarations.push_back(&decl);
        }
        interface_declarations_ = std::move(declarations);
    }

    /**
     * The interfaces an interface extends, and then its members - resolved once the struct and
     * enum names are known, so a signature may name any type the program declares.
     */
    void SemanticAnalyzer::resolve_interface_members()
    {
        // The interfaces each one extends, with their own bases folded in (a flat list).
        for (InterfaceDeclStmt* decl : interface_declarations_)
        {
            InterfaceInfo& info = *model_.interfaces[static_cast<std::size_t>(decl->interface_index)];
            for (std::size_t i = 0; i < decl->bases.size(); ++i)
            {
                const InterfaceInfo* base = find_interface(decl->bases[i]);
                if (base == nullptr)
                {
                    diagnostics_.report(DiagId::NotAnInterface, decl->base_spans[i], decl->bases[i]);
                    continue;
                }
                if (base->index == info.index)
                {
                    diagnostics_.report(DiagId::InterfaceCycle, decl->base_spans[i], info.name);
                    continue;
                }
                if (std::ranges::find(info.bases, base->index) == info.bases.end())
                {
                    info.bases.push_back(base->index);
                }
            }
        }
        // Fold the bases' bases in, repeatedly, until nothing new appears; a cycle is reported
        // once and then broken by leaving the interface out of its own list.
        for (bool changed = true; changed;)
        {
            changed = false;
            for (const auto& owned : model_.interfaces)
            {
                for (std::size_t i = 0; i < owned->bases.size(); ++i)
                {
                    const InterfaceInfo& base = *model_.interfaces[static_cast<std::size_t>(owned->bases[i])];
                    for (const int grand : base.bases)
                    {
                        if (grand != owned->index && std::ranges::find(owned->bases, grand) == owned->bases.end())
                        {
                            owned->bases.push_back(grand);
                            changed = true;
                        }
                    }
                }
            }
        }
        for (const auto& owned : model_.interfaces)
        {
            if (owned->decl != nullptr && std::ranges::any_of(owned->bases, [&](int b)
                    { return std::ranges::find(model_.interfaces[static_cast<std::size_t>(b)]->bases, owned->index)
                          != model_.interfaces[static_cast<std::size_t>(b)]->bases.end(); }))
            {
                diagnostics_.report(DiagId::InterfaceCycle, owned->decl->name_span, owned->name);
                owned->bases.clear();
            }
        }
        for (InterfaceDeclStmt* decl : interface_declarations_)
        {
            InterfaceInfo& info = *model_.interfaces[static_cast<std::size_t>(decl->interface_index)];
            const auto add_member = [&](InterfaceMember member)
            {
                if (info.member(member.name) != nullptr)
                {
                    diagnostics_.report(DiagId::DuplicateMember, member.span, info.name, member.name);
                    return;
                }
                info.members.push_back(std::move(member));
            };
            for (auto& method : decl->methods)
            {
                InterfaceMember member;
                member.name = method->name;
                member.span = method->name_span;
                for (Param& param : method->params)
                {
                    resolve_type(param.type);
                    member.parameter_types.push_back(param.type.type);
                }
                if (method->return_type)
                {
                    resolve_type(*method->return_type);
                    member.return_type = method->return_type->type;
                }
                add_member(std::move(member));
            }
            for (FieldDecl& field : decl->fields)
            {
                resolve_type(field.type);
                InterfaceMember member;
                member.name = field.name;
                member.span = field.name_span;
                member.is_field = true;
                member.parameter_types = field.type.type.parameter_types();
                member.return_type =
                    field.type.type.is_function() ? field.type.type.result_type() : Type::error();
                add_member(std::move(member));
            }
        }
        // What an interface extends is part of it: the members of its bases come after its own
        // (its own win, so an interface may restate - or narrow - a member it inherits).
        for (const auto& owned : model_.interfaces)
        {
            for (const int base_index : owned->bases)
            {
                for (const InterfaceMember& member : model_.interfaces[static_cast<std::size_t>(base_index)]->members)
                {
                    if (owned->member(member.name) == nullptr)
                    {
                        owned->members.push_back(member);
                    }
                }
            }
        }
        // The syntax a struct copies in: closure-valued members and default method bodies.
        for (const auto& owned : model_.interfaces)
        {
            std::vector<const InterfaceInfo*> chain{owned.get()};
            for (const int base_index : owned->bases)
            {
                chain.push_back(model_.interfaces[static_cast<std::size_t>(base_index)].get());
            }
            for (const InterfaceInfo* source : chain)
            {
                if (source->decl == nullptr)
                {
                    continue;
                }
                for (const FieldDecl& field : source->decl->fields)
                {
                    const auto named = [&](const FieldDecl* seen) { return seen->name == field.name; };
                    if (std::ranges::none_of(owned->closure_fields, named))
                    {
                        owned->closure_fields.push_back(&field);
                    }
                }
                for (const auto& method : source->decl->methods)
                {
                    const auto named = [&](const FuncDeclStmt* seen) { return seen->name == method->name; };
                    if (method->body != nullptr && std::ranges::none_of(owned->default_methods, named))
                    {
                        owned->default_methods.push_back(method.get());
                    }
                }
            }
        }
    }

    int StructInfo::method_index(std::string_view method) const
    {
        for (const auto& [member, function] : methods)
        {
            if (member == method)
            {
                return function;
            }
        }
        return -1;
    }

    bool StructInfo::implements(int interface_index) const
    {
        return std::ranges::find(interfaces, interface_index) != interfaces.end();
    }

    const InterfaceMember* InterfaceInfo::member(std::string_view wanted) const
    {
        for (const InterfaceMember& candidate : members)
        {
            if (candidate.name == wanted)
            {
                return &candidate;
            }
        }
        return nullptr;
    }

    void SemanticAnalyzer::apply_mixins(StructDeclStmt& decl)
    {
        if (decl.mixins_applied)
        {
            return;
        }
        decl.mixins_applied = true;
        const bool mock = !decl.is_mixin
            && std::ranges::any_of(decl.annotations, [](const Annotation& a) { return a.name == "mock"; });
        if (mock)
        {
            // "@mock struct FakeClock: Clock" includes the testing module's Mock mixin, which
            // records the calls (it is there whenever the program is a test file).
            const std::string mixin = fold_letters(testing_name(language_, "test")) + "."
                + fold_letters(testing_name(language_, "Mock"));
            if (mixins_.contains(mixin) && std::ranges::find(decl.bases, mixin) == decl.bases.end())
            {
                decl.bases.push_back(mixin);
                decl.base_spans.push_back(decl.name_span);
            }
        }
        // The bases are walked by index: a mixin may append the names of its own interfaces.
        for (std::size_t i = 0; i < decl.bases.size(); ++i)
        {
            const std::string base = decl.bases[i];
            const Span span = decl.base_spans[i];
            if (find_interface(base) != nullptr)
            {
                continue; // recorded per struct (or per instance) once the types exist
            }
            const auto found = mixins_.find(base);
            if (found == mixins_.end())
            {
                // Only a 'mixin' can be included: a struct is a type of its own, not a body to copy.
                if (struct_indexes_.contains(base) || generic_structs_.contains(base))
                {
                    diagnostics_.report(DiagId::StructIsNotMixin, span, base);
                }
                else
                {
                    diagnostics_.report(DiagId::NotAnInterfaceOrMixin, span, base);
                }
                continue;
            }
            const StructDeclStmt* mixin = found->second;
            if (!mixin->type_params.empty())
            {
                continue; // reported at the mixin (S073)
            }
            // One level only: a mixin may implement interfaces but not include another mixin.
            for (const std::string& grand : mixin->bases)
            {
                if (find_interface(grand) == nullptr)
                {
                    continue; // reported at the mixin (S072)
                }
                else if (std::ranges::find(decl.bases, grand) == decl.bases.end())
                {
                    decl.bases.push_back(grand); // the mixin's interfaces come along
                    decl.base_spans.push_back(span);
                }
            }
            for (const FieldDecl& field : mixin->fields)
            {
                const auto same = [&](const FieldDecl& own) { return own.name == field.name; };
                if (std::ranges::any_of(decl.fields, same))
                {
                    diagnostics_.report(DiagId::DuplicateField, span, decl.name, field.name);
                    continue;
                }
                decl.fields.push_back(field);
            }
            for (const auto& method : mixin->methods)
            {
                if (method->is_constructor)
                {
                    continue; // refused at the mixin (S084)
                }
                const auto same = [&](const auto& own) { return own->name == method->name; };
                if (std::ranges::any_of(decl.methods, same))
                {
                    continue; // the struct's own method wins
                }
                StmtPtr cloned = clone_stmt(*method);
                decl.methods.push_back(std::unique_ptr<FuncDeclStmt>(static_cast<FuncDeclStmt*>(cloned.release())));
            }
        }
        if (mock)
        {
            make_mock(decl);
        }
        add_interface_closures(decl);
    }

    void SemanticAnalyzer::make_mock(StructDeclStmt& decl)
    {
        // Every method of every interface the mock stands in for records its call - its name and
        // its arguments as text - and then does what the struct says: the struct's own method
        // when it has one, otherwise nothing, returning the default value of its result type.
        const std::string record = fold_letters(testing_name(language_, "_record"));
        const std::string str = fold_letters(language_.spelling(BuiltinId::Str));
        const std::string result = "(" + language_.term("result") + ")";
        const auto has_record = [&](const auto& method) { return method->name == record; };
        if (!std::ranges::any_of(decl.methods, has_record))
        {
            return; // no testing module: the program was told it is not a test file (S092)
        }
        bool any_interface = false;
        std::unordered_set<std::string> done;
        for (std::size_t i = 0; i < decl.bases.size(); ++i)
        {
            const InterfaceInfo* interface = find_interface(decl.bases[i]);
            if (interface == nullptr || interface->decl == nullptr)
            {
                continue;
            }
            any_interface = true;
            // The interface's own methods and those of the interfaces it extends.
            std::vector<const InterfaceDeclStmt*> sources{interface->decl};
            for (const int base : interface->bases)
            {
                if (const InterfaceDeclStmt* base_decl = model_.interfaces[static_cast<std::size_t>(base)]->decl)
                {
                    sources.push_back(base_decl);
                }
            }
            for (const InterfaceDeclStmt* source : sources)
            {
                for (const auto& signature : source->methods)
                {
                    if (!done.insert(signature->name).second)
                    {
                        continue;
                    }
                    const Span span = decl.name_span;
                    FuncDeclStmt* method = nullptr;
                    for (auto& own : decl.methods)
                    {
                        if (!own->is_constructor && own->name == signature->name && own->body)
                        {
                            method = own.get();
                        }
                    }
                    if (method == nullptr)
                    {
                        StmtPtr cloned = clone_stmt(*signature);
                        auto made = std::unique_ptr<FuncDeclStmt>(static_cast<FuncDeclStmt*>(cloned.release()));
                        made->is_method = true;
                        made->span = span;
                        made->body = std::make_unique<BlockStmt>(span);
                        if (made->return_type)
                        {
                            // var (result): R      return (result)      - the type's default value
                            auto local = std::make_unique<VarDeclStmt>(span, false, result, span);
                            local->declared_type = *made->return_type;
                            made->body->statements.push_back(std::move(local));
                            made->body->statements.push_back(
                                std::make_unique<ReturnStmt>(span, std::make_unique<NameExpr>(span, result)));
                        }
                        method = made.get();
                        decl.methods.push_back(std::move(made));
                    }
                    // _record("name", [str(a), str(b)]) first.
                    auto arguments = std::make_unique<ArrayLiteralExpr>(span);
                    for (const Param& param : method->params)
                    {
                        auto text = std::make_unique<CallExpr>(span, str, span);
                        text->arguments.push_back(std::make_unique<NameExpr>(span, param.name));
                        text->argument_names.emplace_back();
                        arguments->elements.push_back(std::move(text));
                    }
                    auto call = std::make_unique<CallExpr>(span, record, span);
                    call->arguments.push_back(std::make_unique<StringLiteralExpr>(span, signature->name));
                    call->arguments.push_back(std::move(arguments));
                    call->argument_names.assign(2, std::string());
                    auto& statements = method->body->statements;
                    statements.insert(statements.begin(), std::make_unique<ExprStmt>(span, std::move(call)));
                }
            }
        }
        if (!any_interface)
        {
            diagnostics_.report(DiagId::MockWithoutInterface, decl.name_span, decl.name);
        }
    }

    void SemanticAnalyzer::add_interface_closures(StructDeclStmt& decl)
    {
        // "interface Clickable / on_click(): func()" - a closure member an interface declares is
        // part of every struct that implements it: the struct gets the field without writing
        // it again (writing it anyway is allowed; it must then match, S075). The interfaces a
        // mixin brought along are among the bases by now.
        for (std::size_t i = 0; i < decl.bases.size(); ++i)
        {
            const InterfaceInfo* interface = find_interface(decl.bases[i]);
            if (interface == nullptr || interface->decl == nullptr)
            {
                continue;
            }
            for (const FieldDecl* field : interface->closure_fields)
            {
                const auto same_field = [&](const FieldDecl& own) { return own.name == field->name; };
                const auto same_method = [&](const auto& own) { return own->name == field->name; };
                if (std::ranges::any_of(decl.fields, same_field) || std::ranges::any_of(decl.methods, same_method))
                {
                    continue;
                }
                decl.fields.push_back(*field);
            }
            // A method the interface writes a body for is a default implementation: the struct
            // gets a copy of it unless it writes its own (or inherits one from a mixin).
            for (const FuncDeclStmt* method : interface->default_methods)
            {
                const auto same_field = [&](const FieldDecl& own) { return own.name == method->name; };
                const auto same_method = [&](const auto& own) { return own->name == method->name; };
                if (std::ranges::any_of(decl.fields, same_field) || std::ranges::any_of(decl.methods, same_method))
                {
                    continue;
                }
                StmtPtr cloned = clone_stmt(*method);
                decl.methods.push_back(std::unique_ptr<FuncDeclStmt>(static_cast<FuncDeclStmt*>(cloned.release())));
            }
        }
    }

    // ------------------------------------------------------------------ methods
    //
    // There is no 'self' parameter to write: inside a method, a bare member name means the
    // member of the hidden receiver. This rewriter makes that explicit on the syntax tree -
    // "_title" becomes "(self)._title", and "useful()" becomes "(self).useful()" - before
    // anything is checked, with a small scope tracker so parameters and locals still hide
    // members (and built-ins still win for calls, as everywhere).
    struct SemanticAnalyzer::SelfRewriter
    {
        SemanticAnalyzer& sema;
        const StructDeclStmt& decl;
        std::string self_name; // "(self)" in the program's language
        std::vector<std::unordered_set<std::string>> scopes;

        bool is_local(const std::string& name) const
        {
            return std::ranges::any_of(scopes, [&](const auto& scope) { return scope.contains(name); });
        }

        bool is_member(const std::string& name) const
        {
            const auto field = [&](const FieldDecl& candidate) { return candidate.name == name; };
            const auto method = [&](const auto& candidate)
            { return !candidate->is_constructor && candidate->name == name; };
            return std::ranges::any_of(decl.fields, field) || std::ranges::any_of(decl.methods, method);
        }

        ExprPtr self_member(const std::string& name, const Span& span) const
        {
            return std::make_unique<FieldExpr>(span, std::make_unique<NameExpr>(span, self_name), name, span);
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
                if (!is_local(name.name) && is_member(name.name))
                {
                    slot = self_member(name.name, e.span);
                }
                break;
            }
            case ExprKind::Field:
                expr(static_cast<FieldExpr&>(e).target);
                break;
            case ExprKind::Call:
            {
                auto& call = static_cast<CallExpr&>(e);
                if (call.callee_expr)
                {
                    expr(call.callee_expr);
                }
                else if (!is_local(call.callee) && !sema.language_.builtin(call.callee) && is_member(call.callee))
                {
                    // useful() -> (self).useful(): built-in names stay reserved, as in modules.
                    call.callee_expr = self_member(call.callee, call.callee_span);
                }
                for (ExprPtr& argument : call.arguments)
                {
                    expr(argument);
                }
                break;
            }
            case ExprKind::Lambda:
            {
                // A function value inside a method sees the members too (through the captured
                // receiver), unless one of its own parameters hides them.
                auto& lambda = static_cast<LambdaExpr&>(e);
                scopes.emplace_back();
                for (const Param& param : lambda.params)
                {
                    scopes.back().insert(param.name);
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
            default:
                break;
            }
        }

        void stmt(Stmt& s)
        {
            switch (s.kind)
            {
            case StmtKind::VarDecl:
            {
                auto& var = static_cast<VarDeclStmt&>(s);
                expr(var.initializer);
                if (var.names.empty())
                {
                    scopes.back().insert(var.name);
                }
                for (const std::string& name : var.names)
                {
                    scopes.back().insert(name);
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
                for (const std::string& name : loop.names)
                {
                    scopes.back().insert(name);
                }
                stmt(*loop.body);
                scopes.pop_back();
                break;
            }
            case StmtKind::With:
            {
                auto& with = static_cast<WithStmt&>(s);
                expr(with.resource);
                scopes.emplace_back();
                if (!with.name.empty())
                {
                    scopes.back().insert(with.name);
                }
                stmt(*with.body);
                scopes.pop_back();
                break;
            }
            case StmtKind::Return:
                expr(static_cast<ReturnStmt&>(s).value);
                break;
            default:
                break;
            }
        }

        void method(FuncDeclStmt& func)
        {
            scopes.clear();
            scopes.emplace_back();
            for (const Param& param : func.params)
            {
                scopes.back().insert(param.name);
            }
            if (func.body)
            {
                stmt(*func.body);
            }
        }
    };

    void SemanticAnalyzer::rewrite_methods(StructDeclStmt& decl)
    {
        if (decl.methods.empty())
        {
            return;
        }
        SelfRewriter rewriter{*this, decl, "(" + language_.term("self") + ")", {}};
        for (auto& method : decl.methods)
        {
            rewriter.method(*method);
        }
    }

    void SemanticAnalyzer::collect_struct_methods(StructDeclStmt& decl, StructInfo& info,
        const std::unordered_map<std::string, Type>* bindings)
    {
        const std::string self_name = "(" + language_.term("self") + ")";
        for (auto& owned : decl.methods)
        {
            FuncDeclStmt* method = owned.get();
            if (bindings != nullptr)
            {
                // An instance of a generic struct compiles its own copy of every method.
                StmtPtr cloned = clone_stmt(*method);
                method = static_cast<FuncDeclStmt*>(cloned.get());
                model_.instantiated.push_back(std::move(cloned));
            }
            if (method->is_constructor && method->return_type)
            {
                diagnostics_.report(DiagId::BadConstructor, method->name_span, info.name);
            }
            const bool duplicate = info.field_index(method->name) >= 0 || info.method_index(method->name) >= 0
                || (method->is_constructor && info.ctor >= 0);
            if (duplicate)
            {
                diagnostics_.report(DiagId::DuplicateMember, method->name_span, info.name, method->name);
                continue;
            }

            const std::string member_name = method->name;
            method->name = info.name + "." + member_name;
            method->method_struct = info.index;
            Param self;
            self.name = self_name;
            self.name_span = method->name_span;
            self.type.type = Type::struct_type(info.index, &info.name);
            self.type.span = method->name_span;
            method->params.insert(method->params.begin(), std::move(self));

            const TypeBindings saved =
                bindings != nullptr ? std::exchange(bindings_, *bindings) : bindings_;
            for (Param& param : method->params)
            {
                resolve_type(param.type);
            }
            if (method->return_type)
            {
                resolve_type(*method->return_type);
            }
            if (bindings != nullptr)
            {
                bindings_ = saved;
            }

            FunctionInfo function;
            function.name = method->name;
            function.decl = method;
            function.span = method->span;
            function.return_type =
                method->return_type && !method->is_constructor ? method->return_type->type : Type::void_type();
            for (const Param& param : method->params)
            {
                function.parameter_types.push_back(param.type.type);
            }
            method->function_index = static_cast<int>(model_.functions.size());
            model_.functions.push_back(std::move(function));

            if (method->is_constructor)
            {
                info.ctor = method->function_index;
                for (std::size_t i = 1; i < method->params.size(); ++i)
                {
                    info.ctor_params.push_back(method->params[i].type.type);
                }
            }
            else
            {
                info.methods.emplace_back(member_name, method->function_index);
            }

            if (bindings != nullptr)
            {
                pending_functions_.push_back(PendingFunction{method, *bindings});
            }
            else
            {
                items_.push_back(method); // checked in pass 3 and compiled like any function
            }
        }
    }

    void SemanticAnalyzer::check_interface_conformance(StructInfo& info)
    {
        for (const int interface_index : info.interfaces)
        {
            const InterfaceInfo& interface = *model_.interfaces[static_cast<std::size_t>(interface_index)];
            for (const InterfaceMember& member : interface.members)
            {
                const Span span = info.decl != nullptr ? info.decl->name_span : info.span;
                if (member.is_field)
                {
                    const int field = info.field_index(member.name);
                    if (field < 0)
                    {
                        diagnostics_.report(DiagId::InterfaceNotImplemented, span, info.name, interface.name,
                            member.name);
                    }
                    else if (!(info.fields[static_cast<std::size_t>(field)].type == member.function_type()))
                    {
                        diagnostics_.report(DiagId::InterfaceSignatureMismatch, span, member.name, info.name,
                            interface.name, type_name(member.function_type()));
                    }
                    continue;
                }
                const int method = info.method_index(member.name);
                if (method < 0)
                {
                    diagnostics_.report(DiagId::InterfaceNotImplemented, span, info.name, interface.name, member.name);
                    continue;
                }
                const FunctionInfo& function = model_.functions[static_cast<std::size_t>(method)];
                const std::vector<Type> params(function.parameter_types.begin() + 1, function.parameter_types.end());
                if (params != member.parameter_types || !(function.return_type == member.return_type))
                {
                    diagnostics_.report(DiagId::InterfaceSignatureMismatch, span, member.name, info.name,
                        interface.name, type_name(member.function_type()));
                }
            }
        }
    }

    void SemanticAnalyzer::collect_enums()
    {
        // The built-in enums come with their module (Color with graphics, Platform with os), under
        // their names in the program's language; their positions are fixed by the toolchain.
        for (const BuiltinEnumInfo& builtin : all_builtin_enums())
        {
            if (builtin.module != BuiltinModule::None && !model_.imported_modules.contains(builtin.module))
            {
                continue;
            }
            auto info = std::make_unique<EnumInfo>();
            // graphics.Color: a built-in enum is a member of its module, like the functions.
            const std::string module = language_.spelling(builtin.module) + ".";
            info->name = module + language_.spelling(builtin.id);
            info->builtin = static_cast<std::int32_t>(builtin.id);
            for (std::size_t member = 0; member < builtin.members.size(); ++member)
            {
                info->members.push_back(language_.member_spelling(builtin.id, static_cast<std::uint16_t>(member)));
                info->member_spans.emplace_back();
            }
            info->index = static_cast<int>(model_.enums.size());
            enum_indexes_.emplace(fold_letters(info->name), info->index);
            for (const auto& [spelling, id] : language_.enums)
            {
                if (id == builtin.id)
                {
                    // "graphics.Colour" as well as "graphics.Color" (the spelling is already folded)
                    enum_indexes_.emplace(fold_letters(module) + spelling, info->index);
                }
            }
            model_.enums.push_back(std::move(info));
        }
        for (Stmt* item : items_)
        {
            if (item->kind != StmtKind::EnumDecl)
            {
                continue;
            }
            auto& decl = static_cast<EnumDeclStmt&>(*item);
            if (builtin_named(decl.name) || enum_indexes_.contains(decl.name) || struct_indexes_.contains(decl.name)
                || generic_structs_.contains(decl.name) || interface_indexes_.contains(decl.name))
            {
                diagnostics_.report(DiagId::TypeRedefined, decl.name_span, decl.name);
                continue;
            }
            if (decl.members.empty())
            {
                diagnostics_.report(DiagId::EmptyEnum, decl.name_span, decl.name);
            }
            auto info = std::make_unique<EnumInfo>();
            info->name = decl.name;
            info->decl = &decl;
            info->span = decl.span;
            for (std::size_t i = 0; i < decl.members.size(); ++i)
            {
                if (info->member_index(decl.members[i]) >= 0)
                {
                    diagnostics_.report(DiagId::DuplicateEnumMember, decl.member_spans[i], decl.name, decl.members[i]);
                    continue;
                }
                info->members.push_back(decl.members[i]);
                info->member_spans.push_back(decl.member_spans[i]);
            }
            decl.enum_index = static_cast<int>(model_.enums.size());
            info->index = decl.enum_index;
            enum_indexes_.emplace(fold_letters(decl.name), decl.enum_index);
            model_.enums.push_back(std::move(info));
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
            if (func.is_method)
            {
                continue; // a struct's method: registered with its struct (collect_struct_methods)
            }
            if (builtin_named(func.name))
            {
                diagnostics_.report(DiagId::BuiltinRedefined, func.name_span, func.name);
                continue;
            }
            if (struct_indexes_.contains(func.name) || generic_structs_.contains(func.name)
                || interface_indexes_.contains(func.name))
            {
                diagnostics_.report(DiagId::TypeRedefined, func.name_span, func.name);
                continue;
            }
            if (!func.type_params.empty())
            {
                // A template: its signature and body are resolved per instantiation.
                if (func.is_native)
                {
                    diagnostics_.report(DiagId::NativeTypeNotSupported, func.type_param_spans.front(),
                        func.type_params.front());
                    continue;
                }
                if (generic_functions_.contains(func.name) || function_indexes_.contains(func.name))
                {
                    diagnostics_.report(DiagId::FunctionRedefined, func.name_span, func.name);
                    continue;
                }
                generic_functions_.emplace(func.name, &func);
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
            if (native_indexes_.contains(func.name) || function_indexes_.contains(func.name)
                || generic_functions_.contains(func.name))
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
        if (type.is_bytes())
        {
            return 6; // THUNDER_BYTES: a byte[] crosses as a pointer and a length
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
        current_struct_ = func.method_struct; // a method may use its struct's private members
        loop_depth_ = 0;
        function_stack_.push_back(FunctionContext{func.function_index, nullptr, {}});

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
        function_stack_.pop_back();
        current_struct_ = -1;
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
        if (struct_indexes_.contains(name) || enum_indexes_.contains(name) || interface_indexes_.contains(name))
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
        case StmtKind::With:
            check_with(static_cast<WithStmt&>(stmt));
            break;
        case StmtKind::FuncDecl:
        case StmtKind::StructDecl:
        case StmtKind::EnumDecl:
        case StmtKind::Import:
        case StmtKind::Module:
        case StmtKind::Interface:
            // Nested functions, structs, interfaces, enums and imports are rejected by the parser;
            // top-level ones are handled before the statements are checked.
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
            if (declared && declared->is_function())
            {
                wrap_builtin_value(decl.initializer, *declared); // var f: func((int, string)): string = second
            }
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
                symbol = resolve_captured(symbol); // writing to a captured variable writes its box
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

        if (target_type.is_function())
        {
            wrap_builtin_value(assign.value, target_type); // handler = println
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

        const std::size_t self = static_cast<std::size_t>(current_function_);
        if (model_.functions[self].deduce_return)
        {
            // A function value written without its types, given where the result type is not
            // known either: the first 'return' says what it answers with.
            model_.functions[self].deduce_return = false;
            if (!ret.value)
            {
                return; // it answers with nothing
            }
            const Type value = check_expr(*ret.value, std::nullopt, !ret.implicit);
            if (value.is_error())
            {
                return;
            }
            if (ret.implicit && value.is_void())
            {
                // "func(n): println(n)" where nothing is wanted back: carry the expression out.
                ret.discard_value = true;
                return;
            }
            if (value.is_void())
            {
                return; // check_expr has already said the call answers with nothing
            }
            model_.functions[self].return_type = value;
            return;
        }
        // By value: checking the returned expression may add functions to the model, which would
        // move a reference into it.
        const FunctionInfo function = model_.functions[self];
        if (ret.implicit && function.return_type.is_void() && ret.value)
        {
            // The result type was known and is "nothing": the body's expression is carried out.
            check_expr(*ret.value, std::nullopt, false);
            ret.discard_value = true;
            return;
        }
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
        if (function.return_type.is_function())
        {
            wrap_builtin_value(ret.value, function.return_type); // "return println"
        }
        const Type value = coerce(*ret.value, function.return_type, check_expr(*ret.value, function.return_type));
        if (!assignable(function.return_type, value))
        {
            diagnostics_.report(DiagId::ReturnTypeMismatch, ret.value->span, function.name,
                type_name(function.return_type), type_name(value));
        }
    }

    /**
     * with name = resource: the resource is a struct of a module (or of the program) that has a
     * close function taking it. The cleanup - "if name != nothing: module.close(name)" - is built
     * here as an ordinary statement and checked like one; the code generator emits it wherever
     * the block can be left. The name is a constant: the block closes what it opened.
     */
    void SemanticAnalyzer::check_with(WithStmt& with)
    {
        push_scope("with", with.span);
        std::optional<Type> declared;
        if (with.declared_type)
        {
            resolve_type(*with.declared_type);
            declared = with.declared_type->type;
        }
        Type type = check_expr(*with.resource, declared);
        if (declared)
        {
            type = coerce(*with.resource, *declared, type);
            if (!type.is_error() && !declared->is_error() && !assignable(*declared, type))
            {
                diagnostics_.report(DiagId::AssignTypeMismatch, with.resource->span, type_name(type), with.name,
                    type_name(*declared));
            }
            type = *declared;
        }
        const bool named = !with.name.empty();
        const std::string name = named ? with.name : "(" + language_.term("resource") + ")";
        Symbol* symbol = declare(name, SymbolKind::Local, type, true, named ? with.name_span : with.span);
        symbol->used = symbol->used || !named;
        with.symbol = symbol;

        if (!type.is_error())
        {
            // Which close? A struct is an object: its own close() method closes it. Any other
            // value - an int handle, a string - is closed by the module of the function that
            // made it: "with h = handle.open()" closes with handle.close(h).
            if (type.is_struct())
            {
                const StructInfo& info = *model_.structs[static_cast<std::size_t>(type.struct_index)];
                // Named in the program's language ("وتړه" in Pashto), or "close", which is
                // understood in every language (as "main" is).
                std::string close_word = language_.term("close");
                int method = info.method_index(close_word);
                if (method < 0)
                {
                    close_word = "close";
                    method = info.method_index(close_word);
                }
                if (method < 0)
                {
                    diagnostics_.report(DiagId::NoCloseMethod, with.resource->span, type_name(type),
                        language_.term("close"));
                    symbol->used = true; // one message is enough
                }
                else
                {
                    // if r != nothing: r.close()  -  nothing is not closed.
                    auto call = std::make_unique<CallExpr>(with.span, std::string(), with.span);
                    call->callee_expr = std::make_unique<FieldExpr>(with.span,
                        std::make_unique<NameExpr>(with.span, name), close_word, with.span);
                    std::unique_ptr<BlockStmt> block = std::make_unique<BlockStmt>(with.span);
                    check_expr(*call, std::nullopt, false);
                    block->statements.push_back(std::make_unique<ExprStmt>(with.span, std::move(call)));
                    auto condition = std::make_unique<BinaryExpr>(with.span, TokenKind::NotEqual,
                        std::make_unique<NameExpr>(with.span, name), std::make_unique<NothingLiteralExpr>(with.span));
                    condition->op_span = with.span;
                    check_condition(*condition);
                    with.cleanup = std::make_unique<IfStmt>(with.span, std::move(condition), std::move(block), nullptr);
                }
            }
            else
            {
                std::string module;
                if (with.resource->kind == ExprKind::Call)
                {
                    const auto& call = static_cast<const CallExpr&>(*with.resource);
                    module = module_part(call.callee);
                }
                if (module.empty() || !user_modules_.contains(module))
                {
                    diagnostics_.report(DiagId::NotAResource, with.resource->span, type_name(type),
                        language_.spelling(TokenKind::KwWith));
                    symbol->used = true;
                }
                else
                {
                    const auto closer_named = [&](const std::string& word)
                    {
                        const std::string candidate = module + "." + word;
                        const bool found = function_indexes_.contains(candidate) || generic_functions_.contains(candidate)
                            || native_indexes_.contains(candidate);
                        return found ? candidate : std::string();
                    };
                    std::string closer = closer_named(language_.term("close"));
                    if (closer.empty())
                    {
                        closer = closer_named("close");
                    }
                    if (closer.empty())
                    {
                        closer = module + "." + language_.term("close");
                        diagnostics_.report(DiagId::NoCloseFunction, with.resource->span, type_name(type), closer);
                        symbol->used = true;
                    }
                    else
                    {
                        auto call = std::make_unique<CallExpr>(with.span, closer, with.span);
                        call->arguments.push_back(std::make_unique<NameExpr>(with.span, name));
                        std::unique_ptr<BlockStmt> block = std::make_unique<BlockStmt>(with.span);
                        check_expr(*call, std::nullopt, false);
                        block->statements.push_back(std::make_unique<ExprStmt>(with.span, std::move(call)));
                        with.cleanup = std::move(block);
                    }
                }
            }
        }
        check_statement(*with.body);
        pop_scope();
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
        {
            // A whole number from 0 to 255 written where a byte is wanted is that byte.
            const std::int64_t value = static_cast<const IntLiteralExpr&>(expr).value;
            if (expected && expected->is_byte() && value >= 0 && value <= 255)
            {
                expr.narrow_to_byte = true;
                type = Type::byte_type();
            }
            else
            {
                type = Type::int_type();
            }
            break;
        }
        case ExprKind::FloatLiteral:
            type = Type::float_type();
            break;
        case ExprKind::BoolLiteral:
            type = Type::bool_type();
            break;
        case ExprKind::NothingLiteral:
            type = check_nothing(expr, expected);
            break;
        case ExprKind::StringLiteral:
            type = Type::string_type();
            break;
        case ExprKind::ArrayLiteral:
            type = check_array_literal(static_cast<ArrayLiteralExpr&>(expr), expected);
            break;
        case ExprKind::TupleLiteral:
            type = check_tuple_literal(static_cast<TupleLiteralExpr&>(expr), expected);
            break;
        case ExprKind::MapLiteral:
            type = check_map_literal(static_cast<MapLiteralExpr&>(expr), expected);
            break;
        case ExprKind::Name:
            type = check_name(static_cast<NameExpr&>(expr));
            break;
        case ExprKind::Unary:
            type = check_unary(static_cast<UnaryExpr&>(expr));
            break;
        case ExprKind::Binary:
            type = check_binary(static_cast<BinaryExpr&>(expr));
            break;
        case ExprKind::Call:
            type = check_call(static_cast<CallExpr&>(expr), expected, value_required);
            break;
        case ExprKind::Index:
            type = check_index(static_cast<IndexExpr&>(expr));
            break;
        case ExprKind::Field:
            type = check_field(static_cast<FieldExpr&>(expr));
            break;
        case ExprKind::TupleIndex:
            type = check_tuple_index(static_cast<TupleIndexExpr&>(expr));
            break;
        case ExprKind::Lambda:
            type = check_lambda(static_cast<LambdaExpr&>(expr), expected);
            break;
        }

        expr.type = type;
        return type;
    }

    /**
     * A function value written in place: its body is checked (and later compiled) like a
     * function of its own. A variable of an enclosing function the body uses is captured: the
     * variable moves into a heap box both sides share, and the box travels with the value.
     */
    Type SemanticAnalyzer::check_lambda(LambdaExpr& lambda, std::optional<Type> expected)
    {
        FunctionInfo info;
        info.name = std::format("{}@{}", language_.spelling(TokenKind::KwFunc), lambda.span.start.line);
        info.span = lambda.span;
        if (lambda.deduced)
        {
            // "func(n): n * n": the types come from where the function value stands. A result
            // that is not known there (map's, say) is worked out from the body instead - the
            // caller writes an error type for it.
            const bool usable = expected && expected->is_function() && expected->dimensions == 0
                && expected->parameter_types().size() == lambda.params.size();
            for (std::size_t i = 0; i < lambda.params.size(); ++i)
            {
                lambda.params[i].type.type = usable ? expected->parameter_types()[i] : Type::error();
            }
            // A result that is wanted and known is written in; one that is not known is worked
            // out from the body; "nothing" is simply no result type, as in any function.
            if (usable && !expected->result_type().is_error() && !expected->result_type().is_void())
            {
                TypeSyntax result;
                result.type = expected->result_type();
                result.span = lambda.span;
                lambda.return_type = std::move(result);
            }
            else if (!usable || expected->result_type().is_error())
            {
                info.deduce_return = true;
            }
            if (!usable)
            {
                std::vector<Type> any(lambda.params.size(), Type::int_type());
                diagnostics_.report(DiagId::LambdaTypesUnknown, lambda.span,
                    Type::function_of(any, Type::int_type()).to_string(&language_));
            }
        }
        std::vector<Type> parameter_types;
        for (Param& param : lambda.params)
        {
            if (!lambda.deduced)
            {
                resolve_type(param.type);
            }
            parameter_types.push_back(param.type.type);
        }
        if (lambda.return_type)
        {
            if (!lambda.deduced)
            {
                resolve_type(*lambda.return_type);
            }
            info.return_type = lambda.return_type->type;
        }
        info.parameter_types = parameter_types;
        const int index = static_cast<int>(model_.functions.size());
        lambda.function_index = index;
        model_.functions.push_back(std::move(info));
        model_.lambdas.push_back(&lambda);

        const int saved_function = std::exchange(current_function_, index);
        const int saved_loop = std::exchange(loop_depth_, 0);
        function_stack_.push_back(FunctionContext{index, &lambda, {}});
        push_scope("function", lambda.span, model_.functions[static_cast<std::size_t>(index)].name);
        for (Param& param : lambda.params)
        {
            param.symbol = declare(param.name, SymbolKind::Parameter, param.type.type, false, param.name_span);
        }
        check_statements(lambda.body->statements);
        pop_scope();
        function_stack_.pop_back();
        loop_depth_ = saved_loop;
        current_function_ = saved_function;

        const FunctionInfo& checked = model_.functions[static_cast<std::size_t>(index)];
        if (!checked.return_type.is_void() && !checked.return_type.is_error() && !always_returns(*lambda.body))
        {
            diagnostics_.report(DiagId::NotAllPathsReturn, lambda.span, checked.name);
        }
        return Type::function_of(std::move(parameter_types), checked.return_type);
    }

    Symbol* SemanticAnalyzer::resolve_captured(Symbol* symbol)
    {
        if (symbol == nullptr || symbol->kind == SymbolKind::Global || symbol->function_index == current_function_)
        {
            return symbol;
        }
        // The symbol belongs to an enclosing function: capture it here and at every level in
        // between, so each function value hands the box on to the next.
        std::size_t level = function_stack_.size();
        for (std::size_t i = 0; i < function_stack_.size(); ++i)
        {
            if (function_stack_[i].index == symbol->function_index)
            {
                level = i;
                break;
            }
        }
        if (level == function_stack_.size())
        {
            return symbol; // not visible through nesting (defensive: should not happen)
        }
        Symbol* outer = symbol;
        for (std::size_t i = level + 1; i < function_stack_.size(); ++i)
        {
            FunctionContext& context = function_stack_[i];
            if (const auto found = context.captures.find(outer); found != context.captures.end())
            {
                outer = found->second;
                continue;
            }
            if (outer->kind != SymbolKind::Capture)
            {
                outer->boxed = true; // the owner's variable moves into a box
            }
            FunctionInfo& function = model_.functions[static_cast<std::size_t>(context.index)];
            auto hidden = std::make_unique<Symbol>();
            hidden->name = outer->name;
            hidden->kind = SymbolKind::Capture;
            hidden->type = outer->type;
            hidden->is_const = outer->is_const;
            hidden->span = outer->span;
            hidden->used = true;
            hidden->boxed = true;
            hidden->function_index = context.index;
            hidden->slot = static_cast<std::uint16_t>(function.captured.size());
            function.captured.push_back(hidden.get());
            if (context.lambda != nullptr)
            {
                context.lambda->capture_loads.push_back(outer);
            }
            context.captures.emplace(outer, hidden.get());
            outer = hidden.get();
            model_.symbols.push_back(std::move(hidden));
        }
        return outer;
    }

    /** "nothing" is the empty struct reference; the struct (or interface) it stands in for comes from the context. */
    Type SemanticAnalyzer::check_nothing(const Expr& expr, std::optional<Type> expected)
    {
        if (expected && expected->is_reference())
        {
            return *expected;
        }
        if (!expected || !expected->is_error())
        {
            diagnostics_.report(DiagId::NothingNeedsType, expr.span);
        }
        return Type::error();
    }

    Type SemanticAnalyzer::check_array_literal(ArrayLiteralExpr& array, std::optional<Type> expected)
    {
        const std::optional<Type> element_hint =
            expected && expected->is_array() ? std::optional(expected->element()) : std::nullopt;
        if (array.elements.empty())
        {
            if (element_hint)
            {
                return *expected;
            }
            diagnostics_.report(DiagId::EmptyArrayNeedsType, array.span);
            return Type::error();
        }
        Type first = check_expr(*array.elements.front(), element_hint);
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
        // [button, label] where a Widget[] is wanted: structs of different kinds may share an
        // interface, and then the array is an array of that interface.
        if (element_hint && element_hint->is_interface())
        {
            const bool all_fit = std::ranges::all_of(
                element_types, [&](const Type& element) { return assignable(*element_hint, element); });
            if (all_fit)
            {
                return *expected;
            }
        }
        bool ok = !first.is_error();
        for (std::size_t i = 1; i < array.elements.size(); ++i)
        {
            if (!first.is_error() && !element_types[i].is_error() && element_types[i] != first)
            {
                diagnostics_.report(DiagId::ArrayElementMismatch, array.elements[i]->span, type_name(first),
                    type_name(element_types[i]));
                ok = false;
            }
        }
        return ok ? Type::array_of(first) : Type::error();
    }

    Type SemanticAnalyzer::check_tuple_literal(TupleLiteralExpr& tuple, std::optional<Type> expected)
    {
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
        return ok ? Type::tuple_of(std::move(element_types)) : Type::error();
    }

    Type SemanticAnalyzer::check_unary(UnaryExpr& unary)
    {
        Type operand = check_expr(*unary.operand);
        if (operand.is_error())
        {
            return Type::error();
        }
        if (operand.is_byte() && unary.op != TokenKind::KwNot)
        {
            operand = coerce(*unary.operand, Type::int_type(), operand); // -b and ~b work on the int
        }
        const bool fits = (unary.op == TokenKind::Minus && operand.is_number())
            || (unary.op == TokenKind::KwNot && operand.is_bool()) || (unary.op == TokenKind::Tilde && operand.is_int());
        if (!fits)
        {
            diagnostics_.report(DiagId::UnaryOperandMismatch, unary.span, language_.spelling(unary.op), type_name(operand));
            return Type::error();
        }
        return unary.op == TokenKind::KwNot ? Type::bool_type() : operand;
    }

    /** target[index]: an array element, a character of a string, or a map's value. */
    Type SemanticAnalyzer::check_index(IndexExpr& index)
    {
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
            return target.value_type();
        }
        Type position = check_expr(*index.index);
        position = coerce(*index.index, Type::int_type(), position); // a byte may index too
        if (!position.is_error() && !position.is_int())
        {
            diagnostics_.report(DiagId::IndexNotInt, index.index->span, type_name(position));
        }
        if (target.is_error())
        {
            return Type::error();
        }
        if (target.is_array())
        {
            return target.element();
        }
        if (target.is_string())
        {
            return Type::string_type();
        }
        diagnostics_.report(DiagId::NotIndexable, index.target->span, type_name(target));
        return Type::error();
    }

    Type SemanticAnalyzer::check_tuple_index(TupleIndexExpr& element)
    {
        const Type target = check_expr(*element.target);
        if (target.is_error())
        {
            return Type::error();
        }
        if (!target.is_tuple())
        {
            diagnostics_.report(DiagId::NotATuple, element.index_span, element.index, type_name(target));
            return Type::error();
        }
        if (element.index < 0 || static_cast<std::size_t>(element.index) >= target.elements().size())
        {
            diagnostics_.report(DiagId::TupleIndexOutOfRange, element.index_span, type_name(target), element.index);
            return Type::error();
        }
        return target.elements()[static_cast<std::size_t>(element.index)];
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
        // Color.red: the target names an enum, not a value (unless a variable of that name hides it).
        if (field.target->kind == ExprKind::Name)
        {
            const auto& name = static_cast<const NameExpr&>(*field.target);
            if (lookup(name.name) == nullptr)
            {
                if (const EnumInfo* info = find_enum(name.name))
                {
                    return check_enum_field(field, *info);
                }
            }
        }
        const Type target = check_expr(*field.target);
        if (target.is_error())
        {
            return Type::error();
        }
        if (target.is_interface())
        {
            // An interface has no fields: its members are used by calling them.
            const InterfaceInfo& info = *model_.interfaces[static_cast<std::size_t>(target.struct_index)];
            if (info.member(field.field) != nullptr)
            {
                diagnostics_.report(DiagId::InterfaceMemberNotCalled, field.field_span, field.field, info.name);
            }
            else
            {
                diagnostics_.report(DiagId::NoSuchMethod, field.field_span, type_name(target), field.field);
            }
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
            if (info.method_index(field.field) >= 0)
            {
                // A method is not a value: it is used by calling it.
                diagnostics_.report(DiagId::InterfaceMemberNotCalled, field.field_span, field.field, info.name);
            }
            else
            {
                diagnostics_.report(DiagId::NoSuchField, field.field_span, type_name(target), field.field);
            }
            return Type::error();
        }
        report_private_member(field.field, info.index, field.field_span);
        return info.fields[static_cast<std::size_t>(field.field_index)].type;
    }

    /** Color.red (a member), Color.values (every member, in order) or Color.count (how many). */
    Type SemanticAnalyzer::check_enum_field(FieldExpr& field, const EnumInfo& info)
    {
        const Type type = Type::enum_type(info.index, &info.name);
        field.enum_index = info.index;
        field.target->type = type; // the name stands for the type; nothing is evaluated
        int member = info.member_index(field.field);
        if (member < 0 && info.builtin >= 0)
        {
            // The other accepted spellings of a built-in enum's members ("grey" for "gray").
            const auto other = language_.builtin_enum_member(field.field);
            if (other && static_cast<std::int32_t>(other->first) == info.builtin && other->second < info.members.size())
            {
                member = other->second;
            }
        }
        if (member >= 0)
        {
            field.enum_member = member;
            field.enum_use = FieldExpr::EnumUse::Member;
            return type;
        }
        if (field.field == "values" || field.field == language_.term("enum_values"))
        {
            field.enum_use = FieldExpr::EnumUse::Values;
            return Type::array_of(type);
        }
        if (field.field == "count" || field.field == language_.term("enum_count"))
        {
            field.enum_use = FieldExpr::EnumUse::Count;
            return Type::int_type();
        }
        std::string members;
        for (const std::string& candidate : info.members)
        {
            members += (members.empty() ? "" : ", ") + candidate;
        }
        diagnostics_.report(DiagId::NoSuchEnumMember, field.field_span, info.name, field.field, members);
        return Type::error();
    }

    /** Color(2) or Color("red"): the member at a position, or with a name, found when the program runs. */
    Type SemanticAnalyzer::check_enum_construct(CallExpr& call, const EnumInfo& info)
    {
        const Type type = Type::enum_type(info.index, &info.name);
        if (call.has_named_arguments())
        {
            diagnostics_.report(DiagId::NamedArgumentsNotAllowed, call.span);
            check_remaining(call, 0);
            return Type::error();
        }
        if (call.arguments.size() != 1)
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, info.name, 1, call.arguments.size());
            check_remaining(call, 0);
            return Type::error();
        }
        Type argument = check_expr(*call.arguments[0]);
        argument = coerce(*call.arguments[0], Type::int_type(), argument); // a byte is a position too
        if (argument.is_error())
        {
            return Type::error();
        }
        if (!argument.is_int() && !argument.is_string())
        {
            diagnostics_.report(DiagId::EnumNeedsIntOrString, call.arguments[0]->span, info.name, type_name(argument));
            return Type::error();
        }
        call.enum_index = info.index;
        return type;
    }

    Type SemanticAnalyzer::check_name(NameExpr& name)
    {
        Symbol* symbol = lookup(name.name);
        if (symbol == nullptr)
        {
            if (name.name == "(" + language_.term("self") + ")")
            {
                // "my" outside a method: there is no receiver to stand for.
                diagnostics_.report(DiagId::MyOutsideMethod, name.span, language_.spelling(TokenKind::KwMy));
                return Type::error();
            }
            if (struct_indexes_.contains(name.name) || enum_indexes_.contains(name.name)
                || interface_indexes_.contains(name.name))
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
            else if (!report_member_needs_module(name.name, name.span) && !already_reported(name.name))
            {
                // (geometry.nope: the qualifier has already said the module has no such member)
                diagnostics_.report(DiagId::UndeclaredName, name.span, name.name);
            }
            return Type::error();
        }
        symbol->used = true;
        symbol = resolve_captured(symbol); // a variable of an enclosing function is captured
        name.symbol = symbol;
        return symbol->type;
    }

    Type SemanticAnalyzer::check_binary(BinaryExpr& binary)
    {
        const std::optional<std::pair<Type, Type>> operands = check_binary_operands(binary);
        if (!operands)
        {
            return Type::error();
        }
        const auto& [left, right] = *operands;
        if (const std::optional<Type> result = binary_result(binary.op, left, right))
        {
            return *result;
        }
        diagnostics_.report(DiagId::BinaryOperandMismatch, binary.op_span, language_.spelling(binary.op),
            type_name(left), type_name(right));
        return Type::error();
    }

    /**
     * The operand types of a binary expression after the implicit widenings: an int beside a
     * float becomes a float (1 + 0.5), and `nothing` takes the struct type of the other side.
     * Nullopt when either operand already failed.
     */
    std::optional<std::pair<Type, Type>> SemanticAnalyzer::check_binary_operands(BinaryExpr& binary)
    {
        const bool left_nothing = binary.left->kind == ExprKind::NothingLiteral;
        const bool right_nothing = binary.right->kind == ExprKind::NothingLiteral;
        Type left;
        Type right;
        if (left_nothing && !right_nothing)
        {
            right = check_expr(*binary.right);
            left = check_expr(*binary.left, right.is_reference() ? std::optional(right) : std::nullopt);
        }
        else
        {
            left = check_expr(*binary.left);
            right = check_expr(*binary.right, right_nothing && left.is_reference() ? std::optional(left) : std::nullopt);
        }
        if (left.is_error() || right.is_error())
        {
            return std::nullopt;
        }
        // A struct beside an interface it implements compares as the interface (by identity).
        if (left.is_interface() && right.is_struct() && assignable(left, right))
        {
            right = left;
        }
        else if (right.is_interface() && left.is_struct() && assignable(right, left))
        {
            left = right;
        }
        // A byte beside a number (or another byte) is widened to an int, like an int beside a float.
        if (left.is_byte() && (right.is_number() || right.is_byte()))
        {
            left = coerce(*binary.left, right.is_float() ? Type::float_type() : Type::int_type(), left);
        }
        if (right.is_byte() && left.is_number())
        {
            right = coerce(*binary.right, left.is_float() ? Type::float_type() : Type::int_type(), right);
        }
        if (left.is_int() && right.is_float())
        {
            left = coerce(*binary.left, Type::float_type(), left);
        }
        else if (left.is_float() && right.is_int())
        {
            right = coerce(*binary.right, Type::float_type(), right);
        }
        return std::pair{left, right};
    }

    namespace
    {
        // What a binary operator asks of its (already widened) operands, and what it gives back.
        enum class Operands
        {
            NumbersOrStrings, // + : the operand type
            Numbers,          // - * / : the operand type
            Ints,             // % and the bit operators: int
            Ordered,          // < <= > >= : bool, for numbers and strings
            Equal,            // == != : bool, for numbers, strings, bools, tuples and structs
            Bools,            // and or : bool
        };

        const std::unordered_map<TokenKind, Operands>& binary_operands()
        {
            static const std::unordered_map<TokenKind, Operands> table = {
                {TokenKind::Plus, Operands::NumbersOrStrings},
                {TokenKind::Minus, Operands::Numbers}, {TokenKind::Star, Operands::Numbers}, {TokenKind::Slash, Operands::Numbers},
                {TokenKind::Percent, Operands::Ints}, {TokenKind::Amp, Operands::Ints}, {TokenKind::Pipe, Operands::Ints},
                {TokenKind::Caret, Operands::Ints}, {TokenKind::ShiftLeft, Operands::Ints}, {TokenKind::ShiftRight, Operands::Ints},
                {TokenKind::Less, Operands::Ordered}, {TokenKind::LessEqual, Operands::Ordered},
                {TokenKind::Greater, Operands::Ordered}, {TokenKind::GreaterEqual, Operands::Ordered},
                {TokenKind::Equal, Operands::Equal}, {TokenKind::NotEqual, Operands::Equal},
                {TokenKind::KwAnd, Operands::Bools}, {TokenKind::KwOr, Operands::Bools},
            };
            return table;
        }
    }

    /** The type `left op right` has, or nullopt when the operator does not take those operands. */
    std::optional<Type> SemanticAnalyzer::binary_result(TokenKind op, const Type& left, const Type& right)
    {
        const auto found = binary_operands().find(op);
        if (found == binary_operands().end() || left != right)
        {
            return std::nullopt; // (the widening of an int beside a float has already happened)
        }
        const bool text_or_number = left.is_number() || left.is_string();
        switch (found->second)
        {
        case Operands::NumbersOrStrings:
            return text_or_number ? std::optional(left) : std::nullopt;
        case Operands::Numbers:
            return left.is_number() ? std::optional(left) : std::nullopt;
        case Operands::Ints:
            return left.is_int() ? std::optional(left) : std::nullopt;
        case Operands::Ordered:
            // Enum members are ordered by their position.
            return text_or_number || left.is_enum() ? std::optional(Type::bool_type()) : std::nullopt;
        case Operands::Equal:
            // Tuples compare element by element; structs and interfaces by identity (and against nothing).
            return text_or_number || left.is_bool() || left.is_tuple() || left.is_reference() || left.is_enum()
                ? std::optional(Type::bool_type()) : std::nullopt;
        case Operands::Bools:
            return left.is_bool() ? std::optional(Type::bool_type()) : std::nullopt;
        }
        return std::nullopt;
    }

    Type SemanticAnalyzer::check_call(CallExpr& call, std::optional<Type> expected, bool value_required)
    {
        if (call.callee_expr)
        {
            if (call.callee_expr->kind == ExprKind::Field)
            {
                return check_member_call(call, value_required);
            }
            return check_callee_expression(call, value_required);
        }
        if (const StructInfo* info = find_struct(call.callee))
        {
            return check_construct(call, *info);
        }
        if (const auto generic = generic_structs_.find(call.callee); generic != generic_structs_.end())
        {
            return check_generic_construct(call, *generic->second, expected);
        }
        if (const EnumInfo* info = find_enum(call.callee); info != nullptr && lookup(call.callee) == nullptr)
        {
            return check_enum_construct(call, *info);
        }
        if (call.has_named_arguments())
        {
            diagnostics_.report(DiagId::NamedArgumentsNotAllowed, call.span);
            check_remaining(call, 0);
            return Type::error();
        }

        Type result = Type::error();
        Symbol* variable = lookup(call.callee);
        if (const auto builtin = builtin_named(call.callee))
        {
            call.builtin = builtin;
            result = check_builtin(call, *builtin, expected);
        }
        else if (variable != nullptr && variable->type.is_function())
        {
            variable = resolve_captured(variable);      // f of an enclosing function is captured
            result = check_value_call(call, *variable); // f(x) where f holds a function value
        }
        else if (const auto native = native_indexes_.find(call.callee); native != native_indexes_.end())
        {
            result = check_native_call(call, native->second);
        }
        else if (const auto generic = generic_functions_.find(call.callee); generic != generic_functions_.end())
        {
            result = check_generic_call(call, *generic->second, expected);
        }
        else if (const auto found = function_indexes_.find(call.callee); found != function_indexes_.end())
        {
            result = check_function_call(call, found->second);
        }
        else
        {
            report_unknown_callee(call, variable);
            return Type::error();
        }
        if (result.is_void() && value_required)
        {
            diagnostics_.report(DiagId::VoidValue, call.span, call.callee);
            return Type::error();
        }
        return result;
    }

    /** handlers[i](x): the callee is an expression that must produce a function value. */
    Type SemanticAnalyzer::check_callee_expression(CallExpr& call, bool value_required)
    {
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
        if (callee.result_type().is_void() && value_required)
        {
            diagnostics_.report(DiagId::VoidValue, call.span, call.callee);
            return Type::error();
        }
        return callee.result_type();
    }

    /**
     * A function whose body is in a C++ library: the arguments are checked here, the call
     * happens through CALL_NATIVE (see docs/native.md).
     */
    Type SemanticAnalyzer::check_native_call(CallExpr& call, int native_index)
    {
        call.native_index = native_index;
        const NativeInfo& info = model_.natives[static_cast<std::size_t>(native_index)];
        if (call.arguments.size() != info.parameter_types.size())
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, info.parameter_types.size(),
                call.arguments.size());
        }
        check_arguments(call, info.parameter_types);
        return info.return_type;
    }

    /** A call of a function the program declares: each argument is checked against its parameter. */
    Type SemanticAnalyzer::check_function_call(CallExpr& call, int function_index)
    {
        call.function_index = function_index;
        const FunctionInfo& function = model_.functions[static_cast<std::size_t>(function_index)];
        const std::size_t expected_count = function.parameter_types.size();
        if (call.arguments.size() != expected_count)
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, expected_count, call.arguments.size());
        }
        for (std::size_t i = 0; i < call.arguments.size(); ++i)
        {
            const std::optional<Type> hint = i < expected_count ? std::optional(function.parameter_types[i]) : std::nullopt;
            if (hint && hint->is_function())
            {
                wrap_builtin_value(call.arguments[i], *hint); // apply(str, 7)
            }
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
        return function.return_type;
    }

    /** Nothing is called that: the arguments are still checked, and the name's problem reported. */
    void SemanticAnalyzer::report_unknown_callee(CallExpr& call, Symbol* variable)
    {
        check_remaining(call, 0);
        if (variable != nullptr)
        {
            variable->used = true;
            diagnostics_.report(DiagId::NotAFunction, call.callee_span, call.callee);
        }
        else if (mixins_.contains(call.callee))
        {
            diagnostics_.report(DiagId::MixinIsNotType, call.callee_span, call.callee); // Bordered(): never built
        }
        else if (!report_member_needs_module(call.callee, call.callee_span) && !already_reported(call.callee))
        {
            diagnostics_.report(DiagId::UndeclaredName, call.callee_span, call.callee);
        }
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

    bool SemanticAnalyzer::inside_struct(int struct_index) const
    {
        if (current_struct_ < 0 || struct_index < 0)
        {
            return false;
        }
        if (current_struct_ == struct_index)
        {
            return true;
        }
        // Every instance of one generic struct shares its declaration: Stack<int>'s methods may
        // use the private members of a Stack<string> too (they are the same source).
        const StructDeclStmt* own = model_.structs[static_cast<std::size_t>(current_struct_)]->decl;
        const StructDeclStmt* target = model_.structs[static_cast<std::size_t>(struct_index)]->decl;
        return own != nullptr && own == target;
    }

    void SemanticAnalyzer::report_private_member(const std::string& member, int struct_index, const Span& span)
    {
        // A member whose name starts with '_' belongs to the struct itself, exactly as a
        // module's member that starts with '_' belongs to its module.
        if (member.size() > 1 && member.starts_with('_') && !inside_struct(struct_index))
        {
            diagnostics_.report(DiagId::PrivateStructMember, span, member,
                model_.structs[static_cast<std::size_t>(struct_index)]->name);
        }
    }

    /**
     * receiver.member(args): a method of a struct (the function to call is known here), a member
     * of an interface (found when the program runs, through CALL_METHOD), or a field holding a
     * function value (read, then called like any function value).
     */
    Type SemanticAnalyzer::check_member_call(CallExpr& call, bool value_required)
    {
        auto& field = static_cast<FieldExpr&>(*call.callee_expr);
        // Color.red(...): the target names an enum, so this is no method call. The ordinary
        // path checks it (and reports that an enum member cannot be called).
        if (field.target->kind == ExprKind::Name)
        {
            const auto& name = static_cast<const NameExpr&>(*field.target);
            if (lookup(name.name) == nullptr && find_enum(name.name) != nullptr)
            {
                return check_callee_expression(call, value_required);
            }
        }
        const Type target = check_expr(*field.target);
        if (target.is_error())
        {
            check_remaining(call, 0);
            return Type::error();
        }
        if (call.has_named_arguments())
        {
            diagnostics_.report(DiagId::NamedArgumentsNotAllowed, call.span);
            check_remaining(call, 0);
            return Type::error();
        }

        std::vector<Type> parameters;
        Type result = Type::void_type();
        if (target.is_struct())
        {
            const StructInfo& info = *model_.structs[static_cast<std::size_t>(target.struct_index)];
            const int method = info.method_index(field.field);
            if (method >= 0)
            {
                report_private_member(field.field, info.index, field.field_span);
                const FunctionInfo& function = model_.functions[static_cast<std::size_t>(method)];
                call.function_index = method;
                call.callee = function.name;
                call.callee_span = field.field_span;
                call.self_expr = std::move(field.target);
                call.callee_expr.reset();
                parameters.assign(function.parameter_types.begin() + 1, function.parameter_types.end());
                result = function.return_type;
            }
            else if (info.field_index(field.field) >= 0)
            {
                // A field holding a function value: read it, then call it (sock.on_close()).
                report_private_member(field.field, info.index, field.field_span);
                field.field_index = info.field_index(field.field);
                const Type callee = info.fields[static_cast<std::size_t>(field.field_index)].type;
                field.type = callee;
                call.callee = info.name + "." + field.field;
                if (!callee.is_function())
                {
                    diagnostics_.report(DiagId::NotAFunction, field.field_span, call.callee);
                    check_remaining(call, 0);
                    return Type::error();
                }
                parameters = callee.parameter_types();
                result = callee.result_type();
            }
            else
            {
                diagnostics_.report(DiagId::NoSuchMethod, field.field_span, type_name(target), field.field);
                check_remaining(call, 0);
                return Type::error();
            }
        }
        else if (target.is_interface())
        {
            const InterfaceInfo& info = *model_.interfaces[static_cast<std::size_t>(target.struct_index)];
            const InterfaceMember* member = info.member(field.field);
            if (member == nullptr)
            {
                diagnostics_.report(DiagId::NoSuchMethod, field.field_span, type_name(target), field.field);
                check_remaining(call, 0);
                return Type::error();
            }
            call.method_name = field.field;
            call.callee = info.name + "." + field.field;
            call.callee_span = field.field_span;
            call.self_expr = std::move(field.target);
            call.callee_expr.reset();
            parameters = member->parameter_types;
            result = member->return_type;
        }
        else
        {
            diagnostics_.report(DiagId::NotAStruct, field.field_span, type_name(target));
            check_remaining(call, 0);
            return Type::error();
        }

        if (call.arguments.size() != parameters.size())
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, parameters.size(),
                call.arguments.size());
        }
        check_arguments(call, parameters);
        if (result.is_void() && value_required)
        {
            diagnostics_.report(DiagId::VoidValue, call.span, call.callee);
            return Type::error();
        }
        return result;
    }

    bool SemanticAnalyzer::check_arguments(CallExpr& call, const std::vector<Type>& types, std::size_t first)
    {
        bool ok = true;
        for (std::size_t i = first; i < call.arguments.size(); ++i)
        {
            const std::optional<Type> hint = i < types.size() ? std::optional(types[i]) : std::nullopt;
            if (hint && hint->is_function())
            {
                wrap_builtin_value(call.arguments[i], *hint); // "sort_by(names, to_upper)"
            }
            Type argument = check_expr(*call.arguments[i], hint);
            if (hint)
            {
                argument = coerce(*call.arguments[i], *hint, argument);
            }
            if (hint && !assignable(*hint, argument) && !accepts_color(call, i, types.size(), *hint, argument))
            {
                diagnostics_.report(DiagId::ArgumentType, call.arguments[i]->span, i + 1, call.callee, type_name(*hint),
                    type_name(argument));
                ok = false;
            }
            ok = ok && !argument.is_error();
        }
        return ok;
    }

    bool SemanticAnalyzer::accepts_color(const CallExpr& call, std::size_t index, std::size_t count, const Type& hint,
        const Type& argument) const
    {
        // Every colour parameter of the graphics built-ins is the last one, and a string.
        if (!call.builtin || builtin_info(*call.builtin).module != BuiltinModule::Graphics || !hint.is_string()
            || index + 1 != count || !argument.is_enum())
        {
            return false;
        }
        const EnumInfo& info = *model_.enums[static_cast<std::size_t>(argument.struct_index)];
        return info.builtin == static_cast<std::int32_t>(BuiltinEnumId::Color);
    }

    Type SemanticAnalyzer::check_construct(CallExpr& call, const StructInfo& info,
        const std::vector<std::optional<Type>>* precomputed)
    {
        call.struct_index = info.index;
        const Type result = Type::struct_type(call.struct_index, &info.name);
        if (info.ctor >= 0)
        {
            // The struct has a constructor: Button("OK") builds the struct with every field at
            // its default, then runs the constructor's body on it with these arguments.
            call.ctor_index = info.ctor;
            if (call.has_named_arguments())
            {
                diagnostics_.report(DiagId::NamedArgumentsNotAllowed, call.span);
                check_remaining(call, 0);
                return result;
            }
            if (call.arguments.size() != info.ctor_params.size())
            {
                diagnostics_.report(DiagId::ArgumentCount, call.span, info.name, info.ctor_params.size(),
                    call.arguments.size());
            }
            if (precomputed == nullptr)
            {
                check_arguments(call, info.ctor_params);
            }
            else
            {
                // The arguments were already checked to deduce the type parameters.
                for (std::size_t i = 0; i < call.arguments.size() && i < info.ctor_params.size(); ++i)
                {
                    const Type& hint = info.ctor_params[i];
                    Type argument = i < precomputed->size() && (*precomputed)[i].has_value()
                        ? *(*precomputed)[i]
                        : check_expr(*call.arguments[i], hint);
                    argument = coerce(*call.arguments[i], hint, argument);
                    if (!assignable(hint, argument))
                    {
                        diagnostics_.report(DiagId::ArgumentType, call.arguments[i]->span, i + 1, info.name,
                            type_name(hint), type_name(argument));
                    }
                }
            }
            return result;
        }
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
            if (hint && hint->is_function())
            {
                wrap_builtin_value(call.arguments[i], *hint); // Key("A", println)
            }
            // With 'precomputed' the arguments were already checked (to deduce the type
            // parameters of a generic struct), so they are not visited - and reported - twice.
            Type argument = precomputed && i < precomputed->size() && (*precomputed)[i].has_value()
                ? *(*precomputed)[i]
                : check_expr(*call.arguments[i], hint);
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

        // Positional arguments must give every field; named arguments may leave fields at their
        // defaults, and so may no arguments at all - Tally() is a Tally with every field at its default.
        if (!named && !call.arguments.empty() && call.arguments.size() != info.fields.size())
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, info.name, info.fields.size(), call.arguments.size());
            ok = false;
        }
        return ok ? result : Type::error();
    }

    // ---------------------------------------------------------------- generics
    //
    // A declaration with type parameters ("struct Stack<T>", "func first<T>(values: T[]): T")
    // is a template. Nothing is compiled for the template itself: each use with concrete types
    // makes (or finds) an instance - a struct "Stack<int>" with resolved fields, a function
    // "first<int>" with a resolved signature and its own compiled body. The types are deduced
    // from the arguments, or from the type the result is assigned to; they are never written
    // at a call.

    namespace
    {
        // A stable key for one combination of type arguments (always the English spelling).
        std::string instance_key(const std::string& name, const std::vector<Type>& args)
        {
            std::string key = name;
            for (const Type& arg : args)
            {
                key += "|" + arg.to_string(nullptr);
            }
            return key;
        }
    }

    /** Unifies written types with concrete ones pairwise; false if the counts differ or any pair fails. */
    bool SemanticAnalyzer::unify_all(const std::vector<TypeSyntax>& patterns, const std::vector<Type>& actuals,
        const std::vector<std::string>& params, TypeBindings& bindings)
    {
        if (patterns.size() != actuals.size())
        {
            return false;
        }
        bool ok = true;
        for (std::size_t i = 0; i < patterns.size(); ++i)
        {
            ok = unify(patterns[i], actuals[i], params, bindings) && ok; // every pair, for every binding
        }
        return ok;
    }

    /** A written name: a type parameter to bind, a generic struct to match, or a concrete struct. */
    bool SemanticAnalyzer::unify_named(const TypeSyntax& pattern, const Type& actual,
        const std::vector<std::string>& params, TypeBindings& bindings)
    {
        // A type parameter: bind it, or check that it means the same type everywhere.
        if (std::find(params.begin(), params.end(), pattern.struct_name) != params.end())
        {
            const auto [it, inserted] = bindings.try_emplace(pattern.struct_name, actual);
            return inserted || it->second == actual;
        }
        // "Stack<T>" against a value of "Stack<int>": unify the written arguments with the
        // instance's recorded ones.
        if (generic_structs_.contains(pattern.struct_name))
        {
            if (!actual.is_struct() || actual.dimensions != 0)
            {
                return false;
            }
            const StructInfo& info = *model_.structs[static_cast<std::size_t>(actual.struct_index)];
            return info.template_name == pattern.struct_name && unify_all(pattern.parts, info.type_args, params, bindings);
        }
        // A concrete struct name: resolve it (once; the result is kept in the tree) and compare.
        TypeSyntax copy = pattern;
        return resolve_type(copy) && copy.type == actual;
    }

    bool SemanticAnalyzer::unify(const TypeSyntax& pattern, const Type& actual,
        const std::vector<std::string>& params, TypeBindings& bindings)
    {
        if (actual.is_error())
        {
            return false;
        }
        // "T[]" against "int[][]": peel the written dimensions, bind T to int[].
        if (pattern.type.dimensions > 0)
        {
            if (actual.dimensions < pattern.type.dimensions)
            {
                return false;
            }
            TypeSyntax base = pattern;
            base.type.dimensions = 0;
            Type element = actual;
            element.dimensions = static_cast<std::uint8_t>(actual.dimensions - pattern.type.dimensions);
            return unify(base, element, params, bindings);
        }
        if (!pattern.struct_name.empty())
        {
            return unify_named(pattern, actual, params, bindings);
        }
        switch (pattern.type.base)
        {
        case Type::Base::Function:
            // parts[0] is the result type, then the parameters - the same order Type uses.
            return actual.is_function() && unify_all(pattern.parts, actual.elements(), params, bindings);
        case Type::Base::Tuple:
            return actual.is_tuple() && unify_all(pattern.parts, actual.elements(), params, bindings);
        case Type::Base::Map:
            return actual.is_map() && pattern.parts.size() == 2
                && unify(pattern.parts[0], actual.key_type(), params, bindings)
                && unify(pattern.parts[1], actual.value_type(), params, bindings);
        default:
            // A primitive written in the signature: the argument must simply be that type.
            return pattern.type.base == actual.base && actual.dimensions == 0;
        }
    }

    int SemanticAnalyzer::instantiate_struct(StructDeclStmt& tmpl, std::vector<Type> args, const Span& span)
    {
        const std::string key = instance_key(tmpl.name, args);
        if (const auto it = struct_instances_.find(key); it != struct_instances_.end())
        {
            return it->second;
        }
        if (instantiation_depth_ > 32)
        {
            // "field: Stack<Stack<T>>" would otherwise expand for ever.
            diagnostics_.report(DiagId::CannotDeduceTypeParameter, span, tmpl.type_params.front(), tmpl.name);
            return -1;
        }
        ++instantiation_depth_;

        auto info = std::make_unique<StructInfo>();
        info->decl = &tmpl;
        info->span = tmpl.span;
        info->template_name = tmpl.name;
        info->type_args = args;
        info->name = tmpl.name + "<";
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            info->name += (i == 0 ? "" : ", ") + type_name(args[i]);
        }
        info->name += ">";
        const int index = static_cast<int>(model_.structs.size());
        info->index = index;
        struct_instances_.emplace(key, index); // before the fields: they may name this instance
        model_.structs.push_back(std::move(info));

        TypeBindings bindings;
        for (std::size_t i = 0; i < tmpl.type_params.size(); ++i)
        {
            bindings.emplace(tmpl.type_params[i], args[i]);
        }
        const TypeBindings saved = std::exchange(bindings_, bindings);
        for (const FieldDecl& field : tmpl.fields)
        {
            TypeSyntax field_type = field.type;
            resolve_type(field_type);
            StructInfo& instance = *model_.structs[static_cast<std::size_t>(index)];
            if (instance.field_index(field.name) < 0) // duplicates were reported at the declaration
            {
                instance.fields.push_back(FieldInfo{field.name, field_type.type, field.name_span});
            }
        }
        bindings_ = saved;
        {
            // The instance implements what the template promises, and compiles its own copy of
            // every method ("Stack<int>.push"), with the type parameters bound to its arguments.
            StructInfo& instance = *model_.structs[static_cast<std::size_t>(index)];
            for (const std::string& base : tmpl.bases)
            {
                if (const InterfaceInfo* interface = find_interface(base))
                {
                    if (!instance.implements(interface->index))
                    {
                        instance.interfaces.push_back(interface->index);
                    }
                }
            }
            collect_struct_methods(tmpl, instance, &bindings);
            check_interface_conformance(instance);
        }
        --instantiation_depth_;
        return index;
    }

    int SemanticAnalyzer::instantiate_function(FuncDeclStmt& tmpl, const TypeBindings& bindings, const Span& span)
    {
        std::vector<Type> args;
        for (const std::string& param : tmpl.type_params)
        {
            args.push_back(bindings.at(param));
        }
        const std::string key = instance_key(tmpl.name, args);
        if (const auto it = function_instances_.find(key); it != function_instances_.end())
        {
            return it->second;
        }
        if (instantiation_depth_ > 32)
        {
            diagnostics_.report(DiagId::CannotDeduceTypeParameter, span, tmpl.type_params.front(), tmpl.name);
            return -1;
        }
        ++instantiation_depth_;

        StmtPtr cloned = clone_stmt(tmpl);
        auto* instance = static_cast<FuncDeclStmt*>(cloned.get());
        instance->name = tmpl.name + "<";
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            instance->name += (i == 0 ? "" : ", ") + type_name(args[i]);
        }
        instance->name += ">";
        instance->type_params.clear(); // the instance is an ordinary function

        const TypeBindings saved = std::exchange(bindings_, bindings);
        for (Param& param : instance->params)
        {
            resolve_type(param.type);
        }
        if (instance->return_type)
        {
            resolve_type(*instance->return_type);
        }
        bindings_ = saved;

        FunctionInfo info;
        info.name = instance->name;
        info.decl = instance;
        info.span = tmpl.span;
        info.return_type = instance->return_type ? instance->return_type->type : Type::void_type();
        for (const Param& param : instance->params)
        {
            info.parameter_types.push_back(param.type.type);
        }
        const int index = static_cast<int>(model_.functions.size());
        instance->function_index = index;
        function_instances_.emplace(key, index);
        model_.functions.push_back(std::move(info));
        model_.instantiated.push_back(std::move(cloned));
        pending_functions_.push_back(PendingFunction{instance, bindings});
        --instantiation_depth_;
        return index;
    }

    Type SemanticAnalyzer::check_generic_call(CallExpr& call, FuncDeclStmt& tmpl, std::optional<Type> expected)
    {
        if (call.has_named_arguments())
        {
            diagnostics_.report(DiagId::NamedArgumentsNotAllowed, call.span);
        }
        if (call.arguments.size() != tmpl.params.size())
        {
            diagnostics_.report(DiagId::ArgumentCount, call.span, call.callee, tmpl.params.size(),
                call.arguments.size());
            check_remaining(call, 0);
            return Type::error();
        }
        // The arguments are checked once, without hints; their types deduce the type parameters.
        std::vector<Type> args;
        for (ExprPtr& argument : call.arguments)
        {
            args.push_back(check_expr(*argument));
        }
        TypeBindings bindings;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            unify(tmpl.params[i].type, args[i], tmpl.type_params, bindings);
        }
        // What the arguments left open, the type the result is assigned to may close:
        // "var s: Stack<int> = new_stack()".
        if (tmpl.return_type && expected)
        {
            unify(*tmpl.return_type, *expected, tmpl.type_params, bindings);
        }
        for (const std::string& param : tmpl.type_params)
        {
            if (!bindings.contains(param))
            {
                // An argument that already failed suppresses this follow-up, as elsewhere.
                const bool cascaded = std::ranges::any_of(args, [](const Type& t) { return t.is_error(); });
                if (!cascaded)
                {
                    diagnostics_.report(DiagId::CannotDeduceTypeParameter, call.callee_span, param, tmpl.name);
                }
                return Type::error();
            }
        }
        const int index = instantiate_function(tmpl, bindings, call.span);
        if (index < 0)
        {
            return Type::error();
        }
        call.function_index = index;
        const FunctionInfo& function = model_.functions[static_cast<std::size_t>(index)];
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const Type& hint = function.parameter_types[i];
            const Type argument = coerce(*call.arguments[i], hint, args[i]);
            if (!argument.is_error() && !assignable(hint, argument))
            {
                diagnostics_.report(DiagId::ArgumentType, call.arguments[i]->span, i + 1, call.callee,
                    type_name(hint), type_name(argument));
            }
        }
        return function.return_type;
    }

    namespace
    {
        // A literal that needs its context to have a type (`nothing`, `[]`, `{}`) cannot help a
        // deduction; it is checked later, against the instance's field.
        bool needs_context(const Expr& written)
        {
            return written.kind == ExprKind::NothingLiteral
                || (written.kind == ExprKind::ArrayLiteral && static_cast<const ArrayLiteralExpr&>(written).elements.empty())
                || (written.kind == ExprKind::MapLiteral && static_cast<const MapLiteralExpr&>(written).keys.empty());
        }

        // The field a construction argument initialises: by name, or by position.
        const FieldDecl* field_of_argument(const CallExpr& call, const StructDeclStmt& tmpl, std::size_t i)
        {
            if (!call.has_named_arguments())
            {
                return i < tmpl.fields.size() ? &tmpl.fields[i] : nullptr;
            }
            const std::string& name = i < call.argument_names.size() ? call.argument_names[i] : std::string();
            const auto found = std::ranges::find_if(tmpl.fields, [&](const FieldDecl& candidate) { return candidate.name == name; });
            return found == tmpl.fields.end() ? nullptr : &*found;
        }
    }

    /**
     * Checks the arguments of a generic construction and binds the type parameters from them;
     * an argument that could not help (see needs_context) is left unchecked, as nullopt.
     */
    std::vector<std::optional<Type>> SemanticAnalyzer::deduce_from_arguments(CallExpr& call, StructDeclStmt& tmpl,
        TypeBindings& bindings)
    {
        std::vector<std::optional<Type>> args;
        for (std::size_t i = 0; i < call.arguments.size(); ++i)
        {
            if (needs_context(*call.arguments[i]))
            {
                args.emplace_back(std::nullopt);
                continue;
            }
            args.push_back(check_expr(*call.arguments[i]));
            const FieldDecl* field = field_of_argument(call, tmpl, i);
            if (field != nullptr && args.back().has_value())
            {
                unify(field->type, *args.back(), tmpl.type_params, bindings);
            }
        }
        return args;
    }

    Type SemanticAnalyzer::check_generic_construct(CallExpr& call, StructDeclStmt& tmpl, std::optional<Type> expected)
    {
        TypeBindings bindings;
        // The type being assigned to decides first: "var s: Stack<int> = Stack(items: [])".
        if (expected && expected->is_struct() && expected->dimensions == 0)
        {
            const StructInfo& info = *model_.structs[static_cast<std::size_t>(expected->struct_index)];
            if (info.template_name == tmpl.name && info.type_args.size() == tmpl.type_params.size())
            {
                for (std::size_t i = 0; i < tmpl.type_params.size(); ++i)
                {
                    bindings.emplace(tmpl.type_params[i], info.type_args[i]);
                }
                return check_construct(call, info); // arguments are checked once, with field hints
            }
        }
        // Otherwise the arguments decide: each one is matched with the field it initialises -
        // or, when the struct has a constructor, with the constructor's parameter.
        const FuncDeclStmt* tmpl_ctor = nullptr;
        for (const auto& method : tmpl.methods)
        {
            if (method->is_constructor)
            {
                tmpl_ctor = method.get();
                break;
            }
        }
        std::vector<std::optional<Type>> args;
        if (tmpl_ctor != nullptr)
        {
            for (std::size_t i = 0; i < call.arguments.size(); ++i)
            {
                if (needs_context(*call.arguments[i]))
                {
                    args.emplace_back(std::nullopt);
                    continue;
                }
                args.push_back(check_expr(*call.arguments[i]));
                if (i < tmpl_ctor->params.size() && args.back().has_value())
                {
                    unify(tmpl_ctor->params[i].type, *args.back(), tmpl.type_params, bindings);
                }
            }
        }
        else
        {
            args = deduce_from_arguments(call, tmpl, bindings);
        }
        for (const std::string& param : tmpl.type_params)
        {
            if (!bindings.contains(param))
            {
                const bool cascaded = std::ranges::any_of(
                    args, [](const std::optional<Type>& t) { return t && t->is_error(); });
                if (!cascaded)
                {
                    diagnostics_.report(DiagId::CannotDeduceTypeParameter, call.callee_span, param, tmpl.name);
                }
                return Type::error();
            }
        }
        std::vector<Type> ordered;
        for (const std::string& param : tmpl.type_params)
        {
            ordered.push_back(bindings.at(param));
        }
        const int index = instantiate_struct(tmpl, std::move(ordered), call.span);
        if (index < 0)
        {
            return Type::error();
        }
        // The arguments were already checked for deduction; check_construct maps them to fields
        // and verifies them against the instance without visiting them again.
        return check_construct(call, *model_.structs[static_cast<std::size_t>(index)], &args);
    }

    namespace
    {
        struct Signature
        {
            std::vector<Type> parameters;
            Type result;
        };

        // The built-ins whose parameter and result types never vary (the others are generic),
        // as a table: one row per signature, the built-ins that share it listed together.
        const std::unordered_map<BuiltinId, Signature>& fixed_signatures()
        {
            static const std::unordered_map<BuiltinId, Signature> table = []
            {
                const Type i = Type::int_type();
                const Type f = Type::float_type();
                const Type b = Type::bool_type();
                const Type s = Type::string_type();
                const Type v = Type::void_type();
                const Type strings = Type::array_of(s);
                const Type y = Type::byte_type();
                const Type bytes = Type::array_of(y);
                // (A vector, not an initializer_list: the latter's backing array would not
                // outlive the statement that builds the rows.)
                using Ids = std::vector<BuiltinId>;
                const std::vector<std::pair<Ids, Signature>> rows = {
                    {{BuiltinId::Input}, {{s}, s}},
                    {{BuiltinId::EndOfInput}, {{}, b}},
                    {{BuiltinId::Substring}, {{s, i, i}, s}},
                    {{BuiltinId::Trim, BuiltinId::ToUpper, BuiltinId::ToLower}, {{s}, s}},
                    {{BuiltinId::Split}, {{s, s}, strings}},
                    {{BuiltinId::Join}, {{strings, s}, s}},
                    {{BuiltinId::Replace}, {{s, s, s}, s}},
                    {{BuiltinId::StartsWith, BuiltinId::EndsWith}, {{s, s}, b}},
                    {{BuiltinId::Repeat}, {{s, i}, s}},
                    {{BuiltinId::CharCode}, {{s}, i}},
                    {{BuiltinId::Hex, BuiltinId::Bin, BuiltinId::FromCharCode}, {{i}, s}},
                    {{BuiltinId::IsInt, BuiltinId::IsNumber}, {{s}, b}},
                    // The string module's forms of len, contains, index_of, reverse, slice,
                    // to_int and to_float: text only, so their signatures are fixed.
                    {{BuiltinId::StringLen, BuiltinId::StringToInt}, {{s}, i}},
                    {{BuiltinId::StringToFloat}, {{s}, f}},
                    {{BuiltinId::StringContains}, {{s, s}, b}},
                    {{BuiltinId::StringIndexOf}, {{s, s}, i}},
                    {{BuiltinId::StringReverse}, {{s}, s}},
                    {{BuiltinId::StringSlice}, {{s, i, i}, s}},
                    {{BuiltinId::Round, BuiltinId::Floor, BuiltinId::Ceil}, {{f}, i}},
                    {{BuiltinId::Sqrt, BuiltinId::Exp, BuiltinId::Log, BuiltinId::Sin, BuiltinId::Cos, BuiltinId::Tan},
                        {{f}, f}},
                    {{BuiltinId::Pow}, {{f, f}, f}},
                    {{BuiltinId::Pi, BuiltinId::Random}, {{}, f}},
                    {{BuiltinId::Range}, {{i, i}, Type::array_of(i)}},
                    {{BuiltinId::Args}, {{}, strings}},
                    {{BuiltinId::GetEnv, BuiltinId::ReadFile}, {{s}, s}},
                    {{BuiltinId::Run}, {{s}, i}},
                    {{BuiltinId::RunOutput}, {{s}, Type::tuple_of({i, s})}},
                    {{BuiltinId::WriteFile, BuiltinId::AppendFile}, {{s, s}, v}},
                    {{BuiltinId::FileExists}, {{s}, b}},
                    {{BuiltinId::RemoveFile}, {{s}, v}},
                    {{BuiltinId::ListDir}, {{s}, strings}},
                    {{BuiltinId::CurrentDir, BuiltinId::DateTime}, {{}, s}},
                    {{BuiltinId::ToByte}, {{i}, y}},
                    {{BuiltinId::StringToBytes}, {{s}, bytes}},
                    {{BuiltinId::BytesToString}, {{bytes}, s}},
                    {{BuiltinId::BytesToInt}, {{bytes, i, i, b, b}, i}},
                    {{BuiltinId::IntToBytes}, {{i, i, b}, bytes}},
                    {{BuiltinId::ReadBytes}, {{s}, bytes}},
                    {{BuiltinId::WriteBytes, BuiltinId::AppendBytes}, {{s, bytes}, v}},
                    {{BuiltinId::Time, BuiltinId::ClockMs}, {{}, i}},
                    {{BuiltinId::Sleep, BuiltinId::Exit, BuiltinId::SeedRandom, BuiltinId::FrameRate}, {{i}, v}},
                    {{BuiltinId::TextSize, BuiltinId::LineWidth}, {{f}, v}},
                    {{BuiltinId::RandomInt}, {{i, i}, i}},
                    {{BuiltinId::Canvas}, {{i, i}, v}},
                    {{BuiltinId::Clear}, {{s}, v}},
                    // Coordinates and sizes are decimals; whole numbers are widened, so every
                    // existing program still compiles and circle(x, y, 2.5, c) works too.
                    {{BuiltinId::Pixel}, {{f, f, s}, v}},
                    {{BuiltinId::Line, BuiltinId::Rect, BuiltinId::RectOutline}, {{f, f, f, f, s}, v}},
                    {{BuiltinId::Circle, BuiltinId::CircleOutline}, {{f, f, f, s}, v}},
                    {{BuiltinId::Text}, {{f, f, s, s}, v}},
                    {{BuiltinId::Rgb}, {{i, i, i}, s}},
                    {{BuiltinId::Frame, BuiltinId::Show}, {{}, v}},
                    {{BuiltinId::KeyPressed}, {{}, s}},
                    {{BuiltinId::KeyDown}, {{s}, b}},
                    {{BuiltinId::MouseX, BuiltinId::MouseY}, {{}, i}},
                    {{BuiltinId::MouseDown}, {{}, b}},
                    // module task: the concurrency kernel. Tasks, locks and conditions are
                    // named by whole numbers, as sockets are (import "concurrency" for objects).
                    {{BuiltinId::TaskSpawn}, {{Type::function_of({}, v)}, i}},
                    {{BuiltinId::TaskJoin, BuiltinId::TaskSleep, BuiltinId::TaskLock, BuiltinId::TaskUnlock,
                         BuiltinId::TaskSignal, BuiltinId::TaskBroadcast},
                        {{i}, v}},
                    {{BuiltinId::TaskDone, BuiltinId::TaskTryLock}, {{i}, b}},
                    {{BuiltinId::TaskYield}, {{}, v}},
                    {{BuiltinId::TaskCurrent, BuiltinId::TaskCount, BuiltinId::TaskMutex, BuiltinId::TaskCondition},
                        {{}, i}},
                    {{BuiltinId::TaskName}, {{i}, s}},
                    {{BuiltinId::TaskWait}, {{i, i}, v}},
                    {{BuiltinId::Fail}, {{s}, v}},
                };
                std::unordered_map<BuiltinId, Signature> built;
                for (const auto& [ids, signature] : rows)
                {
                    for (const BuiltinId id : ids)
                    {
                        built.emplace(id, signature);
                    }
                }
                return built;
            }();
            return table;
        }

        std::optional<Signature> fixed_signature(BuiltinId id)
        {
            const auto& table = fixed_signatures();
            const auto found = table.find(id);
            return found == table.end() ? std::nullopt : std::optional(found->second);
        }

        // Types whose values can be compared with == (as the operator allows).
        bool comparable(const Type& type)
        {
            return type.is_number() || type.is_bool() || type.is_string() || type.is_tuple() || type.is_byte() || type.is_enum();
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

        if (const std::optional<Type> core = check_core_builtin(call, id, name, expected))
        {
            return *core;
        }
        switch (id)
        {
        case BuiltinId::Has:
        case BuiltinId::Remove:
        case BuiltinId::Keys:
        case BuiltinId::Values:
        case BuiltinId::Items:
        case BuiltinId::Get:
            return check_map_builtin(call, id, name);
        case BuiltinId::ToInt:
        case BuiltinId::ToFloat:
        case BuiltinId::Abs:
        case BuiltinId::Min:
        case BuiltinId::Max:
        case BuiltinId::Sum:
            return check_number_builtin(call, id, name, expected);
        case BuiltinId::Array:
            return check_array_constructor(call, name, expected);
        case BuiltinId::NameOf:
        {
            // name_of(geometry.area): any function value; its declared name, module and all.
            const Type argument = check_expr(*call.arguments[0]);
            if (!argument.is_error() && !argument.is_function())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
                return Type::error();
            }
            return Type::string_type();
        }
        case BuiltinId::Platform:
        {
            // platform() answers with a member of the Platform enum, which "import os" brought in.
            for (const auto& info : model_.enums)
            {
                if (info->builtin == static_cast<std::int32_t>(BuiltinEnumId::Platform))
                {
                    return Type::enum_type(info->index, &info->name);
                }
            }
            return Type::error();
        }
        default:
            return check_generic_builtin(call, id, expected);
        }
    }

    /** print, println, dump, str, inspect, copy, format and len: anything goes in. Nullopt for the rest. */
    std::optional<Type> SemanticAnalyzer::check_core_builtin(CallExpr& call, BuiltinId id, const std::string& name,
        std::optional<Type> expected)
    {
        switch (id)
        {
        case BuiltinId::Print:
        case BuiltinId::Println:
        case BuiltinId::Dump:
            check_remaining(call, 0);
            return Type::void_type();
        case BuiltinId::Str:
        case BuiltinId::Inspect:
            check_remaining(call, 0);
            return Type::string_type();
        case BuiltinId::Copy:
            return check_expr(*call.arguments[0], expected);
        case BuiltinId::Format:
            return check_format_builtin(call, name);
        case BuiltinId::Len:
            return check_len_builtin(call, name);
        default:
            return std::nullopt;
        }
    }

    Type SemanticAnalyzer::check_format_builtin(CallExpr& call, const std::string& name)
    {
        const Type template_type = check_expr(*call.arguments[0]);
        if (!template_type.is_error() && !template_type.is_string())
        {
            diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(template_type));
        }
        check_remaining(call, 1);
        return Type::string_type();
    }

    Type SemanticAnalyzer::check_len_builtin(CallExpr& call, const std::string& name)
    {
        const Type argument = check_expr(*call.arguments[0]);
        const bool measurable = argument.is_string() || argument.is_array() || argument.is_tuple() || argument.is_map();
        if (!argument.is_error() && !measurable)
        {
            diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
            return Type::error();
        }
        return Type::int_type();
    }

    /** has, remove, keys, values, items and get: the first argument is the map. */
    Type SemanticAnalyzer::check_map_builtin(CallExpr& call, BuiltinId id, const std::string& name)
    {
        const Type map = check_expr(*call.arguments[0]);
        if (map.is_error() || !map.is_map())
        {
            if (!map.is_error())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(map));
            }
            check_remaining(call, 1);
            return Type::error();
        }
        switch (id)
        {
        case BuiltinId::Keys:
            return Type::array_of(map.key_type());
        case BuiltinId::Values:
            return Type::array_of(map.value_type());
        case BuiltinId::Items:
            return Type::array_of(Type::tuple_of({map.key_type(), map.value_type()}));
        default:
            break;
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
            return check_arguments(call, {map, map.key_type(), map.value_type()}, 2) ? map.value_type() : Type::error();
        }
        return id == BuiltinId::Has ? Type::bool_type() : Type::void_type();
    }

    /** to_int, to_float, abs, min, max and sum: numbers in, and the type depends on what came in. */
    Type SemanticAnalyzer::check_number_builtin(CallExpr& call, BuiltinId id, const std::string& name,
        std::optional<Type> expected)
    {
        switch (id)
        {
        case BuiltinId::ToInt:
        case BuiltinId::ToFloat:
        {
            // Both read a number from text, and both also accept a number (to_int(2.9) is 2);
            // to_int also takes a byte (its value) and an enum member (its position).
            const Type argument = check_expr(*call.arguments[0]);
            const bool extra = id == BuiltinId::ToInt && (argument.is_byte() || argument.is_enum());
            if (!argument.is_error() && !argument.is_string() && !argument.is_number() && !extra)
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
                return Type::error();
            }
            return id == BuiltinId::ToFloat ? Type::float_type() : Type::int_type();
        }
        case BuiltinId::Abs:
        {
            const Type argument = check_expr(*call.arguments[0], expected);
            if (!argument.is_error() && !argument.is_number())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(argument));
            }
            return argument.is_number() ? argument : Type::error(); // abs keeps the kind of number
        }
        case BuiltinId::Sum:
        {
            const auto element = array_argument(call, 0,
                expected && expected->is_number() ? std::optional(Type::array_of(*expected)) : std::nullopt);
            if (!element)
            {
                return Type::error();
            }
            if (element->is_byte())
            {
                return Type::int_type(); // the sum of bytes is a whole number
            }
            if (!element->is_number())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name,
                    type_name(Type::array_of(*element)));
                return Type::error();
            }
            return *element;
        }
        default: // Min, Max
            return check_min_max(call, name, expected);
        }
    }

    Type SemanticAnalyzer::check_min_max(CallExpr& call, const std::string& name, std::optional<Type> expected)
    {
        Type first = check_expr(*call.arguments[0], expected);
        Type second = check_expr(*call.arguments[1], first.is_error() ? expected : std::optional(first));
        if (first.is_error() || second.is_error())
        {
            return Type::error();
        }
        if (first.is_byte() && (second.is_number() || second.is_byte()))
        {
            first = coerce(*call.arguments[0], second.is_float() ? Type::float_type() : Type::int_type(), first);
        }
        if (second.is_byte() && first.is_number())
        {
            second = coerce(*call.arguments[1], first.is_float() ? Type::float_type() : Type::int_type(), second);
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
        diagnostics_.report(DiagId::ArgumentType, call.arguments[1]->span, 2, name, type_name(first), type_name(second));
        return Type::error();
    }

    /** array(size, value): an array of 'size' copies of 'value'. */
    Type SemanticAnalyzer::check_array_constructor(CallExpr& call, const std::string& name, std::optional<Type> expected)
    {
        const Type size = check_expr(*call.arguments[0]);
        if (!size.is_error() && !size.is_int())
        {
            diagnostics_.report(DiagId::ArgumentType, call.arguments[0]->span, 1, name, type_name(Type::int_type()),
                type_name(size));
        }
        const std::optional<Type> hint = expected && expected->is_array() ? std::optional(expected->element()) : std::nullopt;
        const Type element = check_expr(*call.arguments[1], hint);
        if (element.is_error() || !size.is_int())
        {
            return Type::error();
        }
        return Type::array_of(element);
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

    /**
     * A built-in written as a value - map(pairs, second), for_each(names, println) - becomes a
     * function value: a one-line function that passes its parameters straight on to the
     * built-in. Built-ins are not values (they have no address and many are generic), but the
     * shape wanted here says exactly which call to write, so the compiler writes it.
     */
    void SemanticAnalyzer::wrap_builtin_value(ExprPtr& slot, const Type& shape)
    {
        if (!slot || slot->kind != ExprKind::Name || !shape.is_function() || shape.dimensions != 0)
        {
            return;
        }
        auto& name = static_cast<NameExpr&>(*slot);
        const std::optional<BuiltinId> id = builtin_named(name.name);
        if (!id || lookup(name.name) != nullptr)
        {
            return; // a variable of that name wins, as everywhere
        }
        const BuiltinInfo& info = builtin_info(*id);
        const std::vector<Type>& parameters = shape.parameter_types();
        const int count = static_cast<int>(parameters.size());
        if (count < info.min_arity || (info.arity != variadic && count > info.arity))
        {
            return; // the wrong number of arguments: reported against the built-in's own shape
        }
        if (std::ranges::any_of(parameters, [](const Type& t) { return t.is_error() || t.is_void(); }))
        {
            return;
        }
        const Span span = name.span;
        auto lambda = std::make_unique<LambdaExpr>(span);
        auto callee = std::make_unique<CallExpr>(span, name.name, span);
        for (std::size_t i = 0; i < parameters.size(); ++i)
        {
            Param param;
            param.name = std::format("({} {})", name.name, i + 1); // never a name a program can write
            param.name_span = span;
            param.type.type = parameters[i];
            param.type.span = span;
            lambda->params.push_back(std::move(param));
            callee->arguments.push_back(std::make_unique<NameExpr>(span, lambda->params.back().name));
            callee->argument_names.emplace_back();
        }
        auto body = std::make_unique<BlockStmt>(span);
        auto ret = std::make_unique<ReturnStmt>(span, std::move(callee));
        ret->implicit = true; // a built-in that answers with nothing is simply carried out
        body->statements.push_back(std::move(ret));
        body->span = span;
        lambda->body = std::move(body);
        // The parameter types are written out above; only the result still has to be worked out.
        if (!shape.result_type().is_error() && !shape.result_type().is_void())
        {
            TypeSyntax result;
            result.type = shape.result_type();
            result.span = span;
            lambda->return_type = std::move(result);
        }
        lambda->deduced = true; // its types are not resolved again: they are already types
        slot = std::move(lambda);
    }

    Type SemanticAnalyzer::check_function_argument(CallExpr& call, std::size_t index, const std::vector<Type>& parameters,
        std::optional<Type> result, bool any_result)
    {
        // The shape the function must have. A result the built-in cannot know (map's) is written
        // as the error type: the function value then works it out from its own body.
        const Type shape = Type::function_of(parameters, result ? *result : Type::error());
        wrap_builtin_value(call.arguments[index], shape);
        const Type argument = check_expr(*call.arguments[index], shape);
        if (argument.is_error())
        {
            return Type::error();
        }
        bool fits = argument.is_function() && argument.parameter_types().size() == parameters.size();
        for (std::size_t i = 0; fits && i < parameters.size(); ++i)
        {
            fits = assignable(parameters[i], argument.parameter_types()[i]);
        }
        if (fits && !any_result)
        {
            fits = result ? assignable(*result, argument.result_type()) : !argument.result_type().is_void();
        }
        if (fits)
        {
            return argument.result_type();
        }
        // Describe the expected type; "?" stands for a result of any type.
        std::string wanted = Type::function_of(parameters, Type::void_type()).to_string(&language_);
        if (!any_result && (!result || !result->is_void()))
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
        switch (id)
        {
        case BuiltinId::Contains:
        case BuiltinId::IndexOf:
        case BuiltinId::Reverse:
        case BuiltinId::Slice:
            return check_sequence_builtin(call, id, name, expected);
        case BuiltinId::First:
        case BuiltinId::Second:
        case BuiltinId::Swap:
            return check_tuple_builtin(call, id, name);
        case BuiltinId::Zip:
        {
            const auto left = array_argument(call, 0);
            const auto right = array_argument(call, 1);
            return left && right ? Type::array_of(Type::tuple_of({*left, *right})) : Type::error();
        }
        case BuiltinId::Enumerate:
        {
            const auto element = array_argument(call, 0);
            return element ? Type::array_of(Type::tuple_of({Type::int_type(), *element})) : Type::error();
        }
        case BuiltinId::Push:
        case BuiltinId::Pop:
        case BuiltinId::Insert:
        case BuiltinId::RemoveAt:
        case BuiltinId::Concat:
        case BuiltinId::Sort:
            return check_list_builtin(call, id, name, expected);
        case BuiltinId::Map:
        case BuiltinId::Filter:
        case BuiltinId::Reduce:
        case BuiltinId::ForEach:
            return check_higher_order_builtin(call, id, expected);
        default:
            throw std::logic_error("semantic analysis: unhandled built-in " + std::string(builtin_info(id).canonical_name));
        }
    }

    /** contains, index_of, reverse and slice: a string (searched for a part) or an array (for an element). */
    Type SemanticAnalyzer::check_sequence_builtin(CallExpr& call, BuiltinId id, const std::string& name,
        std::optional<Type> expected)
    {
        Type target = check_expr(*call.arguments[0], expected);
        if (target.is_error())
        {
            check_remaining(call, 1);
            return Type::error();
        }
        const bool searching = id == BuiltinId::Contains || id == BuiltinId::IndexOf;
        const bool searchable = target.is_string() || (target.is_array() && comparable(target.element()));
        const bool sequence = target.is_string() || target.is_array();
        if (!(searching ? searchable : sequence))
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
            return check_arguments(call, {target, Type::int_type(), Type::int_type()}, 1) ? target : Type::error();
        }
        const Type item = target.is_string() ? target : target.element();
        if (!check_arguments(call, {target, item}, 1))
        {
            return Type::error();
        }
        return id == BuiltinId::Contains ? Type::bool_type() : Type::int_type();
    }

    /** first, second and swap: a tuple's elements. */
    Type SemanticAnalyzer::check_tuple_builtin(CallExpr& call, BuiltinId id, const std::string& name)
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

    /** push, pop, insert, remove_at, concat and sort: the first argument is the array. */
    Type SemanticAnalyzer::check_list_builtin(CallExpr& call, BuiltinId id, const std::string& name,
        std::optional<Type> expected)
    {
        const auto element = array_argument(call, 0, id == BuiltinId::Concat ? expected : std::nullopt);
        if (!element)
        {
            check_remaining(call, 1);
            return Type::error();
        }
        Type array = Type::array_of(*element);
        const Type int_type = Type::int_type();
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
            if (!element->is_number() && !element->is_string() && !element->is_byte() && !element->is_enum())
            {
                diagnostics_.report(DiagId::BuiltinArgumentType, call.arguments[0]->span, name, type_name(array));
                return Type::error();
            }
            return array;
        }
    }

    /** map, filter and reduce: an array and a function over its elements. */
    Type SemanticAnalyzer::check_higher_order_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected)
    {
        const auto element = array_argument(call, 0);
        if (id == BuiltinId::Reduce)
        {
            const Type initial = check_expr(*call.arguments[1], expected);
            if (!element || initial.is_error())
            {
                check_remaining(call, 2);
                return Type::error();
            }
            const Type result = check_function_argument(call, 2, {initial, *element}, initial);
            return result.is_error() ? result : initial;
        }
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
        if (id == BuiltinId::ForEach)
        {
            // The function is called for what it does, so it may answer with anything - or with
            // nothing, which is the usual case (for_each(names, println)).
            const Type result = check_function_argument(call, 1, {*element}, Type::void_type(), true);
            return result.is_error() ? result : Type::void_type();
        }
        const Type keep = check_function_argument(call, 1, {*element}, Type::bool_type());
        return keep.is_error() ? keep : Type::array_of(*element);
    }

    // ------------------------------------------------------------------ helpers

    bool SemanticAnalyzer::assignable(const Type& target, const Type& value) const
    {
        if (target.is_error() || value.is_error() || target == value)
        {
            return true;
        }
        // A struct is assignable to any interface it implements (the value stays the struct;
        // calling a member through the interface finds the struct's own method at run time).
        if (target.is_interface() && value.is_struct())
        {
            return model_.structs[static_cast<std::size_t>(value.struct_index)]->implements(target.struct_index);
        }
        // An interface fits the interfaces it extends: every value behind it has their members.
        if (target.is_interface() && value.is_interface() && target.dimensions == 0 && value.dimensions == 0)
        {
            const std::vector<int>& bases = model_.interfaces[static_cast<std::size_t>(value.struct_index)]->bases;
            return std::ranges::find(bases, target.struct_index) != bases.end();
        }
        return false;
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
        // A byte widens to an int the same way (and on to a float: BYTE_TO_INT, then INT_TO_FLOAT).
        if (actual.is_byte() && (target.is_int() || target.is_float()))
        {
            expr.widen_to_int = true;
            expr.widen_to_float = target.is_float();
            expr.type = target;
            return target;
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
        case StmtKind::With:
            return always_returns(*static_cast<const WithStmt&>(stmt).body);
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
        case StmtKind::With:
            return terminates(*static_cast<const WithStmt&>(stmt).body);
        default:
            return false;
        }
    }
}
