#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/lexer.hpp"
#include "thunder/utf8.hpp"

using namespace thunder;
using K = TokenKind;

namespace
{
    std::vector<Token> lex(const std::string& text, DiagnosticBag& diagnostics)
    {
        static std::vector<std::unique_ptr<SourceFile>> keep_alive;
        keep_alive.push_back(std::make_unique<SourceFile>("test.th", text));
        Lexer lexer(*keep_alive.back(), english_pack(), diagnostics);
        return lexer.tokenize();
    }
}

TEST_CASE("lexer: punctuation and operators")
{
    const std::vector<K> expected{K::LeftParen, K::RightParen, K::LeftBrace, K::RightBrace, K::LeftBracket,
        K::RightBracket, K::Comma, K::Semicolon, K::Colon, K::Plus, K::Minus, K::Star, K::Slash, K::Percent, K::Assign,
        K::Equal, K::NotEqual, K::Less, K::LessEqual, K::Greater, K::GreaterEqual, K::AndAnd, K::OrOr, K::Bang};
    CHECK(test::token_kinds("( ) { } [ ] , ; : + - * / % = == != < <= > >= && || !") == expected);
}

TEST_CASE("lexer: maximal munch picks the longest operator")
{
    CHECK(test::token_kinds("a<=b") == (std::vector<K>{K::Identifier, K::LessEqual, K::Identifier}));
    CHECK(test::token_kinds("a===b") == (std::vector<K>{K::Identifier, K::Equal, K::Assign, K::Identifier}));
    CHECK(test::token_kinds("!!x") == (std::vector<K>{K::Bang, K::Bang, K::Identifier}));
}

TEST_CASE("lexer: keywords, types and literals come from the language pack")
{
    const std::vector<K> expected{K::KwVar, K::KwConst, K::KwFunc, K::KwIf, K::KwElse, K::KwWhile, K::KwFor,
        K::KwBreak, K::KwContinue, K::KwReturn, K::KwTrue, K::KwFalse, K::KwInt, K::KwBool, K::KwString, K::Identifier};
    CHECK(test::token_kinds("var const func if else while for break continue return true false int bool string variable")
        == expected);
}

TEST_CASE("lexer: integer literal values")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("0 42 9223372036854775807", diagnostics);
    REQUIRE(tokens.size() == 4);
    CHECK_EQ(tokens[0].int_value, 0);
    CHECK_EQ(tokens[1].int_value, 42);
    CHECK_EQ(tokens[2].int_value, 9223372036854775807LL);
    CHECK(!diagnostics.has_errors());
}

TEST_CASE("lexer: integer literal too large")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("9223372036854775808", diagnostics);
    CHECK(tokens[0].kind == K::Error);
    REQUIRE(diagnostics.items().size() == 1);
    CHECK_EQ(diagnostics.items()[0].code, "L005");
}

TEST_CASE("lexer: number followed by letters is malformed")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("12abc + 1", diagnostics);
    CHECK(tokens[0].kind == K::Error);
    CHECK_EQ(tokens[0].lexeme, "12abc");
    REQUIRE(diagnostics.items().size() == 1);
    CHECK_EQ(diagnostics.items()[0].code, "L006");
    CHECK(tokens[1].kind == K::Plus);
}

TEST_CASE("lexer: string escapes are decoded")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex(R"("a\nb\t\"q\"\\\r\0")", diagnostics);
    REQUIRE(tokens[0].kind == K::StringLiteral);
    CHECK_EQ(tokens[0].text_value, std::string("a\nb\t\"q\"\\\r\0", 10));
    CHECK(!diagnostics.has_errors());
}

TEST_CASE("lexer: invalid escape is reported but scanning continues")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex(R"("a\qb" x)", diagnostics);
    CHECK(tokens[0].kind == K::StringLiteral);
    CHECK_EQ(tokens[0].text_value, "aqb");
    REQUIRE(diagnostics.items().size() == 1);
    CHECK_EQ(diagnostics.items()[0].code, "L004");
    CHECK(tokens[1].kind == K::Identifier);
}

TEST_CASE("lexer: unterminated strings")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("\"abc\nx", diagnostics);
    CHECK(tokens[0].kind == K::Error);
    CHECK_EQ(diagnostics.items().at(0).code, "L003");
    CHECK(tokens[1].kind == K::Identifier);

    DiagnosticBag at_end;
    lex("\"abc\\", at_end);
    CHECK_EQ(at_end.items().at(0).code, "L003");
}

TEST_CASE("lexer: comments are skipped")
{
    CHECK(test::token_kinds("a // comment\n/* block\ncomment */ b /**/ c") ==
        (std::vector<K>{K::Identifier, K::Identifier, K::Identifier}));
}

TEST_CASE("lexer: unterminated block comment")
{
    DiagnosticBag diagnostics;
    lex("a /* never closed", diagnostics);
    REQUIRE(diagnostics.items().size() == 1);
    CHECK_EQ(diagnostics.items()[0].code, "L007");
    CHECK_EQ(diagnostics.items()[0].span.start.column, 3u);
}

TEST_CASE("lexer: unexpected characters, with hints for & and |")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("a # b & c | d", diagnostics);
    REQUIRE(diagnostics.items().size() == 3);
    CHECK_EQ(diagnostics.items()[0].code, "L001");
    CHECK_EQ(diagnostics.items()[1].code, "L002");
    CHECK_CONTAINS(diagnostics.items()[1].message, "did you mean '&&'");
    CHECK_CONTAINS(diagnostics.items()[2].message, "did you mean '||'");
    CHECK(tokens.back().kind == K::EndOfFile);
}

TEST_CASE("lexer: positions count lines and code points")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("var x\n  \"پښتو\" y", diagnostics);
    REQUIRE(tokens.size() == 5);
    CHECK_EQ(tokens[1].span.start.line, 1u);
    CHECK_EQ(tokens[1].span.start.column, 5u);
    CHECK_EQ(tokens[2].span.start.line, 2u);
    CHECK_EQ(tokens[2].span.start.column, 3u);
    CHECK_EQ(tokens[2].span.end.column, 9u); // 4 Pashto letters + 2 quotes
    CHECK_EQ(tokens[3].span.start.column, 10u);
}

TEST_CASE("lexer: identifiers may use non-Latin scripts; bidi marks are layout")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("var شمیر = 1;\xE2\x80\x8F", diagnostics);
    REQUIRE(tokens.size() == 6);
    CHECK(tokens[1].kind == K::Identifier);
    CHECK_EQ(tokens[1].lexeme, "شمیر");
    CHECK(!diagnostics.has_errors());
}

TEST_CASE("lexer: invalid UTF-8 is an unexpected character")
{
    DiagnosticBag diagnostics;
    lex("a \xFF b", diagnostics);
    REQUIRE(diagnostics.items().size() == 1);
    CHECK_EQ(diagnostics.items()[0].code, "L001");
}

TEST_CASE("lexer: empty input produces only end of file")
{
    DiagnosticBag diagnostics;
    const auto tokens = lex("", diagnostics);
    REQUIRE(tokens.size() == 1);
    CHECK(tokens[0].kind == K::EndOfFile);
}

TEST_CASE("tokens: names, categories and descriptions")
{
    CHECK_EQ(token_kind_name(K::LeftParen), "LeftParen");
    CHECK_EQ(token_canonical_spelling(K::KwWhile), "while");
    CHECK_EQ(token_category_name(token_category(K::KwWhile)), "keyword");
    CHECK_EQ(token_category_name(token_category(K::KwInt)), "type");
    CHECK_EQ(token_category_name(token_category(K::Plus)), "operator");
    CHECK_EQ(token_category_name(token_category(K::Comma)), "punctuation");
    CHECK_EQ(token_category_name(token_category(K::Identifier)), "identifier");
    CHECK_EQ(token_category_name(token_category(K::IntLiteral)), "literal");
    CHECK_EQ(token_category_name(token_category(K::EndOfFile)), "special");
    CHECK(is_word_token(K::KwTrue));
    CHECK(!is_word_token(K::Plus));

    Token token;
    token.kind = K::Identifier;
    token.lexeme = "x";
    CHECK_EQ(describe(token, english_pack()), "identifier 'x'");
    token.kind = K::IntLiteral;
    token.lexeme = "5";
    CHECK_EQ(describe(token, english_pack()), "number 5");
    token.kind = K::StringLiteral;
    token.lexeme = "\"s\"";
    CHECK_EQ(describe(token, english_pack()), "string \"s\"");
    token.kind = K::EndOfFile;
    CHECK_EQ(describe(token, english_pack()), "end of file");
    token.kind = K::Semicolon;
    token.lexeme = ";";
    CHECK_EQ(describe(token, english_pack()), "';'");
}

TEST_CASE("language pack: lookup and spelling")
{
    const LanguagePack& english = english_pack();
    CHECK(english.keyword("while") == K::KwWhile);
    CHECK(!english.keyword("whilst").has_value());
    CHECK(english.builtin("println") == BuiltinId::Println);
    CHECK(!english.builtin("printf").has_value());
    CHECK_EQ(english.spelling(K::KwFunc), "func");
    CHECK_EQ(english.spelling(K::Plus), "+");
    CHECK_EQ(english.spelling(BuiltinId::ToInt), "to_int");
    CHECK(find_language_pack("en") == &english);
    CHECK(find_language_pack("English") == &english);
    CHECK(find_language_pack("ps") == &pashto_pack());
    CHECK(find_language_pack("pashto") == &pashto_pack());
    CHECK(find_language_pack("پښتو") == &pashto_pack());
    CHECK(find_language_pack("xx") == nullptr);
    CHECK_EQ(available_language_packs().size(), 2u);
    CHECK(!english.right_to_left);
    CHECK(pashto_pack().right_to_left);
}

TEST_CASE("language pack: a custom pack changes the keywords the lexer accepts")
{
    // This is how a future Pashto pack will work: same token kinds, different spellings.
    LanguagePack custom = english_pack();
    custom.code = "xx";
    custom.keywords.erase("while");
    custom.keyword_spellings.erase(K::KwWhile);
    custom.add_keyword(K::KwWhile, "repeat");
    const SourceFile source("test.th", "repeat while");
    DiagnosticBag diagnostics;
    Lexer lexer(source, custom, diagnostics);
    const auto tokens = lexer.tokenize();
    CHECK(tokens[0].kind == K::KwWhile);
    CHECK(tokens[1].kind == K::Identifier);
    CHECK_EQ(custom.spelling(K::KwWhile), "repeat");
}

TEST_CASE("utf8: decode, encode and length")
{
    CHECK_EQ(utf8::length("abc"), 3u);
    CHECK_EQ(utf8::length("پښتو"), 4u);
    CHECK_EQ(utf8::length("€𝄞"), 2u);
    CHECK_EQ(utf8::code_points("aپ€𝄞").size(), 4u);

    std::string out;
    utf8::append(out, U'A');
    utf8::append(out, U'پ');
    utf8::append(out, U'€');
    utf8::append(out, U'\U0001D11E');
    CHECK_EQ(out, "Aپ€𝄞");

    std::string bad;
    utf8::append(bad, 0xD800); // surrogates are not encodable
    CHECK_EQ(bad, "\xEF\xBF\xBD");

    CHECK(utf8::decode("\xC3", 0).code_point == utf8::replacement_character);        // truncated
    CHECK(utf8::decode("\xC3\x28", 0).code_point == utf8::replacement_character);    // bad continuation
    CHECK(utf8::decode("\xC0\x80", 0).code_point == utf8::replacement_character);    // overlong
    CHECK(utf8::decode("\xED\xA0\x80", 0).code_point == utf8::replacement_character); // surrogate
    CHECK(utf8::decode("\xF8", 0).code_point == utf8::replacement_character);        // invalid lead byte
    CHECK_EQ(utf8::decode("", 0).length, 0u);
    CHECK(utf8::is_space(U'\u200F'));
    CHECK(utf8::is_space(U' '));
    CHECK(!utf8::is_space(U'x'));
}
