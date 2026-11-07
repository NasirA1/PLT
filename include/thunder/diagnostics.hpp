// Diagnostics: errors and warnings reported by each phase of the toolchain.
//
// Every message has a stable code (e.g. "P001") and a template in a single catalog
// (see diagnostics.cpp). Keeping all text in one table makes the messages easy to review
// and, in the future, to translate (for example into Pashto).
#pragma once

#include "thunder/source.hpp"

#include <cstddef>
#include <format>
#include <type_traits>
#include <string>
#include <string_view>
#include <vector>

namespace thunder
{
    // The phase of the pipeline that detected the problem. Showing it to students teaches
    // *where* each kind of mistake is caught.
    enum class Phase
    {
        Lexical,  // characters -> tokens
        Syntax,   // tokens -> abstract syntax tree
        Semantic, // names and types
        Runtime,  // virtual machine
    };

    enum class Severity
    {
        Error,
        Warning,
    };

    enum class DiagId
    {
        // Lexical analysis
        UnexpectedCharacter,
        UnexpectedCharacterHint,
        UnterminatedString,
        InvalidEscape,
        IntegerTooLarge,
        MalformedNumber,
        UnterminatedComment,
        InconsistentIndentation,
        // Syntax analysis
        ExpectedToken,
        ExpectedExpression,
        ExpectedType,
        ExpectedStatement,
        InvalidAssignmentTarget,
        NotCallable,
        NestedFunction,
        VarNeedsTypeOrValue,
        TooManyErrors,
        ExpectedStatementEnd,
        StructNotTopLevel,
        ExpectedIndentedBlock,
        UnexpectedIndent,
        BracesNotUsed,
        TupleNeedsTwoElements,
        ImportNotTopLevel,
        // Semantic analysis
        UndeclaredName,
        AlreadyDeclared,
        FunctionRedefined,
        BuiltinRedefined,
        AssignTypeMismatch,
        BinaryOperandMismatch,
        UnaryOperandMismatch,
        ConditionNotBool,
        ArgumentCount,
        ArgumentType,
        NotAFunction,
        AssignToConstant,
        OutsideLoop,
        ReturnOutsideFunction,
        MissingReturnValue,
        VoidReturnsValue,
        NotAllPathsReturn,
        NotIndexable,
        IndexNotInt,
        ArrayElementMismatch,
        EmptyArrayNeedsType,
        VoidValue,
        BadMainSignature,
        StringImmutable,
        FunctionUsedAsValue,
        BuiltinArgumentType,
        ReturnTypeMismatch,
        TypeRedefined,
        DuplicateField,
        RecursiveStruct,
        UnknownType,
        NoSuchField,
        NamedArgumentsNotAllowed,
        MixedArguments,
        FieldGivenTwice,
        StructUsedAsValue,
        NotAStruct,
        EmptyMapNeedsType,
        MapKeyType,
        MapElementMismatch,
        NotATuple,
        TupleIndexOutOfRange,
        MapKeyMismatch,
        DestructureMismatch,
        ModuleNotFound,
        ModuleLanguageMismatch,
        ModuleStatement,
        ModuleDefinesMain,
        BuiltinNeedsImport,
        UnknownBuiltinModule,
        FunctionArgumentMismatch,
        UnusedVariable,
        UnreachableCode,
        UnknownLanguage,
        // Runtime (virtual machine)
        DivisionByZero,
        IntegerOverflow,
        ArrayIndexOutOfBounds,
        StringIndexOutOfBounds,
        InvalidIntConversion,
        NegativeArraySize,
        ArrayTooLarge,
        CallStackOverflow,
        StepLimitExceeded,
        KeyNotFound,
        RangeOutOfBounds,
        EmptyArray,
        NegativeCount,
        NotOneCharacter,
        InvalidCharCode,
        FileReadFailed,
        FileWriteFailed,
        CommandFailed,
        FunctionNotSet,
        UnknownColor,
        ValueOutOfRange,
        EmptyRandomRange,
        TooManyShapes,
        InvalidBytecode,
    };

    struct DiagInfo
    {
        const char* code;
        Phase phase;
        Severity severity;
        const char* message_template; // English, std::format syntax
    };

    const DiagInfo& info(DiagId id);
    std::size_t diag_count();

    std::string_view phase_name(Phase phase);          // English: "lexical", "syntax", ...
    std::string_view severity_name(Severity severity); // English: "error", "warning"

    // Converts a message argument to text (numbers are localized later, by the language pack).
    template <typename T>
    std::string to_argument(const T& value)
    {
        if constexpr (std::is_convertible_v<const T&, std::string_view>)
        {
            return std::string(std::string_view(value));
        }
        else
        {
            return std::format("{}", value);
        }
    }

    struct LanguagePack;
    const LanguagePack& english_pack();

    struct Diagnostic
    {
        DiagId id{};
        std::string code;
        Phase phase{};
        Severity severity{};
        std::string message;
        Span span;
    };

    class DiagnosticBag
    {
    public:
        static constexpr std::size_t max_errors = 50;

        explicit DiagnosticBag(const LanguagePack& language = english_pack());

        // Reports a diagnostic; the message is built from the language pack's template.
        template <typename... Args>
        void report(DiagId id, const Span& span, const Args&... args)
        {
            add(id, span, std::vector<std::string>{to_argument(args)...});
        }

        void add(DiagId id, const Span& span, const std::vector<std::string>& arguments);

        // Orders the diagnostics by their position in the source (stable for equal positions).
        void sort_by_position();

        const LanguagePack& language() const { return *language_; }
        const std::vector<Diagnostic>& items() const { return items_; }
        std::size_t error_count() const { return error_count_; }
        std::size_t warning_count() const { return items_.size() - error_count_; }
        bool has_errors() const { return error_count_ > 0; }
        bool too_many_errors() const { return error_count_ >= max_errors; }

    private:
        const LanguagePack* language_;
        std::vector<Diagnostic> items_;
        std::size_t error_count_ = 0;
    };

    // "file:line:col: syntax error [P001]: message" (phase and severity words in the given language)
    std::string format_short(const Diagnostic& diagnostic, std::string_view file_name,
        const LanguagePack& language = english_pack());

    // The short form followed by the offending source line and a caret marker.
    std::string format_with_source(const Diagnostic& diagnostic, const SourceFile& source,
        const LanguagePack& language = english_pack());
}
