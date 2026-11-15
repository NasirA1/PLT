#include "thunder/ast.hpp"

#include "thunder/sema.hpp"

#include <format>

namespace thunder
{
    namespace
    {
        std::string type_text(const Type& type, const LanguagePack* language)
        {
            return type.is_error() ? std::string() : type.to_string(language);
        }

        std::string quoted(const std::string& text)
        {
            std::string result = "\"";
            for (const char c : text)
            {
                switch (c)
                {
                case '\n':
                    result += "\\n";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\0':
                    result += "\\0";
                    break;
                case '"':
                    result += "\\\"";
                    break;
                case '\\':
                    result += "\\\\";
                    break;
                default:
                    result += c;
                    break;
                }
            }
            return result + "\"";
        }

        void add(AstNodeView& view, std::string role, const Expr* expr, const LanguagePack* language)
        {
            if (expr != nullptr)
            {
                view.children.push_back(AstChild{std::move(role), describe(*expr, language)});
            }
        }

        void add(AstNodeView& view, std::string role, const Stmt* stmt, const LanguagePack* language)
        {
            if (stmt != nullptr)
            {
                view.children.push_back(AstChild{std::move(role), describe(*stmt, language)});
            }
        }
    }

    std::string operator_text(TokenKind op)
    {
        return std::string(token_canonical_spelling(op));
    }

    namespace
    {
        /** A literal or a name: a node with a label and no children. True if 'expr' was one. */
        bool describe_leaf(const Expr& expr, AstNodeView& view)
        {
            switch (expr.kind)
            {
            case ExprKind::IntLiteral:
                view.kind = "Int";
                view.label = std::to_string(static_cast<const IntLiteralExpr&>(expr).value);
                return true;
            case ExprKind::FloatLiteral:
                view.kind = "Float";
                view.label = DisplayLocale{}.format_float(static_cast<const FloatLiteralExpr&>(expr).value);
                return true;
            case ExprKind::BoolLiteral:
                view.kind = "Bool";
                view.label = static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false";
                return true;
            case ExprKind::NothingLiteral:
                view.kind = "Nothing";
                view.label = "nothing";
                return true;
            case ExprKind::StringLiteral:
                view.kind = "String";
                view.label = quoted(static_cast<const StringLiteralExpr&>(expr).value);
                return true;
            case ExprKind::Name:
                view.kind = "Name";
                view.label = static_cast<const NameExpr&>(expr).name;
                return true;
            default:
                return false;
            }
        }

        /** An array, tuple or map literal: one child per element. True if 'expr' was one. */
        bool describe_collection(const Expr& expr, AstNodeView& view, const LanguagePack* language)
        {
            switch (expr.kind)
            {
            case ExprKind::ArrayLiteral:
            {
                view.kind = "Array";
                const auto& array = static_cast<const ArrayLiteralExpr&>(expr);
                view.label = std::format("{} element(s)", array.elements.size());
                for (std::size_t i = 0; i < array.elements.size(); ++i)
                {
                    add(view, std::format("[{}]", i), array.elements[i].get(), language);
                }
                return true;
            }
            case ExprKind::TupleLiteral:
            {
                view.kind = "Tuple";
                const auto& tuple = static_cast<const TupleLiteralExpr&>(expr);
                view.label = std::format("{} element(s)", tuple.elements.size());
                for (std::size_t i = 0; i < tuple.elements.size(); ++i)
                {
                    add(view, std::format(".{}", i), tuple.elements[i].get(), language);
                }
                return true;
            }
            case ExprKind::MapLiteral:
            {
                view.kind = "Map";
                const auto& map = static_cast<const MapLiteralExpr&>(expr);
                view.label = std::format("{} entry(ies)", map.keys.size());
                for (std::size_t i = 0; i < map.keys.size(); ++i)
                {
                    add(view, std::format("key {}", i), map.keys[i].get(), language);
                    add(view, std::format("value {}", i), map.values[i].get(), language);
                }
                return true;
            }
            default:
                return false;
            }
        }
    }

    AstNodeView describe(const Expr& expr, const LanguagePack* language)
    {
        AstNodeView view;
        view.span = expr.span;
        view.type = type_text(expr.type, language);
        if (describe_leaf(expr, view) || describe_collection(expr, view, language))
        {
            return view;
        }

        switch (expr.kind)
        {
        case ExprKind::Unary:
        {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            view.kind = "Unary";
            view.label = language != nullptr ? language->spelling(unary.op) : operator_text(unary.op);
            add(view, "operand", unary.operand.get(), language);
            break;
        }
        case ExprKind::Binary:
        {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            view.kind = "Binary";
            view.label = language != nullptr ? language->spelling(binary.op) : operator_text(binary.op);
            add(view, "left", binary.left.get(), language);
            add(view, "right", binary.right.get(), language);
            break;
        }
        case ExprKind::Call:
        {
            const auto& call = static_cast<const CallExpr&>(expr);
            view.kind = "Call";
            view.label = call.callee;
            add(view, "callee", call.callee_expr.get(), language);
            for (std::size_t i = 0; i < call.arguments.size(); ++i)
            {
                const bool named = i < call.argument_names.size() && !call.argument_names[i].empty();
                add(view, named ? call.argument_names[i] : std::format("arg {}", i + 1), call.arguments[i].get(), language);
            }
            break;
        }
        case ExprKind::Index:
        {
            const auto& index = static_cast<const IndexExpr&>(expr);
            view.kind = "Index";
            add(view, "target", index.target.get(), language);
            add(view, "index", index.index.get(), language);
            break;
        }
        case ExprKind::Field:
        {
            const auto& field = static_cast<const FieldExpr&>(expr);
            view.kind = "Field";
            view.label = field.field;
            add(view, "target", field.target.get(), language);
            break;
        }
        case ExprKind::TupleIndex:
        {
            const auto& element = static_cast<const TupleIndexExpr&>(expr);
            view.kind = "TupleIndex";
            view.label = std::to_string(element.index);
            add(view, "target", element.target.get(), language);
            break;
        }
        default:
            break; // the leaves and the collections were described above
        }
        return view;
    }

    namespace
    {
        /** "(a, b)" for several names, or the one name. */
        std::string names_label(const std::vector<std::string>& names, const std::string& single)
        {
            if (names.size() <= 1)
            {
                return single;
            }
            std::string label = "(";
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                label += (i == 0 ? "" : ", ") + names[i];
            }
            return label + ")";
        }

        /** A child node for a parameter or a field: its name and its type. */
        AstNodeView member_view(const char* kind, const std::string& name, const TypeSyntax& type,
            const Span& name_span, const LanguagePack* language)
        {
            AstNodeView member;
            member.kind = kind;
            member.label = name;
            member.type = type_text(type.type, language);
            member.span = Span::merge(name_span, type.span);
            return member;
        }

        /** Declarations and imports. True if 'stmt' was one. */
        bool describe_declaration(const Stmt& stmt, AstNodeView& view, const LanguagePack* language)
        {
            switch (stmt.kind)
            {
            case StmtKind::VarDecl:
            {
                const auto& decl = static_cast<const VarDeclStmt&>(stmt);
                view.kind = decl.is_const ? "Const" : "Var";
                view.label = names_label(decl.names, decl.name);
                if (decl.symbol != nullptr)
                {
                    view.type = type_text(decl.symbol->type, language);
                }
                else if (decl.declared_type)
                {
                    view.type = type_text(decl.declared_type->type, language);
                }
                add(view, "value", decl.initializer.get(), language);
                return true;
            }
            case StmtKind::FuncDecl:
            {
                const auto& func = static_cast<const FuncDeclStmt&>(stmt);
                view.kind = func.is_native ? "NativeFunction" : "Function";
                view.label = func.name;
                view.type = func.return_type ? type_text(func.return_type->type, language) : "void";
                for (const Param& param : func.params)
                {
                    view.children.push_back(AstChild{"param", member_view("Param", param.name, param.type, param.name_span, language)});
                }
                add(view, "body", func.body.get(), language);
                return true;
            }
            case StmtKind::StructDecl:
            {
                const auto& decl = static_cast<const StructDeclStmt&>(stmt);
                view.kind = "Struct";
                view.label = decl.name;
                for (const FieldDecl& field : decl.fields)
                {
                    view.children.push_back(AstChild{"field", member_view("Field", field.name, field.type, field.name_span, language)});
                }
                return true;
            }
            case StmtKind::Import:
            {
                const auto& import = static_cast<const ImportStmt&>(stmt);
                view.kind = import.is_native ? "ImportNative" : "Import";
                view.label = import.is_file ? quoted(import.target) : import.target;
                return true;
            }
            default:
                return false;
            }
        }

        /** Blocks, branches and loops. True if 'stmt' was one. */
        bool describe_control(const Stmt& stmt, AstNodeView& view, const LanguagePack* language)
        {
            switch (stmt.kind)
            {
            case StmtKind::Block:
                view.kind = "Block";
                for (const StmtPtr& child : static_cast<const BlockStmt&>(stmt).statements)
                {
                    add(view, "", child.get(), language);
                }
                return true;
            case StmtKind::If:
            {
                const auto& branch = static_cast<const IfStmt&>(stmt);
                view.kind = "If";
                add(view, "condition", branch.condition.get(), language);
                add(view, "then", branch.then_branch.get(), language);
                add(view, "else", branch.else_branch.get(), language);
                return true;
            }
            case StmtKind::While:
            {
                const auto& loop = static_cast<const WhileStmt&>(stmt);
                view.kind = "While";
                add(view, "condition", loop.condition.get(), language);
                add(view, "body", loop.body.get(), language);
                return true;
            }
            case StmtKind::For:
            {
                const auto& loop = static_cast<const ForStmt&>(stmt);
                view.kind = "For";
                add(view, "init", loop.init.get(), language);
                add(view, "condition", loop.condition.get(), language);
                add(view, "step", loop.step.get(), language);
                add(view, "body", loop.body.get(), language);
                return true;
            }
            case StmtKind::ForEach:
            {
                const auto& loop = static_cast<const ForEachStmt&>(stmt);
                view.kind = "ForEach";
                view.label = names_label(loop.names, loop.names.empty() ? std::string() : loop.names[0]);
                add(view, "in", loop.collection.get(), language);
                add(view, "body", loop.body.get(), language);
                return true;
            }
            default:
                return false;
            }
        }
    }

    AstNodeView describe(const Stmt& stmt, const LanguagePack* language)
    {
        AstNodeView view;
        view.span = stmt.span;
        if (describe_declaration(stmt, view, language) || describe_control(stmt, view, language))
        {
            return view;
        }
        switch (stmt.kind)
        {
        case StmtKind::Assign:
        {
            const auto& assign = static_cast<const AssignStmt&>(stmt);
            view.kind = "Assign";
            view.label = assign.compound_op
                ? (language != nullptr ? language->spelling(*assign.compound_op) : operator_text(*assign.compound_op))
                : "=";
            add(view, "target", assign.target.get(), language);
            add(view, "value", assign.value.get(), language);
            break;
        }
        case StmtKind::ExprStmt:
            view.kind = "ExprStmt";
            add(view, "", static_cast<const ExprStmt&>(stmt).expr.get(), language);
            break;
        case StmtKind::Break:
            view.kind = "Break";
            break;
        case StmtKind::Continue:
            view.kind = "Continue";
            break;
        case StmtKind::Pass:
            view.kind = "Pass";
            break;
        case StmtKind::Return:
            view.kind = "Return";
            add(view, "value", static_cast<const ReturnStmt&>(stmt).value.get(), language);
            break;
        default:
            break; // declarations and control flow were described above
        }
        return view;
    }

    AstNodeView describe(const ProgramNode& program, const LanguagePack* language)
    {
        AstNodeView view;
        view.kind = "Program";
        view.span = program.span;
        for (const StmtPtr& item : program.items)
        {
            add(view, "", item.get(), language);
        }
        return view;
    }

    std::string to_sexpr(const AstNodeView& node)
    {
        std::string text = "(" + node.kind;
        if (!node.label.empty() && node.kind != "Array" && node.kind != "Tuple" && node.kind != "Map")
        {
            text += " " + node.label;
        }
        for (const AstChild& child : node.children)
        {
            text += " " + to_sexpr(child.node);
        }
        return text + ")";
    }

    std::string to_sexpr(const Expr& expr)
    {
        return to_sexpr(describe(expr));
    }

    std::string to_sexpr(const ProgramNode& program)
    {
        return to_sexpr(describe(program));
    }

    namespace
    {
        void append_tree(std::string& out, const AstNodeView& node, const std::string& role,
            const std::string& prefix, bool is_root, bool is_last)
        {
            std::string line;
            if (!is_root)
            {
                line = prefix + (is_last ? "└── " : "├── ");
            }
            if (!role.empty())
            {
                line += role + ": ";
            }
            line += node.kind;
            if (!node.label.empty())
            {
                line += " " + node.label;
            }
            if (!node.type.empty())
            {
                line += " : " + node.type;
            }
            out += line + "\n";

            const std::string child_prefix = is_root ? "" : prefix + (is_last ? "    " : "│   ");
            for (std::size_t i = 0; i < node.children.size(); ++i)
            {
                const AstChild& child = node.children[i];
                append_tree(out, child.node, child.role, child_prefix, false, i + 1 == node.children.size());
            }
        }
    }

    std::string to_tree_text(const ProgramNode& program, const LanguagePack* language)
    {
        std::string out;
        append_tree(out, describe(program, language), "", "", true, true);
        return out;
    }
}

namespace thunder
{
    // ---------------------------------------------------------------- cloning
    //
    // A generic function is checked and compiled once per combination of types, each time on a
    // fresh copy of its tree. Only what the parser wrote is copied: every semantic annotation
    // (types, symbols, indexes) starts at its default in the clone.

    namespace
    {
        ExprPtr clone_or_null(const ExprPtr& expr)
        {
            return expr ? clone_expr(*expr) : nullptr;
        }

        StmtPtr clone_stmt_or_null(const StmtPtr& stmt)
        {
            return stmt ? clone_stmt(*stmt) : nullptr;
        }

        std::unique_ptr<BlockStmt> clone_block(const BlockStmt& block)
        {
            auto copy = std::make_unique<BlockStmt>(block.span);
            for (const StmtPtr& statement : block.statements)
            {
                copy->statements.push_back(clone_stmt(*statement));
            }
            return copy;
        }

        TypeSyntax clone_type(const TypeSyntax& type)
        {
            return type; // a value type; the resolved Type inside is overwritten by resolution
        }
    }

    ExprPtr clone_expr(const Expr& expr)
    {
        switch (expr.kind)
        {
        case ExprKind::IntLiteral:
            return std::make_unique<IntLiteralExpr>(expr.span, static_cast<const IntLiteralExpr&>(expr).value);
        case ExprKind::FloatLiteral:
            return std::make_unique<FloatLiteralExpr>(expr.span, static_cast<const FloatLiteralExpr&>(expr).value);
        case ExprKind::BoolLiteral:
            return std::make_unique<BoolLiteralExpr>(expr.span, static_cast<const BoolLiteralExpr&>(expr).value);
        case ExprKind::NothingLiteral:
            return std::make_unique<NothingLiteralExpr>(expr.span);
        case ExprKind::StringLiteral:
            return std::make_unique<StringLiteralExpr>(expr.span, static_cast<const StringLiteralExpr&>(expr).value);
        case ExprKind::ArrayLiteral:
        {
            const auto& array = static_cast<const ArrayLiteralExpr&>(expr);
            auto copy = std::make_unique<ArrayLiteralExpr>(expr.span);
            for (const ExprPtr& element : array.elements)
            {
                copy->elements.push_back(clone_expr(*element));
            }
            return copy;
        }
        case ExprKind::TupleLiteral:
        {
            const auto& tuple = static_cast<const TupleLiteralExpr&>(expr);
            auto copy = std::make_unique<TupleLiteralExpr>(expr.span);
            for (const ExprPtr& element : tuple.elements)
            {
                copy->elements.push_back(clone_expr(*element));
            }
            return copy;
        }
        case ExprKind::MapLiteral:
        {
            const auto& map = static_cast<const MapLiteralExpr&>(expr);
            auto copy = std::make_unique<MapLiteralExpr>(expr.span);
            for (const ExprPtr& key : map.keys)
            {
                copy->keys.push_back(clone_expr(*key));
            }
            for (const ExprPtr& value : map.values)
            {
                copy->values.push_back(clone_expr(*value));
            }
            return copy;
        }
        case ExprKind::Name:
            return std::make_unique<NameExpr>(expr.span, static_cast<const NameExpr&>(expr).name);
        case ExprKind::Unary:
        {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            return std::make_unique<UnaryExpr>(expr.span, unary.op, clone_expr(*unary.operand));
        }
        case ExprKind::Binary:
        {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            auto copy = std::make_unique<BinaryExpr>(
                expr.span, binary.op, clone_expr(*binary.left), clone_expr(*binary.right));
            copy->op_span = binary.op_span;
            return copy;
        }
        case ExprKind::Call:
        {
            const auto& call = static_cast<const CallExpr&>(expr);
            auto copy = std::make_unique<CallExpr>(expr.span, call.callee, call.callee_span);
            copy->callee_expr = clone_or_null(call.callee_expr);
            for (const ExprPtr& argument : call.arguments)
            {
                copy->arguments.push_back(clone_expr(*argument));
            }
            copy->argument_names = call.argument_names;
            return copy;
        }
        case ExprKind::Index:
        {
            const auto& index = static_cast<const IndexExpr&>(expr);
            return std::make_unique<IndexExpr>(expr.span, clone_expr(*index.target), clone_expr(*index.index));
        }
        case ExprKind::Field:
        {
            const auto& field = static_cast<const FieldExpr&>(expr);
            return std::make_unique<FieldExpr>(expr.span, clone_expr(*field.target), field.field, field.field_span);
        }
        case ExprKind::TupleIndex:
        {
            const auto& tuple = static_cast<const TupleIndexExpr&>(expr);
            return std::make_unique<TupleIndexExpr>(
                expr.span, clone_expr(*tuple.target), tuple.index, tuple.index_span);
        }
        }
        return nullptr; // unreachable: every kind is handled above
    }

    StmtPtr clone_stmt(const Stmt& stmt)
    {
        switch (stmt.kind)
        {
        case StmtKind::VarDecl:
        {
            const auto& decl = static_cast<const VarDeclStmt&>(stmt);
            auto copy = std::make_unique<VarDeclStmt>(stmt.span, decl.is_const, decl.name, decl.name_span);
            if (decl.declared_type)
            {
                copy->declared_type = clone_type(*decl.declared_type);
            }
            copy->initializer = clone_or_null(decl.initializer);
            copy->names = decl.names;
            copy->name_spans = decl.name_spans;
            return copy;
        }
        case StmtKind::Assign:
        {
            const auto& assign = static_cast<const AssignStmt&>(stmt);
            auto copy = std::make_unique<AssignStmt>(
                stmt.span, clone_expr(*assign.target), clone_expr(*assign.value));
            copy->compound_op = assign.compound_op;
            return copy;
        }
        case StmtKind::ExprStmt:
            return std::make_unique<ExprStmt>(stmt.span, clone_expr(*static_cast<const ExprStmt&>(stmt).expr));
        case StmtKind::Block:
            return clone_block(static_cast<const BlockStmt&>(stmt));
        case StmtKind::If:
        {
            const auto& branch = static_cast<const IfStmt&>(stmt);
            return std::make_unique<IfStmt>(stmt.span, clone_expr(*branch.condition),
                clone_stmt(*branch.then_branch), clone_stmt_or_null(branch.else_branch));
        }
        case StmtKind::While:
        {
            const auto& loop = static_cast<const WhileStmt&>(stmt);
            return std::make_unique<WhileStmt>(stmt.span, clone_expr(*loop.condition), clone_stmt(*loop.body));
        }
        case StmtKind::For:
        {
            const auto& loop = static_cast<const ForStmt&>(stmt);
            auto copy = std::make_unique<ForStmt>(stmt.span);
            copy->init = clone_stmt_or_null(loop.init);
            copy->condition = clone_or_null(loop.condition);
            copy->step = clone_stmt_or_null(loop.step);
            copy->body = clone_stmt(*loop.body);
            return copy;
        }
        case StmtKind::ForEach:
        {
            const auto& loop = static_cast<const ForEachStmt&>(stmt);
            auto copy = std::make_unique<ForEachStmt>(stmt.span);
            copy->names = loop.names;
            copy->name_spans = loop.name_spans;
            copy->collection = clone_expr(*loop.collection);
            copy->body = clone_stmt(*loop.body);
            return copy;
        }
        case StmtKind::Break:
            return std::make_unique<BreakStmt>(stmt.span);
        case StmtKind::Continue:
            return std::make_unique<ContinueStmt>(stmt.span);
        case StmtKind::Pass:
            return std::make_unique<PassStmt>(stmt.span);
        case StmtKind::Return:
            return std::make_unique<ReturnStmt>(
                stmt.span, clone_or_null(static_cast<const ReturnStmt&>(stmt).value));
        case StmtKind::FuncDecl:
        {
            const auto& func = static_cast<const FuncDeclStmt&>(stmt);
            auto copy = std::make_unique<FuncDeclStmt>(stmt.span, func.name, func.name_span);
            copy->type_params = func.type_params;
            copy->type_param_spans = func.type_param_spans;
            for (const Param& param : func.params)
            {
                Param copied;
                copied.name = param.name;
                copied.name_span = param.name_span;
                copied.type = clone_type(param.type);
                copy->params.push_back(std::move(copied));
            }
            if (func.return_type)
            {
                copy->return_type = clone_type(*func.return_type);
            }
            copy->is_native = func.is_native;
            if (func.body)
            {
                copy->body = clone_block(*func.body);
            }
            return copy;
        }
        case StmtKind::StructDecl:
        case StmtKind::Import:
            break; // never cloned: structs are instantiated by resolving fields, imports are global
        }
        return nullptr;
    }
}
