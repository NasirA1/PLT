#include "thunder/language_pack.hpp"

#include "thunder/utf8.hpp"

#include <algorithm>
#include <cstddef>
#include <format>

namespace thunder
{
    // ------------------------------------------------------------------ display locale

    std::string DisplayLocale::localize_digits(std::string_view text) const
    {
        std::string out;
        out.reserve(text.size());
        for (const char c : text)
        {
            if (c >= '0' && c <= '9')
            {
                out += digits[static_cast<std::size_t>(c - '0')];
            }
            else
            {
                out += c;
            }
        }
        return out;
    }

    std::string DisplayLocale::format_int(std::int64_t value) const
    {
        return localize_digits(std::to_string(value));
    }

    // ------------------------------------------------------------------ building packs

    namespace
    {
        // Registers every proper prefix of a multi-word spelling ("a b c" -> "a", "a b").
        void add_compound_prefixes(std::unordered_set<std::string>& prefixes, std::string_view spelling)
        {
            for (std::size_t space = spelling.find(' '); space != std::string_view::npos;
                 space = spelling.find(' ', space + 1))
            {
                prefixes.emplace(spelling.substr(0, space));
            }
        }
    }

    void LanguagePack::add_keyword(TokenKind kind, std::string spelling)
    {
        add_compound_prefixes(compound_prefixes, spelling);
        keyword_spellings.try_emplace(kind, spelling);
        keywords.insert_or_assign(std::move(spelling), kind);
    }

    void LanguagePack::add_builtin(BuiltinId id, std::string spelling)
    {
        add_compound_prefixes(compound_prefixes, spelling);
        builtin_spellings.try_emplace(id, spelling);
        builtins.insert_or_assign(std::move(spelling), id);
    }

    void LanguagePack::add_module(BuiltinModule module, std::string spelling)
    {
        module_spellings.try_emplace(module, spelling);
        modules.insert_or_assign(std::move(spelling), module);
    }

    void LanguagePack::add_punctuation(TokenKind kind, char32_t character, bool preferred)
    {
        punctuation.insert_or_assign(character, kind);
        if (preferred)
        {
            std::string text;
            utf8::append(text, character);
            punctuation_spellings.insert_or_assign(kind, text);
        }
    }

    // ------------------------------------------------------------------ lookups

    std::string fold_letters(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        std::size_t pos = 0;
        while (pos < text.size())
        {
            const utf8::Decoded decoded = utf8::decode(text, pos);
            pos += decoded.length;
            char32_t cp = decoded.code_point;
            if (cp == 0x0643) // ك Arabic kaf -> ک
            {
                cp = 0x06A9;
            }
            else if (cp == 0x06AF) // گ Persian gaf -> ګ
            {
                cp = 0x06AB;
            }
            utf8::append(out, cp);
        }
        return out;
    }

    std::optional<TokenKind> LanguagePack::keyword(std::string_view spelling) const
    {
        auto it = keywords.find(std::string(spelling));
        if (it == keywords.end())
        {
            it = keywords.find(fold_letters(spelling));
        }
        return it == keywords.end() ? std::nullopt : std::optional(it->second);
    }

    std::optional<BuiltinId> LanguagePack::builtin(std::string_view spelling) const
    {
        auto it = builtins.find(std::string(spelling));
        if (it == builtins.end())
        {
            it = builtins.find(fold_letters(spelling));
        }
        return it == builtins.end() ? std::nullopt : std::optional(it->second);
    }

    bool LanguagePack::starts_compound(std::string_view words) const
    {
        return !compound_prefixes.empty()
            && (compound_prefixes.contains(std::string(words)) || compound_prefixes.contains(fold_letters(words)));
    }

    std::optional<TokenKind> LanguagePack::extra_punctuation(char32_t character) const
    {
        const auto it = punctuation.find(character);
        return it == punctuation.end() ? std::nullopt : std::optional(it->second);
    }

    std::optional<char32_t> LanguagePack::closing_quote(char32_t opening) const
    {
        for (const QuotePair& pair : quotes)
        {
            if (pair.open == opening)
            {
                return pair.close;
            }
        }
        return std::nullopt;
    }

    std::string LanguagePack::spelling(TokenKind kind) const
    {
        if (const auto it = keyword_spellings.find(kind); it != keyword_spellings.end())
        {
            return it->second;
        }
        if (const auto it = punctuation_spellings.find(kind); it != punctuation_spellings.end())
        {
            return it->second;
        }
        return std::string(token_canonical_spelling(kind));
    }

    std::string LanguagePack::spelling(BuiltinId id) const
    {
        const auto it = builtin_spellings.find(id);
        return it != builtin_spellings.end() ? it->second : std::string(builtin_info(id).canonical_name);
    }

    std::string LanguagePack::spelling(BuiltinModule module) const
    {
        const auto it = module_spellings.find(module);
        return it != module_spellings.end() ? it->second : std::string(module_canonical_name(module));
    }

    std::string LanguagePack::key_name(std::string_view english) const
    {
        for (const auto& [key, name] : key_names)
        {
            if (key == english)
            {
                return name;
            }
        }
        return std::string(english);
    }

    std::string LanguagePack::english_key(std::string_view name) const
    {
        const std::string folded = fold_letters(name);
        for (const auto& [key, native] : key_names)
        {
            if (native == folded)
            {
                return key;
            }
        }
        return std::string(name);
    }

    std::string_view LanguagePack::message_template(DiagId id) const
    {
        const auto it = messages.find(id);
        return it != messages.end() && !it->second.empty() ? std::string_view(it->second)
                                                            : std::string_view(info(id).message_template);
    }

    std::string LanguagePack::term(std::string_view key) const
    {
        if (const auto it = terms.find(std::string(key)); it != terms.end())
        {
            return it->second;
        }
        return std::string(key); // English packs use the key itself, e.g. "end of file"
    }

    std::string_view LanguagePack::phase_name(Phase phase) const
    {
        return phase_names.at(static_cast<std::size_t>(phase));
    }

    std::string_view LanguagePack::severity_name(Severity severity) const
    {
        return severity == Severity::Error ? error_word : warning_word;
    }

    namespace
    {
        bool is_plain_integer(std::string_view text)
        {
            if (!text.empty() && text.front() == '-')
            {
                text.remove_prefix(1);
            }
            return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
        }
    }

    std::string LanguagePack::format(DiagId id, const std::vector<std::string>& arguments) const
    {
        std::array<std::string, 6> args;
        for (std::size_t i = 0; i < arguments.size() && i < args.size(); ++i)
        {
            args[i] = is_plain_integer(arguments[i]) ? display.localize_digits(arguments[i]) : arguments[i];
        }
        try
        {
            return std::vformat(message_template(id), std::make_format_args(args[0], args[1], args[2], args[3], args[4],
                                                          args[5]));
        }
        catch (const std::format_error&)
        {
            // A broken translation must never hide the problem being reported: use English.
            return std::vformat(info(id).message_template,
                std::make_format_args(args[0], args[1], args[2], args[3], args[4], args[5]));
        }
    }

    // ------------------------------------------------------------------ packs

    namespace
    {
        LanguagePack make_english()
        {
            LanguagePack pack;
            pack.code = "en";
            pack.display_name = "English";
            pack.native_name = "English";
            for (std::size_t i = 0; i < token_kind_count; ++i)
            {
                const auto kind = static_cast<TokenKind>(i);
                if (is_word_token(kind))
                {
                    pack.add_keyword(kind, std::string(token_canonical_spelling(kind)));
                }
            }
            for (const BuiltinInfo& builtin : all_builtins())
            {
                pack.add_builtin(builtin.id, std::string(builtin.canonical_name));
            }
            pack.add_module(BuiltinModule::Os, "os");
            pack.add_module(BuiltinModule::Graphics, "graphics");
            return pack;
        }
    }

    LanguagePack make_pashto(); // language_pashto.cpp

    const LanguagePack& english_pack()
    {
        static const LanguagePack pack = make_english();
        return pack;
    }

    const LanguagePack& pashto_pack()
    {
        static const LanguagePack pack = make_pashto();
        return pack;
    }

    const LanguagePack* find_language_pack(std::string_view name)
    {
        std::string lower(name);
        std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
        for (const LanguagePack* pack : available_language_packs())
        {
            std::string display(pack->display_name);
            std::ranges::transform(display, display.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
            if (lower == pack->code || lower == display || name == pack->native_name)
            {
                return pack;
            }
        }
        return nullptr;
    }

    std::vector<const LanguagePack*> available_language_packs()
    {
        return {&english_pack(), &pashto_pack()};
    }

    std::optional<BuiltinModule> find_builtin_module(std::string_view name)
    {
        const std::string folded = fold_letters(name);
        for (const LanguagePack* pack : available_language_packs())
        {
            if (const auto it = pack->modules.find(folded); it != pack->modules.end())
            {
                return it->second;
            }
        }
        return std::nullopt;
    }

    std::optional<int> digit_value(char32_t character)
    {
        if (character >= U'0' && character <= U'9')
        {
            return static_cast<int>(character - U'0');
        }
        if (character >= 0x0660 && character <= 0x0669) // Arabic-Indic digits
        {
            return static_cast<int>(character - 0x0660);
        }
        if (character >= 0x06F0 && character <= 0x06F9) // Extended Arabic-Indic digits (Pashto, Persian, Urdu)
        {
            return static_cast<int>(character - 0x06F0);
        }
        return std::nullopt;
    }

    std::optional<std::string> language_directive(std::string_view source)
    {
        // Skip a byte order mark and leading spaces, then expect "//".
        if (source.starts_with("\xEF\xBB\xBF"))
        {
            source.remove_prefix(3);
        }
        const std::size_t end_of_line = source.find('\n');
        std::string_view line = source.substr(0, end_of_line);
        const auto trim = [](std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
            {
                text.remove_suffix(1);
            }
            return text;
        };
        line = trim(line);
        if (!line.starts_with("//"))
        {
            return std::nullopt;
        }
        line = trim(line.substr(2));
        for (const std::string_view key : {std::string_view("lang:"), std::string_view("language:"),
                 std::string_view("ژبه:")})
        {
            if (line.starts_with(key))
            {
                const std::string_view value = trim(line.substr(key.size()));
                if (!value.empty())
                {
                    return std::string(value);
                }
            }
        }
        return std::nullopt;
    }
}
