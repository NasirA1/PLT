#include "thunder/value.hpp"

#include <algorithm>
#include <unordered_map>

namespace thunder
{
    Value Value::from_int(std::int64_t value)
    {
        Value result;
        result.data_ = value;
        return result;
    }

    Value Value::from_float(double value)
    {
        Value result;
        result.data_ = value;
        return result;
    }

    Value Value::from_bool(bool value)
    {
        Value result;
        result.data_ = value;
        return result;
    }

    Value Value::from_string(std::string value)
    {
        Value result;
        result.data_ = std::make_shared<const std::string>(std::move(value));
        return result;
    }

    Value Value::from_array(std::vector<Value> elements)
    {
        Value result;
        result.data_ = std::make_shared<ArrayObject>(ArrayObject{std::move(elements)});
        return result;
    }

    Value Value::from_struct(const StructLayout* layout, std::vector<Value> fields)
    {
        Value result;
        result.data_ = std::make_shared<StructObject>(StructObject{layout, std::move(fields)});
        return result;
    }

    Value Value::nothing()
    {
        Value result;
        result.data_ = std::shared_ptr<StructObject>(); // the empty struct reference
        return result;
    }

    Value Value::from_tuple(std::vector<Value> elements)
    {
        Value result;
        result.data_ = std::make_shared<const TupleObject>(TupleObject{std::move(elements)});
        return result;
    }

    Value Value::from_map(std::vector<std::pair<Value, Value>> entries)
    {
        auto object = std::make_shared<MapObject>();
        for (auto& [key, value] : entries)
        {
            object->set(key, std::move(value)); // a repeated key keeps its first position, last value
        }
        Value result;
        result.data_ = std::move(object);
        return result;
    }

    Value Value::from_function(std::uint16_t index, const std::string* name)
    {
        Value result;
        result.data_ = FunctionRef{index, name, nullptr};
        return result;
    }

    Value Value::from_closure(std::uint16_t index, const std::string* name,
        std::shared_ptr<std::vector<Value>> captures)
    {
        Value result;
        result.data_ = FunctionRef{index, name, std::move(captures)};
        return result;
    }

    Value Value::from_box(Value content)
    {
        Value result;
        result.data_ = std::make_shared<BoxObject>(BoxObject{std::move(content)});
        return result;
    }

    Value Value::from_byte(std::uint8_t value)
    {
        Value result;
        result.data_ = value;
        return result;
    }

    Value Value::from_enum(const EnumLayout* layout, std::uint16_t ordinal)
    {
        Value result;
        result.data_ = EnumRef{layout, ordinal};
        return result;
    }

    std::string MapObject::encode(const Value& key)
    {
        switch (key.kind())
        {
        case Value::Kind::Int:
            return "i" + std::to_string(key.as_int());
        case Value::Kind::Byte:
            return "y" + std::to_string(key.as_byte());
        case Value::Kind::Enum:
            return "e" + (key.as_enum().layout != nullptr ? key.as_enum().layout->name : std::string()) + ":"
                + std::to_string(key.as_enum().ordinal);
        case Value::Kind::Bool:
            return key.as_bool() ? "b1" : "b0";
        case Value::Kind::String:
            return "s" + key.as_string();
        default:
            return "?"; // never a valid key (the type checker only allows int, bool, string, byte and enums)
        }
    }

    const Value* MapObject::find(const Value& key) const
    {
        const auto it = index.find(encode(key));
        return it == index.end() ? nullptr : &entries[it->second].second;
    }

    void MapObject::set(const Value& key, Value value)
    {
        const std::string code = encode(key);
        const auto it = index.find(code);
        if (it != index.end())
        {
            entries[it->second].second = std::move(value);
            return;
        }
        index.emplace(code, entries.size());
        entries.emplace_back(key, std::move(value));
    }

    bool MapObject::erase(const Value& key)
    {
        const auto it = index.find(encode(key));
        if (it == index.end())
        {
            return false;
        }
        const std::size_t position = it->second;
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(position));
        index.erase(it);
        for (auto& [code, slot] : index)
        {
            if (slot > position)
            {
                --slot;
            }
        }
        return true;
    }

    Value::Kind Value::kind() const
    {
        return static_cast<Kind>(data_.index());
    }

    namespace
    {
        // Objects already copied, so that shared (or, with recursive structs, cyclic) parts of
        // the original stay shared in the copy instead of multiplying or recursing for ever.
        using CopyMemo = std::unordered_map<const void*, Value>;

        Value deep_copy_memo(const Value& value, CopyMemo& memo)
        {
            if (value.is_array())
            {
                const void* address = &value.as_array();
                if (const auto it = memo.find(address); it != memo.end())
                {
                    return it->second;
                }
                Value copy = Value::from_array({});
                memo.emplace(address, copy);
                copy.as_array().elements.reserve(value.as_array().elements.size());
                for (const Value& element : value.as_array().elements)
                {
                    copy.as_array().elements.push_back(deep_copy_memo(element, memo));
                }
                return copy;
            }
            if (value.is_struct() && !value.is_nothing())
            {
                const void* address = &value.as_struct();
                if (const auto it = memo.find(address); it != memo.end())
                {
                    return it->second;
                }
                Value copy = Value::from_struct(value.as_struct().layout, {});
                memo.emplace(address, copy);
                copy.as_struct().fields.reserve(value.as_struct().fields.size());
                for (const Value& field : value.as_struct().fields)
                {
                    copy.as_struct().fields.push_back(deep_copy_memo(field, memo));
                }
                return copy;
            }
            if (value.is_tuple())
            {
                std::vector<Value> copy;
                for (const Value& element : value.as_tuple().elements)
                {
                    copy.push_back(deep_copy_memo(element, memo));
                }
                return Value::from_tuple(std::move(copy));
            }
            if (value.is_map())
            {
                const void* address = &value.as_map();
                if (const auto it = memo.find(address); it != memo.end())
                {
                    return it->second;
                }
                Value copy = Value::from_map({});
                memo.emplace(address, copy);
                for (const auto& [key, element] : value.as_map().entries)
                {
                    copy.as_map().set(key, deep_copy_memo(element, memo));
                }
                return copy;
            }
            return value;
        }
    }

    Value Value::deep_copy() const
    {
        CopyMemo memo;
        return deep_copy_memo(*this, memo);
    }

    namespace
    {
        std::string quote(const std::string& text, const DisplayLocale& locale)
        {
            std::string out = locale.open_quote;
            for (const char c : text)
            {
                switch (c)
                {
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\0':
                    out += "\\0";
                    break;
                case '"':
                    out += locale.open_quote == "\"" ? "\\\"" : "\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                default:
                    out += c;
                    break;
                }
            }
            return out + locale.close_quote;
        }

        std::string format_value(const Value& value, bool quote_strings, const DisplayLocale& locale,
            std::vector<const void*>& visiting);

        // True if 'address' is already being written (a cycle through recursive structs).
        bool entering(const void* address, std::vector<const void*>& visiting)
        {
            if (std::find(visiting.begin(), visiting.end(), address) != visiting.end())
            {
                return false;
            }
            visiting.push_back(address);
            return true;
        }

        // The elements of an array or a tuple, separated as the locale says, between 'open' and 'close'.
        std::string format_elements(const std::vector<Value>& elements, const char* open, const char* close,
            const DisplayLocale& locale, std::vector<const void*>& visiting)
        {
            std::string out = open;
            for (std::size_t i = 0; i < elements.size(); ++i)
            {
                out += i == 0 ? "" : locale.separator;
                out += format_value(elements[i], true, locale, visiting);
            }
            return out + close;
        }

        // Point(x: 1, y: 2) - the same form used to create the value.
        std::string format_struct(const StructObject& object, const DisplayLocale& locale, std::vector<const void*>& visiting)
        {
            std::string out = object.layout->name + "(";
            for (std::size_t i = 0; i < object.fields.size(); ++i)
            {
                out += i == 0 ? "" : locale.separator;
                out += object.layout->field_names[i] + ": " + format_value(object.fields[i], true, locale, visiting);
            }
            return out + ")";
        }

        // {"a": 1, "b": 2} - the same form used to create the value, in insertion order.
        std::string format_map(const MapObject& map, const DisplayLocale& locale, std::vector<const void*>& visiting)
        {
            std::string out = "{";
            bool first = true;
            for (const auto& [key, element] : map.entries)
            {
                out += first ? "" : locale.separator;
                first = false;
                out += format_value(key, true, locale, visiting) + ": " + format_value(element, true, locale, visiting);
            }
            return out + "}";
        }

        // Arrays, structs and maps are references, and may contain themselves through a struct:
        // a value already being written is cut short.
        std::string format_reference(const Value& value, const DisplayLocale& locale, std::vector<const void*>& visiting)
        {
            if (value.is_nothing())
            {
                return locale.nothing_text;
            }
            const void* address = value.is_array() ? static_cast<const void*>(&value.as_array())
                : value.is_struct()                ? static_cast<const void*>(&value.as_struct())
                                                   : static_cast<const void*>(&value.as_map());
            if (!entering(address, visiting))
            {
                return value.is_array() ? "[...]" : (value.is_struct() ? "..." : "{...}");
            }
            std::string out;
            if (value.is_array())
            {
                out = format_elements(value.as_array().elements, "[", "]", locale, visiting);
            }
            else if (value.is_struct())
            {
                out = format_struct(value.as_struct(), locale, visiting);
            }
            else
            {
                out = format_map(value.as_map(), locale, visiting);
            }
            visiting.pop_back();
            return out;
        }

        std::string format_value(const Value& value, bool quote_strings, const DisplayLocale& locale,
            std::vector<const void*>& visiting)
        {
            switch (value.kind())
            {
            case Value::Kind::Void:
                return "void";
            case Value::Kind::Int:
                return locale.format_int(value.as_int());
            case Value::Kind::Byte:
                return locale.format_int(value.as_byte());
            case Value::Kind::Enum:
            {
                // The member's name alone where a value is shown (println(Color.red) is "red"), and
                // Enum.member where strings are quoted too (inside arrays, in the debugger).
                const EnumRef& ref = value.as_enum();
                const std::string member = ref.layout != nullptr && ref.ordinal < ref.layout->member_names.size()
                    ? ref.layout->member_names[ref.ordinal]
                    : "?";
                return quote_strings && ref.layout != nullptr ? ref.layout->name + "." + member : member;
            }
            case Value::Kind::Float:
                return locale.format_float(value.as_float());
            case Value::Kind::Bool:
                return value.as_bool() ? locale.true_text : locale.false_text;
            case Value::Kind::String:
                return quote_strings ? quote(value.as_string(), locale) : value.as_string();
            case Value::Kind::Tuple:
                return format_elements(value.as_tuple().elements, "(", ")", locale, visiting);
            case Value::Kind::Function:
            {
                const FunctionRef& function = value.as_function();
                return "<" + locale.function_word + " " + (function.name != nullptr ? *function.name : "?") + ">";
            }
            case Value::Kind::Box:
                // A captured variable's box shows as its content (programs never see the box).
                return format_value(value.as_box().value, quote_strings, locale, visiting);
            case Value::Kind::Array:
            case Value::Kind::Struct:
            case Value::Kind::Map:
                return format_reference(value, locale, visiting);
            }
            return "?";
        }

        // The struct's name, then one field per line indented beneath it (like the source language,
        // blocks are shown by indentation). Nested structs are expanded the same way.
        void inspect_struct(std::string& out, const StructObject& object, const std::string& indent,
            const DisplayLocale& locale, std::vector<const void*>& visiting)
        {
            out += object.layout->name;
            if (!entering(&object, visiting))
            {
                out += " (...)"; // the struct is inside itself (a cycle of references)
                return;
            }
            const std::string inner = indent + "    ";
            for (std::size_t i = 0; i < object.fields.size(); ++i)
            {
                const Value& field = object.fields[i];
                out += "\n" + inner + object.layout->field_names[i] + ": ";
                if (field.is_struct() && !field.is_nothing())
                {
                    inspect_struct(out, field.as_struct(), inner, locale, visiting); // nested struct, expanded
                }
                else
                {
                    out += object.layout->field_types[i] + " = " + format_value(field, true, locale, visiting);
                }
            }
            visiting.pop_back();
        }
    }

    std::string Value::to_display(const DisplayLocale& locale) const
    {
        std::vector<const void*> visiting;
        return format_value(*this, false, locale, visiting);
    }

    std::string Value::to_debug(const DisplayLocale& locale) const
    {
        std::vector<const void*> visiting;
        return format_value(*this, true, locale, visiting);
    }

    std::string Value::inspect(const DisplayLocale& locale) const
    {
        if (!is_struct() || is_nothing())
        {
            return to_debug(locale);
        }
        std::string out;
        std::vector<const void*> visiting;
        inspect_struct(out, as_struct(), "", locale, visiting);
        return out;
    }

    bool operator==(const Value& left, const Value& right)
    {
        if (left.kind() != right.kind())
        {
            return false;
        }
        switch (left.kind())
        {
        case Value::Kind::Void:
            return true;
        case Value::Kind::Int:
            return left.as_int() == right.as_int();
        case Value::Kind::Float:
            return left.as_float() == right.as_float();
        case Value::Kind::Bool:
            return left.as_bool() == right.as_bool();
        case Value::Kind::String:
            return left.as_string() == right.as_string();
        case Value::Kind::Array:
            return &left.as_array() == &right.as_array();
        case Value::Kind::Struct:
            // By identity; a null reference is "nothing", equal only to nothing.
            return std::get<std::shared_ptr<StructObject>>(left.data_)
                == std::get<std::shared_ptr<StructObject>>(right.data_);
        case Value::Kind::Tuple:
            return left.as_tuple().elements == right.as_tuple().elements;
        case Value::Kind::Map:
            return &left.as_map() == &right.as_map();
        case Value::Kind::Function:
            return left.as_function() == right.as_function();
        case Value::Kind::Byte:
            return left.as_byte() == right.as_byte();
        case Value::Kind::Enum:
            return left.as_enum().layout == right.as_enum().layout && left.as_enum().ordinal == right.as_enum().ordinal;
        case Value::Kind::Box:
            return &left.as_box() == &right.as_box();
        }
        return false;
    }

    std::string_view value_kind_name(Value::Kind kind)
    {
        switch (kind)
        {
        case Value::Kind::Void:
            return "void";
        case Value::Kind::Int:
            return "int";
        case Value::Kind::Float:
            return "float";
        case Value::Kind::Bool:
            return "bool";
        case Value::Kind::String:
            return "string";
        case Value::Kind::Array:
            return "array";
        case Value::Kind::Struct:
            return "struct";
        case Value::Kind::Tuple:
            return "tuple";
        case Value::Kind::Map:
            return "map";
        case Value::Kind::Function:
            return "function";
        case Value::Kind::Byte:
            return "byte";
        case Value::Kind::Enum:
            return "enum";
        case Value::Kind::Box:
            return "box";
        }
        return "unknown";
    }
}
