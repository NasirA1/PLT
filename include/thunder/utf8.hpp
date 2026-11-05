// UTF-8 helpers.
//
// Thunder source code and strings are UTF-8. Columns and string lengths/indexes are measured in
// Unicode code points (not bytes), so that non-Latin scripts such as Pashto behave intuitively.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace thunder::utf8
{
    inline constexpr char32_t replacement_character = U'\uFFFD';

    struct Decoded
    {
        char32_t code_point = 0;
        std::size_t length = 0; // number of bytes consumed (at least 1 unless at end of input)
    };

    // Decodes the code point starting at byte offset 'pos'. Malformed input yields U+FFFD and
    // consumes a single byte so callers always make progress.
    Decoded decode(std::string_view text, std::size_t pos);

    // Appends the UTF-8 encoding of 'cp' to 'out'.
    void append(std::string& out, char32_t cp);

    // Number of code points in 'text'.
    std::size_t length(std::string_view text);

    // Splits 'text' into one string per code point.
    std::vector<std::string> code_points(std::string_view text);

    // True for code points the lexer treats as layout (Unicode spaces and bidi control marks).
    bool is_space(char32_t cp);
}
