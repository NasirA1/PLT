// Translating programs between natural languages (e.g. English <-> Pashto).
//
// Translation works on tokens: keywords, type names, true/false, built-in function names,
// punctuation, digits and string quotes are replaced by their spelling in the target language.
// Comments, spacing and the programmer's own names are kept as they are.
//
// A program that imports one of the SDK's modules (import "datastructures", "concurrency", ...)
// also uses the names that module declares, and the SDK has one file per language: the English
// Stack is the Pashto ډېرۍ. Those names are translated too, by reading the module's twin in the
// target language - the two files declare the same things in the same order, so the n-th
// declaration of one is the n-th of the other (see TranslateOptions).
#pragma once

#include "thunder/diagnostics.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/source.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace thunder
{
    struct TranslateOptions
    {
        // The SDK's Thunder modules (lib/thunder): a directory with one folder per language
        // code. Empty: the names the SDK declares are left as they are.
        std::string sdk_module_path;
        // Reads a file; nullopt if it does not exist. The default reads from the disk.
        std::function<std::optional<std::string>(const std::string& path)> read_file;
    };

    struct TranslationResult
    {
        explicit TranslationResult(const LanguagePack& from)
            : diagnostics(from)
        {
        }

        bool ok = false;
        std::string text;                 // the translated program (if ok)
        std::vector<std::string> renamed; // names changed because they are reserved in the target language
        // Names of an imported SDK module that could not be translated because the module uses
        // one word for two things in the target language (then the name is left as written).
        std::vector<std::string> untranslated;
        DiagnosticBag diagnostics;        // lexical errors that prevented translation
    };

    // Translates 'source', written in 'from' (or the language of its directive), into 'to'.
    TranslationResult translate(const SourceFile& source, const LanguagePack& from, const LanguagePack& to,
        const TranslateOptions& options = {});
}
