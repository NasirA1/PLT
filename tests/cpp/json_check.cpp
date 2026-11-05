#include "json_check.hpp"

#include <cctype>
#include <cstddef>

namespace test
{
    namespace
    {
        class Checker
        {
        public:
            explicit Checker(std::string_view text)
                : text_(text)
            {
            }

            bool document()
            {
                skip();
                if (!value())
                {
                    return false;
                }
                skip();
                return pos_ == text_.size();
            }

        private:
            void skip()
            {
                while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t'))
                {
                    ++pos_;
                }
            }

            bool literal(std::string_view word)
            {
                if (text_.substr(pos_, word.size()) == word)
                {
                    pos_ += word.size();
                    return true;
                }
                return false;
            }

            bool value()
            {
                if (pos_ >= text_.size())
                {
                    return false;
                }
                switch (text_[pos_])
                {
                case '{':
                    return object();
                case '[':
                    return array();
                case '"':
                    return string();
                case 't':
                    return literal("true");
                case 'f':
                    return literal("false");
                case 'n':
                    return literal("null");
                default:
                    return number();
                }
            }

            bool object()
            {
                ++pos_;
                skip();
                if (pos_ < text_.size() && text_[pos_] == '}')
                {
                    ++pos_;
                    return true;
                }
                while (true)
                {
                    skip();
                    if (pos_ >= text_.size() || text_[pos_] != '"' || !string())
                    {
                        return false;
                    }
                    skip();
                    if (pos_ >= text_.size() || text_[pos_++] != ':')
                    {
                        return false;
                    }
                    skip();
                    if (!value())
                    {
                        return false;
                    }
                    skip();
                    if (pos_ >= text_.size())
                    {
                        return false;
                    }
                    const char c = text_[pos_++];
                    if (c == '}')
                    {
                        return true;
                    }
                    if (c != ',')
                    {
                        return false;
                    }
                }
            }

            bool array()
            {
                ++pos_;
                skip();
                if (pos_ < text_.size() && text_[pos_] == ']')
                {
                    ++pos_;
                    return true;
                }
                while (true)
                {
                    skip();
                    if (!value())
                    {
                        return false;
                    }
                    skip();
                    if (pos_ >= text_.size())
                    {
                        return false;
                    }
                    const char c = text_[pos_++];
                    if (c == ']')
                    {
                        return true;
                    }
                    if (c != ',')
                    {
                        return false;
                    }
                }
            }

            bool string()
            {
                ++pos_;
                while (pos_ < text_.size())
                {
                    const auto c = static_cast<unsigned char>(text_[pos_++]);
                    if (c == '"')
                    {
                        return true;
                    }
                    if (c < 0x20)
                    {
                        return false;
                    }
                    if (c == '\\')
                    {
                        if (pos_ >= text_.size())
                        {
                            return false;
                        }
                        const char e = text_[pos_++];
                        if (e == 'u')
                        {
                            for (int i = 0; i < 4; ++i)
                            {
                                if (pos_ >= text_.size() || !std::isxdigit(static_cast<unsigned char>(text_[pos_++])))
                                {
                                    return false;
                                }
                            }
                        }
                        else if (std::string_view("\"\\/bfnrt").find(e) == std::string_view::npos)
                        {
                            return false;
                        }
                    }
                }
                return false;
            }

            bool number()
            {
                const std::size_t start = pos_;
                if (pos_ < text_.size() && text_[pos_] == '-')
                {
                    ++pos_;
                }
                const std::size_t digits = pos_;
                while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                {
                    ++pos_;
                }
                if (pos_ == digits)
                {
                    return false;
                }
                if (pos_ < text_.size() && text_[pos_] == '.')
                {
                    ++pos_;
                    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                    {
                        ++pos_;
                    }
                }
                return pos_ > start;
            }

            std::string_view text_;
            std::size_t pos_ = 0;
        };
    }

    bool is_valid_json(std::string_view text)
    {
        return Checker(text).document();
    }
}
