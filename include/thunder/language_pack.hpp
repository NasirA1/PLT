// Language packs: everything that depends on the natural language a program is written in.
//
// The compiler works with token *kinds*, never with keyword text, so the same compiler accepts
// programs written in English or in Pashto (or any language with a pack). A pack supplies:
//   - the spelling of keywords, type names, true/false and built-in functions
//   - extra punctuation characters (e.g. the Arabic comma '،'), digit sets and string quotes
//   - how the virtual machine displays values (digits, true/false, separators)
//   - translated compiler and runtime messages
//   - the text direction (right_to_left), used by the IDE
#pragma once

#include "thunder/builtins.hpp"
#include "thunder/diagnostics.hpp"
#include "thunder/token.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace thunder
{
    // Folds Arabic/Persian letter forms into the Pashto ones (declared below, used inline here).
    std::string fold_letters(std::string_view text);

    // The annotations the compiler knows, by their canonical English names: @test marks a test,
    // a test suite (a struct or a module of tests); @mock makes a struct a stand-in for the
    // interfaces it lists (docs/testing.md).
    inline constexpr std::array<std::string_view, 2> known_annotations{"test", "mock"};

    // How the virtual machine writes values for print, str and inspect.
    struct DisplayLocale
    {
        std::array<std::string, 10> digits{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
        std::string true_text = "true";
        std::string false_text = "false";
        std::string separator = ", ";  // between array elements and struct fields
        std::string open_quote = "\""; // around strings nested in arrays and structs
        std::string close_quote = "\"";
        std::string function_word = "func"; // function values are shown as <func name>
        std::string nothing_text = "nothing"; // the empty struct reference
        std::string decimal_point = "."; // between the whole and the fractional part of a decimal

        std::string format_int(std::int64_t value) const;
        // A decimal number, always with a fractional part so that it reads as one: 1.0, 0.5, -2.75.
        std::string format_float(double value) const;
        // Replaces ASCII digits by this locale's digits.
        std::string localize_digits(std::string_view text) const;
    };

    struct QuotePair
    {
        char32_t open;
        char32_t close;
    };

    struct LanguagePack
    {
        std::string code;         // ISO 639-1 code, e.g. "en", "ps"
        std::string display_name; // in English, e.g. "Pashto"
        std::string native_name;  // e.g. "پښتو"
        bool right_to_left = false;

        // Words. Several spellings may map to one kind; the *_spellings maps hold the preferred one.
        std::unordered_map<std::string, TokenKind> keywords;
        std::unordered_map<TokenKind, std::string> keyword_spellings;
        // Built-ins are looked up within their module: 'builtins' holds the default module (len,
        // push, ...), 'module_builtins' every other module's members (sqrt in math, trim in string).
        // builtin_spellings holds the preferred spelling of the member, without its module.
        std::unordered_map<std::string, BuiltinId> builtins;
        std::map<BuiltinModule, std::unordered_map<std::string, BuiltinId>> module_builtins;
        std::unordered_map<BuiltinId, std::string> builtin_spellings;
        std::unordered_set<std::string> builtin_words; // every member spelling of every module (for the lexer)
        // Names of the built-in modules ("os"); the English names are accepted in every language.
        std::unordered_map<std::string, BuiltinModule> modules;
        std::unordered_map<BuiltinModule, std::string> module_spellings;
        // Built-in enums (Color) and their members (red): spelling -> id, and the preferred spelling.
        std::unordered_map<std::string, BuiltinEnumId> enums;
        std::unordered_map<BuiltinEnumId, std::string> enum_spellings;
        std::unordered_map<std::string, std::pair<BuiltinEnumId, std::uint16_t>> enum_members;
        std::map<std::pair<BuiltinEnumId, std::uint16_t>, std::string> enum_member_spellings;
        // A spelling may consist of several words separated by single spaces (e.g. the Pashto
        // "کرښه چاپ" for println). The lexer joins such words into one token; this set holds every
        // proper prefix ("کرښه") so that it knows when to look ahead.
        std::unordered_set<std::string> compound_prefixes;

        // Additional single-character punctuation/operators (the ASCII forms are always accepted).
        std::unordered_map<char32_t, TokenKind> punctuation;
        std::unordered_map<TokenKind, std::string> punctuation_spellings; // preferred, for translation

        std::array<char32_t, 10> digits{U'0', U'1', U'2', U'3', U'4', U'5', U'6', U'7', U'8', U'9'}; // preferred
        // The decimal separator this language writes in source code (ASCII '.' is always accepted).
        char32_t decimal_point = U'.';
        std::vector<QuotePair> quotes{{U'"', U'"'}}; // string delimiters; the first pair is preferred
        DisplayLocale display;

        // The name of the function that runs after the top-level statements ("main"; English "main"
        // is accepted in every language).
        std::string main_name = "main";
        bool is_main_name(std::string_view name) const
        {
            return name == "main" || fold_letters(name) == fold_letters(main_name);
        }

        // Annotations ("@test", "@ازموینه"): folded spelling -> canonical English name, and the
        // preferred spelling of each. The English names are accepted in every language.
        std::unordered_map<std::string, std::string> annotations;
        std::unordered_map<std::string, std::string> annotation_spellings;
        void add_annotation(const std::string& canonical, const std::string& spelling); // first is preferred
        // The canonical name ("test") of an annotation word as written (without '@'), or nullopt.
        std::optional<std::string> annotation(std::string_view spelling) const;
        // True if 'words' begins (but is not all of) a spelling of several words ("پېښه").
        bool starts_annotation(std::string_view words) const;
        // The preferred spelling of an annotation in this language (without '@').
        std::string annotation_spelling(std::string_view canonical) const;

        // Where the keyword 'with' stands: before the resource ("with f = open()") in English, after
        // it in Pashto ("f = open() سره" - Pashto says "the file with", not "with the file").
        bool with_after_resource = false;

        // Names of keys for key_pressed() and key_down(), English -> this language ("left" -> "کیڼ").
        // Keys without an entry keep their English name.
        std::vector<std::pair<std::string, std::string>> key_names;

        // The API reference in this language: a built-in's signature as the language writes it
        // (its name, parameter names, types and commas: "ریاضي.جذر(شمېره: اعشاري): اعشاري").
        // Absent entries fall back to the English signature in builtin_info. The descriptions
        // live in 'terms' ("builtin.math.sqrt", "enum.Color", "opcode.HALT").
        std::unordered_map<BuiltinId, std::string> builtin_signatures;
        std::string signature(BuiltinId id) const;

        // Messages. Empty entries fall back to English.
        std::unordered_map<DiagId, std::string> messages;
        std::unordered_map<std::string, std::string> terms; // small phrases, e.g. "end_of_file"
        std::array<std::string, 4> phase_names{"lexical", "syntax", "semantic", "runtime"};
        std::string error_word = "error";
        std::string warning_word = "warning";

        void add_keyword(TokenKind kind, std::string spelling); // the first spelling added is preferred
        void add_builtin(BuiltinId id, std::string spelling);
        void add_module(BuiltinModule module, std::string spelling);
        void add_enum(BuiltinEnumId id, std::string spelling);
        void add_enum_member(BuiltinEnumId id, std::uint16_t member, std::string spelling);
        void add_punctuation(TokenKind kind, char32_t character, bool preferred);

        std::optional<TokenKind> keyword(std::string_view spelling) const;
        // A built-in of the default module ("len"), or a qualified one ("math.sqrt", "ریاضي.جذر";
        // the module may also be written in English).
        std::optional<BuiltinId> builtin(std::string_view spelling) const;
        // A member of a built-in module, by its member spelling ("sqrt" in math).
        std::optional<BuiltinId> module_builtin(BuiltinModule module, std::string_view member) const;
        // True if the words are the (member) spelling of any built-in, in any module.
        bool is_builtin_word(std::string_view words) const;
        // True if 'words' is the beginning (but not the whole) of a multi-word keyword or built-in.
        bool starts_compound(std::string_view words) const;
        std::optional<TokenKind> extra_punctuation(char32_t character) const;
        std::optional<char32_t> closing_quote(char32_t opening) const;

        std::string spelling(TokenKind kind) const;
        std::string spelling(BuiltinId id) const;        // qualified: "math.sqrt" ("len" for the default module)
        std::string member_spelling(BuiltinId id) const; // the member alone: "sqrt"
        std::string spelling(BuiltinModule module) const;
        std::string spelling(BuiltinEnumId id) const;
        std::string member_spelling(BuiltinEnumId id, std::uint16_t member) const;
        std::optional<BuiltinEnumId> builtin_enum(std::string_view spelling) const;
        std::optional<std::pair<BuiltinEnumId, std::uint16_t>> builtin_enum_member(std::string_view spelling) const;
        std::string key_name(std::string_view english) const;   // the name a program sees
        std::string english_key(std::string_view name) const;   // a name in either language -> English
        std::string_view message_template(DiagId id) const;
        std::string term(std::string_view key) const;
        std::string_view phase_name(Phase phase) const;
        std::string_view severity_name(Severity severity) const;

        // Formats a message from its template; arguments that are plain integers get this
        // language's digits.
        std::string format(DiagId id, const std::vector<std::string>& arguments) const;
    };

    // Folds letters that Arabic (not Pashto) keyboard layouts produce into the Pashto letters the
    // language packs register: ك (U+0643) -> ک (U+06A9) and گ (U+06AF) -> ګ (U+06AB). Every lookup
    // of a word folds its input, so كرښه چاپ typed with the Arabic kaf still means کرښه چاپ.
    std::string fold_letters(std::string_view text);

    const LanguagePack& english_pack();
    const LanguagePack& pashto_pack();

    // Finds a pack by code ("en", "ps") or by name ("english", "pashto", "پښتو").
    const LanguagePack* find_language_pack(std::string_view name);
    std::vector<const LanguagePack*> available_language_packs();

    // The built-in module with this name, in any language ("os", or a translation of it).
    std::optional<BuiltinModule> find_builtin_module(std::string_view name);

    // Value of a decimal digit in any supported script (ASCII, Arabic-Indic, Extended Arabic-Indic).
    std::optional<int> digit_value(char32_t character);

    // The language named by a directive comment on the first line, e.g. "# lang: ps" or
    // "// ژبه: پښتو". Returns the name as written, or nullopt if there is no directive.
    std::optional<std::string> language_directive(std::string_view source);
}
