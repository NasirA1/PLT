#include "thunder/value.hpp"

namespace thunder
{
    Value Value::from_int(std::int64_t value)
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

    Value::Kind Value::kind() const
    {
        return static_cast<Kind>(data_.index());
    }

    Value Value::deep_copy() const
    {
        if (is_array())
        {
            std::vector<Value> copy;
            copy.reserve(as_array().elements.size());
            for (const Value& element : as_array().elements)
            {
                copy.push_back(element.deep_copy());
            }
            return from_array(std::move(copy));
        }
        if (is_struct())
        {
            std::vector<Value> copy;
            copy.reserve(as_struct().fields.size());
            for (const Value& field : as_struct().fields)
            {
                copy.push_back(field.deep_copy());
            }
            return from_struct(as_struct().layout, std::move(copy));
        }
        return *this;
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

        std::string format_value(const Value& value, bool quote_strings, const DisplayLocale& locale)
        {
            switch (value.kind())
            {
            case Value::Kind::Void:
                return "void";
            case Value::Kind::Int:
                return locale.format_int(value.as_int());
            case Value::Kind::Bool:
                return value.as_bool() ? locale.true_text : locale.false_text;
            case Value::Kind::String:
                return quote_strings ? quote(value.as_string(), locale) : value.as_string();
            case Value::Kind::Array:
            {
                std::string out = "[";
                bool first = true;
                for (const Value& element : value.as_array().elements)
                {
                    out += first ? "" : locale.separator;
                    first = false;
                    out += format_value(element, true, locale);
                }
                return out + "]";
            }
            case Value::Kind::Struct:
            {
                // Point(x: 1, y: 2) - the same form used to create the value.
                const StructObject& object = value.as_struct();
                std::string out = object.layout->name + "(";
                for (std::size_t i = 0; i < object.fields.size(); ++i)
                {
                    out += i == 0 ? "" : locale.separator;
                    out += object.layout->field_names[i] + ": " + format_value(object.fields[i], true, locale);
                }
                return out + ")";
            }
            }
            return "?";
        }

        void inspect_struct(std::string& out, const StructObject& object, const std::string& indent,
            const DisplayLocale& locale)
        {
            out += object.layout->name + "\n" + indent + "{\n";
            const std::string inner = indent + "    ";
            for (std::size_t i = 0; i < object.fields.size(); ++i)
            {
                const Value& field = object.fields[i];
                out += inner + object.layout->field_names[i] + ": ";
                if (field.is_struct())
                {
                    inspect_struct(out, field.as_struct(), inner, locale); // nested struct, expanded
                }
                else
                {
                    out += object.layout->field_types[i] + " = " + format_value(field, true, locale);
                }
                out += "\n";
            }
            out += indent + "}";
        }
    }

    std::string Value::to_display(const DisplayLocale& locale) const
    {
        return format_value(*this, false, locale);
    }

    std::string Value::to_debug(const DisplayLocale& locale) const
    {
        return format_value(*this, true, locale);
    }

    std::string Value::inspect(const DisplayLocale& locale) const
    {
        if (!is_struct())
        {
            return to_debug(locale);
        }
        std::string out;
        inspect_struct(out, as_struct(), "", locale);
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
        case Value::Kind::Bool:
            return left.as_bool() == right.as_bool();
        case Value::Kind::String:
            return left.as_string() == right.as_string();
        case Value::Kind::Array:
            return &left.as_array() == &right.as_array();
        case Value::Kind::Struct:
            return &left.as_struct() == &right.as_struct();
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
        case Value::Kind::Bool:
            return "bool";
        case Value::Kind::String:
            return "string";
        case Value::Kind::Array:
            return "array";
        case Value::Kind::Struct:
            return "struct";
        }
        return "unknown";
    }
}
