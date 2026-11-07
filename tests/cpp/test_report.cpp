#include "helpers.hpp"
#include "json_check.hpp"
#include "minitest.hpp"

#include "thunder/json.hpp"
#include "thunder/report.hpp"

using namespace thunder;

TEST_CASE("json: writer produces valid compact and pretty output")
{
    for (const bool pretty : {false, true})
    {
        JsonWriter json(pretty);
        json.begin_object();
        json.field("s", "quote\" backslash\\ newline\n tab\t ctrl\x01 bs\b ff\f cr\r");
        json.field("i", -5);
        json.field("u", 7u);
        json.field("big", std::uint64_t{18446744073709551615ULL});
        json.field("d", 1.5);
        json.field("inf", std::numeric_limits<double>::infinity());
        json.field("b", true);
        json.key("n").null();
        json.key("empty_array").begin_array().end_array();
        json.key("empty_object").begin_object().end_object();
        json.key("a").begin_array().value(1).value("x").begin_object().end_object().end_array();
        json.end_object();
        CHECK(test::is_valid_json(json.str()));
    }
    CHECK_EQ(JsonWriter::escape("\x01"), "\\u0001");
    CHECK_EQ(JsonWriter::escape("پ"), "پ");
}

TEST_CASE("json: the checker itself rejects invalid documents")
{
    CHECK(!test::is_valid_json("{"));
    CHECK(!test::is_valid_json("{\"a\" 1}"));
    CHECK(!test::is_valid_json("[1,]"));
    CHECK(!test::is_valid_json("\"\\x\""));
    CHECK(!test::is_valid_json("01x"));
    CHECK(test::is_valid_json(" [1, -2.5, \"\\u00e9\", {\"k\": null}] "));
}

TEST_CASE("report: compile report for a successful program")
{
    const SourceFile source("ok.th", "var x = 1\nfunc f(): int\n    return x\nprintln(f())");
    const CompileResult result = compile(source);
    const std::string json = compile_report_json(result, source);
    CHECK(test::is_valid_json(json));
    CHECK_CONTAINS(json, "\"success\":true");
    CHECK_CONTAINS(json, "\"tokens\":[{\"kind\":\"KwVar\",\"category\":\"keyword\",\"lexeme\":\"var\"");
    CHECK_CONTAINS(json, "\"ast\":{\"kind\":\"Program\"");
    CHECK_CONTAINS(json, "\"role\":\"value\"");
    CHECK_CONTAINS(json, "\"scopes\":{\"kind\":\"global\"");
    CHECK_CONTAINS(json, "\"mnemonic\":\"LOAD_GLOBAL\"");
    CHECK_CONTAINS(json, "\"hex\":\"54484e44");
    CHECK_CONTAINS(json, "\"name\":\"code generation\",\"ran\":true,\"succeeded\":true");
    CHECK(test::is_valid_json(compile_report_json(result, source, true)));
}

TEST_CASE("report: compile report for a failing program")
{
    const SourceFile source("bad.th", "var x: int = \"s\"\n");
    const CompileResult result = compile(source);
    const std::string json = compile_report_json(result, source);
    CHECK(test::is_valid_json(json));
    CHECK_CONTAINS(json, "\"success\":false");
    CHECK_CONTAINS(json, "\"code\":\"S005\",\"phase\":\"semantic\",\"severity\":\"error\"");
    CHECK_CONTAINS(json, "\"bytecode\":null");
    CHECK_CONTAINS(json, "\"binary\":null");

    const SourceFile lexical("lex.th", "@");
    const CompileResult lex_result = compile(lexical);
    const std::string lex_json = compile_report_json(lex_result, lexical);
    CHECK(test::is_valid_json(lex_json));
    CHECK_CONTAINS(lex_json, "\"ast\":null");
    CHECK_CONTAINS(lex_json, "\"symbols\":null");
}

TEST_CASE("report: language info")
{
    const std::string json = language_info_json(english_pack());
    CHECK(test::is_valid_json(json));
    CHECK_CONTAINS(json, "{\"kind\":\"KwWhile\",\"category\":\"keyword\",\"text\":\"while\",\"spellings\":[\"while\"]}");
    CHECK_CONTAINS(json, "\"name\":\"println\"");
    CHECK_CONTAINS(json, "\"mnemonic\":\"JUMP_IF_FALSE\"");
    CHECK_CONTAINS(json, "\"right_to_left\":false");
    CHECK(test::is_valid_json(language_info_json(english_pack(), true)));
}

TEST_CASE("report: text forms of tokens, symbols and hex dumps")
{
    const SourceFile source("t.th", "const k = 5\nfunc f(a: int)\n    println(a, k)\nf(1)");
    const CompileResult result = compile(source);
    REQUIRE(result.succeeded());

    const std::string tokens = tokens_text(result);
    CHECK_CONTAINS(tokens, "LINE:COL");
    CHECK_CONTAINS(tokens, "1:1       KwConst        keyword      const");

    const std::string symbols = symbols_text(*result.model);
    CHECK_CONTAINS(symbols, "#1 f(int): void");
    CHECK_CONTAINS(symbols, "scope global");
    CHECK_CONTAINS(symbols, "global     k            int        slot 0  (const)");
    CHECK_CONTAINS(symbols, "scope function f");

    std::vector<bytecode::Section> sections;
    const auto bytes = bytecode::serialize(*result.program, &sections);
    const std::string hex = hex_dump_text(bytes, sections);
    CHECK_CONTAINS(hex, "header (");
    CHECK_CONTAINS(hex, "00000000  54 48 4e 44");
    CHECK_CONTAINS(hex, "THND");
}

TEST_CASE("report: runtime errors and statistics")
{
    const auto result = test::run("func f(): int\n    return 1 / 0\nprintln(f())");
    REQUIRE(result.error.has_value());
    const std::string text = format_runtime_error(*result.error, "x.th");
    CHECK_CONTAINS(text, "x.th:2:14: runtime error [R001]: division by zero");
    CHECK_CONTAINS(text, "\n    in f (line 2)\n    in <main> (line 3)");

    JsonWriter json;
    json.begin_object();
    json.key("error");
    write_runtime_error_json(json, *result.error);
    json.key("stats");
    write_stats_json(json, result.stats);
    json.end_object();
    CHECK(test::is_valid_json(json.str()));
    CHECK_CONTAINS(json.str(), "\"call_stack\":[{\"function\":\"f\"");
}

TEST_CASE("report: execution traces record machine state")
{
    const bytecode::Program program = test::compile_ok("var g = 1\nfunc f(n: int): int\n    return n + g\nprintln(f(41))");
    std::ostringstream out;
    Vm vm(program, out);
    TraceRecorder recorder(1000);
    const ExecResult result = vm.run(&recorder);
    const std::string json = recorder.finish(result);
    CHECK(test::is_valid_json(json));
    CHECK_CONTAINS(json, "\"ok\":true");
    CHECK_CONTAINS(json, "\"truncated\":false");
    CHECK_CONTAINS(json, "\"output\":[{\"step\":");
    CHECK_CONTAINS(json, "\"text\":\"42\\n\"");
    CHECK_CONTAINS(json, "\"function\":\"f\"");
    CHECK_CONTAINS(json, "\"locals\":[{\"name\":\"n\",\"value\":\"41\"}]");
    CHECK_CONTAINS(json, "\"globals\":[{\"name\":\"g\",\"value\":\"1\"}]");
    CHECK_EQ(recorder.recorded_steps(), result.stats.steps);
}

TEST_CASE("report: traces stop recording at the limit but execution continues")
{
    const bytecode::Program program = test::compile_ok("var s = 0\nfor (var i = 0, i < 100, i = i + 1)\n    s = s + i\nprintln(s)");
    std::ostringstream out;
    Vm vm(program, out);
    TraceRecorder recorder(10);
    const ExecResult result = vm.run(&recorder);
    const std::string json = recorder.finish(result);
    CHECK(test::is_valid_json(json));
    CHECK_CONTAINS(json, "\"truncated\":true");
    CHECK_EQ(recorder.recorded_steps(), 10u);
    CHECK_EQ(out.str(), "4950\n");
}

TEST_CASE("report: a trace of a failing program includes the error")
{
    const bytecode::Program program = test::compile_ok("println(1 / 0)");
    std::ostringstream out;
    Vm vm(program, out);
    TraceRecorder recorder(100);
    const std::string json = recorder.finish(vm.run(&recorder));
    CHECK(test::is_valid_json(json));
    CHECK_CONTAINS(json, "\"ok\":false");
    CHECK_CONTAINS(json, "\"error\":{\"code\":\"R001\"");
}

TEST_CASE("diagnostics: formatting with source context")
{
    const SourceFile source("d.th", "var a = 1\nvar b: int =\t\"text\"\n\r\n");
    const CompileResult result = compile(source);
    REQUIRE(result.diagnostics.items().size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.items()[0];
    CHECK_EQ(format_short(diagnostic, "d.th"),
        "d.th:2:14: semantic error [S005]: cannot assign a value of type 'string' to 'b' of type 'int'");
    const std::string full = format_with_source(diagnostic, source);
    CHECK_CONTAINS(full, "    2 | var b: int =\t\"text\"\n");
    CHECK_CONTAINS(full, "      |             \t^~~~~~");

    Diagnostic outside = diagnostic;
    outside.span.start.line = 99;
    CHECK_EQ(format_with_source(outside, source), format_short(outside, "d.th"));
    CHECK_EQ(phase_name(Phase::Runtime), "runtime");
    CHECK_EQ(phase_name(Phase::Lexical), "lexical");
    CHECK_EQ(severity_name(Severity::Warning), "warning");
    CHECK_EQ(info(DiagId::InvalidBytecode).code, std::string("R010"));
}

TEST_CASE("source: line text and span merging")
{
    const SourceFile source("s.th", "first\r\nsecond\nthird");
    CHECK_EQ(source.line_count(), 3u);
    CHECK_EQ(source.line_text(1), "first");
    CHECK_EQ(source.line_text(2), "second");
    CHECK_EQ(source.line_text(3), "third");
    CHECK_EQ(source.line_text(0), "");
    CHECK_EQ(source.line_text(4), "");

    Span a{0, 2, {1, 1}, {1, 3}};
    Span b{10, 3, {2, 1}, {2, 4}};
    const Span merged = Span::merge(a, b);
    CHECK_EQ(merged.length, 13u);
    CHECK(merged.end == (Position{2, 4}));
    const Span backwards = Span::merge(b, a);
    CHECK_EQ(backwards.length, 3u);
}
