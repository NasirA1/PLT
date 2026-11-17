// Semantic analysis: name resolution and type checking.
//
// The analyser walks the AST, records every declared name in a symbol table organised as nested
// scopes, links each use of a name to its declaration, computes the type of every expression and
// reports anything that is syntactically valid but meaningless (e.g. adding a bool to a string).
#pragma once

#include "thunder/ast.hpp"
#include "thunder/diagnostics.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/types.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <vector>

namespace thunder
{
    enum class SymbolKind : std::uint8_t
    {
        Global,    // variable declared at the top level of the program
        Local,     // variable declared inside a function or a block
        Parameter, // function parameter
    };

    std::string_view symbol_kind_name(SymbolKind kind);

    struct Symbol
    {
        std::string name;
        SymbolKind kind = SymbolKind::Local;
        Type type;
        bool is_const = false;
        std::uint16_t slot = 0; // index into the globals table, or into the function's local slots
        int function_index = 0; // owning function (locals and parameters)
        Span span;
        bool used = false;
    };

    struct FieldInfo
    {
        std::string name;
        Type type;
        Span span;
    };

    struct StructInfo
    {
        std::string name;
        std::vector<FieldInfo> fields;
        const StructDeclStmt* decl = nullptr;
        Span span;
        int index = -1; // this struct's place in SemanticModel::structs
        // For an instance of a generic struct ("Stack[int]"): the template's name and the
        // concrete type arguments, so uses of the template unify against it.
        std::string template_name;
        std::vector<Type> type_args;

        int field_index(std::string_view field) const; // -1 if there is no such field
    };

    // An enum: a type whose values are its named members, each with a position (0, 1, 2, ...).
    struct EnumInfo
    {
        std::string name;
        std::vector<std::string> members;
        std::vector<Span> member_spans;
        const EnumDeclStmt* decl = nullptr; // null for a built-in enum (Color, Platform)
        std::int32_t builtin = -1;          // the BuiltinEnumId of a built-in enum, else -1
        Span span;
        int index = -1; // this enum's place in SemanticModel::enums

        int member_index(std::string_view member) const; // -1 if there is no such member
    };

    struct FunctionInfo
    {
        std::string name; // "<main>" for the top-level code
        std::vector<Type> parameter_types;
        Type return_type = Type::void_type();
        const FuncDeclStmt* decl = nullptr; // null for <main>
        std::uint16_t local_count = 0;      // parameters + locals
        std::vector<const Symbol*> locals;  // in slot order (parameters first)
        Span span;
    };

    // A scope as presented to students: what was declared where.
    struct ScopeInfo
    {
        std::string kind; // "global", "function", "block" or "for"
        std::string name; // function name for function scopes
        Span span;
        std::vector<const Symbol*> symbols;
        std::vector<ScopeInfo> children;
    };

    // A "native func" declaration: its body is in a C++ library (see docs/native.md).
    struct NativeInfo
    {
        std::string name;
        std::vector<Type> parameter_types;
        Type return_type = Type::void_type();
        const FuncDeclStmt* decl = nullptr;
        Span span;
    };

    struct SemanticModel
    {
        std::vector<std::unique_ptr<Symbol>> symbols; // owns every symbol
        std::vector<std::unique_ptr<StructInfo>> structs; // in source order (stable addresses for Type::struct_name)
        std::vector<std::unique_ptr<EnumInfo>> enums;     // built-in enums of the imported modules first, then the program's
        // [0] is <main>, then user functions in source order, then generic instances. A deque,
        // not a vector: checking a body may instantiate a generic (which appends here) while a
        // reference to the current function is held, and deque growth never moves elements.
        std::deque<FunctionInfo> functions;
        std::vector<const Symbol*> globals;            // in slot order
        ScopeInfo global_scope;
        int main_function = -1; // index of a user-defined main(), called after the top-level code
        // Every top-level item of the program and of the modules it imports, in execution order:
        // a module's items come before those of the file that first imports it.
        std::vector<const Stmt*> items;
        std::set<BuiltinModule> imported_modules; // built-in modules the program imports (import os)
        // import native "name", in order and without repeats, and the native functions declared.
        std::vector<std::string> native_libraries;
        std::vector<NativeInfo> natives;
        // The bodies of instantiated generic functions (fresh trees the code generator walks
        // after 'items'); the model owns them.
        std::vector<StmtPtr> instantiated;
    };

    // The ThunderKind (see include/thunder/native.h) of a type that may cross the native
    // boundary, or nullopt for a type that may not. Functions are only allowed as parameters.
    std::optional<std::uint8_t> native_kind(const Type& type, bool allow_function);

    // True if control never reaches the end of 'stmt' because every path returns.
    bool always_returns(const Stmt& stmt);

    // True if control never falls through 'stmt' (every path returns, breaks or continues).
    bool terminates(const Stmt& stmt);

    class SemanticAnalyzer
    {
    public:
        SemanticAnalyzer(const LanguagePack& language, DiagnosticBag& diagnostics);

        SemanticModel analyze(ProgramNode& program);

    private:
        struct Scope
        {
            std::unordered_map<std::string, Symbol*> names;
            ScopeInfo* info = nullptr;
        };

        // Files: gathers the items of the program and of every imported file (each once).
        void flatten(ProgramNode& program, bool is_module, std::unordered_set<const ProgramNode*>& seen);

        // Modules (modules.cpp). A "module" block is taken apart where it is found: every member
        // gets its qualified name ("geometry.area") and joins the program's items. Then every
        // reference is written out in full before anything is checked: geometry.area(r) becomes
        // a call of "geometry.area", and inside the module an unqualified "area" does too (unless
        // a local of that name hides it). After that, modules are invisible to the rest of the
        // analyser, the code generator and the virtual machine.
        void declare_module(ModuleDeclStmt& module, bool in_imported_file);
        void qualify_references();
        struct Qualifier;
        friend struct Qualifier;
        // The built-in module a name stands for in this program (math; os only when imported).
        // 'gated' is set to the module when the name is one that still needs its import.
        std::optional<BuiltinModule> visible_builtin_module(std::string_view name, std::optional<BuiltinModule>* gated) const;
        bool is_module_name(std::string_view name) const;
        bool names_a_module(std::string_view name) const;
        bool already_reported(const std::string& qualified) const;
        // The module a member's qualified name belongs to ("geometry" for "geometry.area").
        static std::string module_part(std::string_view qualified);
        // The S067 hint for a name used without its module (sqrt -> math.sqrt), if there is one.
        bool report_member_needs_module(const std::string& name, const Span& span);
        void check_with(WithStmt& with);
        // The built-in with this name, if the program may use it (os functions need "import os").
        std::optional<BuiltinId> builtin_named(std::string_view name) const;

        // Declarations
        void collect_structs();
        void collect_enums();
        const EnumInfo* find_enum(std::string_view name) const;
        bool resolve_type(TypeSyntax& syntax); // false (and reported) for an unknown struct name
        const StructInfo* find_struct(std::string_view name) const;
        void collect_functions();
        void collect_native(FuncDeclStmt& func); // a "native func" declaration
        void check_function(FuncDeclStmt& func);
        Symbol* declare(const std::string& name, SymbolKind kind, Type type, bool is_const, const Span& span);
        Symbol* lookup(const std::string& name);
        void push_scope(std::string kind, const Span& span, std::string name = {});
        void pop_scope();

        // Statements
        void check_statement(Stmt& stmt);
        void check_statements(std::vector<StmtPtr>& statements);
        void check_var_decl(VarDeclStmt& decl);
        void check_assign(AssignStmt& assign);
        // "x += e": checks that 'x op e' is valid and fits back into x.
        void check_compound_assign(AssignStmt& assign, const Type& target_type, Type value);
        void check_for_each(ForEachStmt& loop);
        void check_return(ReturnStmt& ret);
        void check_condition(Expr& condition);

        // Expressions
        Type check_expr(Expr& expr, std::optional<Type> expected = std::nullopt, bool value_required = true);
        Type check_nothing(const Expr& expr, std::optional<Type> expected);
        Type check_array_literal(ArrayLiteralExpr& array, std::optional<Type> expected);
        Type check_tuple_literal(TupleLiteralExpr& tuple, std::optional<Type> expected);
        Type check_unary(UnaryExpr& unary);
        Type check_index(IndexExpr& index);
        Type check_tuple_index(TupleIndexExpr& element);
        Type check_binary(BinaryExpr& binary);
        std::optional<std::pair<Type, Type>> check_binary_operands(BinaryExpr& binary);
        static std::optional<Type> binary_result(TokenKind op, const Type& left, const Type& right);
        Type check_call(CallExpr& call, std::optional<Type> expected, bool value_required);
        Type check_callee_expression(CallExpr& call, bool value_required);
        Type check_native_call(CallExpr& call, int native_index);
        Type check_function_call(CallExpr& call, int function_index);
        void report_unknown_callee(CallExpr& call, Symbol* variable);
        // Built-ins, by the shape of their checking: the groups of check_builtin.
        Type check_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected);
        std::optional<Type> check_core_builtin(CallExpr& call, BuiltinId id, const std::string& name, std::optional<Type> expected);
        Type check_format_builtin(CallExpr& call, const std::string& name);
        Type check_len_builtin(CallExpr& call, const std::string& name);
        Type check_map_builtin(CallExpr& call, BuiltinId id, const std::string& name);
        Type check_number_builtin(CallExpr& call, BuiltinId id, const std::string& name, std::optional<Type> expected);
        Type check_min_max(CallExpr& call, const std::string& name, std::optional<Type> expected);
        Type check_array_constructor(CallExpr& call, const std::string& name, std::optional<Type> expected);
        Type check_generic_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected);
        Type check_sequence_builtin(CallExpr& call, BuiltinId id, const std::string& name, std::optional<Type> expected);
        Type check_tuple_builtin(CallExpr& call, BuiltinId id, const std::string& name);
        Type check_list_builtin(CallExpr& call, BuiltinId id, const std::string& name, std::optional<Type> expected);
        Type check_higher_order_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected);
        // Checks that argument 'index' is an array (or reports it); returns its element type.
        std::optional<Type> array_argument(CallExpr& call, std::size_t index, std::optional<Type> expected = std::nullopt);
        Type check_value_call(CallExpr& call, Symbol& symbol);
        // Checks arguments against fixed types (reports S010); false if any is wrong.
        bool check_arguments(CallExpr& call, const std::vector<Type>& types, std::size_t first = 0);
        // Checks that argument 'index' is a function taking 'parameters'; returns its result type
        // (error if it does not fit). 'result' (if given) is the result the function must have.
        Type check_function_argument(CallExpr& call, std::size_t index, const std::vector<Type>& parameters,
            std::optional<Type> result);
        void check_remaining(CallExpr& call, std::size_t from); // checks arguments not yet checked
        Type check_construct(CallExpr& call, const StructInfo& info,
            const std::vector<std::optional<Type>>* precomputed = nullptr);

        // Generics: templates are instantiated once per combination of concrete types.
        using TypeBindings = std::unordered_map<std::string, Type>;
        // Matches a written type ("T", "Stack[T]", "T[]") against a concrete type, filling in
        // the type parameters named in 'params'. False when the shapes cannot match.
        bool unify(const TypeSyntax& pattern, const Type& actual, const std::vector<std::string>& params,
            TypeBindings& bindings);
        bool unify_all(const std::vector<TypeSyntax>& patterns, const std::vector<Type>& actuals,
            const std::vector<std::string>& params, TypeBindings& bindings);
        bool unify_named(const TypeSyntax& pattern, const Type& actual, const std::vector<std::string>& params,
            TypeBindings& bindings);
        std::vector<std::optional<Type>> deduce_from_arguments(CallExpr& call, StructDeclStmt& tmpl, TypeBindings& bindings);
        int instantiate_struct(StructDeclStmt& tmpl, std::vector<Type> args, const Span& span);
        int instantiate_function(FuncDeclStmt& tmpl, const TypeBindings& bindings, const Span& span);
        Type check_generic_call(CallExpr& call, FuncDeclStmt& tmpl, std::optional<Type> expected);
        Type check_generic_construct(CallExpr& call, StructDeclStmt& tmpl, std::optional<Type> expected);
        Type check_field(FieldExpr& field);
        Type check_enum_field(FieldExpr& field, const EnumInfo& info);
        Type check_enum_construct(CallExpr& call, const EnumInfo& info);
        // A graphics built-in takes a Color where it asks for a colour string.
        bool accepts_color(const CallExpr& call, std::size_t index, std::size_t count, const Type& hint, const Type& argument) const;
        Type check_map_literal(MapLiteralExpr& map, std::optional<Type> expected);
        Type check_name(NameExpr& name);
        std::string type_name(const Type& type) const { return type.to_string(&language_); }

        static bool assignable(const Type& target, const Type& value);
        // Widens 'expr' from int to float, or from byte to int or float, if that is what 'target' needs;
        // returns the type it now has.
        static Type coerce(Expr& expr, const Type& target, const Type& actual);

        const LanguagePack& language_;
        DiagnosticBag& diagnostics_;
        SemanticModel model_;
        std::unordered_map<std::string, int> function_indexes_;
        std::unordered_map<std::string, int> native_indexes_;
        std::unordered_map<std::string, int> struct_indexes_;
        std::unordered_map<std::string, int> enum_indexes_;
        std::vector<Scope> scopes_;
        std::vector<Stmt*> items_; // see SemanticModel::items
        int current_function_ = 0;
        int loop_depth_ = 0;

        // Generics
        std::unordered_map<std::string, StructDeclStmt*> generic_structs_;
        std::unordered_map<std::string, FuncDeclStmt*> generic_functions_;
        std::unordered_map<std::string, int> struct_instances_;        // "Stack|int" -> struct index
        std::unordered_map<std::string, int> function_instances_;      // "first|int" -> function index
        TypeBindings bindings_;     // the active type parameters while an instance is resolved
        int instantiation_depth_ = 0; // guards against Stack[Stack[...]] without end
        struct PendingFunction
        {
            FuncDeclStmt* decl;
            TypeBindings bindings;
        };
        std::vector<PendingFunction> pending_functions_; // instance bodies still to check

        // Modules
        std::unordered_map<std::string, const ModuleDeclStmt*> user_modules_; // by (folded) name
        std::unordered_set<std::string> module_members_;                    // "geometry.area", ...
        std::unordered_map<const Stmt*, std::string> module_of_;            // member -> its module
    };
}
