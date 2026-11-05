#include "thunder/utf8.hpp"

namespace thunder::utf8
{
    Decoded decode(std::string_view text, std::size_t pos)
    {
        if (pos >= text.size())
        {
            return {0, 0};
        }

        const auto byte_at = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
        const unsigned char lead = byte_at(pos);

        if (lead < 0x80)
        {
            return {lead, 1};
        }

        std::size_t length = 0;
        char32_t cp = 0;
        char32_t minimum = 0;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            cp = lead & 0x1Fu;
            minimum = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            cp = lead & 0x0Fu;
            minimum = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            cp = lead & 0x07u;
            minimum = 0x10000;
        }
        else
        {
            return {replacement_character, 1};
        }

        if (pos + length > text.size())
        {
            return {replacement_character, 1};
        }

        for (std::size_t i = 1; i < length; ++i)
        {
            const unsigned char next = byte_at(pos + i);
            if ((next & 0xC0) != 0x80)
            {
                return {replacement_character, 1};
            }
            cp = (cp << 6) | (next & 0x3Fu);
        }

        const bool surrogate = cp >= 0xD800 && cp <= 0xDFFF;
        if (cp < minimum || cp > 0x10FFFF || surrogate)
        {
            return {replacement_character, 1};
        }
        return {cp, length};
    }

    void append(std::string& out, char32_t cp)
    {
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            cp = replacement_character;
        }

        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::size_t length(std::string_view text)
    {
        std::size_t count = 0;
        for (std::size_t pos = 0; pos < text.size(); pos += decode(text, pos).length)
        {
            ++count;
        }
        return count;
    }

    std::vector<std::string> code_points(std::string_view text)
    {
        std::vector<std::string> result;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            const std::size_t len = decode(text, pos).length;
            result.emplace_back(text.substr(pos, len));
            pos += len;
        }
        return result;
    }

    bool is_space(char32_t cp)
    {
        switch (cp)
        {
        case U' ':
        case U'\t':
        case U'\n':
        case U'\r':
        case U'\f':
        case U'\v':
        case U'\u00A0': // no-break space
        case U'\u200E': // left-to-right mark
        case U'\u200F': // right-to-left mark (common in right-to-left source text)
        case U'\u061C': // Arabic letter mark
        case U'\u2028': // line separator
        case U'\u2029': // paragraph separator
        case U'\u202F': // narrow no-break space
        case U'\u3000': // ideographic space
        case U'\uFEFF': // byte order mark / zero-width no-break space
            return true;
        default:
            return cp >= U'\u2000' && cp <= U'\u200B'; // en quad ... zero-width space
        }
    }
}
