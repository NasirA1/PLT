// A minimal streaming JSON writer and a small JSON reader (no external dependencies).
#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
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
        // Writes an already-formatted number (must be valid JSON, e.g. "10" or "-2.5").
        JsonWriter& raw_number(std::string_view text);
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

    // A parsed JSON document. Objects keep their members by name; numbers are kept as doubles
    // (every integer a client sends fits exactly up to 2^53, far beyond what the tools need).
    class JsonValue
    {
    public:
        using Array = std::vector<JsonValue>;
        using Object = std::map<std::string, JsonValue, std::less<>>;

        JsonValue() = default; // null
        explicit JsonValue(bool flag);
        explicit JsonValue(double number);
        explicit JsonValue(std::string text);
        explicit JsonValue(Array items);
        explicit JsonValue(Object members);

        bool is_null() const { return data_.index() == 0; }
        bool is_bool() const { return data_.index() == 1; }
        bool is_number() const { return data_.index() == 2; }
        bool is_string() const { return data_.index() == 3; }
        bool is_array() const { return data_.index() == 4; }
        bool is_object() const { return data_.index() == 5; }

        bool as_bool() const { return std::get<bool>(data_); }
        double as_number() const { return std::get<double>(data_); }
        const std::string& as_string() const { return std::get<std::string>(data_); }
        const Array& as_array() const { return *std::get<std::shared_ptr<Array>>(data_); }
        const Object& as_object() const { return *std::get<std::shared_ptr<Object>>(data_); }

        // Members of an object, with defaults for anything missing or of another type (so a
        // request may leave optional fields out). A non-object has no members.
        const JsonValue* get(std::string_view name) const;
        std::string string_or(std::string_view name, std::string fallback = {}) const;
        std::int64_t int_or(std::string_view name, std::int64_t fallback = 0) const;
        bool bool_or(std::string_view name, bool fallback = false) const;
        const Array& array_or_empty(std::string_view name) const;

    private:
        std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> data_;
    };

    // Parses exactly one JSON value (RFC 8259); the error names what was found and where.
    std::expected<JsonValue, std::string> parse_json(std::string_view text);
}
