#include "helpers.hpp"

#include "thunder/diagnostics.hpp"
#include "thunder/utf8.hpp"

#include <format>
#include <stdexcept>

namespace test
{
    using namespace thunder;

    std::vector<TokenKind> token_kinds(const std::string& text)
    {
        const SourceFile source("test.th", text);
        DiagnosticBag diagnostics;
        Lexer lexer(source, english_pack(), diagnostics);
        std::vector<TokenKind> kinds;
        for (const Token& token : lexer.tokenize())
        {
            if (token.kind != TokenKind::EndOfFile)
            {
                kinds.push_back(token.kind);
            }
        }
        return kinds;
    }

    std::vector<std::string> diagnostic_codes(const std::string& text)
    {
        const SourceFile source("test.th", text);
        const CompileResult result = compile(source);
        std::vector<std::string> codes;
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            codes.push_back(diagnostic.code);
        }
        return codes;
    }

    std::string parse_expr(const std::string& text)
    {
        const SourceFile source("test.th", text);
        DiagnosticBag diagnostics;
        Lexer lexer(source, english_pack(), diagnostics);
        Parser parser(lexer.tokenize(), english_pack(), diagnostics);
        const ExprPtr expr = parser.parse_standalone_expression();
        return expr && !diagnostics.has_errors() ? to_sexpr(*expr) : std::string();
    }

    std::string parse_program(const std::string& text)
    {
        const SourceFile source("test.th", text);
        DiagnosticBag diagnostics;
        Lexer lexer(source, english_pack(), diagnostics);
        Parser parser(lexer.tokenize(), english_pack(), diagnostics);
        return to_sexpr(parser.parse_program());
    }

    RunResult run(const std::string& text, VmOptions options)
    {
        RunResult run_result;
        const SourceFile source("test.th", text);
        const CompileResult result = compile(source);
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            run_result.diagnostics.push_back(std::format("{}:{}: {}", diagnostic.span.start.line,
                diagnostic.span.start.column, diagnostic.code));
        }
        if (!result.program)
        {
            return run_result;
        }
        run_result.compiled = true;
        std::ostringstream output;
        Vm vm(*result.program, output, options);
        const ExecResult exec = vm.run();
        run_result.output = output.str();
        run_result.error = exec.error;
        run_result.stats = exec.stats;
        return run_result;
    }

    std::string run_output(const std::string& text)
    {
        const RunResult result = run(text);
        if (!result.compiled)
        {
            std::string out = "COMPILE ERROR";
            for (const std::string& diagnostic : result.diagnostics)
            {
                out += " " + diagnostic;
            }
            return out;
        }
        std::string out = result.output;
        if (result.error)
        {
            out += std::format("ERROR {}: {}", result.error->code, result.error->message);
        }
        return out;
    }

    bytecode::Program compile_ok(const std::string& text)
    {
        const SourceFile source("test.th", text);
        CompileResult result = compile(source);
        if (!result.program)
        {
            std::string message = "compilation failed:";
            for (const Diagnostic& diagnostic : result.diagnostics.items())
            {
                message += "\n  " + format_short(diagnostic, "test.th");
            }
            throw std::runtime_error(message);
        }
        return std::move(*result.program);
    }

    std::size_t comment_start(const std::string& line)
    {
        const std::string_view text(line);
        char32_t closing = 0; // the quote that would end the string we are inside, 0 outside
        std::size_t pos = 0;
        while (pos < text.size())
        {
            const utf8::Decoded decoded = utf8::decode(text, pos);
            const char32_t c = decoded.code_point;
            if (closing != 0)
            {
                if (c == U'\\')
                {
                    pos += decoded.length;
                    pos += utf8::decode(text, pos).length; // the escaped character
                    continue;
                }
                const bool curly = closing == U'\u201c' && (c == U'\u201c' || c == U'\u201d' || c == U'\u201e');
                if (c == closing || curly)
                {
                    closing = 0;
                }
            }
            else if (c == U'"')
            {
                closing = U'"';
            }
            else if (c == U'\u00ab') // «...»
            {
                closing = U'\u00bb';
            }
            else if (c == U'\u201c' || c == U'\u201d' || c == U'\u201e') // any curly double quote
            {
                closing = U'\u201c';
            }
            else if (c == U'#')
            {
                return pos;
            }
            pos += decoded.length;
        }
        return std::string::npos;
    }
}
