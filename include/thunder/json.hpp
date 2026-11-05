// A minimal streaming JSON writer (no external dependencies).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace thunder
{
    class JsonWriter
    {
    public:
        explicit JsonWriter(bool pretty = false);

        JsonWriter& begin_object();
        JsonWriter& end_object();
        JsonWriter& begin_array();
        JsonWriter& end_array();
        JsonWriter& key(std::string_view name);

        JsonWriter& value(std::string_view text);
        JsonWriter& value(const char* text) { return value(std::string_view(text)); }
        JsonWriter& value(const std::string& text) { return value(std::string_view(text)); }
        JsonWriter& value(bool flag);
        JsonWriter& value(std::int64_t number);
        JsonWriter& value(std::uint64_t number);
        JsonWriter& value(int number) { return value(static_cast<std::int64_t>(number)); }
        JsonWriter& value(unsigned number) { return value(static_cast<std::uint64_t>(number)); }
        JsonWriter& value(double number);
        JsonWriter& null();

        // Shorthand for key(name).value(v).
        template <typename T>
        JsonWriter& field(std::string_view name, const T& v)
        {
            key(name);
            return value(v);
        }

        const std::string& str() const { return out_; }

        static std::string escape(std::string_view text);

    private:
        void before_value();
        void newline();

        std::string out_;
        bool pretty_;
        std::vector<bool> first_; // per open container: no element written yet
        bool after_key_ = false;
    };
}
