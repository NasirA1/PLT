#include "thunder/lexer.hpp"

#include "thunder/utf8.hpp"

#include <charconv>
#include <cmath>
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
        // A line ending with an operator (also "and", "or", "not") continues on the next line. A comma
        // does not: outside brackets (where line breaks do not matter anyway) commas only separate the
        // parts of a for loop and the fields of a struct, and "for ,," must still open a block.
        return token_category(kind) == TokenCategory::Operator;
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
            token.span = Span{next.span.offset, 0, next.span.start, next.span.start, next.span.file};
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
            previous_kind_ = previous;
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

    std::optional<TokenKind> Lexer::compound_assignment(TokenKind op)
    {
        switch (op)
        {
        case TokenKind::Plus:
            return TokenKind::PlusAssign;
        case TokenKind::Minus:
            return TokenKind::MinusAssign;
        case TokenKind::Star:
            return TokenKind::StarAssign;
        case TokenKind::Slash:
            return TokenKind::SlashAssign;
        case TokenKind::Percent:
            return TokenKind::PercentAssign;
        default:
            return std::nullopt;
        }
    }

    std::optional<TokenKind> Lexer::compound_operator(TokenKind assignment)
    {
        switch (assignment)
        {
        case TokenKind::PlusAssign:
            return TokenKind::Plus;
        case TokenKind::MinusAssign:
            return TokenKind::Minus;
        case TokenKind::StarAssign:
            return TokenKind::Star;
        case TokenKind::SlashAssign:
            return TokenKind::Slash;
        case TokenKind::PercentAssign:
            return TokenKind::Percent;
        default:
            return std::nullopt;
        }
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
        token.span.file = source_.id();
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
            // The language's own operator signs also form compound assignments: ×= is *=.
            if (peek() == U'=')
            {
                if (const auto compound = compound_assignment(*kind))
                {
                    advance();
                    return make_token(*compound, start, start_position);
                }
            }
            return make_token(*kind, start, start_position);
        }
        if (is_identifier_start(c))
        {
            return scan_identifier_or_keyword();
        }

        advance();
        const auto one = [&](TokenKind kind) { return make_token(kind, start, start_position); };
        // A symbol Thunder does not use but other languages do: reported with what to write instead.
        const auto suggest = [&](TokenKind instead)
        {
            Token error = make_token(TokenKind::Error, start, start_position);
            diagnostics_.report(DiagId::UnexpectedCharacterHint, error.span, error.lexeme, language_.spelling(instead));
            return error;
        };
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
        case U':':
            return one(TokenKind::Colon);
        case U'.':
            return one(TokenKind::Dot);
        case U'+':
            return two(U'=', TokenKind::PlusAssign, TokenKind::Plus);
        case U'-':
            return two(U'=', TokenKind::MinusAssign, TokenKind::Minus);
        case U'*':
            return two(U'=', TokenKind::StarAssign, TokenKind::Star);
        case U'/':
            return two(U'=', TokenKind::SlashAssign, TokenKind::Slash);
        case U'%':
            return two(U'=', TokenKind::PercentAssign, TokenKind::Percent);
        case U'=':
            return two(U'=', TokenKind::Equal, TokenKind::Assign);
        case U'!':
            if (peek() == U'=')
            {
                advance();
                return one(TokenKind::NotEqual);
            }
            return suggest(TokenKind::KwNot); // "!x" is written "not x"
        case U'<':
            if (peek() == U'<')
            {
                advance();
                return one(TokenKind::ShiftLeft);
            }
            return two(U'=', TokenKind::LessEqual, TokenKind::Less);
        case U'>':
            if (peek() == U'>')
            {
                advance();
                return one(TokenKind::ShiftRight);
            }
            return two(U'=', TokenKind::GreaterEqual, TokenKind::Greater);
        case U'&':
        case U'|':
            // One '&' or '|' is a bitwise operator; "a && b" and "a || b" are written
            // "a and b" and "a or b".
            if (peek() == c)
            {
                advance();
                return suggest(c == U'&' ? TokenKind::KwAnd : TokenKind::KwOr);
            }
            return one(c == U'&' ? TokenKind::Amp : TokenKind::Pipe);
        case U'^':
            return one(TokenKind::Caret);
        case U'~':
            return one(TokenKind::Tilde);
        case U';':
        case U'\u061B': // Arabic semicolon
            return suggest(TokenKind::Comma); // the parts of a for loop are separated by commas
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
        std::string plain; // the same number with ASCII digits, so it can be read as a double
        constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

        // 0x2A (hexadecimal) and 0b101010 (binary): a zero in any script, the marker letter, then
        // the digits of that base (hex letters in either case). Whole numbers only.
        std::uint64_t base = 10;
        if (digit_value(peek()) == 0 && (peek(1) == U'x' || peek(1) == U'X' || peek(1) == U'b' || peek(1) == U'B'))
        {
            base = (peek(1) == U'x' || peek(1) == U'X') ? 16 : 2;
            advance();
            advance();
            bool any_digit = false;
            while (true)
            {
                const char32_t c = peek();
                int digit = -1;
                if (const auto decimal_digit = digit_value(c))
                {
                    digit = *decimal_digit;
                }
                else if (base == 16 && c >= U'a' && c <= U'f')
                {
                    digit = static_cast<int>(c - U'a') + 10;
                }
                else if (base == 16 && c >= U'A' && c <= U'F')
                {
                    digit = static_cast<int>(c - U'A') + 10;
                }
                if (digit < 0 || static_cast<std::uint64_t>(digit) >= base)
                {
                    break;
                }
                advance();
                any_digit = true;
                const auto unsigned_digit = static_cast<std::uint64_t>(digit);
                if (value > (max - unsigned_digit) / base)
                {
                    overflow = true;
                }
                else
                {
                    value = value * base + unsigned_digit;
                }
            }
            if (!any_digit)
            {
                // "0x" or "0b7": take whatever follows into one malformed token.
                while (is_identifier_part(peek()) || digit_value(peek()))
                {
                    advance();
                }
                Token error = make_token(TokenKind::Error, start, start_position);
                diagnostics_.report(DiagId::MalformedNumber, error.span, error.lexeme);
                return error;
            }
        }
        else
        {
            while (const auto next = digit_value(peek()))
            {
                advance();
                const auto digit = static_cast<std::uint64_t>(*next);
                plain += static_cast<char>('0' + digit);
                if (value > (max - digit) / 10)
                {
                    overflow = true;
                }
                else
                {
                    value = value * 10 + digit;
                }
            }
        }

        // A decimal point followed by a digit makes this a decimal number: 3.14, ۳٫۱۴.
        // "p.0" is still a tuple element, because a number never starts with a point.
        // "t.0.1" is the element 1 of the element 0 of t, not "t" followed by "0.1", so a number
        // that already follows a '.' never takes a decimal part of its own.
        const bool decimal = base == 10 && previous_kind_ != TokenKind::Dot
            && (peek() == U'.' || peek() == language_.decimal_point) && digit_value(peek(1)).has_value();
        if (decimal)
        {
            advance();
            plain += '.';
            while (const auto next = digit_value(peek()))
            {
                advance();
                plain += static_cast<char>('0' + *next);
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

        if (decimal)
        {
            Token token = make_token(TokenKind::FloatLiteral, start, start_position);
            double parsed = 0.0;
            const auto [ptr, error] = std::from_chars(plain.data(), plain.data() + plain.size(), parsed);
            if (error != std::errc{} || !std::isfinite(parsed))
            {
                diagnostics_.report(DiagId::FloatTooLarge, token.span, token.lexeme);
                token.kind = TokenKind::Error;
                return token;
            }
            token.float_value = parsed;
            return token;
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
            const auto curly = [](char32_t q) { return q == U'\u201C' || q == U'\u201D' || q == U'\u201E'; };
            // Keyboard layouts differ in which curly double quote they produce, so any member of
            // the family closes a string that one of them opened.
            if (c == closing_quote || (curly(closing_quote) && curly(c)))
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
            case U'\u201C': // “ ” „ (curly double quotes, also Pashto string delimiters)
            case U'\u201D':
            case U'\u201E':
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
