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
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace thunder
{
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

        std::string format_int(std::int64_t value) const;
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
        std::unordered_map<std::string, BuiltinId> builtins;
        std::unordered_map<BuiltinId, std::string> builtin_spellings;
        // Names of the built-in modules ("os"); the English names are accepted in every language.
        std::unordered_map<std::string, BuiltinModule> modules;
        std::unordered_map<BuiltinModule, std::string> module_spellings;
        // A spelling may consist of several words separated by single spaces (e.g. the Pashto
        // "کرښه چاپ" for println). The lexer joins such words into one token; this set holds every
        // proper prefix ("کرښه") so that it knows when to look ahead.
        std::unordered_set<std::string> compound_prefixes;

        // Additional single-character punctuation/operators (the ASCII forms are always accepted).
        std::unordered_map<char32_t, TokenKind> punctuation;
        std::unordered_map<TokenKind, std::string> punctuation_spellings; // preferred, for translation

        std::array<char32_t, 10> digits{U'0', U'1', U'2', U'3', U'4', U'5', U'6', U'7', U'8', U'9'}; // preferred
        std::vector<QuotePair> quotes{{U'"', U'"'}}; // string delimiters; the first pair is preferred
        DisplayLocale display;

        // The name of the function that runs after the top-level statements ("main"; English "main"
        // is accepted in every language).
        std::string main_name = "main";
        bool is_main_name(std::string_view name) const { return name == "main" || name == main_name; }

        // Names of keys for key_pressed() and key_down(), English -> this language ("left" -> "کیڼ").
        // Keys without an entry keep their English name.
        std::vector<std::pair<std::string, std::string>> key_names;

        // Messages. Empty entries fall back to English.
        std::unordered_map<DiagId, std::string> messages;
        std::unordered_map<std::string, std::string> terms; // small phrases, e.g. "end_of_file"
        std::array<std::string, 4> phase_names{"lexical", "syntax", "semantic", "runtime"};
        std::string error_word = "error";
        std::string warning_word = "warning";

        void add_keyword(TokenKind kind, std::string spelling); // the first spelling added is preferred
        void add_builtin(BuiltinId id, std::string spelling);
        void add_module(BuiltinModule module, std::string spelling);
        void add_punctuation(TokenKind kind, char32_t character, bool preferred);

        std::optional<TokenKind> keyword(std::string_view spelling) const;
        std::optional<BuiltinId> builtin(std::string_view spelling) const;
        // True if 'words' is the beginning (but not the whole) of a multi-word keyword or built-in.
        bool starts_compound(std::string_view words) const;
        std::optional<TokenKind> extra_punctuation(char32_t character) const;
        std::optional<char32_t> closing_quote(char32_t opening) const;

        std::string spelling(TokenKind kind) const;
        std::string spelling(BuiltinId id) const;
        std::string spelling(BuiltinModule module) const;
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

    // The language named by a directive comment on the first line, e.g. "// lang: ps" or
    // "// ژبه: پښتو". Returns the name as written, or nullopt if there is no directive.
    std::optional<std::string> language_directive(std::string_view source);
}
