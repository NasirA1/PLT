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
            DiagInfo{"L008", Lexical, Error, "this line is indented less than the line before it, but not back to the level of an enclosing block"},
            DiagInfo{"L009", Lexical, Error, "decimal literal {} is too large"},
            // Syntax analysis
            DiagInfo{"P001", Syntax, Error, "expected {} but found {}"},
            DiagInfo{"P002", Syntax, Error, "expected an expression but found {}"},
            DiagInfo{"P003", Syntax, Error, "expected a type (int, bool, string or a struct name) but found {}"},
            DiagInfo{"P004", Syntax, Error, "expected a statement but found {}"},
            DiagInfo{"P005", Syntax, Error, "invalid assignment target: only variables, array elements and struct fields can be assigned"},
            DiagInfo{"P006", Syntax, Error, "this value cannot be called; only functions and function values can be called"},
            DiagInfo{"P007", Syntax, Error, "functions can only be declared at the top level of a program"},
            DiagInfo{"P008", Syntax, Error, "variable '{}' needs a type annotation or an initial value"},
            DiagInfo{"P009", Syntax, Error, "too many errors; giving up"},
            DiagInfo{"P010", Syntax, Error, "expected the end of the statement (a new line) but found {}"},
            DiagInfo{"P011", Syntax, Error, "structs can only be declared at the top level of a program"},
            DiagInfo{"P012", Syntax, Error, "expected an indented block after {} but found {}"},
            DiagInfo{"P013", Syntax, Error, "unexpected indentation (this line is indented more than the line before it)"},
            DiagInfo{"P014", Syntax, Error, "expected an indented block after {}; Thunder marks blocks by indentation, not by '{{' and '}}'"},
            DiagInfo{"P015", Syntax, Error, "a tuple needs at least two elements"},
            DiagInfo{"P016", Syntax, Error, "'import' can only be used at the top level of a program"},
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
            DiagInfo{"S021", Semantic, Error, "cannot infer the element type of an empty array; add a type annotation, e.g. var a: int[] = []"},
            DiagInfo{"S022", Semantic, Error, "function '{}' does not return a value, so its result cannot be used"},
            DiagInfo{"S023", Semantic, Error, "'main' must take no parameters and must not declare a return type"},
            DiagInfo{"S024", Semantic, Error, "strings are immutable; a character of a string cannot be assigned"},
            DiagInfo{"S025", Semantic, Error, "'{}' is a built-in function and cannot be used as a value; call it with parentheses, e.g. {}(...)"},
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
            DiagInfo{"S038", Semantic, Error, "an empty map literal needs a type annotation, e.g. var m: {{string: int}} = {{}}"},
            DiagInfo{"S039", Semantic, Error, "map keys must be of type int, bool or string, not '{}'"},
            DiagInfo{"S040", Semantic, Error, "all map {} must have the same type: expected '{}' but found '{}'"},
            DiagInfo{"S041", Semantic, Error, "'.{}' selects a tuple element, but the value has type '{}'"},
            DiagInfo{"S042", Semantic, Error, "a tuple of type '{}' has no element {}"},
            DiagInfo{"S043", Semantic, Error, "the key of a map of type '{}' must be of type '{}' but is '{}'"},
            DiagInfo{"S044", Semantic, Error, "cannot unpack a value of type '{}' into {} names"},
            DiagInfo{"S045", Semantic, Error, "module '{}' not found (looked for {})"},
            DiagInfo{"S046", Semantic, Error, "module '{}' is written in {} but this program is in {}; translate it with thunderc --translate"},
            DiagInfo{"S047", Semantic, Error, "a module can only declare functions, structs, variables and constants; this statement would run whenever the module is imported"},
            DiagInfo{"S048", Semantic, Error, "a module cannot define 'main'; only the program that is run can"},
            DiagInfo{"S049", Semantic, Error, "'{0}' belongs to the '{1}' module; add 'import {1}' at the top of the program"},
            DiagInfo{"S050", Semantic, Error, "there is no built-in module '{0}' (to import a file, put its name in quotes: import \"{0}\")"},
            DiagInfo{"S051", Semantic, Error, "argument {} of '{}' must be a function of type '{}' but has type '{}'"},
            DiagInfo{"S052", Semantic, Error, "'for ... in' needs an array, a string or a map, not '{}'"},
            DiagInfo{"S053", Semantic, Error, "a native function cannot use the type '{}'; it may use int, float, bool, string, and a function type for a callback"},
            DiagInfo{"S054", Semantic, Error, "'native func {}' needs a library: add 'import native \"...\"' to the program"},
            DiagInfo{"S055", Semantic, Error, "native function '{}' has more than {} parameters"},
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
            DiagInfo{"R011", Runtime, Error, "key {} is not in the map"},
            DiagInfo{"R012", Runtime, Error, "range {}..{} is out of bounds for a length of {}"},
            DiagInfo{"R013", Runtime, Error, "'{}' needs an array that is not empty"},
            DiagInfo{"R014", Runtime, Error, "the count given to '{}' cannot be negative (got {})"},
            DiagInfo{"R015", Runtime, Error, "char_code needs a string of exactly one character, not \"{}\""},
            DiagInfo{"R016", Runtime, Error, "{} is not a valid Unicode code point"},
            DiagInfo{"R017", Runtime, Error, "cannot read '{}': {}"},
            DiagInfo{"R018", Runtime, Error, "cannot write '{}': {}"},
            DiagInfo{"R019", Runtime, Error, "cannot run the command '{}'"},
            DiagInfo{"R020", Runtime, Error, "a function variable was called before a function was assigned to it"},
            DiagInfo{"R021", Runtime, Error, "\"{}\" is not a color (use a name such as \"red\", \"#ff8800\" or rgb(255, 136, 0))"},
            DiagInfo{"R022", Runtime, Error, "the {} given to '{}' must be from {} to {} (got {})"},
            DiagInfo{"R023", Runtime, Error, "random_int({}, {}): the lowest number must not be greater than the highest"},
            DiagInfo{"R024", Runtime, Error, "more than {} shapes have been drawn since the canvas was last cleared (call clear() at the start of each frame)"},
            DiagInfo{"R025", Runtime, Error, "the result of {} {} {} is too large for a decimal number"},
            DiagInfo{"R026", Runtime, Error, "cannot convert \"{}\" to float"},
            DiagInfo{"R027", Runtime, Error, "'{}' is not defined for {}"},
            DiagInfo{"R028", Runtime, Error, "cannot load the native library '{}': {}"},
            DiagInfo{"R029", Runtime, Error, "the native library does not provide a function called '{}'"},
            DiagInfo{"R030", Runtime, Error, "the native function failed: {}"},
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
            if (left.span.file != right.span.file)
            {
                return left.span.file < right.span.file;
            }
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
