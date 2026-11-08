// Syntax analysis: turns tokens into an abstract syntax tree.
//
// Statements are parsed by recursive descent: one function per grammar rule (see
// docs/language.md for the grammar). Expressions are parsed with Pratt's top-down operator
// precedence algorithm, in which each operator has a "binding power" that decides how tightly
// it holds on to its operands.
//
// A statement ends at the end of its line. A line break does not end a
// statement inside parentheses or brackets, or after an operator that still needs its right
// operand, so long expressions can be split over several lines. Blocks are marked by
// indentation: the lexer turns changes of indentation into Indent and Dedent tokens, and a block
// is "Indent { statement } Dedent" (see docs/language.md section 6).
#pragma once

#include "thunder/ast.hpp"
#include "thunder/diagnostics.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/token.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace thunder
{
    // Binding powers used by the Pratt expression parser (higher binds tighter).
    // Every binary operator is left-associative.
    namespace binding_power
    {
        inline constexpr int none = 0;
        inline constexpr int logical_or = 1;  // or
        inline constexpr int logical_and = 2; // and
        inline constexpr int logical_not = 3; // not (prefix: binds less tightly than comparisons)
        inline constexpr int equality = 4;    // == !=
        inline constexpr int comparison = 5;  // < <= > >=
        inline constexpr int additive = 6;    // + -
        inline constexpr int multiplicative = 7; // * / %
        inline constexpr int prefix = 8;      // unary -
        inline constexpr int postfix = 9;     // call f(...), index a[i], field p.x
    }

    int infix_binding_power(TokenKind kind);

    class Parser
    {
    public:
        Parser(std::vector<Token> tokens, const LanguagePack& language, DiagnosticBag& diagnostics);

        ProgramNode parse_program();

        // Parses a single expression followed by end of file (used by tests and tools).
        ExprPtr parse_standalone_expression();

    private:
        struct ParseError
        {
        };

        // Declarations and statements
        StmtPtr parse_item(bool top_level);
        StmtPtr parse_function(bool is_native); // "native func" has no body
        StmtPtr parse_struct();
        StmtPtr parse_import();
        void end_statement(); // a ';', a line break, the end of the block or of the file
        StmtPtr parse_statement();
        std::unique_ptr<VarDeclStmt> parse_var_decl(bool is_statement);
        std::unique_ptr<BlockStmt> parse_block(TokenKind after); // an indented block after a keyword
        void skip_indented_block(); // skips an Indent ... Dedent region (error recovery)
        void expect_block_start(TokenKind after); // consumes the Indent that opens a block
        StmtPtr parse_if();
        StmtPtr parse_while();
        StmtPtr parse_for();
        bool at_for_each() const; // "for x in ..." rather than "for init, condition, step"
        StmtPtr parse_for_each(const Span& start);
        StmtPtr parse_return();
        StmtPtr parse_simple_statement(); // assignment or expression, without ';'
        TypeSyntax parse_type();

        // Expressions (Pratt parser)
        ExprPtr parse_expression(int min_binding_power = binding_power::none);
        ExprPtr parse_prefix();
        ExprPtr parse_postfix(ExprPtr left);

        // Token helpers
        const Token& peek(std::size_t ahead = 0) const;
        const Token& previous() const;
        bool check(TokenKind kind) const;
        bool match(TokenKind kind);
        const Token& advance();
        const Token& expect(TokenKind kind);
        [[noreturn]] void fail_expected(const std::string& what);
        Span span_from(const Span& start) const;

        void synchronize(std::size_t statement_start);
        bool stop_parsing();
        // True if the next token starts a new line outside any parentheses or brackets.
        bool at_line_break() const { return nesting_ == 0 && peek().newline_before; }

        // Line breaks inside (...) and [...] do not end statements.
        class NestingGuard
        {
        public:
            explicit NestingGuard(Parser& parser)
                : parser_(parser)
            {
                ++parser_.nesting_;
            }
            ~NestingGuard() { --parser_.nesting_; }
            NestingGuard(const NestingGuard&) = delete;
            NestingGuard& operator=(const NestingGuard&) = delete;

        private:
            Parser& parser_;
        };

        std::vector<Token> tokens_;
        const LanguagePack& language_;
        DiagnosticBag& diagnostics_;
        std::size_t current_ = 0;
        int nesting_ = 0;
        bool gave_up_ = false;
    };
}
