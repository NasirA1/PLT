// Semantic types.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
            Tuple,  // a fixed-size group of values of given types, e.g. (int, string); see parts
            Map,    // an associative array, e.g. {string: int}; parts holds the key and value types
            Function, // a reference to a function, e.g. func(int): bool; parts holds the result type, then the parameters
        };

        Base base = Base::Error;
        std::uint8_t dimensions = 0; // 0 = scalar, 1 = T[], 2 = T[][], ...
        std::int32_t struct_index = -1;            // index into the program's struct table (Base::Struct)
        const std::string* struct_name = nullptr; // owned by the semantic model (Base::Struct)
        // Tuple: the element types; Map: the key type followed by the value type. Shared, because
        // types are copied freely.
        std::shared_ptr<const std::vector<Type>> parts;

        static Type of(Base base)
        {
            Type result;
            result.base = base;
            return result;
        }
        static Type error() { return {}; }
        static Type void_type() { return of(Base::Void); }
        static Type int_type() { return of(Base::Int); }
        static Type bool_type() { return of(Base::Bool); }
        static Type string_type() { return of(Base::String); }
        static Type struct_type(std::int32_t index, const std::string* name)
        {
            Type result = of(Base::Struct);
            result.struct_index = index;
            result.struct_name = name;
            return result;
        }
        static Type tuple_of(std::vector<Type> elements)
        {
            Type result = of(Base::Tuple);
            result.parts = std::make_shared<const std::vector<Type>>(std::move(elements));
            return result;
        }
        static Type map_of(Type key, Type value)
        {
            Type result = of(Base::Map);
            result.parts = std::make_shared<const std::vector<Type>>(std::vector<Type>{std::move(key), std::move(value)});
            return result;
        }
        // func(parameters...): result ('result' is void for a function that returns nothing).
        static Type function_of(std::vector<Type> parameters, Type result)
        {
            Type type = of(Base::Function);
            parameters.insert(parameters.begin(), std::move(result));
            type.parts = std::make_shared<const std::vector<Type>>(std::move(parameters));
            return type;
        }
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
        bool is_tuple() const { return base == Base::Tuple && dimensions == 0; }
        bool is_map() const { return base == Base::Map && dimensions == 0; }
        bool is_function() const { return base == Base::Function && dimensions == 0; }
        // True for int, bool and string: the types a map key may have.
        bool is_key_type() const { return is_int() || is_bool() || is_string(); }
        const std::vector<Type>& elements() const; // tuple elements, or {key, value} for a map
        const Type& key_type() const { return elements().at(0); }
        const Type& value_type() const { return elements().at(1); }
        // A function type's result and parameter types.
        const Type& result_type() const { return elements().at(0); }
        std::vector<Type> parameter_types() const
        {
            return elements().empty() ? std::vector<Type>{} : std::vector<Type>(elements().begin() + 1, elements().end());
        }
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
                && left.struct_index == right.struct_index && left.elements() == right.elements();
        }
    };
}
