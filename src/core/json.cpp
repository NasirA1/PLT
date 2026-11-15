#include "thunder/json.hpp"

#include <charconv>
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

    JsonWriter& JsonWriter::raw_number(std::string_view text)
    {
        before_value();
        out_ += text.empty() ? "0" : text;
        return *this;
    }

    JsonWriter& JsonWriter::null()
    {
        before_value();
        out_ += "null";
        return *this;
    }
}

// ---------------------------------------------------------------------- reading

namespace thunder
{
    JsonValue::JsonValue(bool flag)
        : data_(flag)
    {
    }

    JsonValue::JsonValue(double number)
        : data_(number)
    {
    }

    JsonValue::JsonValue(std::string text)
        : data_(std::move(text))
    {
    }

    JsonValue::JsonValue(Array items)
        : data_(std::make_shared<Array>(std::move(items)))
    {
    }

    JsonValue::JsonValue(Object members)
        : data_(std::make_shared<Object>(std::move(members)))
    {
    }

    const JsonValue* JsonValue::get(std::string_view name) const
    {
        if (!is_object())
        {
            return nullptr;
        }
        const Object& members = as_object();
        const auto found = members.find(name);
        return found == members.end() ? nullptr : &found->second;
    }

    std::string JsonValue::string_or(std::string_view name, std::string fallback) const
    {
        const JsonValue* member = get(name);
        if (member != nullptr && member->is_string())
        {
            return member->as_string();
        }
        return fallback;
    }

    std::int64_t JsonValue::int_or(std::string_view name, std::int64_t fallback) const
    {
        const JsonValue* member = get(name);
        return member != nullptr && member->is_number() ? static_cast<std::int64_t>(member->as_number()) : fallback;
    }

    bool JsonValue::bool_or(std::string_view name, bool fallback) const
    {
        const JsonValue* member = get(name);
        return member != nullptr && member->is_bool() ? member->as_bool() : fallback;
    }

    const JsonValue::Array& JsonValue::array_or_empty(std::string_view name) const
    {
        static const Array none;
        const JsonValue* member = get(name);
        return member != nullptr && member->is_array() ? member->as_array() : none;
    }

    namespace
    {
        class JsonReader
        {
        public:
            explicit JsonReader(std::string_view text)
                : text_(text)
            {
            }

            std::expected<JsonValue, std::string> document()
            {
                auto value = parse_value();
                if (!value)
                {
                    return value;
                }
                skip_spaces();
                if (position_ != text_.size())
                {
                    return failure("unexpected text after the value");
                }
                return value;
            }

        private:
            std::unexpected<std::string> failure(std::string_view what) const
            {
                return std::unexpected(std::format("invalid JSON at offset {}: {}", position_, what));
            }

            void skip_spaces()
            {
                while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t'
                           || text_[position_] == '\n' || text_[position_] == '\r'))
                {
                    ++position_;
                }
            }

            bool consume(char expected)
            {
                skip_spaces();
                if (position_ < text_.size() && text_[position_] == expected)
                {
                    ++position_;
                    return true;
                }
                return false;
            }

            bool consume_word(std::string_view word)
            {
                if (text_.substr(position_, word.size()) == word)
                {
                    position_ += word.size();
                    return true;
                }
                return false;
            }

            std::expected<JsonValue, std::string> parse_value()
            {
                skip_spaces();
                if (position_ >= text_.size())
                {
                    return failure("a value was expected");
                }
                const char c = text_[position_];
                if (c == '{')
                {
                    return parse_object();
                }
                if (c == '[')
                {
                    return parse_array();
                }
                if (c == '"')
                {
                    auto text = parse_string();
                    if (!text)
                    {
                        return std::unexpected(text.error());
                    }
                    return JsonValue(std::move(*text));
                }
                if (consume_word("true"))
                {
                    return JsonValue(true);
                }
                if (consume_word("false"))
                {
                    return JsonValue(false);
                }
                if (consume_word("null"))
                {
                    return JsonValue();
                }
                return parse_number();
            }

            std::expected<JsonValue, std::string> parse_object()
            {
                ++position_; // '{'
                JsonValue::Object members;
                if (consume('}'))
                {
                    return JsonValue(std::move(members));
                }
                while (true)
                {
                    skip_spaces();
                    if (position_ >= text_.size() || text_[position_] != '"')
                    {
                        return failure("a member name was expected");
                    }
                    auto name = parse_string();
                    if (!name)
                    {
                        return std::unexpected(name.error());
                    }
                    if (!consume(':'))
                    {
                        return failure("':' was expected");
                    }
                    auto value = parse_value();
                    if (!value)
                    {
                        return value;
                    }
                    members[std::move(*name)] = std::move(*value);
                    if (consume('}'))
                    {
                        return JsonValue(std::move(members));
                    }
                    if (!consume(','))
                    {
                        return failure("',' or '}' was expected");
                    }
                }
            }

            std::expected<JsonValue, std::string> parse_array()
            {
                ++position_; // '['
                JsonValue::Array items;
                if (consume(']'))
                {
                    return JsonValue(std::move(items));
                }
                while (true)
                {
                    auto value = parse_value();
                    if (!value)
                    {
                        return value;
                    }
                    items.push_back(std::move(*value));
                    if (consume(']'))
                    {
                        return JsonValue(std::move(items));
                    }
                    if (!consume(','))
                    {
                        return failure("',' or ']' was expected");
                    }
                }
            }

            std::expected<std::string, std::string> parse_string()
            {
                ++position_; // the opening quote
                std::string out;
                while (position_ < text_.size())
                {
                    const char c = text_[position_++];
                    if (c == '"')
                    {
                        return out;
                    }
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        return std::unexpected(failure("a control character inside a string").error());
                    }
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (position_ >= text_.size())
                    {
                        break;
                    }
                    const char escaped = text_[position_++];
                    switch (escaped)
                    {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        auto code = parse_code_unit();
                        if (!code)
                        {
                            return std::unexpected(code.error());
                        }
                        std::uint32_t code_point = *code;
                        if (code_point >= 0xD800 && code_point <= 0xDBFF && consume_word("\\u"))
                        {
                            auto low = parse_code_unit();
                            if (!low)
                            {
                                return std::unexpected(low.error());
                            }
                            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (*low - 0xDC00);
                        }
                        append_utf8(out, code_point);
                        break;
                    }
                    default:
                        return std::unexpected(failure("an unknown escape sequence").error());
                    }
                }
                return std::unexpected(failure("an unterminated string").error());
            }

            std::expected<std::uint32_t, std::string> parse_code_unit()
            {
                if (position_ + 4 > text_.size())
                {
                    return std::unexpected(failure("a short \\u escape").error());
                }
                std::uint32_t code = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = text_[position_++];
                    code <<= 4;
                    if (c >= '0' && c <= '9')
                    {
                        code |= static_cast<std::uint32_t>(c - '0');
                    }
                    else if (c >= 'a' && c <= 'f')
                    {
                        code |= static_cast<std::uint32_t>(c - 'a' + 10);
                    }
                    else if (c >= 'A' && c <= 'F')
                    {
                        code |= static_cast<std::uint32_t>(c - 'A' + 10);
                    }
                    else
                    {
                        return std::unexpected(failure("a bad \\u escape").error());
                    }
                }
                return code;
            }

            static void append_utf8(std::string& out, std::uint32_t code_point)
            {
                if (code_point < 0x80)
                {
                    out += static_cast<char>(code_point);
                }
                else if (code_point < 0x800)
                {
                    out += static_cast<char>(0xC0 | (code_point >> 6));
                    out += static_cast<char>(0x80 | (code_point & 0x3F));
                }
                else if (code_point < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (code_point >> 12));
                    out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code_point & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (code_point >> 18));
                    out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code_point & 0x3F));
                }
            }

            std::expected<JsonValue, std::string> parse_number()
            {
                const std::size_t start = position_;
                if (position_ < text_.size() && text_[position_] == '-')
                {
                    ++position_;
                }
                const auto digits = [this]
                {
                    const std::size_t from = position_;
                    while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9')
                    {
                        ++position_;
                    }
                    return position_ > from;
                };
                if (!digits())
                {
                    return failure("a value was expected");
                }
                if (position_ < text_.size() && text_[position_] == '.')
                {
                    ++position_;
                    if (!digits())
                    {
                        return failure("digits were expected after '.'");
                    }
                }
                if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E'))
                {
                    ++position_;
                    if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-'))
                    {
                        ++position_;
                    }
                    if (!digits())
                    {
                        return failure("digits were expected in the exponent");
                    }
                }
                const std::string_view literal = text_.substr(start, position_ - start);
                double number = 0;
                const auto [end, error] = std::from_chars(literal.data(), literal.data() + literal.size(), number);
                if (error != std::errc{} || end != literal.data() + literal.size())
                {
                    return failure("a number that cannot be read");
                }
                return JsonValue(number);
            }

            std::string_view text_;
            std::size_t position_ = 0;
        };
    }

    std::expected<JsonValue, std::string> parse_json(std::string_view text)
    {
        return JsonReader(text).document();
    }
}
