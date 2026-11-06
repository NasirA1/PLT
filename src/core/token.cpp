#include "thunder/token.hpp"

#include "thunder/language_pack.hpp"

#include <array>
#include <cstddef>
#include <format>

namespace thunder
{
    namespace
    {
        struct TokenInfo
        {
            std::string_view name;
            TokenCategory category;
            std::string_view spelling;
        };

        constexpr std::array token_table{
#define THUNDER_TOKEN_INFO(name, category, spelling) TokenInfo{#name, TokenCategory::category, spelling},
            THUNDER_TOKENS(THUNDER_TOKEN_INFO)
#undef THUNDER_TOKEN_INFO
        };

        const TokenInfo& lookup(TokenKind kind)
        {
            return token_table.at(static_cast<std::size_t>(kind));
        }
    }

    std::string_view token_kind_name(TokenKind kind)
    {
        return lookup(kind).name;
    }

    std::string_view token_canonical_spelling(TokenKind kind)
    {
        return lookup(kind).spelling;
    }

    TokenCategory token_category(TokenKind kind)
    {
        return lookup(kind).category;
    }

    std::string_view token_category_name(TokenCategory category)
    {
        switch (category)
        {
        case TokenCategory::Punctuation:
            return "punctuation";
        case TokenCategory::Operator:
            return "operator";
        case TokenCategory::Identifier:
            return "identifier";
        case TokenCategory::Literal:
            return "literal";
        case TokenCategory::Keyword:
            return "keyword";
        case TokenCategory::Type:
            return "type";
        case TokenCategory::Special:
            return "special";
        }
        return "unknown";
    }

    bool is_word_token(TokenKind kind)
    {
        const TokenCategory category = token_category(kind);
        return category == TokenCategory::Keyword || category == TokenCategory::Type || kind == TokenKind::KwTrue
            || kind == TokenKind::KwFalse;
    }

    std::string describe(const Token& token, const LanguagePack& language)
    {
        switch (token.kind)
        {
        case TokenKind::EndOfFile:
            return language.term("end of file");
        case TokenKind::Indent:
            return language.term("an indented line");
        case TokenKind::Dedent:
            return language.term("the end of the block");
        case TokenKind::Identifier:
            return std::format("{} '{}'", language.term("identifier"), token.lexeme);
        case TokenKind::IntLiteral:
            return std::format("{} {}", language.term("number"), token.lexeme);
        case TokenKind::StringLiteral:
            return std::format("{} {}", language.term("string"), token.lexeme);
        default:
            return std::format("'{}'", token.lexeme);
        }
    }
}
