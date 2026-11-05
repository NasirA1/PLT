#include "thunder/diagnostics.hpp"

#include "thunder/language_pack.hpp"
#include "thunder/utf8.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace thunder
{
    namespace
    {
        using enum Phase;
        using enum Severity;

        // The message catalog. The order must match the DiagId enumeration.
        constexpr std::array catalog{
            // Lexical analysis
            DiagInfo{"L001", Lexical, Error, "unexpected character '{}'"},
            DiagInfo{"L002", Lexical, Error, "unexpected character '{}'; did you mean '{}'?"},
            DiagInfo{"L003", Lexical, Error, "unterminated string literal (strings must end with \" on the same line)"},
            DiagInfo{"L004", Lexical, Error, "invalid escape sequence '\\{}' (valid escapes are \\n \\t \\r \\0 \\\" \\\\)"},
            DiagInfo{"L005", Lexical, Error, "integer literal {} is too large (the largest int is 9223372036854775807)"},
            DiagInfo{"L006", Lexical, Error, "malformed number '{}'"},
            DiagInfo{"L007", Lexical, Error, "unterminated block comment (missing */)"},
            // Syntax analysis
            DiagInfo{"P001", Syntax, Error, "expected {} but found {}"},
            DiagInfo{"P002", Syntax, Error, "expected an expression but found {}"},
            DiagInfo{"P003", Syntax, Error, "expected a type (int, bool, string or a struct name) but found {}"},
            DiagInfo{"P004", Syntax, Error, "expected a statement but found {}"},
            DiagInfo{"P005", Syntax, Error, "invalid assignment target: only variables, array elements and struct fields can be assigned"},
            DiagInfo{"P006", Syntax, Error, "this expression cannot be called; only named functions can be called"},
            DiagInfo{"P007", Syntax, Error, "functions can only be declared at the top level of a program"},
            DiagInfo{"P008", Syntax, Error, "variable '{}' needs a type annotation or an initial value"},
            DiagInfo{"P009", Syntax, Error, "too many errors; giving up"},
            DiagInfo{"P010", Syntax, Error, "expected the end of the statement (a new line or ';') but found {}"},
            DiagInfo{"P011", Syntax, Error, "structs can only be declared at the top level of a program"},
            // Semantic analysis
            DiagInfo{"S001", Semantic, Error, "'{}' is not declared"},
            DiagInfo{"S002", Semantic, Error, "'{}' is already declared in this scope"},
            DiagInfo{"S003", Semantic, Error, "function '{}' is already defined"},
            DiagInfo{"S004", Semantic, Error, "'{}' is a built-in function and cannot be redefined"},
            DiagInfo{"S005", Semantic, Error, "cannot assign a value of type '{}' to '{}' of type '{}'"},
            DiagInfo{"S006", Semantic, Error, "operator '{}' cannot be applied to '{}' and '{}'"},
            DiagInfo{"S007", Semantic, Error, "operator '{}' cannot be applied to '{}'"},
            DiagInfo{"S008", Semantic, Error, "condition must be of type 'bool' but is '{}'"},
            DiagInfo{"S009", Semantic, Error, "function '{}' expects {} argument(s) but {} were given"},
            DiagInfo{"S010", Semantic, Error, "argument {} of '{}' must be of type '{}' but is '{}'"},
            DiagInfo{"S011", Semantic, Error, "'{}' is not a function"},
            DiagInfo{"S012", Semantic, Error, "cannot assign to constant '{}'"},
            DiagInfo{"S013", Semantic, Error, "'{}' can only be used inside a loop"},
            DiagInfo{"S014", Semantic, Error, "'return' can only be used inside a function"},
            DiagInfo{"S015", Semantic, Error, "function '{}' must return a value of type '{}'"},
            DiagInfo{"S016", Semantic, Error, "function '{}' does not declare a return type, so it cannot return a value"},
            DiagInfo{"S017", Semantic, Error, "not all code paths in function '{}' return a value"},
            DiagInfo{"S018", Semantic, Error, "a value of type '{}' cannot be indexed"},
            DiagInfo{"S019", Semantic, Error, "index must be of type 'int' but is '{}'"},
            DiagInfo{"S020", Semantic, Error, "all array elements must have the same type: expected '{}' but found '{}'"},
            DiagInfo{"S021", Semantic, Error, "cannot infer the element type of an empty array; add a type annotation, e.g. var a: int[] = [];"},
            DiagInfo{"S022", Semantic, Error, "function '{}' does not return a value, so its result cannot be used"},
            DiagInfo{"S023", Semantic, Error, "'main' must take no parameters and must not declare a return type"},
            DiagInfo{"S024", Semantic, Error, "strings are immutable; a character of a string cannot be assigned"},
            DiagInfo{"S025", Semantic, Error, "'{}' is a function; call it with parentheses, e.g. {}(...)"},
            DiagInfo{"S026", Semantic, Error, "built-in '{}' cannot be applied to a value of type '{}'"},
            DiagInfo{"S027", Semantic, Error, "function '{}' must return a value of type '{}' but this value has type '{}'"},
            DiagInfo{"S028", Semantic, Error, "the name '{}' is already used by another type or function"},
            DiagInfo{"S029", Semantic, Error, "struct '{}' already has a field named '{}'"},
            DiagInfo{"S030", Semantic, Error, "struct '{}' contains itself through field '{}'; recursive structs are not supported"},
            DiagInfo{"S031", Semantic, Error, "unknown type '{}'"},
            DiagInfo{"S032", Semantic, Error, "a value of type '{}' has no field named '{}'"},
            DiagInfo{"S033", Semantic, Error, "named arguments (name: value) can only be used to create a struct value"},
            DiagInfo{"S034", Semantic, Error, "when creating '{}', give either all arguments by name or all by position"},
            DiagInfo{"S035", Semantic, Error, "field '{}' is given more than once"},
            DiagInfo{"S036", Semantic, Error, "'{}' is a struct type; create a value with {}(...)"},
            DiagInfo{"S037", Semantic, Error, "fields can only be used on struct values, not on a value of type '{}'"},
            DiagInfo{"W001", Semantic, Warning, "variable '{}' is declared but never used"},
            DiagInfo{"W002", Semantic, Warning, "unreachable code"},
            DiagInfo{"W003", Semantic, Warning, "unknown language '{}' in the language directive; using {}"},
            // Runtime
            DiagInfo{"R001", Runtime, Error, "division by zero"},
            DiagInfo{"R002", Runtime, Error, "integer overflow in {} {} {}"},
            DiagInfo{"R003", Runtime, Error, "index {} is out of bounds for an array of length {}"},
            DiagInfo{"R004", Runtime, Error, "index {} is out of bounds for a string of length {}"},
            DiagInfo{"R005", Runtime, Error, "cannot convert \"{}\" to int"},
            DiagInfo{"R006", Runtime, Error, "array size cannot be negative (got {})"},
            DiagInfo{"R007", Runtime, Error, "array size {} is too large (the limit is {})"},
            DiagInfo{"R008", Runtime, Error, "stack overflow: more than {} nested function calls (is the recursion missing a base case?)"},
            DiagInfo{"R009", Runtime, Error, "step limit of {} instructions exceeded (possible infinite loop)"},
            DiagInfo{"R010", Runtime, Error, "invalid bytecode: {}"},
        };

        static_assert(catalog.size() == static_cast<std::size_t>(DiagId::InvalidBytecode) + 1,
            "the message catalog must have one entry per DiagId");
    }

    const DiagInfo& info(DiagId id)
    {
        return catalog.at(static_cast<std::size_t>(id));
    }

    std::size_t diag_count()
    {
        return catalog.size();
    }

    std::string_view phase_name(Phase phase)
    {
        switch (phase)
        {
        case Phase::Lexical:
            return "lexical";
        case Phase::Syntax:
            return "syntax";
        case Phase::Semantic:
            return "semantic";
        case Phase::Runtime:
            return "runtime";
        }
        return "unknown";
    }

    std::string_view severity_name(Severity severity)
    {
        return severity == Severity::Error ? "error" : "warning";
    }

    DiagnosticBag::DiagnosticBag(const LanguagePack& language)
        : language_(&language)
    {
    }

    void DiagnosticBag::add(DiagId id, const Span& span, const std::vector<std::string>& arguments)
    {
        const DiagInfo& entry = info(id);
        if (entry.severity == Severity::Error)
        {
            ++error_count_;
        }
        items_.push_back(Diagnostic{id, entry.code, entry.phase, entry.severity, language_->format(id, arguments), span});
    }

    void DiagnosticBag::sort_by_position()
    {
        std::ranges::stable_sort(items_, [](const Diagnostic& left, const Diagnostic& right)
        {
            if (left.span.start.line != right.span.start.line)
            {
                return left.span.start.line < right.span.start.line;
            }
            return left.span.start.column < right.span.start.column;
        });
    }

    std::string format_short(const Diagnostic& diagnostic, std::string_view file_name, const LanguagePack& language)
    {
        return std::format("{}:{}:{}: {} {} [{}]: {}", file_name, diagnostic.span.start.line,
            diagnostic.span.start.column, language.phase_name(diagnostic.phase),
            language.severity_name(diagnostic.severity), diagnostic.code, diagnostic.message);
    }

    std::string format_with_source(const Diagnostic& diagnostic, const SourceFile& source, const LanguagePack& language)
    {
        std::string result = format_short(diagnostic, source.name(), language);
        const std::string_view line = source.line_text(diagnostic.span.start.line);
        if (line.empty())
        {
            return result;
        }

        const std::string gutter = std::format("{:>5} | ", diagnostic.span.start.line);
        result += '\n';
        result += gutter;
        result += line;
        result += '\n';
        result += std::string(gutter.size() - 2, ' ');
        result += "| ";

        // Pad up to the start column, keeping tabs so the caret lines up with the source line.
        const std::vector<std::string> chars = utf8::code_points(line);
        const std::size_t start = diagnostic.span.start.column - 1;
        for (std::size_t i = 0; i < start && i < chars.size(); ++i)
        {
            result += chars[i] == "\t" ? '\t' : ' ';
        }

        std::size_t width = 1;
        if (diagnostic.span.end.line == diagnostic.span.start.line
            && diagnostic.span.end.column > diagnostic.span.start.column)
        {
            width = diagnostic.span.end.column - diagnostic.span.start.column;
        }
        result += '^';
        result += std::string(width - 1, '~');
        return result;
    }
}
