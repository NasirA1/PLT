// Source files and positions within them.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace thunder
{
    // A 1-based line/column position. Columns count Unicode code points.
    struct Position
    {
        std::uint32_t line = 1;
        std::uint32_t column = 1;

        friend bool operator==(const Position&, const Position&) = default;
    };

    // A region of source text: a byte range plus the human-readable start/end positions.
    // 'end' is the position just past the last character.
    struct Span
    {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
        Position start;
        Position end;

        // The smallest span covering both 'first' and 'last' ('first' must come first).
        static Span merge(const Span& first, const Span& last);

        friend bool operator==(const Span&, const Span&) = default;
    };

    class SourceFile
    {
    public:
        SourceFile(std::string name, std::string text);

        const std::string& name() const { return name_; }
        const std::string& text() const { return text_; }

        std::size_t line_count() const { return line_starts_.size(); }

        // Text of a 1-based line without its line terminator (empty if out of range).
        std::string_view line_text(std::uint32_t line) const;

    private:
        std::string name_;
        std::string text_;
        std::vector<std::size_t> line_starts_;
    };
}
