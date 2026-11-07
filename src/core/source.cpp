#include "thunder/source.hpp"

#include <utility>

namespace thunder
{
    Span Span::merge(const Span& first, const Span& last)
    {
        Span result;
        result.offset = first.offset;
        const std::uint32_t last_end = last.offset + last.length;
        result.length = last_end > first.offset ? last_end - first.offset : first.length;
        result.start = first.start;
        result.end = last_end > first.offset ? last.end : first.end;
        result.file = first.file;
        return result;
    }

    SourceFile::SourceFile(std::string name, std::string text, std::uint16_t id)
        : name_(std::move(name))
        , text_(std::move(text))
        , id_(id)
    {
        line_starts_.push_back(0);
        for (std::size_t i = 0; i < text_.size(); ++i)
        {
            if (text_[i] == '\n')
            {
                line_starts_.push_back(i + 1);
            }
        }
    }

    std::string_view SourceFile::line_text(std::uint32_t line) const
    {
        if (line == 0 || line > line_starts_.size())
        {
            return {};
        }
        const std::size_t begin = line_starts_[line - 1];
        std::size_t end = line < line_starts_.size() ? line_starts_[line] - 1 : text_.size();
        if (end > begin && text_[end - 1] == '\r')
        {
            --end;
        }
        return std::string_view(text_).substr(begin, end - begin);
    }
}
