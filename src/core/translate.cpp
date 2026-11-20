#include "thunder/translate.hpp"

#include "thunder/ast.hpp"
#include "thunder/compiler.hpp"
#include "thunder/lexer.hpp"
#include "thunder/parser.hpp"
#include "thunder/utf8.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace thunder
{
    namespace
    {
        std::string translate_number(const std::string& lexeme, const LanguagePack& from, const LanguagePack& to)
        {
            // 0x2A and 0b1010 are a notation, not a spelling: they stay exactly as written.
            if (lexeme.size() > 2 && (lexeme[1] == 'x' || lexeme[1] == 'X' || lexeme[1] == 'b' || lexeme[1] == 'B'))
            {
                return lexeme;
            }
            std::string out;
            std::size_t pos = 0;
            while (pos < lexeme.size())
            {
                const utf8::Decoded decoded = utf8::decode(lexeme, pos);
                const auto digit = digit_value(decoded.code_point);
                if (digit)
                {
                    utf8::append(out, to.digits[static_cast<std::size_t>(*digit)]);
                }
                else if (decoded.code_point == U'.' || decoded.code_point == from.decimal_point)
                {
                    utf8::append(out, to.decimal_point); // 3.14 <-> ۳٫۱۴
                }
                else
                {
                    utf8::append(out, decoded.code_point);
                }
                pos += decoded.length;
            }
            return out;
        }

        std::string translate_string(const std::string& value, const LanguagePack& to)
        {
            const QuotePair quotes = to.quotes.front();
            // Escapes are written with the target language's own letters: \n becomes \ن in
            // Pashto and back again in English (the lexer understands both everywhere).
            const bool pashto = to.code != "en";
            std::string out;
            utf8::append(out, quotes.open);
            std::size_t pos = 0;
            while (pos < value.size())
            {
                const utf8::Decoded decoded = utf8::decode(value, pos);
                pos += decoded.length;
                switch (decoded.code_point)
                {
                case U'\n':
                    out += pashto ? "\\ن" : "\\n";
                    break;
                case U'\t':
                    out += pashto ? "\\ت" : "\\t";
                    break;
                case U'\r':
                    out += pashto ? "\\ر" : "\\r";
                    break;
                case U'\0':
                    out += pashto ? "\\۰" : "\\0";
                    break;
                case U'\\':
                    out += "\\\\";
                    break;
                default:
                    if (decoded.code_point == quotes.close)
                    {
                        out += "\\"; // escape the closing quote of the target language
                    }
                    utf8::append(out, decoded.code_point);
                    break;
                }
            }
            utf8::append(out, quotes.close);
            return out;
        }

        // The built-in module a word names, in the source's language or in English.
        std::optional<BuiltinModule> module_named(const LanguagePack& from, const std::string& word)
        {
            const std::string folded = fold_letters(word);
            if (const auto own = from.modules.find(folded); own != from.modules.end())
            {
                return own->second;
            }
            if (const auto english = english_pack().modules.find(folded); english != english_pack().modules.end())
            {
                return english->second;
            }
            return std::nullopt;
        }

        // Where each 'with' moves. English writes it first on its line; Pashto puts it after the
        // handle ("لاستی سره = ...") or, without a handle, at the end ("ساکټ سره"). Translating
        // between them moves the keyword accordingly within the logical line (the tokens up to a
        // line break outside brackets). A 'with' already in the wrong place for its language is
        // translated where it stands.
        struct WithMoves
        {
            std::unordered_set<std::size_t> drop;   // 'with' tokens not written where they stand
            std::unordered_set<std::size_t> before; // tokens to write 'with' (and a space) before
            std::unordered_set<std::size_t> after;  // tokens to write (a space and) 'with' after
        };

        WithMoves with_moves(const std::vector<Token>& tokens, const LanguagePack& from, const LanguagePack& to)
        {
            WithMoves moves;
            if (from.with_after_resource == to.with_after_resource)
            {
                return moves;
            }
            // The first and last token of every logical line.
            std::vector<std::pair<std::size_t, std::size_t>> lines;
            int depth = 0;
            for (std::size_t i = 0; i < tokens.size(); ++i)
            {
                const TokenKind kind = tokens[i].kind;
                if (kind == TokenKind::Indent || kind == TokenKind::Dedent || kind == TokenKind::EndOfFile)
                {
                    continue;
                }
                if (lines.empty() || (tokens[i].newline_before && depth <= 0))
                {
                    lines.emplace_back(i, i);
                    depth = 0;
                }
                lines.back().second = i;
                if (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket || kind == TokenKind::LeftBrace)
                {
                    ++depth;
                }
                else if (kind == TokenKind::RightParen || kind == TokenKind::RightBracket || kind == TokenKind::RightBrace)
                {
                    --depth;
                }
            }
            for (const auto& [first, last] : lines)
            {
                if (first == last)
                {
                    continue;
                }
                // "with handle = ..." <-> "handle سره = ..."; "with value" <-> "value سره".
                const auto named_after = [&](std::size_t name)
                {
                    return tokens[name].kind == TokenKind::Identifier && name + 1 <= last
                        && (tokens[name + 1].kind == TokenKind::Assign || tokens[name + 1].kind == TokenKind::Colon);
                };
                if (!from.with_after_resource && tokens[first].kind == TokenKind::KwWith)
                {
                    moves.drop.insert(first);
                    moves.after.insert(first + 1 < last && named_after(first + 1) ? first + 1 : last);
                }
                else if (from.with_after_resource && tokens[last].kind == TokenKind::KwWith)
                {
                    moves.drop.insert(last);
                    moves.before.insert(first);
                }
                else if (from.with_after_resource && first + 1 < last && tokens[first + 1].kind == TokenKind::KwWith
                    && tokens[first].kind == TokenKind::Identifier
                    && (tokens[first + 2].kind == TokenKind::Assign || tokens[first + 2].kind == TokenKind::Colon))
                {
                    moves.drop.insert(first + 1);
                    moves.before.insert(first);
                }
            }
            return moves;
        }

        // The directive comment that selects 'to' (written with the target language's keyword).
        std::string directive_for(const LanguagePack& to)
        {
            return to.code == "en" ? "# lang: en" : "# ژبه: " + to.code;
        }

        // ------------------------------------------------------------ the SDK's own names
        //
        // import "datastructures" brings in Stack, push, pop ...; the Pashto file of the same
        // module declares ډېرۍ, ورزیات_کړه, واخله ... in the same order. Walking both syntax
        // trees together pairs them name by name.

        // One declared name: the member on its own ("push") and with its owner ("Stack.push").
        struct DeclaredName
        {
            std::string qualified;
            std::string plain;
            bool is_module = false;
        };

        void collect_names(const Stmt& item, const std::string& owner, std::vector<DeclaredName>& names);

        void collect_function(const FuncDeclStmt& function, const std::string& owner, std::vector<DeclaredName>& names)
        {
            names.push_back(DeclaredName{owner.empty() ? function.name : owner + "." + function.name, function.name});
            for (const std::string& parameter : function.type_params)
            {
                names.push_back(DeclaredName{parameter, parameter}); // "func first<T>(...)"
            }
        }

        void collect_names(const Stmt& item, const std::string& owner, std::vector<DeclaredName>& names)
        {
            const auto qualify = [&owner](const std::string& name)
            { return owner.empty() ? name : owner + "." + name; };
            switch (item.kind)
            {
            case StmtKind::FuncDecl:
                collect_function(static_cast<const FuncDeclStmt&>(item), owner, names);
                break;
            case StmtKind::StructDecl:
            {
                const auto& decl = static_cast<const StructDeclStmt&>(item);
                names.push_back(DeclaredName{qualify(decl.name), decl.name});
                for (const std::string& parameter : decl.type_params)
                {
                    names.push_back(DeclaredName{parameter, parameter}); // "struct Stack<T>"
                }
                const std::string inside = qualify(decl.name);
                for (const FieldDecl& field : decl.fields)
                {
                    names.push_back(DeclaredName{inside + "." + field.name, field.name});
                }
                for (const auto& method : decl.methods)
                {
                    collect_function(*method, inside, names);
                }
                break;
            }
            case StmtKind::EnumDecl:
            {
                const auto& decl = static_cast<const EnumDeclStmt&>(item);
                names.push_back(DeclaredName{qualify(decl.name), decl.name});
                for (const std::string& member : decl.members)
                {
                    names.push_back(DeclaredName{qualify(decl.name) + "." + member, member});
                }
                break;
            }
            case StmtKind::VarDecl:
            {
                const auto& decl = static_cast<const VarDeclStmt&>(item);
                names.push_back(DeclaredName{qualify(decl.name), decl.name});
                break;
            }
            case StmtKind::Module:
            {
                const auto& decl = static_cast<const ModuleDeclStmt&>(item);
                names.push_back(DeclaredName{qualify(decl.name), decl.name, true});
                for (const StmtPtr& member : decl.members)
                {
                    collect_names(*member, qualify(decl.name), names);
                }
                break;
            }
            default:
                break; // statements declare nothing
            }
        }

        std::vector<DeclaredName> declared_names(const std::string& text, const LanguagePack& language)
        {
            const SourceFile source("module.th", text);
            DiagnosticBag diagnostics(language);
            Lexer lexer(source, language, diagnostics);
            const std::vector<Token> tokens = lexer.tokenize();
            Parser parser(tokens, language, diagnostics);
            const ProgramNode program = parser.parse_program();
            std::vector<DeclaredName> names;
            if (diagnostics.has_errors())
            {
                return names; // a module that does not parse translates nothing
            }
            for (const StmtPtr& item : program.items)
            {
                collect_names(*item, {}, names);
            }
            return names;
        }

        // The names an imported SDK module declares, from the source language to the target one.
        struct SdkNames
        {
            std::unordered_map<std::string, std::string> plain;     // "push" -> "ورزیات_کړه"
            std::unordered_map<std::string, std::string> qualified; // "timer.after" -> "وروسته"
            std::unordered_set<std::string> modules;                // names that are modules ("timer")
            std::unordered_map<std::string, std::string> module_of;  // "ټایمر" -> "timer"

            std::unordered_set<std::string> ambiguous;              // one word in the source, two in the target
        };

        std::optional<std::string> read_module(const TranslateOptions& options, const std::string& directory,
            const std::string& name)
        {
            const std::string path = (std::filesystem::path(options.sdk_module_path) / directory / (name + ".th"))
                                         .lexically_normal()
                                         .generic_string();
            if (options.read_file)
            {
                return options.read_file(path);
            }
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return std::nullopt;
            }
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        SdkNames sdk_names(const std::vector<Token>& tokens, const LanguagePack& from, const LanguagePack& to,
            const TranslateOptions& options)
        {
            SdkNames map;
            if (options.sdk_module_path.empty())
            {
                return map;
            }
            for (std::size_t i = 0; i + 1 < tokens.size(); ++i)
            {
                // import "concurrency": a module of the SDK, if a file of that name is there.
                if (tokens[i].kind != TokenKind::KwImport || tokens[i + 1].kind != TokenKind::StringLiteral)
                {
                    continue;
                }
                const std::string module = tokens[i + 1].text_value;
                const auto mine = read_module(options, from.code, module);
                const auto theirs = read_module(options, to.code, module);
                if (!mine || !theirs)
                {
                    continue;
                }
                const std::vector<DeclaredName> ours = declared_names(*mine, from);
                const std::vector<DeclaredName> twins = declared_names(*theirs, to);
                if (ours.empty() || ours.size() != twins.size())
                {
                    continue; // the two files do not pair up: translate nothing rather than wrongly
                }
                for (std::size_t n = 0; n < ours.size(); ++n)
                {
                    if (ours[n].plain.starts_with('_'))
                    {
                        continue; // a module's own name: no program can write it
                    }
                    const std::string key = fold_letters(ours[n].plain);
                    if (const auto found = map.plain.find(key); found != map.plain.end())
                    {
                        if (found->second != twins[n].plain)
                        {
                            map.ambiguous.insert(key); // "wait" is two different words over there
                        }
                    }
                    else
                    {
                        map.plain.emplace(key, twins[n].plain);
                    }
                    map.qualified[fold_letters(ours[n].qualified)] = twins[n].plain;
                }
                for (std::size_t n = 0; n < ours.size(); ++n)
                {
                    if (ours[n].is_module)
                    {
                        map.module_of[fold_letters(ours[n].plain)] = twins[n].plain;
                    }
                }
                for (const DeclaredName& name : ours)
                {
                    if (name.is_module)
                    {
                        // Remember the modules, so that "timer.after" can be looked up by its
                        // owner even where "after" alone would be ambiguous.
                        map.modules.insert(fold_letters(name.plain));
                    }
                }
            }
            for (const std::string& name : map.ambiguous)
            {
                map.plain.erase(name);
            }
            return map;
        }

        // The names the program declares itself - a function, a struct, a module, an enum, a
        // variable, a loop variable, a parameter or a field. They are the programmer's own and
        // are never translated, even where an imported module happens to declare the same word.
        // 'members' holds the ones declared inside a struct, a mixin or an interface: only
        // those can stand after a dot, so "t.name()" is still the module's name even where the
        // program has a parameter called 'name'.
        struct OwnNames
        {
            std::unordered_set<std::string> all;
            std::unordered_set<std::string> members;
        };

        OwnNames own_names(const std::vector<Token>& tokens)
        {
            OwnNames names;
            std::size_t indent_depth = 0;
            std::unordered_set<std::size_t> struct_bodies; // the depths that hold members
            bool struct_ahead = false;
            for (std::size_t i = 0; i < tokens.size(); ++i)
            {
                const TokenKind kind = tokens[i].kind;
                if (kind == TokenKind::Indent)
                {
                    ++indent_depth;
                    if (struct_ahead)
                    {
                        struct_bodies.insert(indent_depth);
                        struct_ahead = false;
                    }
                    continue;
                }
                if (kind == TokenKind::Dedent)
                {
                    struct_bodies.erase(indent_depth);
                    indent_depth = indent_depth > 0 ? indent_depth - 1 : 0;
                    continue;
                }
                const bool declares = kind == TokenKind::KwFunc || kind == TokenKind::KwStruct
                    || kind == TokenKind::KwMixin || kind == TokenKind::KwInterface || kind == TokenKind::KwEnum
                    || kind == TokenKind::KwModule || kind == TokenKind::KwVar || kind == TokenKind::KwConst
                    || kind == TokenKind::KwFor;
                const bool member_here = struct_bodies.contains(indent_depth);
                if (kind == TokenKind::KwStruct || kind == TokenKind::KwMixin || kind == TokenKind::KwInterface)
                {
                    struct_ahead = true;
                }
                if (declares)
                {
                    // "for a, b in pairs": every name up to 'in' is a loop variable.
                    for (std::size_t n = i + 1; n < tokens.size() && tokens[n].kind != TokenKind::KwIn; ++n)
                    {
                        if (tokens[n].kind == TokenKind::Identifier)
                        {
                            names.all.insert(fold_letters(tokens[n].lexeme));
                            if (member_here)
                            {
                                names.members.insert(fold_letters(tokens[n].lexeme));
                            }
                        }
                        if (kind != TokenKind::KwFor || tokens[n].kind == TokenKind::Assign)
                        {
                            break; // the other keywords name one thing
                        }
                    }
                }
                // "n: int" - a parameter, a field, a typed variable or a named argument.
                if (kind == TokenKind::Identifier && i + 1 < tokens.size() && tokens[i + 1].kind == TokenKind::Colon)
                {
                    names.all.insert(fold_letters(tokens[i].lexeme));
                    if (member_here && tokens[i].newline_before)
                    {
                        names.members.insert(fold_letters(tokens[i].lexeme)); // a field of its own
                    }
                }
            }
            return names;
        }

        // The type each variable was declared with, where the program says it: "var s: Stack<int>",
        // "var s = Stack()", "func f(s: Stack<int>)". It tells whose member a name after a dot
        // is, which matters where one word of the source language is two in the target
        // ("واخله" is both Stack.pop and Queue.take).
        std::unordered_map<std::string, std::string> variable_types(const std::vector<Token>& tokens)
        {
            std::unordered_map<std::string, std::string> types;
            const auto remember = [&types](const Token& name, const Token& type)
            {
                if (name.kind == TokenKind::Identifier && type.kind == TokenKind::Identifier)
                {
                    types[fold_letters(name.lexeme)] = fold_letters(type.lexeme);
                }
            };
            for (std::size_t i = 0; i + 2 < tokens.size(); ++i)
            {
                if (tokens[i].kind == TokenKind::Identifier && tokens[i + 1].kind == TokenKind::Colon)
                {
                    remember(tokens[i], tokens[i + 2]); // "s: Stack<int>" - a variable, a parameter, a field
                }
                else if ((tokens[i].kind == TokenKind::KwVar || tokens[i].kind == TokenKind::KwConst)
                    && tokens[i + 1].kind == TokenKind::Identifier && tokens[i + 2].kind == TokenKind::Assign
                    && i + 4 < tokens.size() && tokens[i + 4].kind == TokenKind::LeftParen)
                {
                    remember(tokens[i + 1], tokens[i + 3]); // "var s = Stack()"
                }
            }
            return types;
        }

        // The target language's word for a name of an imported SDK module: the member of its
        // owner where the owner is known ("timer.after"), else the name on its own.
        std::optional<std::string> sdk_name(const SdkNames& map, const std::string& owner, const std::string& name)
        {
            const std::string folded = fold_letters(name);
            if (!owner.empty())
            {
                if (const auto found = map.qualified.find(owner + "." + folded); found != map.qualified.end())
                {
                    return found->second;
                }
            }
            if (const auto found = map.plain.find(folded); found != map.plain.end())
            {
                return found->second;
            }
            return std::nullopt;
        }
    }

    TranslationResult translate(const SourceFile& source, const LanguagePack& from_default, const LanguagePack& to,
        const TranslateOptions& options)
    {
        const LanguagePack& from = choose_language(source, from_default);
        TranslationResult result(from);
        Lexer lexer(source, from, result.diagnostics);
        const std::vector<Token> tokens = lexer.tokenize();
        if (result.diagnostics.has_errors())
        {
            return result;
        }

        const std::string& text = source.text();
        std::string out;
        std::size_t copied = 0; // source bytes already copied to 'out'
        // Source text between the tokens (spaces, comments, line breaks) is copied as it is,
        // except that a Windows line break loses its carriage return: the translation is written
        // with the platform's own line breaks, and a kept '\r' would double them (one empty line
        // after every line of code).
        const auto copy_source = [&](std::size_t begin, std::size_t end)
        {
            for (std::size_t at = begin; at < end && at < text.size(); ++at)
            {
                if (text[at] != '\r')
                {
                    out += text[at];
                }
            }
        };

        // Rewrite (or add) the language directive.
        const bool has_directive = language_directive(text).has_value();
        if (has_directive)
        {
            copied = std::min(text.find('\n'), text.size());
            if (copied > 0 && text[copied - 1] == '\r')
            {
                --copied;
            }
            out += directive_for(to);
        }
        else if (to.code != "en")
        {
            out += directive_for(to) + "\n";
        }

        std::unordered_set<std::string> renamed;
        std::unordered_set<std::string> untranslated;
        const SdkNames sdk = sdk_names(tokens, from, to, options);
        const OwnNames own = own_names(tokens);
        const std::unordered_map<std::string, std::string> variables = variable_types(tokens);
        const WithMoves moves = with_moves(tokens, from, to);
        const std::string with_word = to.spelling(TokenKind::KwWith);
        std::optional<BuiltinModule> module_before; // the built-in module named just before a '.'
        std::string sdk_owner_before;               // an SDK module named just before a '.'
        for (std::size_t index = 0; index < tokens.size(); ++index)
        {
            const Token& token = tokens[index];
            if (moves.drop.contains(index))
            {
                // The keyword moves to the other end of the line; the space beside it goes too.
                if (from.with_after_resource)
                {
                    copied = std::max<std::size_t>(copied, token.span.offset + token.span.length);
                }
                else
                {
                    if (token.span.offset > copied)
                    {
                        copy_source(copied, token.span.offset); // the line break and indentation before it stay
                    }
                    copied = std::max<std::size_t>(copied, index + 1 < tokens.size() ? tokens[index + 1].span.offset
                                                                                      : token.span.offset + token.span.length);
                }
                continue;
            }
            if (token.span.offset > copied)
            {
                copy_source(copied, token.span.offset); // spaces and comments
            }
            copied = std::max<std::size_t>(copied, token.span.offset + token.span.length);
            if (token.kind == TokenKind::EndOfFile)
            {
                break;
            }
            if (token.kind == TokenKind::Indent || token.kind == TokenKind::Dedent)
            {
                continue; // indentation markers have no text of their own
            }
            if (moves.before.contains(index))
            {
                out += with_word + " ";
            }
            // math.sqrt: the word after a built-in module's dot is one of its members.
            const bool after_dot = index > 0 && tokens[index - 1].kind == TokenKind::Dot;
            const std::optional<BuiltinModule> member_of = after_dot ? module_before : std::nullopt;
            const std::string sdk_owner = after_dot ? sdk_owner_before : std::string();
            module_before.reset();
            sdk_owner_before.clear();
            if (token.kind == TokenKind::Dot && index > 0 && !(index > 1 && tokens[index - 2].kind == TokenKind::Dot))
            {
                const Token& previous = tokens[index - 1];
                if (previous.kind == TokenKind::Identifier || previous.kind == TokenKind::KwString)
                {
                    module_before = module_named(from, previous.lexeme);
                    const std::string folded = fold_letters(previous.lexeme);
                    if (sdk.modules.contains(folded))
                    {
                        sdk_owner_before = folded; // "timer.after": a module of the SDK
                    }
                    else if (const auto type = variables.find(folded); type != variables.end())
                    {
                        sdk_owner_before = type->second; // "plates.pop()": whatever 'plates' holds
                    }
                    else
                    {
                        sdk_owner_before = folded; // a struct named directly: "socket.Socket"
                    }
                }
            }

            if (member_of && from.module_builtin(*member_of, token.lexeme))
            {
                out += to.member_spelling(*from.module_builtin(*member_of, token.lexeme)); // sqrt <-> جذر
            }
            else if (token.kind == TokenKind::Identifier && index + 1 < tokens.size()
                && (tokens[index + 1].kind == TokenKind::Dot || (index > 0 && tokens[index - 1].kind == TokenKind::KwImport))
                && !after_dot && module_named(from, token.lexeme))
            {
                out += to.spelling(*module_named(from, token.lexeme)); // math <-> ریاضي, import os <-> راوړه سیسټم
            }
            else if (token.kind == TokenKind::Identifier)
            {
                if (const auto builtin = after_dot ? std::nullopt : from.builtin(token.lexeme))
                {
                    out += to.spelling(*builtin);
                }
                else if (from.is_main_name(token.lexeme))
                {
                    out += to.main_name; // main() is named in each language
                }
                else if (const auto enumeration = from.builtin_enum(token.lexeme))
                {
                    out += to.spelling(*enumeration); // Color <-> رنګونه
                }
                else if (const auto member = from.builtin_enum_member(token.lexeme))
                {
                    out += to.member_spelling(member->first, member->second); // red <-> سور
                }
                else if (after_dot ? own.members.contains(fold_letters(token.lexeme))
                                   : own.all.contains(fold_letters(token.lexeme)))
                {
                    // The programmer's own name, whatever an imported module calls its own -
                    // but a name that is reserved over there still gets its '_'.
                    if (to.keyword(token.lexeme) || to.builtin(token.lexeme))
                    {
                        out += token.lexeme + "_";
                        renamed.insert(token.lexeme);
                    }
                    else
                    {
                        out += token.lexeme;
                    }
                }
                else if (index + 1 < tokens.size() && tokens[index + 1].kind == TokenKind::Dot
                    && sdk.module_of.contains(fold_letters(token.lexeme)))
                {
                    // "socket.Socket": a module and the struct inside it may share a word in
                    // one language; before a dot the name is the module's.
                    out += sdk.module_of.at(fold_letters(token.lexeme));
                }
                else if (const auto name = sdk_name(sdk, sdk_owner, token.lexeme))
                {
                    out += *name; // Stack <-> ډېرۍ: a name the imported SDK module declares
                }
                else if (sdk.ambiguous.contains(fold_letters(token.lexeme)))
                {
                    out += token.lexeme; // the module writes two different things with this word
                    untranslated.insert(token.lexeme);
                }
                else if (to.keyword(token.lexeme) || to.builtin(token.lexeme))
                {
                    out += token.lexeme + "_"; // the name is reserved in the target language
                    renamed.insert(token.lexeme);
                }
                else
                {
                    out += token.lexeme;
                }
            }
            else if (token.kind == TokenKind::IntLiteral || token.kind == TokenKind::FloatLiteral)
            {
                out += translate_number(token.lexeme, from, to);
            }
            else if (token.kind == TokenKind::StringLiteral)
            {
                out += translate_string(token.text_value, to);
            }
            else if (token.kind == TokenKind::Annotation)
            {
                // "@test" <-> "@ازموینه"; an unknown annotation stays as written.
                const auto canonical = from.annotation(token.text_value);
                out += canonical ? "@" + to.annotation_spelling(*canonical) : token.lexeme;
            }
            else
            {
                out += to.spelling(token.kind); // keywords, types, true/false and punctuation
            }
            if (moves.after.contains(index))
            {
                out += " " + with_word;
            }
        }
        if (copied < text.size())
        {
            copy_source(copied, text.size());
        }

        result.ok = true;
        result.text = std::move(out);
        result.renamed.assign(renamed.begin(), renamed.end());
        std::ranges::sort(result.renamed);
        result.untranslated.assign(untranslated.begin(), untranslated.end());
        std::ranges::sort(result.untranslated);
        return result;
    }
}
