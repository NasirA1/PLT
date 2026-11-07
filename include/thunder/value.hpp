// Runtime values manipulated by the virtual machine.
#pragma once

#include "thunder/language_pack.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
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

    // Tuples are immutable, so sharing one is indistinguishable from copying it.
    struct TupleObject
    {
        std::vector<Value> elements;
    };

    // Maps (associative arrays) keep their entries in insertion order; 'index' finds an entry by
    // an encoding of its key (int, bool or string).
    struct MapObject
    {
        std::vector<std::pair<Value, Value>> entries;
        std::unordered_map<std::string, std::size_t> index;

        const Value* find(const Value& key) const;
        void set(const Value& key, Value value); // inserts or replaces
        bool erase(const Value& key);
        static std::string encode(const Value& key);
    };

    // A function value: which function of the program it refers to. The name (owned by the
    // program's function table) is only used to display the value.
    struct FunctionRef
    {
        std::uint16_t index = 0;
        const std::string* name = nullptr;
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
            Tuple,
            Map,
            Function,
        };

        Value() = default;
        static Value from_int(std::int64_t value);
        static Value from_bool(bool value);
        static Value from_string(std::string value);
        static Value from_array(std::vector<Value> elements);
        static Value from_struct(const StructLayout* layout, std::vector<Value> fields);
        static Value from_tuple(std::vector<Value> elements);
        static Value from_map(std::vector<std::pair<Value, Value>> entries);
        static Value from_function(std::uint16_t index, const std::string* name);

        Kind kind() const;
        bool is_int() const { return kind() == Kind::Int; }
        bool is_bool() const { return kind() == Kind::Bool; }
        bool is_string() const { return kind() == Kind::String; }
        bool is_array() const { return kind() == Kind::Array; }
        bool is_struct() const { return kind() == Kind::Struct; }
        bool is_tuple() const { return kind() == Kind::Tuple; }
        bool is_map() const { return kind() == Kind::Map; }
        bool is_function() const { return kind() == Kind::Function; }

        std::int64_t as_int() const { return std::get<std::int64_t>(data_); }
        bool as_bool() const { return std::get<bool>(data_); }
        const std::string& as_string() const { return *std::get<std::shared_ptr<const std::string>>(data_); }
        ArrayObject& as_array() const { return *std::get<std::shared_ptr<ArrayObject>>(data_); }
        StructObject& as_struct() const { return *std::get<std::shared_ptr<StructObject>>(data_); }
        const TupleObject& as_tuple() const { return *std::get<std::shared_ptr<const TupleObject>>(data_); }
        MapObject& as_map() const { return *std::get<std::shared_ptr<MapObject>>(data_); }
        const FunctionRef& as_function() const { return std::get<FunctionRef>(data_); }

        // A copy that shares nothing with the original (nested arrays and structs are copied too).
        Value deep_copy() const;

        // Text form used by print/str: strings are written as-is at the top level and quoted
        // inside arrays and structs, e.g. ["a", "b"] or Point(x: 1, y: 2).
        std::string to_display(const DisplayLocale& locale = DisplayLocale{}) const;

        // Text form used by debuggers and traces: strings are always quoted.
        std::string to_debug(const DisplayLocale& locale = DisplayLocale{}) const;

        // Detailed form used by inspect/dump: structs are written field by field with their types,
        // one field per line, indented beneath the struct's name.
        std::string inspect(const DisplayLocale& locale = DisplayLocale{}) const;

        // Value equality (strings and tuples by content; arrays, structs and maps by identity).
        friend bool operator==(const Value& left, const Value& right);

    private:
        using Data = std::variant<std::monostate, std::int64_t, bool, std::shared_ptr<const std::string>,
            std::shared_ptr<ArrayObject>, std::shared_ptr<StructObject>, std::shared_ptr<const TupleObject>,
            std::shared_ptr<MapObject>, FunctionRef>;
        Data data_;
    };

    std::string_view value_kind_name(Value::Kind kind);
}
