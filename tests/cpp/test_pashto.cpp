#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/lexer.hpp"
#include "thunder/report.hpp"
#include "thunder/translate.hpp"
#include "thunder/value.hpp"

#include <algorithm>
#include <format>

using namespace thunder;
using K = TokenKind;

namespace
{
    std::vector<K> pashto_kinds(const std::string& text)
    {
        const SourceFile source("p.th", text);
        DiagnosticBag diagnostics(pashto_pack());
        Lexer lexer(source, pashto_pack(), diagnostics);
        std::vector<K> kinds;
        for (const Token& token : lexer.tokenize())
        {
            if (token.kind != K::EndOfFile)
            {
                kinds.push_back(token.kind);
            }
        }
        return kinds;
    }

    std::string run_pashto(const std::string& text)
    {
        const SourceFile source("p.th", text);
        const CompileResult result = compile(source, pashto_pack());
        if (!result.program)
        {
            std::string out = "COMPILE ERROR";
            for (const Diagnostic& diagnostic : result.diagnostics.items())
            {
                out += " " + diagnostic.code;
            }
            return out;
        }
        std::ostringstream output;
        Vm vm(*result.program, output);
        const ExecResult exec = vm.run();
        return output.str() + (exec.error ? "ERROR " + exec.error->message : std::string());
    }
}

TEST_CASE("pashto: every keyword, type and literal word has a Pashto spelling")
{
    const LanguagePack& ps = pashto_pack();
    for (std::size_t i = 0; i < token_kind_count; ++i)
    {
        const auto kind = static_cast<K>(i);
        if (is_word_token(kind))
        {
            const std::string spelling = ps.spelling(kind);
            CHECK(spelling != std::string(token_canonical_spelling(kind)));
            CHECK(ps.keyword(spelling) == kind);
        }
    }
    for (const BuiltinInfo& builtin : all_builtins())
    {
        CHECK(ps.spelling(builtin.id) != std::string(builtin.canonical_name));
        CHECK(ps.builtin(ps.spelling(builtin.id)) == builtin.id);
    }
}

TEST_CASE("pashto: keywords, digits, punctuation and quotes are tokens")
{
    CHECK(pashto_kinds("متغیر شمېر: عدد = ۴۲") ==
        (std::vector<K>{K::KwVar, K::Identifier, K::Colon, K::KwInt, K::Assign, K::IntLiteral}));
    CHECK(pashto_kinds("که (رښتیا)\n    تېر\nکنه\n    تېر") ==
        (std::vector<K>{K::KwIf, K::LeftParen, K::KwTrue, K::RightParen, K::Indent, K::KwPass, K::Dedent, K::KwElse,
            K::Indent, K::KwPass, K::Dedent}));
    CHECK(pashto_kinds("f(۱، ۲, ۳) ۷ ٪ ۲ × ۳ ÷ ۱") ==
        (std::vector<K>{K::Identifier, K::LeftParen, K::IntLiteral, K::Comma, K::IntLiteral, K::Comma, K::IntLiteral,
            K::RightParen, K::IntLiteral, K::Percent, K::IntLiteral, K::Star, K::IntLiteral, K::Slash, K::IntLiteral}));
    CHECK(pashto_kinds("که الف او ب یا نه ج") ==
        (std::vector<K>{K::KwIf, K::Identifier, K::KwAnd, K::Identifier, K::KwOr, K::KwNot, K::Identifier}));
    // Built-in names that contain one of these words are still one name.
    CHECK(pashto_kinds("نېټه او وخت()") == (std::vector<K>{K::Identifier, K::LeftParen, K::RightParen}));
    CHECK(pashto_kinds("«سلام» \"hi\"") == (std::vector<K>{K::StringLiteral, K::StringLiteral}));
    // English keywords are ordinary names in a Pashto program.
    CHECK(pashto_kinds("while") == std::vector<K>{K::Identifier});
}

TEST_CASE("pashto: Arabic keyboard letters are folded (ك means ک, گ means ګ)")
{
    // Arabic keyboard layouts produce ك (U+0643) and گ (U+06AF); Pashto registers ک (U+06A9)
    // and ګ (U+06AB). The letters look alike, so typed variants must mean the same word.
    CHECK_EQ(fold_letters("كرښه چاپ"), "کرښه چاپ");
    CHECK_EQ(fold_letters("راگرځه"), "راګرځه");
    CHECK_EQ(fold_letters("abc"), "abc");
    // A whole program written with the Arabic kaf: که، کنه، کرښه چاپ، متن کړه... all still work,
    // also with the two kinds of kaf mixed inside one word.
    CHECK_EQ(run_pashto("كه رښتیا\n    كرښه چاپ(متن كړه(۴۲))\nكنه\n    تېر\n"), "۴۲\n");
    CHECK_EQ(run_pashto("دنده دوه_چنده(ن: عدد): عدد\n    راگرځه ن * ۲\nکرښه چاپ(دوه_چنده(۲۱))\n"), "۴۲\n");
    CHECK(pashto_pack().keyword("كه") == TokenKind::KwIf);
    CHECK(pashto_pack().builtin("کرښه چاپ") == pashto_pack().builtin("كرښه چاپ"));
    CHECK(pashto_pack().starts_compound("كرښه"));
    CHECK_EQ(pashto_pack().english_key("كیڼ"), "left");
    CHECK(find_builtin_module("گرافیک") == BuiltinModule::Graphics); // راوړه with the Persian gaf
    CHECK(find_builtin_module("ګرافيک") == BuiltinModule::Graphics); // ي for ی (Arabic keyboards) is folded too
    CHECK(pashto_pack().keyword("متغير") == TokenKind::KwVar);  // متغیر with the Arabic ي
    CHECK(pashto_pack().keyword("رښتيا") == TokenKind::KwTrue); // رښتیا with the Arabic ي
    CHECK(pashto_pack().keyword("درواغ") == TokenKind::KwFalse);
    CHECK(pashto_pack().keyword("دروغ") == TokenKind::KwFalse); // the earlier spelling stays accepted
}

TEST_CASE("pashto: curly double quotes delimit strings, in any combination")
{
    // Keyboard layouts differ in which curly quote they produce, so the family is interchangeable.
    CHECK_EQ(run_pashto("کرښه چاپ(”سلام”)\n"), "سلام\n");
    CHECK_EQ(run_pashto("کرښه چاپ(“سلام”)\n"), "سلام\n");
    CHECK_EQ(run_pashto("کرښه چاپ(“سلام“)\n"), "سلام\n");
    CHECK_EQ(run_pashto("کرښه چاپ(„سلام“ + \"!\")\n"), "سلام!\n");
}

TEST_CASE("pashto: multi-word built-ins are one token")
{
    const SourceFile source("p.th", "کرښه چاپ(۱) کرښه  \tچاپ کرښه\nچاپ کرښه x متن کړه(۲)");
    DiagnosticBag diagnostics(pashto_pack());
    Lexer lexer(source, pashto_pack(), diagnostics);
    const auto tokens = lexer.tokenize();
    std::vector<std::string> lexemes;
    for (const Token& token : tokens)
    {
        lexemes.push_back(token.lexeme);
    }
    CHECK(lexemes == (std::vector<std::string>{"کرښه چاپ", "(", "۱", ")", "کرښه چاپ", "کرښه", "چاپ", "کرښه", "x",
                         "متن کړه", "(", "۲", ")", ""}));
    CHECK_EQ(tokens[0].span.length, std::string("کرښه چاپ").size());
    CHECK_EQ(tokens[4].span.length, std::string("کرښه  \tچاپ").size()); // the span covers the text as written
    CHECK_EQ(tokens[4].span.end.column, 23u);
    CHECK_EQ(tokens[5].span.start.line, 1u); // "کرښه" at the end of the line is a plain name
    CHECK_EQ(tokens[6].span.start.line, 2u);
    CHECK(pashto_pack().builtin("کرښه چاپ") == BuiltinId::Println);
    CHECK(pashto_pack().starts_compound("کرښه"));
    CHECK(!pashto_pack().starts_compound("کرښه چاپ"));
    CHECK(!english_pack().starts_compound("print"));
    CHECK(!diagnostics.has_errors());
}

TEST_CASE("pashto: digit values in every script")
{
    CHECK(digit_value(U'7') == 7);
    CHECK(digit_value(U'۵') == 5); // ۵ extended Arabic-Indic (Pashto)
    CHECK(digit_value(U'٣') == 3); // ٣ Arabic-Indic
    CHECK(!digit_value(U'a').has_value());

    const SourceFile source("p.th", "۱۲۳ ١٢ 4۵");
    DiagnosticBag diagnostics(pashto_pack());
    Lexer lexer(source, pashto_pack(), diagnostics);
    const auto tokens = lexer.tokenize();
    CHECK_EQ(tokens[0].int_value, 123);
    CHECK_EQ(tokens[1].int_value, 12);
    CHECK_EQ(tokens[2].int_value, 45);
}

TEST_CASE("pashto: a number cannot start a name, and Arabic punctuation is not a letter")
{
    CHECK(pashto_kinds("ب۲") == std::vector<K>{K::Identifier});
    const SourceFile source("p.th", "۲ب");
    DiagnosticBag diagnostics(pashto_pack());
    Lexer lexer(source, pashto_pack(), diagnostics);
    lexer.tokenize();
    REQUIRE(diagnostics.items().size() == 1);
    CHECK_EQ(diagnostics.items()[0].code, "L006");
    // In an English program the Arabic comma is not punctuation, but it is not a letter either.
    CHECK(test::diagnostic_codes("var a، b") == std::vector<std::string>{"L001"});
    CHECK(Lexer::is_symbol(U'،'));
    CHECK(!Lexer::is_symbol(U'پ'));
}

TEST_CASE("pashto: «» strings with escapes")
{
    const SourceFile source("p.th", "«a»«b\\»c» «x\\ny»");
    DiagnosticBag diagnostics(pashto_pack());
    Lexer lexer(source, pashto_pack(), diagnostics);
    const auto tokens = lexer.tokenize();
    CHECK_EQ(tokens[0].text_value, "a");
    CHECK_EQ(tokens[1].text_value, "b»c");
    CHECK_EQ(tokens[2].text_value, "x\ny");
    CHECK(!diagnostics.has_errors());
}

TEST_CASE("pashto: programs run and print with Pashto digits and words")
{
    const char* program = R"(
جوړښت ټکی
    x: عدد
    y: عدد
دنده جمع(a: عدد، b: عدد): عدد
    راګرځه a + b
متغیر لړ = [۱، ۲، ۳]
کرښه چاپ(جمع(۲۰، ۲۲)، « »، رښتیا، « »، لړ، « »، اوږدوالی(«سلام»))
متغیر ټ = ټکی(y: ۵، x: ۱۰)
کرښه چاپ(ټ)
لپاره (متغیر i = ۰، i < ۳، i = i + ۱)
    که (i == ۱)
        دوام
    چاپ(i)
کرښه چاپ()
کرښه چاپ(عدد کړه(«۴۲») + ۱، « »، متن کړه(درواغ))
وښایه(ټ)
)";
    CHECK_EQ(run_pashto(program),
        "۴۲ رښتیا [۱، ۲، ۳] ۴\n"
        "ټکی(x: ۱۰، y: ۵)\n"
        "۰۲\n"
        "۴۳ درواغ\n"
        "ټکی\n    x: عدد = ۱۰\n    y: عدد = ۵\n");
}

TEST_CASE("pashto: decimals, for-in and compound assignment")
{
    const char* program = R"(
متغیر ټول: اعشاري = ۰
لپاره ا په [۱٫۵، ۲٫۵، ۳]
    ټول += ا
کرښه چاپ(ټول، " "، ټول / ۲، " "، جذر(۹٫۰)، " "، نږدې کړه(۲٫۵))
کرښه چاپ(اعشاري کړه("۲٫۵")، " "، عدد کړه(۲٫۹)، " "، کمینه(۱، ۲٫۵)، " "، اعظمه(۳، ۱))
کرښه چاپ(توان کړه(۲٫۰، ۱۰٫۰)، " "، مطلق(-۳٫۲۵))
)";
    CHECK_EQ(run_pashto(program),
        "۷٫۰ ۳٫۵ ۳٫۰ ۳\n"
        "۲٫۵ ۲ ۱٫۰ ۳\n"
        "۱۰۲۴٫۰ ۳٫۲۵\n");
}

TEST_CASE("translate: decimal points, 'in' and compound assignment")
{
    const std::string english = "var total: float = 0.0\nfor v in [1.5, 2]\n    total += v\nprintln(total)\n";
    const SourceFile source("t.th", english);
    const TranslationResult to_pashto = translate(source, english_pack(), pashto_pack());
    REQUIRE(to_pashto.ok);
    CHECK_CONTAINS(to_pashto.text, "متغیر total: اعشاري = ۰٫۰");
    CHECK_CONTAINS(to_pashto.text, "لپاره v په [۱٫۵، ۲]");
    CHECK_CONTAINS(to_pashto.text, "total += v");

    const SourceFile pashto("p.th", to_pashto.text);
    const TranslationResult back = translate(pashto, english_pack(), english_pack());
    REQUIRE(back.ok);
    CHECK_EQ(back.text, "// lang: en\n" + english);
}

TEST_CASE("pashto: compile and runtime messages are in Pashto")
{
    const SourceFile source("p.th", "متغیر ش: عدد = «درې»\n");
    const CompileResult result = compile(source, pashto_pack());
    REQUIRE(result.diagnostics.items().size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.items()[0];
    CHECK_EQ(diagnostic.code, "S005");
    CHECK_EQ(diagnostic.message, "د 'متن' ډول ارزښت د 'عدد' ډول 'ش' ته نشي ورکول کېدای");
    CHECK_CONTAINS(format_short(diagnostic, "p.th", pashto_pack()), "p.th:1:16: معنايي تېروتنه [S005]");

    CHECK_EQ(run_pashto("متغیر لړ = [۱]\nکرښه چاپ(لړ[۳])\n"), "ERROR لړ شمېره ۳ د ۱ توکو لړۍ لپاره له حده بهر ده");
    CHECK_EQ(run_pashto("کرښه چاپ(۱ / ۰)"), "ERROR په صفر وېش");
}

TEST_CASE("pashto: every message template formats correctly")
{
    const LanguagePack& ps = pashto_pack();
    const std::vector<std::string> args{"a1", "b2", "c3", "d4", "e5", "f6"};
    for (std::size_t i = 0; i < diag_count(); ++i)
    {
        const auto id = static_cast<DiagId>(i);
        const std::string_view text = ps.message_template(id);
        CHECK(text != std::string_view(info(id).message_template)); // translated
        // Formatting must not fall back to English (which would happen on a broken template).
        const std::string formatted = ps.format(id, args);
        CHECK(formatted != english_pack().format(id, args));
    }
}

TEST_CASE("diagnostics: catalog codes are unique and grouped by phase")
{
    std::vector<std::string> codes;
    for (std::size_t i = 0; i < diag_count(); ++i)
    {
        const DiagInfo& entry = info(static_cast<DiagId>(i));
        codes.emplace_back(entry.code);
        const char expected = entry.phase == Phase::Lexical ? 'L'
            : entry.phase == Phase::Syntax                  ? 'P'
            : entry.phase == Phase::Runtime                 ? 'R'
            : entry.severity == Severity::Warning           ? 'W'
                                                            : 'S';
        CHECK_EQ(entry.code[0], expected);
    }
    std::ranges::sort(codes);
    CHECK(std::ranges::adjacent_find(codes) == codes.end());
}

TEST_CASE("pashto: a broken translation falls back to English rather than failing")
{
    LanguagePack broken = english_pack();
    broken.messages[DiagId::UndeclaredName] = "{0} and {}"; // mixes automatic and manual indexing
    CHECK_EQ(broken.format(DiagId::UndeclaredName, {"x"}), "'x' is not declared");
}

TEST_CASE("language directive")
{
    CHECK(language_directive("// lang: ps\nx") == std::string("ps"));
    CHECK(language_directive("\xEF\xBB\xBF  // language: en  \r\n") == std::string("en"));
    CHECK(language_directive("// ژبه: پښتو") == std::string("پښتو"));
    CHECK(!language_directive("var x = 1 // lang: ps").has_value());
    CHECK(!language_directive("// just a comment").has_value());
    CHECK(!language_directive("// lang:").has_value());

    const SourceFile pashto("p.th", "// ژبه: ps\nکرښه چاپ(۱)\n");
    CHECK(compile(pashto).succeeded()); // English default overridden by the directive
    CHECK_EQ(compile(pashto).program->language, "ps");
    const SourceFile unknown("u.th", "// lang: tlh\nprintln(1)\n");
    const CompileResult result = compile(unknown);
    CHECK(result.succeeded());
    REQUIRE(result.diagnostics.items().size() == 1);
    CHECK_EQ(result.diagnostics.items()[0].code, "W003");
}

TEST_CASE("translate: main() has a name in each language")
{
    const SourceFile source("t.th", "func main()\n    println(1)\n");
    const TranslationResult pashto = translate(source, english_pack(), pashto_pack());
    REQUIRE(pashto.ok);
    CHECK_CONTAINS(pashto.text, "دنده اصلي()");
    CHECK_EQ(run_pashto(pashto.text), "۱\n");
    CHECK_EQ(run_pashto("دنده main()\n    کرښه چاپ(۲)\n"), "۲\n"); // the English name works too
    const TranslationResult back = translate(SourceFile("p.th", pashto.text), pashto_pack(), english_pack());
    CHECK_CONTAINS(back.text, "func main()");
}

TEST_CASE("translate: English to Pashto and back")
{
    const std::string english = "// a comment stays\nstruct P\n    x: int\nfunc f(n: int): bool\n    return n % 2 == 0 and true\n"
                                "var s = \"it's \\\"quoted\\\" »\"\nprintln(f(10), len([1, 2]), s, P(x: 3))\nbreak\n";
    const SourceFile source("t.th", english);
    const TranslationResult to_pashto = translate(source, english_pack(), pashto_pack());
    REQUIRE(to_pashto.ok);
    CHECK_CONTAINS(to_pashto.text, "// ژبه: ps\n// a comment stays\nجوړښت P");
    CHECK_CONTAINS(to_pashto.text, "دنده f(n: عدد): منطقي");
    CHECK_CONTAINS(to_pashto.text, "راګرځه n % ۲ == ۰ او رښتیا");
    CHECK_CONTAINS(to_pashto.text, "متغیر s = \"it's \\\"quoted\\\" »\"");
    CHECK_CONTAINS(to_pashto.text, "کرښه چاپ(f(۱۰)، اوږدوالی([۱، ۲])، s، P(x: ۳))\nودرېږه");

    const SourceFile pashto("p.th", to_pashto.text);
    const TranslationResult back = translate(pashto, english_pack(), english_pack());
    REQUIRE(back.ok);
    CHECK_EQ(back.text, "// lang: en\n" + english);
}

TEST_CASE("translate: punctuation and quotes become the preferred forms of each language")
{
    // Pashto prefers the Arabic comma '،' and the ASCII quote; English the ASCII comma.
    const SourceFile source("p.th", "// ژبه: ps\nکرښه   چاپ(«a\\»\"b», ۱)\nx = ۷ ٪ ۲\n");
    const TranslationResult result = translate(source, english_pack(), pashto_pack());
    REQUIRE(result.ok);
    CHECK_EQ(result.text, "// ژبه: ps\nکرښه چاپ(\"a»\\\"b\"، ۱)\nx = ۷ % ۲\n");
    const TranslationResult english = translate(source, english_pack(), english_pack());
    REQUIRE(english.ok);
    CHECK_EQ(english.text, "// lang: en\nprintln(\"a»\\\"b\", 1)\nx = 7 % 2\n");
}

TEST_CASE("translate: names that are reserved in the target language are renamed")
{
    const SourceFile source("t.th", "var که = 1\nprintln(که)\n");
    const TranslationResult result = translate(source, english_pack(), pashto_pack());
    REQUIRE(result.ok);
    CHECK_CONTAINS(result.text, "متغیر که_ = ۱\nکرښه چاپ(که_)");
    CHECK(result.renamed == std::vector<std::string>{"که"});
}

TEST_CASE("translate: lexical errors stop translation")
{
    const SourceFile source("t.th", "var x = #\n");
    const TranslationResult result = translate(source, english_pack(), pashto_pack());
    CHECK(!result.ok);
    CHECK(result.diagnostics.has_errors());
}

TEST_CASE("pashto: reports and language info")
{
    const SourceFile source("p.th", "// ژبه: ps\nجوړښت ټ\n    x: عدد\nمتغیر a = ټ(۱)\nکرښه چاپ(a)");
    const CompileResult result = compile(source);
    REQUIRE(result.succeeded());
    const std::string json = compile_report_json(result, source);
    CHECK_CONTAINS(json, "\"language\":{\"code\":\"ps\",\"name\":\"پښتو\",\"right_to_left\":true}");
    CHECK_CONTAINS(json, "\"type\":\"عدد\"");
    CHECK_CONTAINS(json, "\"structs\":[{\"index\":0,\"name\":\"ټ\"");
    const std::string info = language_info_json(pashto_pack());
    CHECK_CONTAINS(info, "\"native_name\":\"پښتو\"");
    CHECK_CONTAINS(info, "\"spellings\":[\"راګرځه\",\"راگرځه\"]");
    CHECK_CONTAINS(info, "{\"text\":\"،\",\"kind\":\"Comma\",\"means\":\",\"}");
    CHECK_CONTAINS(info, "\"digits\":[\"۰\",\"۱\"");
    CHECK_CONTAINS(info, "\"quotes\":[[\"\\\"\",\"\\\"\"],[\"«\",\"»\"],[\"“\",\"”\"],[\"”\",\"”\"],[\"„\",\"“\"]]");
    CHECK_CONTAINS(info, "ارزښتونه چاپوي");
    CHECK_CONTAINS(to_tree_text(*result.ast, &pashto_pack()), "Field x : عدد");
    CHECK_CONTAINS(symbols_text(*result.model, pashto_pack()), "ټ { x: عدد }");

    RuntimeError error;
    error.code = "R001";
    error.message = "په صفر وېش";
    error.position = Position{3, 4};
    error.call_stack = {RuntimeError::Frame{"f", Position{3, 4}, ""}};
    CHECK_EQ(format_runtime_error(error, "p.th", pashto_pack()), "p.th:3:4: د اجرا تېروتنه [R001]: په صفر وېش\n    په f کې (کرښه ۳)");
}

TEST_CASE("display locale")
{
    DisplayLocale locale = pashto_pack().display;
    CHECK_EQ(locale.format_int(-120), "-۱۲۰");
    CHECK_EQ(locale.localize_digits("a1b2"), "a۱b۲");
    CHECK_EQ(Value::from_string("x\"y").to_debug(locale), "«x\"y»");
    CHECK_EQ(Value::from_array({Value::from_bool(true), Value::from_int(3)}).to_display(locale), "[رښتیا، ۳]");
}
