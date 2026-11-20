#include "thunder/parser.hpp"

#include "thunder/lexer.hpp"

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
        case TokenKind::KwOr:
            return binding_power::logical_or;
        case TokenKind::KwAnd:
            return binding_power::logical_and;
        case TokenKind::Equal:
        case TokenKind::NotEqual:
            return binding_power::equality;
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return binding_power::comparison;
        case TokenKind::Pipe:
            return binding_power::bitwise_or;
        case TokenKind::Caret:
            return binding_power::bitwise_xor;
        case TokenKind::Amp:
            return binding_power::bitwise_and;
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
            return binding_power::shift;
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
            if (peek().newline_before && current_ > statement_start)
            {
                return;
            }
            switch (peek().kind)
            {
            case TokenKind::Dedent:
            case TokenKind::KwStruct:
            case TokenKind::KwMixin:
            case TokenKind::Annotation:
            case TokenKind::KwInterface:
            case TokenKind::KwEnum:
            case TokenKind::KwVar:
            case TokenKind::KwConst:
            case TokenKind::KwFunc:
            case TokenKind::KwIf:
            case TokenKind::KwWhile:
            case TokenKind::KwFor:
            case TokenKind::KwReturn:
            case TokenKind::KwBreak:
            case TokenKind::KwContinue:
            case TokenKind::KwImport:
            case TokenKind::KwNative:
            case TokenKind::KwModule:
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

    std::vector<Annotation> Parser::parse_annotations()
    {
        std::vector<Annotation> annotations;
        while (check(TokenKind::Annotation))
        {
            const Token& token = advance();
            Annotation annotation;
            annotation.span = token.span;
            const std::optional<std::string> canonical = language_.annotation(token.text_value);
            // "@test("adds up")": one argument in brackets, on the same line.
            if (check(TokenKind::LeftParen) && !peek().newline_before)
            {
                advance();
                if (check(TokenKind::StringLiteral))
                {
                    const Token& text = advance();
                    annotation.argument = text.text_value;
                    annotation.argument_span = text.span;
                }
                else
                {
                    diagnostics_.report(DiagId::ExpectedToken, peek().span, language_.term("a string"),
                        describe(peek(), language_));
                }
                expect(TokenKind::RightParen);
                annotation.span = Span::merge(annotation.span, previous().span);
            }
            if (!canonical)
            {
                std::string known;
                for (const std::string_view name : known_annotations)
                {
                    known += (known.empty() ? "" : language_.display.separator) + std::string("@") + language_.annotation_spelling(name);
                }
                diagnostics_.report(DiagId::UnknownAnnotation, token.span, token.text_value, known);
                continue;
            }
            annotation.name = *canonical;
            annotations.push_back(std::move(annotation));
        }
        return annotations;
    }

    void Parser::expect_annotated_declaration(const std::vector<Annotation>& annotations, bool member)
    {
        if (annotations.empty())
        {
            return;
        }
        const bool declaration = check(TokenKind::KwFunc)
            || (!member && (check(TokenKind::KwStruct) || check(TokenKind::KwMixin) || check(TokenKind::KwModule)
                || check(TokenKind::KwInterface) || check(TokenKind::KwEnum) || check(TokenKind::KwNative)));
        if (!declaration)
        {
            diagnostics_.report(DiagId::OrphanAnnotation, peek().span, describe(peek(), language_));
        }
    }

    StmtPtr Parser::parse_item(bool top_level)
    {
        if (check(TokenKind::Annotation))
        {
            std::vector<Annotation> annotations = parse_annotations();
            expect_annotated_declaration(annotations, false);
            if (check(TokenKind::Annotation) || check(TokenKind::EndOfFile) || check(TokenKind::Dedent))
            {
                throw ParseError{};
            }
            StmtPtr item = parse_item(top_level);
            switch (item->kind)
            {
            case StmtKind::FuncDecl:
                static_cast<FuncDeclStmt&>(*item).annotations = std::move(annotations);
                break;
            case StmtKind::StructDecl:
                static_cast<StructDeclStmt&>(*item).annotations = std::move(annotations);
                break;
            case StmtKind::Module:
                static_cast<ModuleDeclStmt&>(*item).annotations = std::move(annotations);
                break;
            default:
                break; // interfaces and enums: the analyser says what an annotation cannot mark
            }
            if (item->kind == StmtKind::Interface || item->kind == StmtKind::EnumDecl)
            {
                for (const Annotation& annotation : annotations)
                {
                    diagnostics_.report(DiagId::AnnotationTarget, annotation.span,
                        language_.annotation_spelling(annotation.name),
                        std::format("'{}'", language_.spelling(item->kind == StmtKind::Interface
                                ? TokenKind::KwInterface : TokenKind::KwEnum)));
                }
            }
            return item;
        }
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
            return parse_function(false);
        }
        if (check(TokenKind::KwNative))
        {
            // "native func f(...)": the body is in a C++ library (see docs/native.md).
            const Span keyword = advance().span;
            if (!check(TokenKind::KwFunc))
            {
                diagnostics_.report(DiagId::ExpectedToken, peek().span,
                    "'" + language_.spelling(TokenKind::KwFunc) + "'", describe(peek(), language_));
                throw ParseError{};
            }
            if (!top_level)
            {
                diagnostics_.report(DiagId::NestedFunction, keyword);
            }
            StmtPtr func = parse_function(true);
            func->span = Span::merge(keyword, func->span);
            return func;
        }
        if (check(TokenKind::KwStruct) || check(TokenKind::KwMixin))
        {
            if (!top_level)
            {
                diagnostics_.report(DiagId::StructNotTopLevel, peek().span);
            }
            return parse_struct();
        }
        if (check(TokenKind::KwInterface))
        {
            if (!top_level)
            {
                diagnostics_.report(DiagId::InterfaceNotTopLevel, peek().span);
            }
            return parse_interface();
        }
        if (check(TokenKind::KwEnum))
        {
            if (!top_level)
            {
                diagnostics_.report(DiagId::EnumNotTopLevel, peek().span);
            }
            return parse_enum();
        }
        if (check(TokenKind::KwImport))
        {
            if (!top_level)
            {
                diagnostics_.report(DiagId::ImportNotTopLevel, peek().span);
            }
            return parse_import();
        }
        if (check(TokenKind::KwModule))
        {
            if (!top_level || in_module_)
            {
                diagnostics_.report(DiagId::ModuleNotTopLevel, peek().span);
            }
            return parse_module();
        }
        return parse_statement();
    }

    StmtPtr Parser::parse_module()
    {
        const Span start = advance().span; // 'module'
        const Token& name = expect(TokenKind::Identifier);
        auto module = std::make_unique<ModuleDeclStmt>(start, name.lexeme, name.span);
        expect_block_start(TokenKind::KwModule);
        const bool outer = std::exchange(in_module_, true);
        // The members are parsed like top-level items; the analyser rejects anything that is
        // not a declaration (a module only declares things).
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile) && !stop_parsing())
        {
            const std::size_t member_start = current_;
            try
            {
                module->members.push_back(parse_item(true));
            }
            catch (const ParseError&)
            {
                synchronize(member_start);
            }
        }
        in_module_ = outer;
        match(TokenKind::Dedent);
        module->span = span_from(start);
        return module;
    }

    bool Parser::line_ends_with_with() const
    {
        // The tokens of the current logical line: up to the next line break outside brackets,
        // or the Indent of the block that follows.
        int depth = 0;
        std::size_t last = current_;
        for (std::size_t i = current_; i < tokens_.size(); ++i)
        {
            const Token& token = tokens_[i];
            if (i > current_ && depth <= 0 && (token.newline_before || token.kind == TokenKind::Indent
                || token.kind == TokenKind::Dedent || token.kind == TokenKind::EndOfFile))
            {
                break;
            }
            if (token.kind == TokenKind::EndOfFile)
            {
                break;
            }
            switch (token.kind)
            {
            case TokenKind::LeftParen:
            case TokenKind::LeftBracket:
            case TokenKind::LeftBrace:
                ++depth;
                break;
            case TokenKind::RightParen:
            case TokenKind::RightBracket:
            case TokenKind::RightBrace:
                --depth;
                break;
            default:
                break;
            }
            last = i;
        }
        return last > current_ && tokens_[last].kind == TokenKind::KwWith;
    }

    StmtPtr Parser::parse_with(bool keyword_first)
    {
        const Span start = peek().span;
        // The keyword stands where this language puts it: English says "with handle = open()",
        // Pashto "handle سره = open()" (with the handle) and, for a value without a name,
        // "socket سره". Written in the other language's place it is reported, then understood.
        if (keyword_first == language_.with_after_resource)
        {
            const std::string keyword = language_.spelling(TokenKind::KwWith);
            diagnostics_.report(DiagId::WithPosition, keyword_first ? peek().span : start, keyword,
                language_.term(language_.with_after_resource ? "after the handle" : "at the start of the line"));
        }
        if (keyword_first)
        {
            advance(); // 'with'
        }
        auto with = std::make_unique<WithStmt>(start);
        // "with name = resource" names the resource; "with resource" (an existing variable, or a
        // value nobody needs by name) does not.
        const bool named = check(TokenKind::Identifier)
            && (peek(1).kind == TokenKind::Assign || peek(1).kind == TokenKind::Colon || at_handle_with());
        bool keyword_at_end = !keyword_first;
        if (named)
        {
            const Token& name = advance();
            with->name = name.lexeme;
            with->name_span = name.span;
            if (!keyword_first && check(TokenKind::KwWith))
            {
                advance(); // handle سره: the keyword follows the name
                keyword_at_end = false;
            }
            else if (!keyword_first && language_.with_after_resource)
            {
                // "handle = resource سره": the keyword belongs right after the handle.
                diagnostics_.report(DiagId::WithPosition, name.span, language_.spelling(TokenKind::KwWith),
                    language_.term("after the handle"));
            }
            if (match(TokenKind::Colon))
            {
                with->declared_type = parse_type();
            }
            expect(TokenKind::Assign);
        }
        with->resource = parse_expression();
        if (keyword_at_end)
        {
            expect(TokenKind::KwWith); // "socket سره": the keyword ends the line
        }
        with->body = parse_block(TokenKind::KwWith);
        with->span = span_from(start);
        return with;
    }

    bool Parser::at_handle_with() const
    {
        // "handle سره = ...": a name, then the keyword, then '=' or a type.
        return check(TokenKind::Identifier) && peek(1).kind == TokenKind::KwWith
            && (peek(2).kind == TokenKind::Assign || peek(2).kind == TokenKind::Colon);
    }

    std::string Parser::member_name()
    {
        // random.int, random.float: the word after a '.' may be a type word.
        switch (peek().kind)
        {
        case TokenKind::KwInt:
        case TokenKind::KwFloat:
        case TokenKind::KwBool:
        case TokenKind::KwString:
        case TokenKind::KwByte:
            return advance().lexeme;
        default:
            return expect(TokenKind::Identifier).lexeme;
        }
    }

    StmtPtr Parser::parse_import()
    {
        const Span start = advance().span; // 'import'
        // import native "mathx" loads a C++ library (docs/native.md).
        if (match(TokenKind::KwNative))
        {
            if (!check(TokenKind::StringLiteral))
            {
                fail_expected(language_.term("the name of a native library in quotes"));
            }
            const Token& library = advance();
            auto import = std::make_unique<ImportStmt>(span_from(start), library.text_value, library.span, true);
            import->is_native = true;
            end_statement();
            return import;
        }
        // import "lib/shapes" names a file; import os names a module built into the toolchain.
        if (!check(TokenKind::StringLiteral) && !check(TokenKind::Identifier))
        {
            fail_expected(language_.term("a module name or a file name in quotes"));
        }
        const Token& target = advance();
        const bool is_file = target.kind == TokenKind::StringLiteral;
        auto import = std::make_unique<ImportStmt>(span_from(start), is_file ? target.text_value : target.lexeme,
            target.span, is_file);
        end_statement();
        return import;
    }

    StmtPtr Parser::parse_struct()
    {
        const bool is_mixin = check(TokenKind::KwMixin);
        const TokenKind keyword = advance().kind; // 'struct' or 'mixin'
        const Span start = previous().span;
        const Token& name = expect(TokenKind::Identifier);
        auto decl = std::make_unique<StructDeclStmt>(start, name.lexeme, name.span);
        decl->is_mixin = is_mixin;
        parse_type_parameters(decl->type_params, decl->type_param_spans);
        // "struct Button: Widget, Clickable, UsefulMixin" - the interfaces the struct implements
        // and the structs (mixins) it includes.
        if (match(TokenKind::Colon))
        {
            do
            {
                const Token& base = expect(TokenKind::Identifier);
                std::string base_name = base.lexeme;
                Span base_span = base.span;
                // "ui.Widget": an interface or mixin declared in a module.
                while (check(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier && !peek().newline_before)
                {
                    advance();
                    base_name += "." + advance().lexeme;
                    base_span = Span::merge(base_span, previous().span);
                }
                decl->bases.push_back(std::move(base_name));
                decl->base_spans.push_back(base_span);
            } while (match(TokenKind::Comma));
        }
        // "struct AnyClock: Clock" with nothing below: a struct that only takes what its bases
        // give it (an interface's closures, a mixin's members, a mock's recording methods).
        if (!decl->bases.empty() && !check(TokenKind::Indent) && (peek().newline_before || check(TokenKind::EndOfFile)))
        {
            decl->span = span_from(start);
            return decl;
        }
        expect_block_start(keyword);
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile) && !stop_parsing())
        {
            const std::size_t member_start = current_;
            try
            {
                parse_member(decl.get(), nullptr);
            }
            catch (const ParseError&)
            {
                synchronize(member_start);
            }
        }
        match(TokenKind::Dedent);
        decl->span = span_from(start);
        return decl;
    }

    StmtPtr Parser::parse_interface()
    {
        const Span start = advance().span; // 'interface'
        const Token& name = expect(TokenKind::Identifier);
        auto decl = std::make_unique<InterfaceDeclStmt>(start, name.lexeme, name.span);
        // "interface Button: Clickable, Drawable" - the interfaces this one extends.
        if (match(TokenKind::Colon))
        {
            do
            {
                const Token& base = expect(TokenKind::Identifier);
                std::string base_name = base.lexeme;
                Span base_span = base.span;
                while (check(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier && !peek().newline_before)
                {
                    advance();
                    base_name += "." + advance().lexeme;
                    base_span = Span::merge(base_span, previous().span);
                }
                decl->bases.push_back(std::move(base_name));
                decl->base_spans.push_back(base_span);
            } while (match(TokenKind::Comma));
            // "interface Shape: Drawable" with nothing below: an interface that only joins others.
            if (!check(TokenKind::Indent) && (peek().newline_before || check(TokenKind::EndOfFile)))
            {
                decl->span = span_from(start);
                return decl;
            }
        }
        expect_block_start(TokenKind::KwInterface);
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile) && !stop_parsing())
        {
            const std::size_t member_start = current_;
            try
            {
                parse_member(nullptr, decl.get());
            }
            catch (const ParseError&)
            {
                synchronize(member_start);
            }
        }
        match(TokenKind::Dedent);
        decl->span = span_from(start);
        return decl;
    }

    void Parser::parse_member(StructDeclStmt* decl, InterfaceDeclStmt* interface)
    {
        if (check(TokenKind::Indent))
        {
            diagnostics_.report(DiagId::UnexpectedIndent, peek().span);
            skip_indented_block();
            throw ParseError{};
        }
        // "@test" before a method: a test of the suite the struct is.
        std::vector<Annotation> annotations = parse_annotations();
        expect_annotated_declaration(annotations, true);
        if (!annotations.empty() && interface != nullptr)
        {
            for (const Annotation& annotation : annotations)
            {
                diagnostics_.report(DiagId::AnnotationTarget, annotation.span, language_.annotation_spelling(annotation.name),
                    std::format("'{}'", language_.spelling(TokenKind::KwInterface)));
            }
        }
        // "func draw(...)": a method - with a body in a struct, a bare signature in an interface.
        if (check(TokenKind::KwFunc))
        {
            const Span start = advance().span;
            const Token& name = expect(TokenKind::Identifier);
            auto method = std::make_unique<FuncDeclStmt>(start, name.lexeme, name.span);
            method->is_method = true;
            method->annotations = std::move(annotations);
            parse_signature(method->params, method->return_type);
            if (decl != nullptr)
            {
                method->body = parse_block(TokenKind::KwFunc);
                method->span = span_from(start);
                decl->methods.push_back(std::move(method));
            }
            else
            {
                // In an interface a method is a bare signature, unless an indented block follows:
                // that is a default implementation, which an implementing struct may leave as it is.
                if (check(TokenKind::Indent))
                {
                    method->body = parse_block(TokenKind::KwFunc);
                    method->span = span_from(start);
                }
                else
                {
                    method->span = span_from(start);
                    end_statement();
                }
                interface->methods.push_back(std::move(method));
            }
            return;
        }
        // "Button(title: string)": the constructor - the member spelled like the struct.
        if (decl != nullptr && check(TokenKind::Identifier) && peek().lexeme == decl->name
            && peek(1).kind == TokenKind::LeftParen)
        {
            const Token& name = advance();
            auto ctor = std::make_unique<FuncDeclStmt>(name.span, name.lexeme, name.span);
            ctor->is_method = true;
            ctor->is_constructor = true;
            parse_signature(ctor->params, ctor->return_type);
            ctor->body = parse_block(TokenKind::KwFunc);
            ctor->span = span_from(name.span);
            decl->methods.push_back(std::move(ctor));
            return;
        }
        // "on_click: func()": a member whose value is a function (a closure), or
        // "_title: string": an ordinary field. Both are written the same way; a member is
        // callable exactly when its type is a function type.
        const Token& field_name = expect(TokenKind::Identifier);
        FieldDecl field;
        field.name = field_name.lexeme;
        field.name_span = field_name.span;
        expect(TokenKind::Colon);
        field.type = parse_type();
        field.callable = field.type.type.base == Type::Base::Function && field.type.type.dimensions == 0;
        if (interface != nullptr && !field.callable)
        {
            // An interface declares behaviour: methods and callable members, not plain fields.
            diagnostics_.report(DiagId::ExpectedToken, field_name.span,
                std::format("'{}'", language_.spelling(TokenKind::KwFunc)), describe(field_name, language_));
        }
        (decl != nullptr ? decl->fields : interface->fields).push_back(std::move(field));
        // Fields are separated by line breaks; ',' is also accepted.
        if (!match(TokenKind::Comma) && !check(TokenKind::Dedent) && !peek().newline_before)
        {
            // Recover in place: report the missing separator and carry on with the next field.
            diagnostics_.report(DiagId::ExpectedToken, peek().span, language_.term("a new line or ','"),
                describe(peek(), language_));
        }
    }

    StmtPtr Parser::parse_enum()
    {
        const Span start = advance().span; // 'enum'
        const Token& name = expect(TokenKind::Identifier);
        auto decl = std::make_unique<EnumDeclStmt>(start, name.lexeme, name.span);
        expect_block_start(TokenKind::KwEnum);
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile))
        {
            const Token& member = expect(TokenKind::Identifier);
            decl->members.push_back(member.lexeme);
            decl->member_spans.push_back(member.span);
            // Members are separated by line breaks; ',' is also accepted (several per line).
            if (!match(TokenKind::Comma) && !check(TokenKind::Dedent) && !peek().newline_before)
            {
                diagnostics_.report(DiagId::ExpectedToken, peek().span, language_.term("a new line or ','"),
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
        if (check(TokenKind::Dedent) || check(TokenKind::EndOfFile) || peek().newline_before)
        {
            return;
        }
        diagnostics_.report(DiagId::ExpectedStatementEnd, peek().span, describe(peek(), language_));
        throw ParseError{};
    }

    void Parser::parse_signature(std::vector<Param>& params, std::optional<TypeSyntax>& return_type,
        bool* deduced)
    {
        expect(TokenKind::LeftParen);
        if (!check(TokenKind::RightParen))
        {
            // A function value may leave the types out - "func(n): n * n" - and then they are
            // worked out from where it stands. All of them, or none: half a signature says less
            // than either, and the ':' cannot be both a type and the start of the body.
            const bool untyped = deduced != nullptr && check(TokenKind::Identifier)
                && peek(1).kind != TokenKind::Colon;
            do
            {
                const Token& param_name = expect(TokenKind::Identifier);
                Param param;
                param.name = param_name.lexeme;
                param.name_span = param_name.span;
                if (untyped)
                {
                    param.type.span = param_name.span; // filled in by semantic analysis
                }
                else
                {
                    expect(TokenKind::Colon);
                    param.type = parse_type();
                }
                params.push_back(std::move(param));
            } while (match(TokenKind::Comma));
            if (untyped)
            {
                *deduced = true;
            }
        }
        expect(TokenKind::RightParen);

        if (deduced != nullptr && *deduced)
        {
            return; // the ':' that follows opens the body
        }
        if (match(TokenKind::Colon))
        {
            return_type = parse_type();
        }
    }

    StmtPtr Parser::parse_function(bool is_native)
    {
        const Span start = advance().span; // 'func'
        const Token& name = expect(TokenKind::Identifier);
        auto func = std::make_unique<FuncDeclStmt>(start, name.lexeme, name.span);
        func->is_native = is_native;
        parse_type_parameters(func->type_params, func->type_param_spans);
        parse_signature(func->params, func->return_type);
        if (is_native)
        {
            // A native declaration is one line: the body lives in the library.
            end_statement();
            func->span = span_from(start);
            return func;
        }
        func->body = parse_block(TokenKind::KwFunc);
        func->span = span_from(start);
        return func;
    }

    // "<T, U>" after a struct or function name: the declaration is generic.
    void Parser::parse_type_parameters(std::vector<std::string>& names, std::vector<Span>& spans)
    {
        if (!check(TokenKind::Less) || peek().newline_before)
        {
            return;
        }
        advance();
        const NestingGuard nested(*this);
        do
        {
            const Token& name = expect(TokenKind::Identifier);
            names.push_back(name.lexeme);
            spans.push_back(name.span);
        } while (match(TokenKind::Comma));
        expect_closing_angle();
    }

    // The '>' that closes a type argument list. "Stack<Stack<int>>" lexes its last two
    // characters as one '>>' token (maximal munch), and "var x: Box<int>= 1" as '>='; here
    // the first '>' is consumed and the rest of the token is put back for the next reader.
    void Parser::expect_closing_angle()
    {
        if (check(TokenKind::ShiftRight) || check(TokenKind::GreaterEqual))
        {
            Token& token = tokens_[current_];
            token.kind = token.kind == TokenKind::ShiftRight ? TokenKind::Greater : TokenKind::Assign;
            token.lexeme = token.kind == TokenKind::Greater ? ">" : "=";
            token.span.start.column += 1;
            token.span.length -= 1;
            return; // the remaining character stays as the current token
        }
        expect(TokenKind::Greater);
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
        case TokenKind::KwFloat:
            syntax.type = Type::float_type();
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
        case TokenKind::KwByte:
            syntax.type = Type::byte_type();
            advance();
            break;
        case TokenKind::Identifier:
            syntax.struct_name = token.lexeme; // resolved by semantic analysis
            advance();
            // "stack.Stack", "graphics.Color": a type declared in a module.
            while (check(TokenKind::Dot) && peek(1).kind == TokenKind::Identifier && !peek().newline_before)
            {
                advance();
                syntax.struct_name += "." + advance().lexeme;
            }
            // "Stack<int>": the type arguments of a generic struct. In a type, a name is
            // never compared, so '<' here always opens the arguments.
            if (check(TokenKind::Less) && !peek().newline_before)
            {
                advance();
                const NestingGuard nested(*this);
                do
                {
                    syntax.parts.push_back(parse_type());
                } while (match(TokenKind::Comma));
                expect_closing_angle();
            }
            break;
        case TokenKind::KwFunc: // func(int, string): bool - the type of a function value
        {
            advance();
            expect(TokenKind::LeftParen);
            std::vector<Type> parameter_types;
            TypeSyntax result;
            result.type = Type::void_type();
            result.span = token.span;
            syntax.parts.push_back(std::move(result)); // parts: the result type, then the parameter types
            {
                const NestingGuard nested(*this);
                if (!check(TokenKind::RightParen))
                {
                    do
                    {
                        syntax.parts.push_back(parse_type());
                        parameter_types.push_back(syntax.parts.back().type);
                    } while (match(TokenKind::Comma));
                }
                expect(TokenKind::RightParen);
            }
            if (check(TokenKind::Colon) && !peek().newline_before)
            {
                advance();
                syntax.parts[0] = parse_type();
            }
            syntax.type = Type::function_of(std::move(parameter_types), syntax.parts[0].type);
            break;
        }
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
            if (syntax.parts.size() == 1 && syntax.parts.front().type.base == Type::Base::Function)
            {
                // (func(int): int)[] - brackets around a function type, to make an array of functions.
                TypeSyntax inner = std::move(syntax.parts.front());
                syntax = std::move(inner);
                break;
            }
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
        case TokenKind::KwWith:
            return parse_with(true);
        default:
        {
            if (line_ends_with_with() || at_handle_with())
            {
                return parse_with(false);
            }
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
        // A block that is the body of a function value written inside brackets also ends at the
        // bracket that holds it: no statement can begin with ')' or ','.
        while (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile) && !check(TokenKind::RightParen)
            && !check(TokenKind::RightBracket) && !check(TokenKind::RightBrace) && !check(TokenKind::Comma)
            && !stop_parsing())
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

    bool Parser::at_for_each() const
    {
        // "for x in a" and "for (k, v) in a" differ from "for i = 0, i < n, i = i + 1" only after
        // the loop variables, so look ahead for the 'in' that follows them.
        std::size_t ahead = 0;
        if (check(TokenKind::LeftParen))
        {
            ahead = 1;
            while (true)
            {
                if (peek(ahead).kind != TokenKind::Identifier)
                {
                    return false;
                }
                ++ahead;
                if (peek(ahead).kind != TokenKind::Comma)
                {
                    break;
                }
                ++ahead;
            }
            if (peek(ahead).kind != TokenKind::RightParen)
            {
                return false;
            }
            ++ahead;
        }
        else if (peek(0).kind == TokenKind::Identifier)
        {
            ahead = 1;
        }
        else
        {
            return false;
        }
        return peek(ahead).kind == TokenKind::KwIn;
    }

    StmtPtr Parser::parse_for_each(const Span& start)
    {
        auto loop = std::make_unique<ForEachStmt>(start);
        if (match(TokenKind::LeftParen))
        {
            do
            {
                const Token& name = expect(TokenKind::Identifier);
                loop->names.push_back(name.lexeme);
                loop->name_spans.push_back(name.span);
            } while (match(TokenKind::Comma));
            expect(TokenKind::RightParen);
        }
        else
        {
            const Token& name = expect(TokenKind::Identifier);
            loop->names.push_back(name.lexeme);
            loop->name_spans.push_back(name.span);
        }
        expect(TokenKind::KwIn);
        loop->collection = parse_expression();
        loop->body = parse_block(TokenKind::KwFor);
        loop->span = span_from(start);
        return loop;
    }

    StmtPtr Parser::parse_for()
    {
        const Span start = advance().span; // 'for'
        if (at_for_each())
        {
            return parse_for_each(start);
        }
        auto loop = std::make_unique<ForStmt>(start);
        // for init, condition, step - the header may be written in parentheses.
        const bool parenthesised = match(TokenKind::LeftParen);
        {
            std::optional<NestingGuard> nested;
            if (parenthesised)
            {
                nested.emplace(*this); // the loop header may then span several lines
            }
            const TokenKind header_end = parenthesised ? TokenKind::RightParen : TokenKind::Indent;
            if (!check(TokenKind::Comma))
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
            expect(TokenKind::Comma);

            if (!check(TokenKind::Comma))
            {
                loop->condition = parse_expression();
            }
            expect(TokenKind::Comma);

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
        if (!check(TokenKind::Dedent) && !check(TokenKind::EndOfFile) && !peek().newline_before)
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
        const TokenKind next = peek().kind;
        const std::optional<TokenKind> compound =
            Lexer::compound_operator(next) ? std::optional(next) : std::nullopt; // '+=', '-=', ...
        if (compound || match(TokenKind::Assign))
        {
            if (compound)
            {
                advance();
            }
            if (expr->kind != ExprKind::Name && expr->kind != ExprKind::Index && expr->kind != ExprKind::Field)
            {
                diagnostics_.report(DiagId::InvalidAssignmentTarget, expr->span); // tuples are immutable, too
            }
            ExprPtr value = parse_expression();
            const Span span = Span::merge(expr->span, value->span);
            auto assign = std::make_unique<AssignStmt>(span, std::move(expr), std::move(value));
            assign->compound_op = compound;
            return assign;
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
        case TokenKind::FloatLiteral:
            advance();
            return std::make_unique<FloatLiteralExpr>(token.span, token.float_value);
        case TokenKind::StringLiteral:
            advance();
            return std::make_unique<StringLiteralExpr>(token.span, token.text_value);
        case TokenKind::KwTrue:
        case TokenKind::KwFalse:
            advance();
            return std::make_unique<BoolLiteralExpr>(token.span, token.kind == TokenKind::KwTrue);
        case TokenKind::KwNothing:
            advance();
            return std::make_unique<NothingLiteralExpr>(token.span);
        case TokenKind::Identifier:
            advance();
            return std::make_unique<NameExpr>(token.span, token.lexeme);
        case TokenKind::KwMy:
            // "my" is the object the method was called on - the receiver the compiler passes as
            // a hidden first parameter. It is simply that parameter's name.
            advance();
            return std::make_unique<NameExpr>(token.span, "(" + language_.term("self") + ")");
        case TokenKind::KwString:
            if (peek(1).kind == TokenKind::Dot)
            {
                // string.trim(s): the built-in module is spelled like the type.
                advance();
                return std::make_unique<NameExpr>(token.span, token.lexeme);
            }
            diagnostics_.report(DiagId::ExpectedExpression, token.span, describe(token, language_));
            throw ParseError{};
        case TokenKind::KwFunc:
            // func(x: int): int ... - a function value written in place (a closure).
            return parse_lambda();
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
        case TokenKind::Tilde:
        case TokenKind::KwNot:
        {
            // "-x" and "~x" bind tightly; "not" binds less tightly than a comparison, as in Python:
            // not a == b means not (a == b).
            const Span start = advance().span;
            ExprPtr operand = parse_expression(token.kind == TokenKind::KwNot ? binding_power::logical_not
                                                                              : binding_power::prefix);
            const Span span = Span::merge(start, operand->span);
            return std::make_unique<UnaryExpr>(span, token.kind, std::move(operand));
        }
        default:
            if (token.newline_before && current_ > 0 && nesting_ == 0)
            {
                // e.g. "x = a *" followed by a line break: point at the end of the incomplete line.
                const Span& prev = previous().span;
                const Span end{prev.offset + prev.length, 1, prev.end, Position{prev.end.line, prev.end.column + 1}, prev.file};
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
            const Span field_span = peek().span;
            std::string field = member_name();
            const Span span = span_from(left->span);
            return std::make_unique<FieldExpr>(span, std::move(left), std::move(field), field_span);
        }
        if (match(TokenKind::LeftBracket))
        {
            const NestingGuard nested(*this);
            ExprPtr index = parse_expression();
            expect(TokenKind::RightBracket);
            const Span span = span_from(left->span);
            return std::make_unique<IndexExpr>(span, std::move(left), std::move(index));
        }

        // Function call: a name (a function, or a variable holding a function value), or any other
        // expression whose value is a function, e.g. handlers[i](x). Literals can never be called.
        const Token& paren = advance();
        switch (left->kind)
        {
        case ExprKind::IntLiteral:
        case ExprKind::FloatLiteral:
        case ExprKind::BoolLiteral:
        case ExprKind::NothingLiteral:
        case ExprKind::StringLiteral:
        case ExprKind::ArrayLiteral:
        case ExprKind::TupleLiteral:
        case ExprKind::MapLiteral:
            diagnostics_.report(DiagId::NotCallable, Span::merge(left->span, paren.span));
            throw ParseError{};
        default:
            break;
        }
        std::unique_ptr<CallExpr> call;
        if (left->kind == ExprKind::Name)
        {
            const auto& name = static_cast<const NameExpr&>(*left);
            call = std::make_unique<CallExpr>(left->span, name.name, name.span);
        }
        else
        {
            call = std::make_unique<CallExpr>(left->span, std::string(), left->span);
            call->callee_expr = std::move(left);
        }
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
        call->span = span_from(call->callee_span);
        return call;
    }

    ExprPtr Parser::parse_lambda()
    {
        const Span start = advance().span; // 'func'
        auto lambda = std::make_unique<LambdaExpr>(start);
        parse_signature(lambda->params, lambda->return_type, &lambda->deduced);
        if (lambda->deduced)
        {
            // "func(n): n * n" - the ':' separates the parameters from the body, and every type
            // comes from where the function value stands.
            expect(TokenKind::Colon);
            if (check(TokenKind::Indent))
            {
                lambda->body = parse_block(TokenKind::KwFunc);
            }
            else if (!peek().newline_before && !check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)
                && !check(TokenKind::RightParen) && !check(TokenKind::RightBracket) && !check(TokenKind::Comma))
            {
                const Span body_start = peek().span;
                auto block = std::make_unique<BlockStmt>(body_start);
                StmtPtr body = parse_simple_statement();
                if (body->kind == StmtKind::ExprStmt)
                {
                    // One expression: its value is what the function answers with, unless the
                    // function turns out to answer with nothing.
                    ExprPtr value = std::move(static_cast<ExprStmt&>(*body).expr);
                    const Span value_span = value->span;
                    auto ret = std::make_unique<ReturnStmt>(value_span, std::move(value));
                    ret->implicit = true;
                    body = std::move(ret);
                }
                block->statements.push_back(std::move(body));
                block->span = span_from(body_start);
                lambda->body = std::move(block);
            }
            else
            {
                diagnostics_.report(DiagId::ExpectedLambdaBody, peek().span);
                throw ParseError{};
            }
            lambda->span = span_from(start);
            return lambda;
        }
        if (check(TokenKind::Indent))
        {
            // The body is the indented block that follows (possible outside parentheses only:
            // inside them, line breaks and indentation do not count).
            lambda->body = parse_block(TokenKind::KwFunc);
        }
        else if (!peek().newline_before && !check(TokenKind::Dedent) && !check(TokenKind::EndOfFile)
            && !check(TokenKind::RightParen) && !check(TokenKind::RightBracket) && !check(TokenKind::Comma))
        {
            // The body is a single statement on the same line: "func() count += 1". With a
            // return type it is a single expression, whose value the function returns.
            const Span body_start = peek().span;
            auto block = std::make_unique<BlockStmt>(body_start);
            if (lambda->return_type)
            {
                ExprPtr value = parse_expression();
                const Span value_span = value->span;
                block->statements.push_back(std::make_unique<ReturnStmt>(value_span, std::move(value)));
            }
            else
            {
                block->statements.push_back(parse_simple_statement());
            }
            block->span = span_from(body_start);
            lambda->body = std::move(block);
        }
        else
        {
            diagnostics_.report(DiagId::ExpectedLambdaBody, peek().span);
            throw ParseError{};
        }
        lambda->span = span_from(start);
        return lambda;
    }
}
