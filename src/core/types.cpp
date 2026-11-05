#include "thunder/types.hpp"

#include "thunder/language_pack.hpp"

namespace thunder
{
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
        }
        for (std::uint8_t i = 0; i < dimensions; ++i)
        {
            text += "[]";
        }
        return text;
    }
}
