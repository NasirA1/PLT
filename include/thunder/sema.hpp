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
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <string>
#include <unordered_map>
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

        int field_index(std::string_view field) const; // -1 if there is no such field
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
        std::vector<FunctionInfo> functions;           // [0] is <main>, then user functions in source order
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

        // Modules: gathers the items of the program and of every imported file (each once).
        void flatten(ProgramNode& program, bool is_module, std::unordered_set<const ProgramNode*>& seen);
        // The built-in with this name, if the program may use it (os functions need "import os").
        std::optional<BuiltinId> builtin_named(std::string_view name) const;

        // Declarations
        void collect_structs();
        void check_recursive_structs();
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
        Type check_binary(BinaryExpr& binary);
        Type check_call(CallExpr& call, std::optional<Type> expected, bool value_required);
        Type check_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected);
        Type check_generic_builtin(CallExpr& call, BuiltinId id, std::optional<Type> expected);
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
        Type check_construct(CallExpr& call, const StructInfo& info);
        Type check_field(FieldExpr& field);
        Type check_map_literal(MapLiteralExpr& map, std::optional<Type> expected);
        Type check_name(NameExpr& name);
        std::string type_name(const Type& type) const { return type.to_string(&language_); }

        static bool assignable(const Type& target, const Type& value);
        // Widens 'expr' from int to float if that is what 'target' needs; returns the type it now has.
        static Type coerce(Expr& expr, const Type& target, const Type& actual);

        const LanguagePack& language_;
        DiagnosticBag& diagnostics_;
        SemanticModel model_;
        std::unordered_map<std::string, int> function_indexes_;
        std::unordered_map<std::string, int> native_indexes_;
        std::unordered_map<std::string, int> struct_indexes_;
        std::vector<Scope> scopes_;
        std::vector<Stmt*> items_; // see SemanticModel::items
        int current_function_ = 0;
        int loop_depth_ = 0;
    };
}
