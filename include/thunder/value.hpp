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
        // The struct's methods: member name -> index into the program's function table. The
        // virtual machine looks a method up here when a call goes through an interface
        // (CALL_METHOD does not know the struct until the program runs).
        std::vector<std::pair<std::string, std::uint16_t>> methods;

        // The function a CALL_METHOD of 'method' runs, or -1 if the struct has no such method.
        int method_function(std::string_view method) const
        {
            for (const auto& [member, index] : methods)
            {
                if (member == method)
                {
                    return static_cast<int>(index);
                }
            }
            return -1;
        }

        friend bool operator==(const StructLayout&, const StructLayout&) = default;
    };

    // The runtime description of an enum type (from the program's enum table).
    struct EnumLayout
    {
        std::string name;
        std::vector<std::string> member_names;
        // For a built-in enum (Color, Platform): its id in BuiltinEnumId, else -1. The virtual
        // machine uses it to give a Color its colour whatever language the names are in.
        std::int32_t builtin = -1;

        friend bool operator==(const EnumLayout&, const EnumLayout&) = default;
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

    // A captured variable's heap box: the closure and the function that made it share one, so
    // writes on either side are seen by both. (Defined after Value: it holds one directly.)
    struct BoxObject;

    // A function value: which function of the program it refers to. The name (owned by the
    // program's function table) is only used to display the value. A closure also carries the
    // boxes of the variables it captured.
    struct FunctionRef
    {
        std::uint16_t index = 0;
        const std::string* name = nullptr;
        std::shared_ptr<std::vector<Value>> captures; // null for a plain function

        friend bool operator==(const FunctionRef& left, const FunctionRef& right)
        {
            return left.index == right.index && left.captures == right.captures;
        }
    };

    // An enum value: which enum, and which of its members (by position).
    struct EnumRef
    {
        const EnumLayout* layout = nullptr;
        std::uint16_t ordinal = 0;
    };

    class Value
    {
    public:
        // In the order of the variant below (kind() is the variant's index): new kinds go at the end.
        enum class Kind : std::uint8_t
        {
            Void,
            Int,
            Float,
            Bool,
            String,
            Array,
            Struct,
            Tuple,
            Map,
            Function,
            Byte,
            Enum,
            Box, // a captured variable's heap box (never seen by programs: loads and stores go through it)
        };

        Value() = default;
        static Value from_int(std::int64_t value);
        static Value from_float(double value);
        static Value from_bool(bool value);
        static Value from_string(std::string value);
        static Value from_array(std::vector<Value> elements);
        static Value from_struct(const StructLayout* layout, std::vector<Value> fields);
        // "nothing": the empty struct reference (the default of a recursive field).
        static Value nothing();
        static Value from_tuple(std::vector<Value> elements);
        static Value from_map(std::vector<std::pair<Value, Value>> entries);
        static Value from_function(std::uint16_t index, const std::string* name);
        static Value from_closure(std::uint16_t index, const std::string* name,
            std::shared_ptr<std::vector<Value>> captures);
        static Value from_byte(std::uint8_t value);
        static Value from_enum(const EnumLayout* layout, std::uint16_t ordinal);
        static Value from_box(Value content);

        Kind kind() const;
        bool is_int() const { return kind() == Kind::Int; }
        bool is_float() const { return kind() == Kind::Float; }
        bool is_bool() const { return kind() == Kind::Bool; }
        bool is_string() const { return kind() == Kind::String; }
        bool is_array() const { return kind() == Kind::Array; }
        bool is_struct() const { return kind() == Kind::Struct; }
        bool is_nothing() const
        {
            return is_struct() && std::get<std::shared_ptr<StructObject>>(data_) == nullptr;
        }
        bool is_tuple() const { return kind() == Kind::Tuple; }
        bool is_map() const { return kind() == Kind::Map; }
        bool is_function() const { return kind() == Kind::Function; }
        bool is_byte() const { return kind() == Kind::Byte; }
        bool is_enum() const { return kind() == Kind::Enum; }
        bool is_box() const { return kind() == Kind::Box; }

        std::int64_t as_int() const { return std::get<std::int64_t>(data_); }
        double as_float() const { return std::get<double>(data_); }
        // The numeric value of an int or a float, as a double (used by the maths built-ins).
        double as_number() const { return is_float() ? as_float() : static_cast<double>(as_integer()); }
        bool as_bool() const { return std::get<bool>(data_); }
        const std::string& as_string() const { return *std::get<std::shared_ptr<const std::string>>(data_); }
        ArrayObject& as_array() const { return *std::get<std::shared_ptr<ArrayObject>>(data_); }
        StructObject& as_struct() const { return *std::get<std::shared_ptr<StructObject>>(data_); }
        // The shared reference itself (null for "nothing"); the VM registers it to break cycles.
        const std::shared_ptr<StructObject>& struct_ref() const
        {
            return std::get<std::shared_ptr<StructObject>>(data_);
        }
        const TupleObject& as_tuple() const { return *std::get<std::shared_ptr<const TupleObject>>(data_); }
        MapObject& as_map() const { return *std::get<std::shared_ptr<MapObject>>(data_); }
        const FunctionRef& as_function() const { return std::get<FunctionRef>(data_); }
        std::uint8_t as_byte() const { return std::get<std::uint8_t>(data_); }
        const EnumRef& as_enum() const { return std::get<EnumRef>(data_); }
        BoxObject& as_box() const { return *std::get<std::shared_ptr<BoxObject>>(data_); }
        // The whole-number value of an int or a byte (bytes widen to int wherever an int is wanted).
        std::int64_t as_integer() const { return is_byte() ? static_cast<std::int64_t>(as_byte()) : as_int(); }
        bool is_integer() const { return is_int() || is_byte(); }

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
        using Data = std::variant<std::monostate, std::int64_t, double, bool, std::shared_ptr<const std::string>,
            std::shared_ptr<ArrayObject>, std::shared_ptr<StructObject>, std::shared_ptr<const TupleObject>,
            std::shared_ptr<MapObject>, FunctionRef, std::uint8_t, EnumRef, std::shared_ptr<BoxObject>>;
        Data data_;
    };

    struct BoxObject
    {
        Value value;
    };

    std::string_view value_kind_name(Value::Kind kind);
}
