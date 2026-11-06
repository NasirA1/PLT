// Tokens: the output of lexical analysis.
#pragma once

#include "thunder/source.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace thunder
{
    // X-macro listing every token kind with its category and canonical (English) spelling.
    // Keywords are listed here with their English spelling; the spelling actually recognised
    // by the lexer comes from the active LanguagePack (see language_pack.hpp).
#define THUNDER_TOKENS(X)                         \
    X(LeftParen, Punctuation, "(")                \
    X(RightParen, Punctuation, ")")               \
    X(LeftBrace, Punctuation, "{")                \
    X(RightBrace, Punctuation, "}")               \
    X(LeftBracket, Punctuation, "[")              \
    X(RightBracket, Punctuation, "]")             \
    X(Comma, Punctuation, ",")                    \
    X(Semicolon, Punctuation, ";")                \
    X(Colon, Punctuation, ":")                    \
    X(Dot, Punctuation, ".")                      \
    X(Plus, Operator, "+")                        \
    X(Minus, Operator, "-")                       \
    X(Star, Operator, "*")                        \
    X(Slash, Operator, "/")                       \
    X(Percent, Operator, "%")                     \
    X(Assign, Operator, "=")                      \
    X(Equal, Operator, "==")                      \
    X(NotEqual, Operator, "!=")                   \
    X(Less, Operator, "<")                        \
    X(LessEqual, Operator, "<=")                  \
    X(Greater, Operator, ">")                     \
    X(GreaterEqual, Operator, ">=")               \
    X(AndAnd, Operator, "&&")                     \
    X(OrOr, Operator, "||")                       \
    X(Bang, Operator, "!")                        \
    X(Identifier, Identifier, "identifier")       \
    X(IntLiteral, Literal, "integer literal")     \
    X(StringLiteral, Literal, "string literal")   \
    X(KwVar, Keyword, "var")                      \
    X(KwConst, Keyword, "const")                  \
    X(KwFunc, Keyword, "func")                    \
    X(KwStruct, Keyword, "struct")                \
    X(KwIf, Keyword, "if")                        \
    X(KwElse, Keyword, "else")                    \
    X(KwWhile, Keyword, "while")                  \
    X(KwFor, Keyword, "for")                      \
    X(KwBreak, Keyword, "break")                  \
    X(KwContinue, Keyword, "continue")            \
    X(KwReturn, Keyword, "return")                \
    X(KwPass, Keyword, "pass")                    \
    X(KwTrue, Literal, "true")                    \
    X(KwFalse, Literal, "false")                  \
    X(KwInt, Type, "int")                         \
    X(KwBool, Type, "bool")                       \
    X(KwString, Type, "string")                   \
    X(Indent, Special, "indent")                  \
    X(Dedent, Special, "dedent")                  \
    X(EndOfFile, Special, "end of file")          \
    X(Error, Special, "invalid token")

    enum class TokenKind : std::uint8_t
    {
#define THUNDER_TOKEN_ENUM(name, category, spelling) name,
        THUNDER_TOKENS(THUNDER_TOKEN_ENUM)
#undef THUNDER_TOKEN_ENUM
    };

    enum class TokenCategory : std::uint8_t
    {
        Punctuation,
        Operator,
        Identifier,
        Literal,
        Keyword,
        Type,
        Special,
    };

    std::string_view token_kind_name(TokenKind kind);        // e.g. "LeftParen"
    std::string_view token_canonical_spelling(TokenKind kind); // e.g. "(" or "while"
    TokenCategory token_category(TokenKind kind);
    std::string_view token_category_name(TokenCategory category); // e.g. "keyword"

    inline constexpr std::size_t token_kind_count = static_cast<std::size_t>(TokenKind::Error) + 1;

    // True for kinds whose spelling comes from the language pack (keywords, types, true/false).
    bool is_word_token(TokenKind kind);

    struct Token
    {
        TokenKind kind = TokenKind::Error;
        std::string lexeme;      // the exact source text
        Span span;
        bool newline_before = false; // a line break separates this token from the previous one
        // Indent and Dedent tokens mark where a line is indented more or less than the line before
        // it (outside brackets); they have an empty lexeme and a zero-length span.
        std::int64_t int_value = 0; // value of an IntLiteral
        std::string text_value;  // decoded value of a StringLiteral (escapes processed)
    };

    struct LanguagePack;

    // Human-readable description used in syntax errors, e.g. "'while'", "identifier 'x'", "end of file".
    std::string describe(const Token& token, const LanguagePack& language);
}
