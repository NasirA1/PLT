#include "thunder/json.hpp"

#include <cmath>
#include <format>

namespace thunder
{
    JsonWriter::JsonWriter(bool pretty)
        : pretty_(pretty)
    {
    }

    std::string JsonWriter::escape(std::string_view text)
    {
        std::string out;
        out.reserve(text.size() + 2);
        for (const char c : text)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                }
                else
                {
                    out += c; // UTF-8 passes through unchanged
                }
                break;
            }
        }
        return out;
    }

    void JsonWriter::newline()
    {
        if (pretty_)
        {
            out_ += '\n';
            out_.append(first_.size() * 2, ' ');
        }
    }

    void JsonWriter::before_value()
    {
        if (after_key_)
        {
            after_key_ = false;
            return;
        }
        if (!first_.empty())
        {
            if (!first_.back())
            {
                out_ += ',';
            }
            first_.back() = false;
            newline();
        }
    }

    JsonWriter& JsonWriter::begin_object()
    {
        before_value();
        out_ += '{';
        first_.push_back(true);
        return *this;
    }

    JsonWriter& JsonWriter::end_object()
    {
        const bool empty = first_.back();
        first_.pop_back();
        if (!empty)
        {
            newline();
        }
        out_ += '}';
        return *this;
    }

    JsonWriter& JsonWriter::begin_array()
    {
        before_value();
        out_ += '[';
        first_.push_back(true);
        return *this;
    }

    JsonWriter& JsonWriter::end_array()
    {
        const bool empty = first_.back();
        first_.pop_back();
        if (!empty)
        {
            newline();
        }
        out_ += ']';
        return *this;
    }

    JsonWriter& JsonWriter::key(std::string_view name)
    {
        before_value();
        out_ += '"';
        out_ += escape(name);
        out_ += pretty_ ? "\": " : "\":";
        after_key_ = true;
        return *this;
    }

    JsonWriter& JsonWriter::value(std::string_view text)
    {
        before_value();
        out_ += '"';
        out_ += escape(text);
        out_ += '"';
        return *this;
    }

    JsonWriter& JsonWriter::value(bool flag)
    {
        before_value();
        out_ += flag ? "true" : "false";
        return *this;
    }

    JsonWriter& JsonWriter::value(std::int64_t number)
    {
        before_value();
        out_ += std::to_string(number);
        return *this;
    }

    JsonWriter& JsonWriter::value(std::uint64_t number)
    {
        before_value();
        out_ += std::to_string(number);
        return *this;
    }

    JsonWriter& JsonWriter::value(double number)
    {
        before_value();
        out_ += std::isfinite(number) ? std::format("{}", number) : "null";
        return *this;
    }

    JsonWriter& JsonWriter::null()
    {
        before_value();
        out_ += "null";
        return *this;
    }
}
