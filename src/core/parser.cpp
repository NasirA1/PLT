#include "thunder/parser.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

namespace thunder
{
    int infix_binding_power(TokenKind kind)
    {
        switch (kind)
        {
        case TokenKind::OrOr:
            return binding_power::logical_or;
        case TokenKind::AndAnd:
            return binding_power::logical_and;
        case TokenKind::Equal:
        case TokenKind::NotEqual:
            return binding_power::equality;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return binding_power::comparison;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return binding_power::additive;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            return binding_power::multiplicative;
        default:
            return binding_power::none;
        }
    }

    Parser::Parser(std::vector<Token> tokens, const LanguagePack& language, DiagnosticBag& diagnostics)
        : tokens_(std::move(tokens))
        , language_(language)
        , diagnostics_(diagnostics)
    {
        // Error tokens were already reported by the lexer; the parser never sees them.
        std::erase_if(tokens_, [](const Token& token) { return token.kind == TokenKind::Error; });
        if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile)
        {
            Token eof;
            eof.kind = TokenKind::EndOfFile;
            if (!tokens_.empty())
            {
                eof.span = tokens_.back().span;
                eof.span.offset += eof.span.length;
                eof.span.length = 0;
                eof.span.start = eof.span.end;
            }
            tokens_.push_back(eof);
        }
    }

    // ------------------------------------------------------------------ token helpers

    const Token& Parser::peek(std::size_t ahead) const
    {
        return tokens_[std::min(current_ + ahead, tokens_.size() - 1)];
    }

    const Token& Parser::previous() const
    {
        return tokens_[current_ == 0 ? 0 : current_ - 1];
    }

    bool Parser::check(TokenKind kind) const
    {
        return peek().kind == kind;
    }

    bool Parser::match(TokenKind kind)
    {
        if (check(kind))
        {
            advance();
            return true;
        }
        return false;
    }

    const Token& Parser::advance()
    {
        if (current_ < tokens_.size() - 1)
        {
            ++current_;
        }
        return previous();
    }

    const Token& Parser::expect(TokenKind kind)
    {
        if (check(kind))
        {
            return advance();
        }
        fail_expected(std::format("'{}'", language_.spelling(kind)));
    }

    void Parser::fail_expected(const std::string& what)
    {
        // When the expected token is missing at the end of a line (typically a ';'), point just
        // after the previous token rather than at the start of the next line.
        Span span = peek().span;
        if (current_ > 0 && (peek().kind == TokenKind::EndOfFile || peek().span.start.line != previous().span.end.line))
        {
            const Span& prev = previous().span;
            span.offset = prev.offset + prev.length;
            span.length = 1;
            span.start = prev.end;
            span.end = Position{prev.end.line, prev.end.column + 1};
        }
        diagnostics_.report(DiagId::ExpectedToken, span, what, describe(peek(), language_));
        throw ParseError{};
    }

    Span Parser::span_from(const Span& start) const
    {
        return Span::merge(start, previous().span);
    }

    bool Parser::stop_parsing()
    {
        if (!gave_up_ && diagnostics_.too_many_errors())
        {
            gave_up_ = true;
            diagnostics_.report(DiagId::TooManyErrors, peek().span);
        }
        return gave_up_;
    }

    void Parser::synchronize(std::size_t statement_start)
    {
        // Panic-mode recovery: skip tokens until something that looks like the start of the
        // next statement, so that one mistake does not produce a cascade of errors.
        if (current_ == statement_start)
        {
            advance();
        }
        nesting_ = 0; // brackets left open by the broken statement must not swallow later lines
        while (!check(TokenKind::EndOfFile))
        {
            if (check(TokenKind::Indent) && current_ > statement_start)
            {
                skip_indented_block(); // the body of the broken statement: not a second error
                continue;
            }
            if ((previous().kind == TokenKind::Semicolon || peek().newline_before) && current_ > statement_start)
            {
                return;
            }
            switch (peek().kind)
            {
            case TokenKind::Dedent:
            case TokenKind::KwStruct:
            case TokenKind::KwVar:
            case TokenKind::KwConst:
            case TokenKind::KwFunc:
            case TokenKind::KwIf:
            case TokenKind::KwWhile:
            case TokenKind::KwFor:
            case TokenKind::KwReturn:
            case TokenKind::KwBreak:
            case TokenKind::KwContinue:
                return;
            default:
                advance();
                break;
            }
        }
    }

    // ------------------------------------------------------------------ declarations

    ProgramNode Parser::parse_program()
    {
        ProgramNode program;
        program.span = peek().span;
        while (!check(TokenKind::EndOfFile) && !stop_parsing())
        {
            const std::size_t start = current_;
            try
            {
                program.items.push_back(parse_item(true));
            }
            catch (const ParseError&)
            {
                synchronize(start);
            }
        }
        program.span = Span::merge(program.span, peek().span);
        return program;
    }

    ExprPtr Parser::parse_standalone_expression()
    {
        try
        {
            ExprPtr expr = parse_expression();
            expect(TokenKind::EndOfFile);
            return expr;
        }
        catch (const ParseError&)
        {
            return nullptr;
        }
    }

    StmtPtr Parser::parse_item(bool top_level)
    {
        if (check(TokenKind::Indent))
        {
            // A line indented more than the one before it, where no block was expected.
            diagnostics_.report(DiagId::UnexpectedIndent, peek().span);
            skip_indented_block();
            throw ParseError{};
        }
        if (check(TokenKind::KwFunc))
        {
            if (!top_level)
            {
                diagnostics_.report(DiagId::NestedFunction, peek().span);
            }
            return parse_function();
        }
        if (check(TokenKind::KwStruct))
        {
            if (!top_level)
            {
                diagnostics_.report(DiagId::StructNotTopLevel, peek().span);
            }
            return parse_struct();
        }
        return parse_statement();
    }

    StmtPtr Parser::parse_struct()
    {
        const Span start = advance().span; // 'struct'
        const Token& name = expect(TokenKind::Identifier);
        auto decl = std::make_unique<StructDeclStmt>(start, name.lexeme, name.span);
        expect_block_start(TokenKind::KwStruct);
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile))
        {
            const Token& field_name = expect(TokenKind::Identifier);
            FieldDecl field;
            field.name = field_name.lexeme;
            field.name_span = field_name.span;
            expect(TokenKind::Colon);
            field.type = parse_type();
            decl->fields.push_back(std::move(field));
            // Fields are separated by line breaks; ',' and ';' are also accepted.
            if (!match(TokenKind::Comma) && !match(TokenKind::Semicolon) && !check(TokenKind::Dedent)
                && !peek().newline_before)
            {
                // Recover in place: report the missing separator and carry on with the next field.
                diagnostics_.report(DiagId::ExpectedToken, peek().span, language_.term("a new line, ',' or ';'"),
                    describe(peek(), language_));
            }
        }
        match(TokenKind::Dedent);
        decl->span = span_from(start);
        return decl;
    }

    void Parser::expect_block_start(TokenKind after)
    {
        if (match(TokenKind::Indent))
        {
            return;
        }
        const std::string keyword = std::format("'{}'", language_.spelling(after));
        if (check(TokenKind::LeftBrace))
        {
            diagnostics_.report(DiagId::BracesNotUsed, peek().span, keyword);
            // Skip the braced block as a whole: its contents would only produce follow-up errors.
            int depth = 0;
            do
            {
                depth += check(TokenKind::LeftBrace) ? 1 : (check(TokenKind::RightBrace) ? -1 : 0);
                advance();
            } while (depth > 0 && !check(TokenKind::EndOfFile));
        }
        else
        {
            diagnostics_.report(DiagId::ExpectedIndentedBlock, peek().span, keyword, describe(peek(), language_));
        }
        throw ParseError{};
    }

    void Parser::skip_indented_block()
    {
        int depth = 0;
        do
        {
            if (check(TokenKind::Indent))
            {
                ++depth;
            }
            else if (check(TokenKind::Dedent))
            {
                --depth;
            }
            advance();
        } while (depth > 0 && !check(TokenKind::EndOfFile));
    }

    void Parser::end_statement()
    {
        if (match(TokenKind::Semicolon) || check(TokenKind::Dedent) || check(TokenKind::EndOfFile)
            || peek().newline_before)
        {
            return;
        }
        diagnostics_.report(DiagId::ExpectedStatementEnd, peek().span, describe(peek(), language_));
        throw ParseError{};
    }

    StmtPtr Parser::parse_function()
    {
        const Span start = advance().span; // 'func'
        const Token& name = expect(TokenKind::Identifier);
        auto func = std::make_unique<FuncDeclStmt>(start, name.lexeme, name.span);

        expect(TokenKind::LeftParen);
        if (!check(TokenKind::RightParen))
        {
            do
            {
                const Token& param_name = expect(TokenKind::Identifier);
                Param param;
                param.name = param_name.lexeme;
                param.name_span = param_name.span;
                expect(TokenKind::Colon);
                param.type = parse_type();
                func->params.push_back(std::move(param));
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen);

        if (match(TokenKind::Colon))
        {
            func->return_type = parse_type();
        }
        func->body = parse_block(TokenKind::KwFunc);
        func->span = span_from(start);
        return func;
    }

    TypeSyntax Parser::parse_type()
    {
        TypeSyntax syntax;
        const Token& token = peek();
        switch (token.kind)
        {
        case TokenKind::KwInt:
            syntax.type = Type::int_type();
            advance();
            break;
        case TokenKind::KwBool:
            syntax.type = Type::bool_type();
            advance();
            break;
        case TokenKind::KwString:
            syntax.type = Type::string_type();
            advance();
            break;
        case TokenKind::Identifier:
            syntax.struct_name = token.lexeme; // resolved by semantic analysis
            advance();
            break;
        case TokenKind::LeftParen: // (int, string): a tuple type
        {
            advance();
            const NestingGuard nested(*this);
            std::vector<Type> element_types;
            do
            {
                syntax.parts.push_back(parse_type());
                element_types.push_back(syntax.parts.back().type);
            } while (match(TokenKind::Comma));
            expect(TokenKind::RightParen);
            if (syntax.parts.size() < 2)
            {
                diagnostics_.report(DiagId::TupleNeedsTwoElements, Span::merge(token.span, previous().span));
                throw ParseError{};
            }
            syntax.type = Type::tuple_of(std::move(element_types));
            break;
        }
        case TokenKind::LeftBrace: // {string: int}: a map type
        {
            advance();
            const NestingGuard nested(*this);
            syntax.parts.push_back(parse_type());
            expect(TokenKind::Colon);
            syntax.parts.push_back(parse_type());
            expect(TokenKind::RightBrace);
            syntax.type = Type::map_of(syntax.parts[0].type, syntax.parts[1].type);
            break;
        }
        default:
            diagnostics_.report(DiagId::ExpectedType, token.span, describe(token, language_));
            throw ParseError{};
        }
        while (check(TokenKind::LeftBracket) && peek(1).kind == TokenKind::RightBracket && !peek().newline_before)
        {
            advance();
            advance();
            syntax.type = Type::array_of(syntax.type);
        }
        syntax.span = span_from(token.span);
        return syntax;
    }

    // ------------------------------------------------------------------ statements

    StmtPtr Parser::parse_statement()
    {
        switch (peek().kind)
        {
        case TokenKind::KwVar:
        case TokenKind::KwConst:
            return parse_var_decl(true);
        case TokenKind::KwIf:
            return parse_if();
        case TokenKind::KwWhile:
            return parse_while();
        case TokenKind::KwFor:
            return parse_for();
        case TokenKind::KwReturn:
            return parse_return();
        case TokenKind::KwPass:
        {
            const Span start = advance().span;
            auto stmt = std::make_unique<PassStmt>(start);
            end_statement();
            return stmt;
        }
        case TokenKind::KwBreak:
        {
            const Span start = advance().span;
            auto stmt = std::make_unique<BreakStmt>(start);
            end_statement();
            return stmt;
        }
        case TokenKind::KwContinue:
        {
            const Span start = advance().span;
            auto stmt = std::make_unique<ContinueStmt>(start);
            end_statement();
            return stmt;
        }
        default:
        {
            StmtPtr stmt = parse_simple_statement();
            end_statement();
            return stmt;
        }
        }
    }

    std::unique_ptr<VarDeclStmt> Parser::parse_var_decl(bool is_statement)
    {
        const Token& keyword = advance(); // 'var' or 'const'
        const Span start = keyword.span;
        const bool is_const = keyword.kind == TokenKind::KwConst;
        if (check(TokenKind::LeftParen))
        {
            // var (a, b) = pair: one name per element of a tuple.
            auto decl = std::make_unique<VarDeclStmt>(start, is_const, std::string(), peek().span);
            advance();
            do
            {
                const Token& part = expect(TokenKind::Identifier);
                decl->names.push_back(part.lexeme);
                decl->name_spans.push_back(part.span);
            } while (match(TokenKind::Comma));
            expect(TokenKind::RightParen);
            decl->name_span = span_from(start);
            if (decl->names.size() < 2)
            {
                diagnostics_.report(DiagId::TupleNeedsTwoElements, decl->name_span);
                throw ParseError{};
            }
            expect(TokenKind::Assign);
            decl->initializer = parse_expression();
            decl->span = span_from(start);
            if (is_statement)
            {
                end_statement();
            }
            return decl;
        }
        const Token& name = expect(TokenKind::Identifier);
        auto decl = std::make_unique<VarDeclStmt>(start, is_const, name.lexeme, name.span);

        if (match(TokenKind::Colon))
        {
            decl->declared_type = parse_type();
        }
        if (is_const)
        {
            expect(TokenKind::Assign);
            decl->initializer = parse_expression();
        }
        else if (match(TokenKind::Assign))
        {
            decl->initializer = parse_expression();
        }
        else if (!decl->declared_type)
        {
            diagnostics_.report(DiagId::VarNeedsTypeOrValue, decl->name_span, decl->name);
        }

        decl->span = span_from(start);
        if (is_statement)
        {
            end_statement();
        }
        return decl;
    }

    std::unique_ptr<BlockStmt> Parser::parse_block(TokenKind after)
    {
        // The block is the indented lines that follow; it ends where the indentation returns to
        // the level of the keyword (the lexer marks that with a Dedent token).
        const Span start = peek().span;
        expect_block_start(after);
        auto block = std::make_unique<BlockStmt>(start);
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile) && !stop_parsing())
        {
            const std::size_t statement_start = current_;
            try
            {
                block->statements.push_back(parse_item(false));
            }
            catch (const ParseError&)
            {
                synchronize(statement_start);
            }
        }
        match(TokenKind::Dedent);
        block->span = span_from(start);
        return block;
    }

    StmtPtr Parser::parse_if()
    {
        const Span start = advance().span; // 'if'
        ExprPtr condition = parse_expression();
        StmtPtr then_branch = parse_block(TokenKind::KwIf);
        StmtPtr else_branch;
        if (match(TokenKind::KwElse))
        {
            else_branch = check(TokenKind::KwIf) ? parse_if() : StmtPtr(parse_block(TokenKind::KwElse));
        }
        return std::make_unique<IfStmt>(span_from(start), std::move(condition), std::move(then_branch),
            std::move(else_branch));
    }

    StmtPtr Parser::parse_while()
    {
        const Span start = advance().span; // 'while'
        ExprPtr condition = parse_expression();
        StmtPtr body = parse_block(TokenKind::KwWhile);
        return std::make_unique<WhileStmt>(span_from(start), std::move(condition), std::move(body));
    }

    StmtPtr Parser::parse_for()
    {
        const Span start = advance().span; // 'for'
        auto loop = std::make_unique<ForStmt>(start);
        // for init; condition; step - the header may be written in parentheses, as in C.
        const bool parenthesised = match(TokenKind::LeftParen);
        {
            std::optional<NestingGuard> nested;
            if (parenthesised)
            {
                nested.emplace(*this); // the loop header may then span several lines
            }
            const TokenKind header_end = parenthesised ? TokenKind::RightParen : TokenKind::Indent;
            if (!check(TokenKind::Semicolon))
            {
                if (check(TokenKind::KwVar) || check(TokenKind::KwConst))
                {
                    loop->init = parse_var_decl(false);
                }
                else
                {
                    loop->init = parse_simple_statement();
                }
            }
            expect(TokenKind::Semicolon);

            if (!check(TokenKind::Semicolon))
            {
                loop->condition = parse_expression();
            }
            expect(TokenKind::Semicolon);

            if (!check(header_end))
            {
                loop->step = parse_simple_statement();
            }
            if (parenthesised)
            {
                expect(TokenKind::RightParen);
            }
        }

        loop->body = parse_block(TokenKind::KwFor);
        loop->span = span_from(start);
        return loop;
    }

    StmtPtr Parser::parse_return()
    {
        const Span start = advance().span; // 'return'
        ExprPtr value;
        // "return" at the end of a line returns no value, as in JavaScript and Go.
        if (!check(TokenKind::Semicolon) && !check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)
            && !peek().newline_before)
        {
            value = parse_expression();
        }
        auto stmt = std::make_unique<ReturnStmt>(span_from(start), std::move(value));
        end_statement();
        return stmt;
    }

    StmtPtr Parser::parse_simple_statement()
    {
        ExprPtr expr = parse_expression();
        if (match(TokenKind::Assign))
        {
            if (expr->kind != ExprKind::Name && expr->kind != ExprKind::Index && expr->kind != ExprKind::Field)
            {
                diagnostics_.report(DiagId::InvalidAssignmentTarget, expr->span); // tuples are immutable, too
            }
            ExprPtr value = parse_expression();
            const Span span = Span::merge(expr->span, value->span);
            return std::make_unique<AssignStmt>(span, std::move(expr), std::move(value));
        }
        const Span span = expr->span;
        return std::make_unique<ExprStmt>(span, std::move(expr));
    }

    // ------------------------------------------------------------------ expressions (Pratt)

    ExprPtr Parser::parse_expression(int min_binding_power)
    {
        // 1. Every expression starts with a prefix form: a literal, a name, a parenthesised
        //    expression, an array literal or a unary operator.
        ExprPtr left = parse_prefix();

        // 2. Then, while the next operator binds more tightly than our caller's operator, it
        //    takes 'left' as its left operand. Postfix operators (calls, indexing) bind tightest.
        while (true)
        {
            // An operator at the start of a new line begins a new statement, unless we are inside
            // parentheses or brackets.
            if (at_line_break())
            {
                return left;
            }
            const TokenKind kind = peek().kind;
            if (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket || kind == TokenKind::Dot)
            {
                left = parse_postfix(std::move(left));
                continue;
            }

            const int power = infix_binding_power(kind);
            if (power == binding_power::none || power <= min_binding_power)
            {
                return left;
            }

            const Token& op = advance();
            const Span op_span = op.span;
            // Passing 'power' (not power - 1) makes operators of equal precedence group to the
            // left: 1 - 2 - 3 parses as (1 - 2) - 3.
            ExprPtr right = parse_expression(power);
            const Span span = Span::merge(left->span, right->span);
            auto binary = std::make_unique<BinaryExpr>(span, kind, std::move(left), std::move(right));
            binary->op_span = op_span;
            left = std::move(binary);
        }
    }

    ExprPtr Parser::parse_prefix()
    {
        const Token& token = peek();
        switch (token.kind)
        {
        case TokenKind::IntLiteral:
            advance();
            return std::make_unique<IntLiteralExpr>(token.span, token.int_value);
        case TokenKind::StringLiteral:
            advance();
            return std::make_unique<StringLiteralExpr>(token.span, token.text_value);
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            advance();
            return std::make_unique<BoolLiteralExpr>(token.span, token.kind == TokenKind::KwTrue);
        case TokenKind::Identifier:
            advance();
            return std::make_unique<NameExpr>(token.span, token.lexeme);
        case TokenKind::LeftParen:
        {
            const Span start = advance().span;
            const NestingGuard nested(*this);
            ExprPtr inner = parse_expression();
            if (!check(TokenKind::Comma))
            {
                expect(TokenKind::RightParen);
                return inner; // a parenthesised expression
            }
            // (a, b, ...): a tuple literal.
            auto tuple = std::make_unique<TupleLiteralExpr>(start);
            tuple->elements.push_back(std::move(inner));
            while (match(TokenKind::Comma))
            {
                tuple->elements.push_back(parse_expression());
            }
            expect(TokenKind::RightParen);
            tuple->span = span_from(start);
            return tuple;
        }
        case TokenKind::LeftBrace:
        {
            // {key: value, ...}: a map literal ({} is an empty map).
            const Span start = advance().span;
            const NestingGuard nested(*this);
            auto map = std::make_unique<MapLiteralExpr>(start);
            if (!check(TokenKind::RightBrace))
            {
                do
                {
                    map->keys.push_back(parse_expression());
                    expect(TokenKind::Colon);
                    map->values.push_back(parse_expression());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RightBrace);
            map->span = span_from(start);
            return map;
        }
        case TokenKind::LeftBracket:
        {
            const Span start = advance().span;
            const NestingGuard nested(*this);
            auto array = std::make_unique<ArrayLiteralExpr>(start);
            if (!check(TokenKind::RightBracket))
            {
                do
                {
                    array->elements.push_back(parse_expression());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RightBracket);
            array->span = span_from(start);
            return array;
        }
        case TokenKind::Minus:
        case TokenKind::Bang:
        {
            const Span start = advance().span;
            ExprPtr operand = parse_expression(binding_power::prefix);
            const Span span = Span::merge(start, operand->span);
            return std::make_unique<UnaryExpr>(span, token.kind, std::move(operand));
        }
        default:
            if (token.newline_before && current_ > 0 && nesting_ == 0)
            {
                // e.g. "x = a *" followed by a line break: point at the end of the incomplete line.
                const Span& prev = previous().span;
                const Span end{prev.offset + prev.length, 1, prev.end, Position{prev.end.line, prev.end.column + 1}};
                diagnostics_.report(DiagId::ExpectedExpression, end, language_.term("the end of the line"));
            }
            else
            {
                diagnostics_.report(DiagId::ExpectedExpression, token.span, describe(token, language_));
            }
            throw ParseError{};
        }
    }

    ExprPtr Parser::parse_postfix(ExprPtr left)
    {
        if (match(TokenKind::Dot))
        {
            if (check(TokenKind::IntLiteral))
            {
                const Token& index = advance(); // t.0, t.1: a tuple element
                const Span span = span_from(left->span);
                return std::make_unique<TupleIndexExpr>(span, std::move(left), index.int_value, index.span);
            }
            const Token& field = expect(TokenKind::Identifier);
            const Span span = span_from(left->span);
            return std::make_unique<FieldExpr>(span, std::move(left), field.lexeme, field.span);
        }
        if (match(TokenKind::LeftBracket))
        {
            const NestingGuard nested(*this);
            ExprPtr index = parse_expression();
            expect(TokenKind::RightBracket);
            const Span span = span_from(left->span);
            return std::make_unique<IndexExpr>(span, std::move(left), std::move(index));
        }

        // Function call: only a plain name can be called.
        const Token& paren = advance();
        if (left->kind != ExprKind::Name)
        {
            diagnostics_.report(DiagId::NotCallable, Span::merge(left->span, paren.span));
            throw ParseError{};
        }
        const auto& name = static_cast<const NameExpr&>(*left);
        auto call = std::make_unique<CallExpr>(left->span, name.name, name.span);
        const NestingGuard nested(*this);
        if (!check(TokenKind::RightParen))
        {
            do
            {
                // "name: value" names the struct field an argument initialises.
                std::string argument_name;
                if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Colon)
                {
                    argument_name = advance().lexeme;
                    advance(); // ':'
                }
                call->arguments.push_back(parse_expression());
                call->argument_names.push_back(std::move(argument_name));
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen);
        call->span = span_from(left->span);
        return call;
    }
}
