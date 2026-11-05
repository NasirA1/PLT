// Semantic types.
#pragma once

#include <cstdint>
#include <string>

namespace thunder
{
    struct LanguagePack;

    struct Type
    {
        enum class Base : std::uint8_t
        {
            Error, // the type of an expression that already produced an error (suppresses follow-up errors)
            Void,  // "no value": the result of a function without a return type
            Int,
            Bool,
            String,
            Struct, // a user-defined struct; see struct_index / struct_name
        };

        Base base = Base::Error;
        std::uint8_t dimensions = 0; // 0 = scalar, 1 = T[], 2 = T[][], ...
        std::int32_t struct_index = -1;            // index into the program's struct table (Base::Struct)
        const std::string* struct_name = nullptr; // owned by the semantic model (Base::Struct)

        static Type error() { return {}; }
        static Type void_type() { return {Base::Void}; }
        static Type int_type() { return {Base::Int}; }
        static Type bool_type() { return {Base::Bool}; }
        static Type string_type() { return {Base::String}; }
        static Type struct_type(std::int32_t index, const std::string* name) { return {Base::Struct, 0, index, name}; }
        static Type array_of(Type element)
        {
            element.dimensions = static_cast<std::uint8_t>(element.dimensions + 1);
            return element;
        }

        bool is_error() const { return base == Base::Error; }
        bool is_void() const { return base == Base::Void && dimensions == 0; }
        bool is_array() const { return dimensions > 0; }
        bool is_int() const { return base == Base::Int && dimensions == 0; }
        bool is_bool() const { return base == Base::Bool && dimensions == 0; }
        bool is_string() const { return base == Base::String && dimensions == 0; }
        bool is_struct() const { return base == Base::Struct && dimensions == 0; }
        Type element() const
        {
            Type result = *this;
            result.dimensions = static_cast<std::uint8_t>(dimensions - 1);
            return result;
        }

        // "int", "Point[][]", ... Type names are spelled in 'language' (English if null).
        std::string to_string(const LanguagePack* language = nullptr) const;

        friend bool operator==(const Type& left, const Type& right)
        {
            return left.base == right.base && left.dimensions == right.dimensions
                && left.struct_index == right.struct_index;
        }
    };
}
