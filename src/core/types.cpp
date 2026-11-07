#include "thunder/types.hpp"

#include "thunder/language_pack.hpp"

namespace thunder
{
    const std::vector<Type>& Type::elements() const
    {
        static const std::vector<Type> none;
        return parts ? *parts : none;
    }

    std::string Type::to_string(const LanguagePack* language) const
    {
        const auto word = [&](TokenKind kind, const char* english)
        {
            return language != nullptr ? language->spelling(kind) : std::string(english);
        };

        std::string text;
        switch (base)
        {
        case Base::Error:
            text = "<error>";
            break;
        case Base::Void:
            text = "void";
            break;
        case Base::Int:
            text = word(TokenKind::KwInt, "int");
            break;
        case Base::Bool:
            text = word(TokenKind::KwBool, "bool");
            break;
        case Base::String:
            text = word(TokenKind::KwString, "string");
            break;
        case Base::Struct:
            text = struct_name != nullptr ? *struct_name : "<struct>";
            break;
        case Base::Tuple:
            text = "(";
            for (std::size_t i = 0; i < elements().size(); ++i)
            {
                text += (i == 0 ? "" : ", ") + elements()[i].to_string(language);
            }
            text += ")";
            break;
        case Base::Map:
            text = "{" + key_type().to_string(language) + ": " + value_type().to_string(language) + "}";
            break;
        case Base::Function:
        {
            text = word(TokenKind::KwFunc, "func") + "(";
            for (std::size_t i = 1; i < elements().size(); ++i)
            {
                text += (i == 1 ? "" : ", ") + elements()[i].to_string(language);
            }
            text += ")";
            if (!elements().empty() && !result_type().is_void())
            {
                text += ": " + result_type().to_string(language);
            }
            if (dimensions > 0)
            {
                text = "(" + text + ")"; // (func(int): int)[] - the brackets belong to the whole type
            }
            break;
        }
        }
        for (std::uint8_t i = 0; i < dimensions; ++i)
        {
            text += "[]";
        }
        return text;
    }
}
