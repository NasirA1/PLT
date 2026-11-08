// Lexical analysis: turns source text into a list of tokens.
#pragma once

#include "thunder/diagnostics.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/source.hpp"
#include "thunder/token.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thunder
{
    class Lexer
    {
    public:
        Lexer(const SourceFile& source, const LanguagePack& language, DiagnosticBag& diagnostics);

        // Scans the whole file. The result always ends with an EndOfFile token. Characters that
        // cannot start a token are reported and produce an Error token, then scanning continues.
        // Blocks are marked by indentation (as in Python): a line indented more than the line
        // before it produces an Indent token, a line indented less produces one Dedent token per
        // closed level. Lines inside brackets and lines that continue an expression (after an
        // operator) are exempt.
        std::vector<Token> tokenize();

        // Width of the whitespace at the start of the line containing 'offset' (a tab advances
        // to the next multiple of 4).
        std::size_t indentation_of_line(std::size_t offset) const;
        // True if a line break after a token of this kind continues the statement.
        static bool continues_line(TokenKind kind);

        // Character classes (shared with the translator).
        static bool is_identifier_start(char32_t cp);
        static bool is_identifier_part(char32_t cp);
        static bool is_symbol(char32_t cp); // non-ASCII punctuation that can never be part of a name
        // The compound assignment an operator forms with '=': Plus -> PlusAssign, ... (none for others).
        static std::optional<TokenKind> compound_assignment(TokenKind op);
        // The arithmetic operator a compound assignment applies: PlusAssign -> Plus, ...
        static std::optional<TokenKind> compound_operator(TokenKind assignment);

    private:
        Token next_token();
        bool skip_layout_and_comments(); // returns true if a line break was skipped
        Token scan_number();
        Token scan_identifier_or_keyword();
        std::string scan_word(); // the identifier characters at the current position
        Token scan_string(char32_t closing_quote);
        Token make_token(TokenKind kind, std::size_t start, Position start_position) const;

        char32_t peek(std::size_t ahead = 0) const;
        char32_t advance();
        bool at_end() const { return pos_ >= text_.size(); }

        const SourceFile& source_;
        const LanguagePack& language_;
        DiagnosticBag& diagnostics_;
        std::string_view text_;
        std::size_t pos_ = 0;
        TokenKind previous_kind_ = TokenKind::EndOfFile; // the token before the one being scanned
        Position position_;
    };
}
