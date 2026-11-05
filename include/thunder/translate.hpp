// Translating programs between natural languages (e.g. English <-> Pashto).
//
// Translation works on tokens: keywords, type names, true/false, built-in function names,
// punctuation, digits and string quotes are replaced by their spelling in the target language.
// Comments, spacing and the programmer's own names are kept as they are.
#pragma once

#include "thunder/diagnostics.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/source.hpp"

#include <string>
#include <vector>

namespace thunder
{
    struct TranslationResult
    {
        explicit TranslationResult(const LanguagePack& from)
            : diagnostics(from)
        {
        }

        bool ok = false;
        std::string text;                 // the translated program (if ok)
        std::vector<std::string> renamed; // names changed because they are reserved in the target language
        DiagnosticBag diagnostics;        // lexical errors that prevented translation
    };

    // Translates 'source', written in 'from' (or the language of its directive), into 'to'.
    TranslationResult translate(const SourceFile& source, const LanguagePack& from, const LanguagePack& to);
}
