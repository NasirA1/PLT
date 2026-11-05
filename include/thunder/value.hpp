// Runtime values manipulated by the virtual machine.
#pragma once

#include "thunder/language_pack.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace thunder
{
    class Value;

    // The runtime description of a struct type (from the program's struct table).
    struct StructLayout
    {
        std::string name;
        std::vector<std::string> field_names;
        std::vector<std::string> field_types; // as written in the source language, e.g. "int[]"

        friend bool operator==(const StructLayout&, const StructLayout&) = default;
    };

    // Arrays and structs are reference types: assigning one or passing it to a function shares
    // it. Reference counting reclaims them; the type system makes reference cycles impossible
    // (a struct may not contain itself).
    struct ArrayObject
    {
        std::vector<Value> elements;
    };

    struct StructObject
    {
        const StructLayout* layout = nullptr;
        std::vector<Value> fields;
    };

    class Value
    {
    public:
        enum class Kind : std::uint8_t
        {
            Void,
            Int,
            Bool,
            String,
            Array,
            Struct,
        };

        Value() = default;
        static Value from_int(std::int64_t value);
        static Value from_bool(bool value);
        static Value from_string(std::string value);
        static Value from_array(std::vector<Value> elements);
        static Value from_struct(const StructLayout* layout, std::vector<Value> fields);

        Kind kind() const;
        bool is_int() const { return kind() == Kind::Int; }
        bool is_bool() const { return kind() == Kind::Bool; }
        bool is_string() const { return kind() == Kind::String; }
        bool is_array() const { return kind() == Kind::Array; }
        bool is_struct() const { return kind() == Kind::Struct; }

        std::int64_t as_int() const { return std::get<std::int64_t>(data_); }
        bool as_bool() const { return std::get<bool>(data_); }
        const std::string& as_string() const { return *std::get<std::shared_ptr<const std::string>>(data_); }
        ArrayObject& as_array() const { return *std::get<std::shared_ptr<ArrayObject>>(data_); }
        StructObject& as_struct() const { return *std::get<std::shared_ptr<StructObject>>(data_); }

        // A copy that shares nothing with the original (nested arrays and structs are copied too).
        Value deep_copy() const;

        // Text form used by print/str: strings are written as-is at the top level and quoted
        // inside arrays and structs, e.g. ["a", "b"] or Point(x: 1, y: 2).
        std::string to_display(const DisplayLocale& locale = DisplayLocale{}) const;

        // Text form used by debuggers and traces: strings are always quoted.
        std::string to_debug(const DisplayLocale& locale = DisplayLocale{}) const;

        // Detailed form used by inspect/dump: structs are written field by field with their types,
        // one field per line, in Allman style.
        std::string inspect(const DisplayLocale& locale = DisplayLocale{}) const;

        // Value equality (strings by content; arrays and structs by identity).
        friend bool operator==(const Value& left, const Value& right);

    private:
        using Data = std::variant<std::monostate, std::int64_t, bool, std::shared_ptr<const std::string>,
            std::shared_ptr<ArrayObject>, std::shared_ptr<StructObject>>;
        Data data_;
    };

    std::string_view value_kind_name(Value::Kind kind);
}
