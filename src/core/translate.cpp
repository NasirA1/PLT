#include "thunder/translate.hpp"

#include "thunder/compiler.hpp"
#include "thunder/lexer.hpp"
#include "thunder/utf8.hpp"

#include <algorithm>
#include <unordered_set>

namespace thunder
{
    namespace
    {
        std::string translate_number(const std::string& lexeme, const LanguagePack& from, const LanguagePack& to)
        {
            // 0x2A and 0b1010 are a notation, not a spelling: they stay exactly as written.
            if (lexeme.size() > 2 && (lexeme[1] == 'x' || lexeme[1] == 'X' || lexeme[1] == 'b' || lexeme[1] == 'B'))
            {
                return lexeme;
            }
            std::string out;
            std::size_t pos = 0;
            while (pos < lexeme.size())
            {
                const utf8::Decoded decoded = utf8::decode(lexeme, pos);
                const auto digit = digit_value(decoded.code_point);
                if (digit)
                {
                    utf8::append(out, to.digits[static_cast<std::size_t>(*digit)]);
                }
                else if (decoded.code_point == U'.' || decoded.code_point == from.decimal_point)
                {
                    utf8::append(out, to.decimal_point); // 3.14 <-> ۳٫۱۴
                }
                else
                {
                    utf8::append(out, decoded.code_point);
                }
                pos += decoded.length;
            }
            return out;
        }

        std::string translate_string(const std::string& value, const LanguagePack& to)
        {
            const QuotePair quotes = to.quotes.front();
            // Escapes are written with the target language's own letters: \n becomes \ن in
            // Pashto and back again in English (the lexer understands both everywhere).
            const bool pashto = to.code != "en";
            std::string out;
            utf8::append(out, quotes.open);
            std::size_t pos = 0;
            while (pos < value.size())
            {
                const utf8::Decoded decoded = utf8::decode(value, pos);
                pos += decoded.length;
                switch (decoded.code_point)
                {
                case U'\n':
                    out += pashto ? "\\ن" : "\\n";
                    break;
                case U'\t':
                    out += pashto ? "\\ت" : "\\t";
                    break;
                case U'\r':
                    out += pashto ? "\\ر" : "\\r";
                    break;
                case U'\0':
                    out += pashto ? "\\۰" : "\\0";
                    break;
                case U'\\':
                    out += "\\\\";
                    break;
                default:
                    if (decoded.code_point == quotes.close)
                    {
                        out += "\\"; // escape the closing quote of the target language
                    }
                    utf8::append(out, decoded.code_point);
                    break;
                }
            }
            utf8::append(out, quotes.close);
            return out;
        }

        // The built-in module a word names, in the source's language or in English.
        std::optional<BuiltinModule> module_named(const LanguagePack& from, const std::string& word)
        {
            const std::string folded = fold_letters(word);
            if (const auto own = from.modules.find(folded); own != from.modules.end())
            {
                return own->second;
            }
            if (const auto english = english_pack().modules.find(folded); english != english_pack().modules.end())
            {
                return english->second;
            }
            return std::nullopt;
        }

        // Where each 'with' moves. English writes it first on its line, Pashto last ("ساکټ سره"),
        // so translating between them moves the keyword to the other end of the logical line
        // (the tokens up to a line break outside brackets). A 'with' already in the wrong place
        // for its language is translated where it stands.
        struct WithMoves
        {
            std::unordered_set<std::size_t> drop;   // 'with' tokens not written where they stand
            std::unordered_set<std::size_t> before; // tokens to write 'with' (and a space) before
            std::unordered_set<std::size_t> after;  // tokens to write (a space and) 'with' after
        };

        WithMoves with_moves(const std::vector<Token>& tokens, const LanguagePack& from, const LanguagePack& to)
        {
            WithMoves moves;
            if (from.with_after_resource == to.with_after_resource)
            {
                return moves;
            }
            // The first and last token of every logical line.
            std::vector<std::pair<std::size_t, std::size_t>> lines;
            int depth = 0;
            for (std::size_t i = 0; i < tokens.size(); ++i)
            {
                const TokenKind kind = tokens[i].kind;
                if (kind == TokenKind::Indent || kind == TokenKind::Dedent || kind == TokenKind::EndOfFile)
                {
                    continue;
                }
                if (lines.empty() || (tokens[i].newline_before && depth <= 0))
                {
                    lines.emplace_back(i, i);
                    depth = 0;
                }
                lines.back().second = i;
                if (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket || kind == TokenKind::LeftBrace)
                {
                    ++depth;
                }
                else if (kind == TokenKind::RightParen || kind == TokenKind::RightBracket || kind == TokenKind::RightBrace)
                {
                    --depth;
                }
            }
            for (const auto& [first, last] : lines)
            {
                if (first == last)
                {
                    continue;
                }
                if (!from.with_after_resource && tokens[first].kind == TokenKind::KwWith)
                {
                    moves.drop.insert(first);
                    moves.after.insert(last);
                }
                else if (from.with_after_resource && tokens[last].kind == TokenKind::KwWith)
                {
                    moves.drop.insert(last);
                    moves.before.insert(first);
                }
            }
            return moves;
        }

        // The directive comment that selects 'to' (written with the target language's keyword).
        std::string directive_for(const LanguagePack& to)
        {
            return to.code == "en" ? "# lang: en" : "# ژبه: " + to.code;
        }
    }

    TranslationResult translate(const SourceFile& source, const LanguagePack& from_default, const LanguagePack& to)
    {
        const LanguagePack& from = choose_language(source, from_default);
        TranslationResult result(from);
        Lexer lexer(source, from, result.diagnostics);
        const std::vector<Token> tokens = lexer.tokenize();
        if (result.diagnostics.has_errors())
        {
            return result;
        }

        const std::string& text = source.text();
        std::string out;
        std::size_t copied = 0; // source bytes already copied to 'out'
        // Source text between the tokens (spaces, comments, line breaks) is copied as it is,
        // except that a Windows line break loses its carriage return: the translation is written
        // with the platform's own line breaks, and a kept '\r' would double them (one empty line
        // after every line of code).
        const auto copy_source = [&](std::size_t begin, std::size_t end)
        {
            for (std::size_t at = begin; at < end && at < text.size(); ++at)
            {
                if (text[at] != '\r')
                {
                    out += text[at];
                }
            }
        };

        // Rewrite (or add) the language directive.
        const bool has_directive = language_directive(text).has_value();
        if (has_directive)
        {
            copied = std::min(text.find('\n'), text.size());
            if (copied > 0 && text[copied - 1] == '\r')
            {
                --copied;
            }
            out += directive_for(to);
        }
        else if (to.code != "en")
        {
            out += directive_for(to) + "\n";
        }

        std::unordered_set<std::string> renamed;
        const WithMoves moves = with_moves(tokens, from, to);
        const std::string with_word = to.spelling(TokenKind::KwWith);
        std::optional<BuiltinModule> module_before; // the built-in module named just before a '.'
        for (std::size_t index = 0; index < tokens.size(); ++index)
        {
            const Token& token = tokens[index];
            if (moves.drop.contains(index))
            {
                // The keyword moves to the other end of the line; the space beside it goes too.
                if (from.with_after_resource)
                {
                    copied = std::max<std::size_t>(copied, token.span.offset + token.span.length);
                }
                else
                {
                    if (token.span.offset > copied)
                    {
                        copy_source(copied, token.span.offset); // the line break and indentation before it stay
                    }
                    copied = std::max<std::size_t>(copied, index + 1 < tokens.size() ? tokens[index + 1].span.offset
                                                                                      : token.span.offset + token.span.length);
                }
                continue;
            }
            if (token.span.offset > copied)
            {
                copy_source(copied, token.span.offset); // spaces and comments
            }
            copied = std::max<std::size_t>(copied, token.span.offset + token.span.length);
            if (token.kind == TokenKind::EndOfFile)
            {
                break;
            }
            if (token.kind == TokenKind::Indent || token.kind == TokenKind::Dedent)
            {
                continue; // indentation markers have no text of their own
            }
            if (moves.before.contains(index))
            {
                out += with_word + " ";
            }
            // math.sqrt: the word after a built-in module's dot is one of its members.
            const bool after_dot = index > 0 && tokens[index - 1].kind == TokenKind::Dot;
            const std::optional<BuiltinModule> member_of = after_dot ? module_before : std::nullopt;
            module_before.reset();
            if (token.kind == TokenKind::Dot && index > 0 && !(index > 1 && tokens[index - 2].kind == TokenKind::Dot))
            {
                const Token& previous = tokens[index - 1];
                if (previous.kind == TokenKind::Identifier || previous.kind == TokenKind::KwString)
                {
                    module_before = module_named(from, previous.lexeme);
                }
            }

            if (member_of && from.module_builtin(*member_of, token.lexeme))
            {
                out += to.member_spelling(*from.module_builtin(*member_of, token.lexeme)); // sqrt <-> جذر
            }
            else if (token.kind == TokenKind::Identifier && index + 1 < tokens.size()
                && (tokens[index + 1].kind == TokenKind::Dot || (index > 0 && tokens[index - 1].kind == TokenKind::KwImport))
                && !after_dot && module_named(from, token.lexeme))
            {
                out += to.spelling(*module_named(from, token.lexeme)); // math <-> ریاضي, import os <-> راوړه سیسټم
            }
            else if (token.kind == TokenKind::Identifier)
            {
                if (const auto builtin = after_dot ? std::nullopt : from.builtin(token.lexeme))
                {
                    out += to.spelling(*builtin);
                }
                else if (from.is_main_name(token.lexeme))
                {
                    out += to.main_name; // main() is named in each language
                }
                else if (const auto enumeration = from.builtin_enum(token.lexeme))
                {
                    out += to.spelling(*enumeration); // Color <-> رنګونه
                }
                else if (const auto member = from.builtin_enum_member(token.lexeme))
                {
                    out += to.member_spelling(member->first, member->second); // red <-> سور
                }
                else if (to.keyword(token.lexeme) || to.builtin(token.lexeme))
                {
                    out += token.lexeme + "_"; // the name is reserved in the target language
                    renamed.insert(token.lexeme);
                }
                else
                {
                    out += token.lexeme;
                }
            }
            else if (token.kind == TokenKind::IntLiteral || token.kind == TokenKind::FloatLiteral)
            {
                out += translate_number(token.lexeme, from, to);
            }
            else if (token.kind == TokenKind::StringLiteral)
            {
                out += translate_string(token.text_value, to);
            }
            else
            {
                out += to.spelling(token.kind); // keywords, types, true/false and punctuation
            }
            if (moves.after.contains(index))
            {
                out += " " + with_word;
            }
        }
        if (copied < text.size())
        {
            copy_source(copied, text.size());
        }

        result.ok = true;
        result.text = std::move(out);
        result.renamed.assign(renamed.begin(), renamed.end());
        std::ranges::sort(result.renamed);
        return result;
    }
}
