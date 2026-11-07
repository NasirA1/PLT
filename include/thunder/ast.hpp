// The abstract syntax tree (AST) produced by the parser and annotated by semantic analysis.
#pragma once

#include "thunder/builtins.hpp"
#include "thunder/source.hpp"
#include "thunder/token.hpp"
#include "thunder/types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace thunder
{
    struct Symbol; // defined in sema.hpp
    struct LanguagePack;

    // ---------------------------------------------------------------- type annotations

    // A type as written in the source, e.g. "int[]" or "Point[]".
    struct TypeSyntax
    {
        Type type;               // for struct types: resolved by semantic analysis
        std::string struct_name; // non-empty if the base type is a (user-defined) struct name
        std::vector<TypeSyntax> parts; // the element types of a tuple type, or the key and value of a map type
        Span span;
    };

    // ---------------------------------------------------------------- expressions

    enum class ExprKind : std::uint8_t
    {
        IntLiteral,
        BoolLiteral,
        StringLiteral,
        ArrayLiteral,
        TupleLiteral,
        MapLiteral,
        Name,
        Unary,
        Binary,
        Call,
        Index,
        Field,
        TupleIndex,
    };

    struct Expr
    {
        explicit Expr(ExprKind k, Span s)
            : kind(k)
            , span(s)
        {
        }
        virtual ~Expr() = default;
        Expr(const Expr&) = delete;
        Expr& operator=(const Expr&) = delete;

        const ExprKind kind;
        Span span;
        Type type = Type::error(); // filled in by semantic analysis
    };

    using ExprPtr = std::unique_ptr<Expr>;

    struct IntLiteralExpr final : Expr
    {
        IntLiteralExpr(Span s, std::int64_t v)
            : Expr(ExprKind::IntLiteral, s)
            , value(v)
        {
        }
        std::int64_t value;
    };

    struct BoolLiteralExpr final : Expr
    {
        BoolLiteralExpr(Span s, bool v)
            : Expr(ExprKind::BoolLiteral, s)
            , value(v)
        {
        }
        bool value;
    };

    struct StringLiteralExpr final : Expr
    {
        StringLiteralExpr(Span s, std::string v)
            : Expr(ExprKind::StringLiteral, s)
            , value(std::move(v))
        {
        }
        std::string value;
    };

    struct ArrayLiteralExpr final : Expr
    {
        explicit ArrayLiteralExpr(Span s)
            : Expr(ExprKind::ArrayLiteral, s)
        {
        }
        std::vector<ExprPtr> elements;
    };

    // (a, b, c)
    struct TupleLiteralExpr final : Expr
    {
        explicit TupleLiteralExpr(Span s)
            : Expr(ExprKind::TupleLiteral, s)
        {
        }
        std::vector<ExprPtr> elements;
    };

    // {key: value, ...}
    struct MapLiteralExpr final : Expr
    {
        explicit MapLiteralExpr(Span s)
            : Expr(ExprKind::MapLiteral, s)
        {
        }
        std::vector<ExprPtr> keys;
        std::vector<ExprPtr> values; // same length as keys
    };

    struct NameExpr final : Expr
    {
        NameExpr(Span s, std::string n)
            : Expr(ExprKind::Name, s)
            , name(std::move(n))
        {
        }
        std::string name;
        const Symbol* symbol = nullptr; // resolved by semantic analysis
        int function_index = -1;        // set instead of 'symbol' when the name refers to a function (a function value)
    };

    struct UnaryExpr final : Expr
    {
        UnaryExpr(Span s, TokenKind o, ExprPtr e)
            : Expr(ExprKind::Unary, s)
            , op(o)
            , operand(std::move(e))
        {
        }
        TokenKind op; // Minus or Bang
        ExprPtr operand;
    };

    struct BinaryExpr final : Expr
    {
        BinaryExpr(Span s, TokenKind o, ExprPtr l, ExprPtr r)
            : Expr(ExprKind::Binary, s)
            , op(o)
            , left(std::move(l))
            , right(std::move(r))
        {
        }
        TokenKind op;
        Span op_span;
        ExprPtr left;
        ExprPtr right;
    };

    struct CallExpr final : Expr
    {
        CallExpr(Span s, std::string c, Span cs)
            : Expr(ExprKind::Call, s)
            , callee(std::move(c))
            , callee_span(cs)
        {
        }
        std::string callee;
        Span callee_span;
        ExprPtr callee_expr; // set when the callee is not a plain name, e.g. ops[i](x) - it must produce a function value
        std::vector<ExprPtr> arguments;
        std::vector<std::string> argument_names; // "name: value" arguments; empty string if positional
        // Resolved by semantic analysis: exactly one of these is set for a valid call.
        std::optional<BuiltinId> builtin;
        int function_index = -1; // index into the program's function table
        int struct_index = -1;   // set when the call creates a struct value
        const Symbol* callee_symbol = nullptr; // set when the callee is a variable holding a function value
        std::vector<int> argument_fields; // for struct creation: the field each argument initialises

        bool has_named_arguments() const
        {
            for (const std::string& name : argument_names)
            {
                if (!name.empty())
                {
                    return true;
                }
            }
            return false;
        }
    };

    struct IndexExpr final : Expr
    {
        IndexExpr(Span s, ExprPtr t, ExprPtr i)
            : Expr(ExprKind::Index, s)
            , target(std::move(t))
            , index(std::move(i))
        {
        }
        ExprPtr target;
        ExprPtr index;
    };

    // target.field
    struct FieldExpr final : Expr
    {
        FieldExpr(Span s, ExprPtr t, std::string f, Span fs)
            : Expr(ExprKind::Field, s)
            , target(std::move(t))
            , field(std::move(f))
            , field_span(fs)
        {
        }
        ExprPtr target;
        std::string field;
        Span field_span;
        int field_index = -1; // resolved by semantic analysis
    };

    // target.0, target.1, ...: an element of a tuple
    struct TupleIndexExpr final : Expr
    {
        TupleIndexExpr(Span s, ExprPtr t, std::int64_t i, Span is)
            : Expr(ExprKind::TupleIndex, s)
            , target(std::move(t))
            , index(i)
            , index_span(is)
        {
        }
        ExprPtr target;
        std::int64_t index;
        Span index_span;
    };

    // ---------------------------------------------------------------- statements

    enum class StmtKind : std::uint8_t
    {
        VarDecl,
        Assign,
        ExprStmt,
        Block,
        If,
        While,
        For,
        Break,
        Continue,
        Pass,
        Return,
        FuncDecl,
        StructDecl,
        Import,
    };

    struct Stmt
    {
        explicit Stmt(StmtKind k, Span s)
            : kind(k)
            , span(s)
        {
        }
        virtual ~Stmt() = default;
        Stmt(const Stmt&) = delete;
        Stmt& operator=(const Stmt&) = delete;

        const StmtKind kind;
        Span span;
    };

    using StmtPtr = std::unique_ptr<Stmt>;

    struct VarDeclStmt final : Stmt
    {
        VarDeclStmt(Span s, bool c, std::string n, Span ns)
            : Stmt(StmtKind::VarDecl, s)
            , is_const(c)
            , name(std::move(n))
            , name_span(ns)
        {
        }
        bool is_const;
        std::string name;
        Span name_span;
        std::optional<TypeSyntax> declared_type;
        ExprPtr initializer; // may be null for "var x: int;"
        const Symbol* symbol = nullptr;
        // "var (a, b) = pair" declares several names from the elements of a tuple; then 'name' is
        // empty and these hold the names (and, after semantic analysis, their symbols).
        std::vector<std::string> names;
        std::vector<Span> name_spans;
        std::vector<const Symbol*> symbols;
    };

    struct AssignStmt final : Stmt
    {
        AssignStmt(Span s, ExprPtr t, ExprPtr v)
            : Stmt(StmtKind::Assign, s)
            , target(std::move(t))
            , value(std::move(v))
        {
        }
        ExprPtr target; // NameExpr, IndexExpr or FieldExpr
        ExprPtr value;
    };

    struct ExprStmt final : Stmt
    {
        ExprStmt(Span s, ExprPtr e)
            : Stmt(StmtKind::ExprStmt, s)
            , expr(std::move(e))
        {
        }
        ExprPtr expr;
    };

    struct BlockStmt final : Stmt
    {
        explicit BlockStmt(Span s)
            : Stmt(StmtKind::Block, s)
        {
        }
        std::vector<StmtPtr> statements;
    };

    struct IfStmt final : Stmt
    {
        IfStmt(Span s, ExprPtr c, StmtPtr t, StmtPtr e)
            : Stmt(StmtKind::If, s)
            , condition(std::move(c))
            , then_branch(std::move(t))
            , else_branch(std::move(e))
        {
        }
        ExprPtr condition;
        StmtPtr then_branch; // BlockStmt
        StmtPtr else_branch; // BlockStmt, IfStmt ("else if") or null
    };

    struct WhileStmt final : Stmt
    {
        WhileStmt(Span s, ExprPtr c, StmtPtr b)
            : Stmt(StmtKind::While, s)
            , condition(std::move(c))
            , body(std::move(b))
        {
        }
        ExprPtr condition;
        StmtPtr body;
    };

    struct ForStmt final : Stmt
    {
        explicit ForStmt(Span s)
            : Stmt(StmtKind::For, s)
        {
        }
        StmtPtr init;      // VarDeclStmt, AssignStmt, ExprStmt or null
        ExprPtr condition; // null means "forever"
        StmtPtr step;      // AssignStmt, ExprStmt or null
        StmtPtr body;
    };

    struct BreakStmt final : Stmt
    {
        explicit BreakStmt(Span s)
            : Stmt(StmtKind::Break, s)
        {
        }
    };

    struct ContinueStmt final : Stmt
    {
        explicit ContinueStmt(Span s)
            : Stmt(StmtKind::Continue, s)
        {
        }
    };

    // "pass": does nothing; it gives an otherwise empty block a statement.
    struct PassStmt final : Stmt
    {
        explicit PassStmt(Span s)
            : Stmt(StmtKind::Pass, s)
        {
        }
    };

    struct ReturnStmt final : Stmt
    {
        ReturnStmt(Span s, ExprPtr v)
            : Stmt(StmtKind::Return, s)
            , value(std::move(v))
        {
        }
        ExprPtr value; // may be null
    };

    struct Param
    {
        std::string name;
        Span name_span;
        TypeSyntax type;
        const Symbol* symbol = nullptr;
    };

    struct FuncDeclStmt final : Stmt
    {
        FuncDeclStmt(Span s, std::string n, Span ns)
            : Stmt(StmtKind::FuncDecl, s)
            , name(std::move(n))
            , name_span(ns)
        {
        }
        std::string name;
        Span name_span;
        std::vector<Param> params;
        std::optional<TypeSyntax> return_type; // absent = returns nothing
        std::unique_ptr<BlockStmt> body;
        int function_index = -1; // set by semantic analysis
    };

    struct FieldDecl
    {
        std::string name;
        Span name_span;
        TypeSyntax type;
    };

    struct StructDeclStmt final : Stmt
    {
        StructDeclStmt(Span s, std::string n, Span ns)
            : Stmt(StmtKind::StructDecl, s)
            , name(std::move(n))
            , name_span(ns)
        {
        }
        std::string name;
        Span name_span;
        std::vector<FieldDecl> fields;
        int struct_index = -1; // set by semantic analysis
    };

    struct ProgramNode;

    // import "lib/shapes"  (a file: a module written in Thunder)
    // import os            (a module built into the toolchain)
    struct ImportStmt final : Stmt
    {
        ImportStmt(Span s, std::string t, Span ts, bool f)
            : Stmt(StmtKind::Import, s)
            , target(std::move(t))
            , target_span(ts)
            , is_file(f)
        {
        }
        std::string target; // the file name as written (without quotes), or the module name
        Span target_span;
        bool is_file;
        // Filled in by the compiler driver, which loads the files (see compiler.hpp):
        ProgramNode* module = nullptr;      // the module's syntax tree (null if it could not be loaded)
        std::uint16_t module_file = 0;      // its file id (Span::file)
        std::string resolved_path;          // where it was found
        // Filled in by semantic analysis:
        std::optional<BuiltinModule> builtin_module; // for "import os"
    };

    // ---------------------------------------------------------------- program

    struct ProgramNode
    {
        Span span;
        std::vector<StmtPtr> items; // function declarations and top-level statements, in source order
    };

    // ---------------------------------------------------------------- generic description
    //
    // A uniform view of any node, used by all AST printers (S-expressions for tests, an indented
    // tree for the command line and JSON for the IDE).

    struct AstChild;

    struct AstNodeView
    {
        std::string kind;  // e.g. "Binary"
        std::string label; // e.g. "+" or a variable name
        std::string type;  // semantic type, empty if none / not yet analysed
        Span span;
        std::vector<AstChild> children;
    };

    struct AstChild
    {
        std::string role; // e.g. "condition", "left", or empty
        AstNodeView node;
    };

    // Type names in the views are spelled in 'language' (English if null).
    AstNodeView describe(const ProgramNode& program, const LanguagePack* language = nullptr);
    AstNodeView describe(const Stmt& stmt, const LanguagePack* language = nullptr);
    AstNodeView describe(const Expr& expr, const LanguagePack* language = nullptr);

    // Compact one-line form, e.g. (binary + (int 1) (int 2)). Used heavily by unit tests.
    std::string to_sexpr(const AstNodeView& node);
    std::string to_sexpr(const Expr& expr);
    std::string to_sexpr(const ProgramNode& program);

    // Indented tree using box-drawing characters, for terminals.
    std::string to_tree_text(const ProgramNode& program, const LanguagePack* language = nullptr);

    // Display text for an operator token kind, e.g. "+" or "&&".
    std::string operator_text(TokenKind op);
}
