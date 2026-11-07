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
        std::string translate_number(const std::string& lexeme, const LanguagePack& to)
        {
            std::string out;
            std::size_t pos = 0;
            while (pos < lexeme.size())
            {
                const utf8::Decoded decoded = utf8::decode(lexeme, pos);
                const auto digit = digit_value(decoded.code_point);
                utf8::append(out, digit ? to.digits[static_cast<std::size_t>(*digit)] : decoded.code_point);
                pos += decoded.length;
            }
            return out;
        }

        std::string translate_string(const std::string& value, const LanguagePack& to)
        {
            const QuotePair quotes = to.quotes.front();
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
                    out += "\\n";
                    break;
                case U'\t':
                    out += "\\t";
                    break;
                case U'\r':
                    out += "\\r";
                    break;
                case U'\0':
                    out += "\\0";
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

        // The directive comment that selects 'to' (written with the target language's keyword).
        std::string directive_for(const LanguagePack& to)
        {
            return to.code == "en" ? "// lang: en" : "// ژبه: " + to.code;
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
        for (const Token& token : tokens)
        {
            if (token.span.offset > copied)
            {
                out += text.substr(copied, token.span.offset - copied); // spaces and comments
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

            if (token.kind == TokenKind::Identifier)
            {
                if (const auto builtin = from.builtin(token.lexeme))
                {
                    out += to.spelling(*builtin);
                }
                else if (from.is_main_name(token.lexeme))
                {
                    out += to.main_name; // main() is named in each language
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
            else if (token.kind == TokenKind::IntLiteral)
            {
                out += translate_number(token.lexeme, to);
            }
            else if (token.kind == TokenKind::StringLiteral)
            {
                out += translate_string(token.text_value, to);
            }
            else
            {
                out += to.spelling(token.kind); // keywords, types, true/false and punctuation
            }
        }
        if (copied < text.size())
        {
            out += text.substr(copied);
        }

        result.ok = true;
        result.text = std::move(out);
        result.renamed.assign(renamed.begin(), renamed.end());
        std::ranges::sort(result.renamed);
        return result;
    }
}
