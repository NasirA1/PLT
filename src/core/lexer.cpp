#include "thunder/lexer.hpp"

#include "thunder/utf8.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace thunder
{
    Lexer::Lexer(const SourceFile& source, const LanguagePack& language, DiagnosticBag& diagnostics)
        : source_(source)
        , language_(language)
        , diagnostics_(diagnostics)
        , text_(source.text())
    {
    }

    bool Lexer::continues_line(TokenKind kind)
    {
        return token_category(kind) == TokenCategory::Operator || kind == TokenKind::Comma;
    }

    std::size_t Lexer::indentation_of_line(std::size_t offset) const
    {
        std::size_t start = offset;
        while (start > 0 && text_[start - 1] != '\n')
        {
            --start;
        }
        std::size_t width = 0;
        for (std::size_t i = start; i < offset && (text_[i] == ' ' || text_[i] == '\t'); ++i)
        {
            width = text_[i] == '\t' ? (width / 4 + 1) * 4 : width + 1;
        }
        return width;
    }

    namespace
    {
        Token marker(TokenKind kind, const Token& next)
        {
            Token token;
            token.kind = kind;
            token.span = Span{next.span.offset, 0, next.span.start, next.span.start};
            token.newline_before = true;
            return token;
        }
    }

    std::vector<Token> Lexer::tokenize()
    {
        std::vector<Token> tokens;
        std::vector<std::size_t> levels{0}; // the indentation widths of the open blocks
        int depth = 0;                      // open brackets: line breaks inside them are not significant
        TokenKind previous = TokenKind::EndOfFile;
        while (true)
        {
            const bool newline = skip_layout_and_comments() || tokens.empty();
            Token token = next_token();
            token.newline_before = newline;
            if (token.kind == TokenKind::EndOfFile)
            {
                for (std::size_t i = 1; i < levels.size(); ++i)
                {
                    tokens.push_back(marker(TokenKind::Dedent, token));
                }
                tokens.push_back(std::move(token));
                return tokens;
            }

            if (newline && depth == 0 && !continues_line(previous) && token.kind != TokenKind::Error)
            {
                const std::size_t width = indentation_of_line(token.span.offset);
                if (width > levels.back())
                {
                    levels.push_back(width);
                    tokens.push_back(marker(TokenKind::Indent, token));
                }
                else
                {
                    while (width < levels.back())
                    {
                        levels.pop_back();
                        tokens.push_back(marker(TokenKind::Dedent, token));
                    }
                    if (width != levels.back())
                    {
                        diagnostics_.report(DiagId::InconsistentIndentation, token.span);
                    }
                }
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
                depth = depth > 0 ? depth - 1 : 0;
                break;
            default:
                break;
            }
            previous = token.kind;
            tokens.push_back(std::move(token));
        }
    }

    char32_t Lexer::peek(std::size_t ahead) const
    {
        std::size_t pos = pos_;
        for (std::size_t i = 0; i < ahead && pos < text_.size(); ++i)
        {
            pos += utf8::decode(text_, pos).length;
        }
        return pos < text_.size() ? utf8::decode(text_, pos).code_point : 0;
    }

    char32_t Lexer::advance()
    {
        const utf8::Decoded decoded = utf8::decode(text_, pos_);
        pos_ += decoded.length;
        if (decoded.code_point == U'\n')
        {
            ++position_.line;
            position_.column = 1;
        }
        else
        {
            ++position_.column;
        }
        return decoded.code_point;
    }

    bool Lexer::is_symbol(char32_t cp)
    {
        // Punctuation, symbols and digits outside ASCII that must not be read as letters.
        return (cp >= 0x00A1 && cp <= 0x00BF) || cp == 0x00D7 || cp == 0x00F7 // Latin-1 symbols, × ÷
            || cp == 0x060C || cp == 0x060D || cp == 0x061B || cp == 0x061F     // ، ؍ ؛ ؟
            || (cp >= 0x066A && cp <= 0x066D) || cp == 0x06D4                   // ٪ ٫ ٬ ٭ ۔
            || (cp >= 0x2010 && cp <= 0x205E && cp != 0x200C && cp != 0x200D)   // general punctuation
            || (cp >= 0x2190 && cp <= 0x23FF)                                    // arrows, math operators
            || digit_value(cp).has_value() || cp == utf8::replacement_character;
    }

    bool Lexer::is_identifier_start(char32_t cp)
    {
        // Any non-ASCII letter-like code point is accepted so identifiers can be written in
        // other scripts (e.g. Pashto).
        return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') || cp == U'_'
            || (cp >= 0x80 && !utf8::is_space(cp) && !is_symbol(cp));
    }

    bool Lexer::is_identifier_part(char32_t cp)
    {
        return is_identifier_start(cp) || digit_value(cp).has_value();
    }

    Token Lexer::make_token(TokenKind kind, std::size_t start, Position start_position) const
    {
        Token token;
        token.kind = kind;
        token.lexeme = std::string(text_.substr(start, pos_ - start));
        token.span.offset = static_cast<std::uint32_t>(start);
        token.span.length = static_cast<std::uint32_t>(pos_ - start);
        token.span.start = start_position;
        token.span.end = position_;
        return token;
    }

    bool Lexer::skip_layout_and_comments()
    {
        const std::uint32_t start_line = position_.line;
        while (!at_end())
        {
            const char32_t c = peek();
            if (utf8::is_space(c))
            {
                advance();
            }
            else if (c == U'/' && peek(1) == U'/')
            {
                while (!at_end() && peek() != U'\n')
                {
                    advance();
                }
            }
            else if (c == U'/' && peek(1) == U'*')
            {
                const std::size_t start = pos_;
                const Position start_position = position_;
                advance();
                advance();
                bool closed = false;
                while (!at_end())
                {
                    if (peek() == U'*' && peek(1) == U'/')
                    {
                        advance();
                        advance();
                        closed = true;
                        break;
                    }
                    advance();
                }
                if (!closed)
                {
                    Span span = make_token(TokenKind::Error, start, start_position).span;
                    span.length = 2;
                    span.end = Position{start_position.line, start_position.column + 2};
                    diagnostics_.report(DiagId::UnterminatedComment, span);
                }
            }
            else
            {
                break;
            }
        }
        return position_.line != start_line;
    }

    Token Lexer::next_token()
    {
        const std::size_t start = pos_;
        const Position start_position = position_;
        if (at_end())
        {
            return make_token(TokenKind::EndOfFile, start, start_position);
        }

        const char32_t c = peek();
        if (digit_value(c))
        {
            return scan_number();
        }
        if (const auto closing = language_.closing_quote(c))
        {
            return scan_string(*closing);
        }
        if (const auto kind = language_.extra_punctuation(c))
        {
            advance();
            return make_token(*kind, start, start_position);
        }
        if (is_identifier_start(c))
        {
            return scan_identifier_or_keyword();
        }

        advance();
        const auto one = [&](TokenKind kind) { return make_token(kind, start, start_position); };
        const auto two = [&](char32_t second, TokenKind if_two, TokenKind if_one)
        {
            if (peek() == second)
            {
                advance();
                return make_token(if_two, start, start_position);
            }
            return make_token(if_one, start, start_position);
        };

        switch (c)
        {
        case U'(':
            return one(TokenKind::LeftParen);
        case U')':
            return one(TokenKind::RightParen);
        case U'{':
            return one(TokenKind::LeftBrace);
        case U'}':
            return one(TokenKind::RightBrace);
        case U'[':
            return one(TokenKind::LeftBracket);
        case U']':
            return one(TokenKind::RightBracket);
        case U',':
            return one(TokenKind::Comma);
        case U';':
            return one(TokenKind::Semicolon);
        case U':':
            return one(TokenKind::Colon);
        case U'.':
            return one(TokenKind::Dot);
        case U'+':
            return one(TokenKind::Plus);
        case U'-':
            return one(TokenKind::Minus);
        case U'*':
            return one(TokenKind::Star);
        case U'/':
            return one(TokenKind::Slash);
        case U'%':
            return one(TokenKind::Percent);
        case U'=':
            return two(U'=', TokenKind::Equal, TokenKind::Assign);
        case U'!':
            return two(U'=', TokenKind::NotEqual, TokenKind::Bang);
        case U'<':
            return two(U'=', TokenKind::LessEqual, TokenKind::Less);
        case U'>':
            return two(U'=', TokenKind::GreaterEqual, TokenKind::Greater);
        case U'&':
        case U'|':
        {
            const TokenKind kind = c == U'&' ? TokenKind::AndAnd : TokenKind::OrOr;
            if (peek() == c)
            {
                advance();
                return make_token(kind, start, start_position);
            }
            Token error = make_token(TokenKind::Error, start, start_position);
            diagnostics_.report(DiagId::UnexpectedCharacterHint, error.span, error.lexeme,
                token_canonical_spelling(kind));
            return error;
        }
        default:
        {
            Token error = make_token(TokenKind::Error, start, start_position);
            diagnostics_.report(DiagId::UnexpectedCharacter, error.span, error.lexeme);
            return error;
        }
        }
    }

    Token Lexer::scan_number()
    {
        const std::size_t start = pos_;
        const Position start_position = position_;

        std::uint64_t value = 0;
        bool overflow = false;
        while (const auto next = digit_value(peek()))
        {
            advance();
            const auto digit = static_cast<std::uint64_t>(*next);
            constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            if (value > (max - digit) / 10)
            {
                overflow = true;
            }
            else
            {
                value = value * 10 + digit;
            }
        }

        // A number running straight into letters (e.g. "12abc") is a single malformed token.
        if (is_identifier_start(peek()))
        {
            while (is_identifier_part(peek()))
            {
                advance();
            }
            Token error = make_token(TokenKind::Error, start, start_position);
            diagnostics_.report(DiagId::MalformedNumber, error.span, error.lexeme);
            return error;
        }

        Token token = make_token(TokenKind::IntLiteral, start, start_position);
        if (overflow)
        {
            diagnostics_.report(DiagId::IntegerTooLarge, token.span, token.lexeme);
            token.kind = TokenKind::Error;
            return token;
        }
        token.int_value = static_cast<std::int64_t>(value);
        return token;
    }

    std::string Lexer::scan_word()
    {
        const std::size_t start = pos_;
        while (is_identifier_part(peek()))
        {
            advance();
        }
        return std::string(text_.substr(start, pos_ - start));
    }

    Token Lexer::scan_identifier_or_keyword()
    {
        const std::size_t start = pos_;
        const Position start_position = position_;
        std::string words = scan_word();

        // Some languages spell a keyword or built-in with several words (Pashto "کرښه چاپ" for
        // println). When the word read so far begins such a name, look ahead across spaces on the
        // same line for the remaining words and join them into one token. The lexeme keeps a single
        // space between the words; the span covers the text as written.
        std::string accepted = words;
        std::size_t accepted_pos = pos_;
        Position accepted_position = position_;
        while (language_.starts_compound(words))
        {
            while (peek() == U' ' || peek() == U'\t')
            {
                advance();
            }
            if (!is_identifier_start(peek()))
            {
                break;
            }
            words += ' ';
            words += scan_word();
            if (language_.keyword(words) || language_.builtin(words))
            {
                accepted = words;
                accepted_pos = pos_;
                accepted_position = position_;
            }
        }
        pos_ = accepted_pos;
        position_ = accepted_position;

        Token token = make_token(TokenKind::Identifier, start, start_position);
        token.lexeme = std::move(accepted);
        if (const auto keyword = language_.keyword(token.lexeme))
        {
            token.kind = *keyword;
        }
        return token;
    }

    Token Lexer::scan_string(char32_t closing_quote)
    {
        const std::size_t start = pos_;
        const Position start_position = position_;
        advance(); // opening quote

        std::string value;
        while (true)
        {
            if (at_end() || peek() == U'\n' || peek() == U'\r')
            {
                Token error = make_token(TokenKind::Error, start, start_position);
                diagnostics_.report(DiagId::UnterminatedString, error.span);
                return error;
            }

            const std::size_t char_start = pos_;
            const Position char_position = position_;
            const char32_t c = advance();
            if (c == closing_quote)
            {
                break;
            }
            if (c != U'\\')
            {
                utf8::append(value, c);
                continue;
            }

            if (at_end() || peek() == U'\n')
            {
                continue; // reported as unterminated on the next iteration
            }
            const char32_t escape = advance();
            switch (escape)
            {
            case U'n':
                value += '\n';
                break;
            case U't':
                value += '\t';
                break;
            case U'r':
                value += '\r';
                break;
            case U'0':
                value += '\0';
                break;
            case U'"':
                value += '"';
                break;
            case U'\u00AB': // « and » (Pashto quotes)
            case U'\u00BB':
                utf8::append(value, escape);
                break;
            case U'\\':
                value += '\\';
                break;
            default:
            {
                Span span = make_token(TokenKind::Error, char_start, char_position).span;
                std::string text;
                utf8::append(text, escape);
                diagnostics_.report(DiagId::InvalidEscape, span, text);
                utf8::append(value, escape);
                break;
            }
            }
        }

        Token token = make_token(TokenKind::StringLiteral, start, start_position);
        token.text_value = std::move(value);
        return token;
    }
}
