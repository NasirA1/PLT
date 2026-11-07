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

    AstNodeView describe(const Expr& expr, const LanguagePack* language)
    {
        AstNodeView view;
        view.span = expr.span;
        view.type = type_text(expr.type, language);

        switch (expr.kind)
        {
        case ExprKind::IntLiteral:
            view.kind = "Int";
            view.label = std::to_string(static_cast<const IntLiteralExpr&>(expr).value);
            break;
        case ExprKind::BoolLiteral:
            view.kind = "Bool";
            view.label = static_cast<const BoolLiteralExpr&>(expr).value ? "true" : "false";
            break;
        case ExprKind::StringLiteral:
            view.kind = "String";
            view.label = quoted(static_cast<const StringLiteralExpr&>(expr).value);
            break;
        case ExprKind::ArrayLiteral:
        {
            view.kind = "Array";
            const auto& array = static_cast<const ArrayLiteralExpr&>(expr);
            view.label = std::format("{} element(s)", array.elements.size());
            for (std::size_t i = 0; i < array.elements.size(); ++i)
            {
                add(view, std::format("[{}]", i), array.elements[i].get(), language);
            }
            break;
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
            break;
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
            break;
        }
        case ExprKind::Name:
            view.kind = "Name";
            view.label = static_cast<const NameExpr&>(expr).name;
            break;
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
        }
        return view;
    }

    AstNodeView describe(const Stmt& stmt, const LanguagePack* language)
    {
        AstNodeView view;
        view.span = stmt.span;

        switch (stmt.kind)
        {
        case StmtKind::VarDecl:
        {
            const auto& decl = static_cast<const VarDeclStmt&>(stmt);
            view.kind = decl.is_const ? "Const" : "Var";
            view.label = decl.name;
            if (!decl.names.empty())
            {
                view.label = "(";
                for (std::size_t i = 0; i < decl.names.size(); ++i)
                {
                    view.label += (i == 0 ? "" : ", ") + decl.names[i];
                }
                view.label += ")";
            }
            if (decl.symbol != nullptr)
            {
                view.type = type_text(decl.symbol->type, language);
            }
            else if (decl.declared_type)
            {
                view.type = type_text(decl.declared_type->type, language);
            }
            add(view, "value", decl.initializer.get(), language);
            break;
        }
        case StmtKind::Assign:
        {
            const auto& assign = static_cast<const AssignStmt&>(stmt);
            view.kind = "Assign";
            view.label = "=";
            add(view, "target", assign.target.get(), language);
            add(view, "value", assign.value.get(), language);
            break;
        }
        case StmtKind::ExprStmt:
            view.kind = "ExprStmt";
            add(view, "", static_cast<const ExprStmt&>(stmt).expr.get(), language);
            break;
        case StmtKind::Block:
            view.kind = "Block";
            for (const StmtPtr& child : static_cast<const BlockStmt&>(stmt).statements)
            {
                add(view, "", child.get(), language);
            }
            break;
        case StmtKind::If:
        {
            const auto& branch = static_cast<const IfStmt&>(stmt);
            view.kind = "If";
            add(view, "condition", branch.condition.get(), language);
            add(view, "then", branch.then_branch.get(), language);
            add(view, "else", branch.else_branch.get(), language);
            break;
        }
        case StmtKind::While:
        {
            const auto& loop = static_cast<const WhileStmt&>(stmt);
            view.kind = "While";
            add(view, "condition", loop.condition.get(), language);
            add(view, "body", loop.body.get(), language);
            break;
        }
        case StmtKind::For:
        {
            const auto& loop = static_cast<const ForStmt&>(stmt);
            view.kind = "For";
            add(view, "init", loop.init.get(), language);
            add(view, "condition", loop.condition.get(), language);
            add(view, "step", loop.step.get(), language);
            add(view, "body", loop.body.get(), language);
            break;
        }
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
        case StmtKind::FuncDecl:
        {
            const auto& func = static_cast<const FuncDeclStmt&>(stmt);
            view.kind = "Function";
            view.label = func.name;
            view.type = func.return_type ? type_text(func.return_type->type, language) : "void";
            for (const Param& param : func.params)
            {
                AstNodeView param_view;
                param_view.kind = "Param";
                param_view.label = param.name;
                param_view.type = type_text(param.type.type, language);
                param_view.span = Span::merge(param.name_span, param.type.span);
                view.children.push_back(AstChild{"param", std::move(param_view)});
            }
            add(view, "body", func.body.get(), language);
            break;
        }
        case StmtKind::Import:
        {
            const auto& import = static_cast<const ImportStmt&>(stmt);
            view.kind = "Import";
            view.label = import.is_file ? quoted(import.target) : import.target;
            break;
        }
        case StmtKind::StructDecl:
        {
            const auto& decl = static_cast<const StructDeclStmt&>(stmt);
            view.kind = "Struct";
            view.label = decl.name;
            for (const FieldDecl& field : decl.fields)
            {
                AstNodeView field_view;
                field_view.kind = "Field";
                field_view.label = field.name;
                field_view.type = type_text(field.type.type, language);
                field_view.span = Span::merge(field.name_span, field.type.span);
                view.children.push_back(AstChild{"field", std::move(field_view)});
            }
            break;
        }
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
