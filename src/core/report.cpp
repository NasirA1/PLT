#include "thunder/report.hpp"

#include "thunder/ast.hpp"
#include "thunder/builtins.hpp"
#include "thunder/utf8.hpp"

#include <algorithm>

#include <format>

namespace thunder
{
    namespace
    {
        void write_span(JsonWriter& json, const Span& span)
        {
            json.field("line", span.start.line);
            json.field("column", span.start.column);
            json.field("end_line", span.end.line);
            json.field("end_column", span.end.column);
            if (span.file != 0)
            {
                json.field("file", static_cast<unsigned>(span.file)); // in an imported module
            }
        }

        void write_diagnostic(JsonWriter& json, const Diagnostic& diagnostic, const std::string& file_name,
            const LanguagePack& language)
        {
            json.begin_object();
            json.field("code", diagnostic.code);
            json.field("phase", phase_name(diagnostic.phase));
            json.field("severity", severity_name(diagnostic.severity));
            json.field("message", diagnostic.message);
            write_span(json, diagnostic.span);
            json.field("file_name", file_name);
            json.field("text", format_short(diagnostic, file_name, language));
            json.end_object();
        }

        void write_ast(JsonWriter& json, const AstNodeView& node, const std::string& role)
        {
            json.begin_object();
            json.field("kind", node.kind);
            json.field("label", node.label);
            json.field("type", node.type);
            json.field("role", role);
            write_span(json, node.span);
            json.key("children").begin_array();
            for (const AstChild& child : node.children)
            {
                write_ast(json, child.node, child.role);
            }
            json.end_array();
            json.end_object();
        }

        void write_symbol(JsonWriter& json, const Symbol& symbol, const LanguagePack& language)
        {
            json.begin_object();
            json.field("name", symbol.name);
            json.field("kind", symbol_kind_name(symbol.kind));
            json.field("type", symbol.type.to_string(&language));
            json.field("const", symbol.is_const);
            json.field("slot", static_cast<unsigned>(symbol.slot));
            json.field("used", symbol.used);
            write_span(json, symbol.span);
            json.end_object();
        }

        void write_scope(JsonWriter& json, const ScopeInfo& scope, const LanguagePack& language)
        {
            json.begin_object();
            json.field("kind", scope.kind);
            json.field("name", scope.name);
            write_span(json, scope.span);
            json.key("symbols").begin_array();
            for (const Symbol* symbol : scope.symbols)
            {
                write_symbol(json, *symbol, language);
            }
            json.end_array();
            json.key("children").begin_array();
            for (const ScopeInfo& child : scope.children)
            {
                write_scope(json, child, language);
            }
            json.end_array();
            json.end_object();
        }

        void write_symbols(JsonWriter& json, const SemanticModel& model, const LanguagePack& language)
        {
            json.begin_object();
            json.key("scopes");
            write_scope(json, model.global_scope, language);
            json.key("structs").begin_array();
            for (std::size_t i = 0; i < model.structs.size(); ++i)
            {
                const StructInfo& info = *model.structs[i];
                json.begin_object();
                json.field("index", static_cast<std::uint64_t>(i));
                json.field("name", info.name);
                write_span(json, info.span);
                json.key("fields").begin_array();
                for (const FieldInfo& field : info.fields)
                {
                    json.begin_object();
                    json.field("name", field.name);
                    json.field("type", field.type.to_string(&language));
                    write_span(json, field.span);
                    json.end_object();
                }
                json.end_array();
                json.end_object();
            }
            json.end_array();
            json.key("functions").begin_array();
            for (std::size_t i = 0; i < model.functions.size(); ++i)
            {
                const FunctionInfo& function = model.functions[i];
                json.begin_object();
                json.field("index", static_cast<std::uint64_t>(i));
                json.field("name", function.name);
                json.key("parameters").begin_array();
                for (const Type& type : function.parameter_types)
                {
                    json.value(type.to_string(&language));
                }
                json.end_array();
                json.field("returns", function.return_type.to_string(&language));
                json.field("local_slots", static_cast<unsigned>(function.local_count));
                write_span(json, function.span);
                json.end_object();
            }
            json.end_array();
            json.field("main_function", model.main_function);
            json.end_object();
        }

        void write_opcodes(JsonWriter& json)
        {
            json.begin_array();
            for (std::size_t i = 0; i < bytecode::opcode_count(); ++i)
            {
                const bytecode::OpcodeInfo& op = bytecode::opcode_info(static_cast<bytecode::Opcode>(i));
                json.begin_object();
                json.field("code", static_cast<std::uint64_t>(i));
                json.field("mnemonic", op.mnemonic);
                json.field("operand_bytes", bytecode::operand_size(op.operand));
                json.field("stack_effect", op.stack_effect);
                json.field("description", op.description);
                json.end_object();
            }
            json.end_array();
        }

        void write_bytecode(JsonWriter& json, const bytecode::Program& program)
        {
            json.begin_object();
            json.key("constants").begin_array();
            for (std::size_t i = 0; i < program.constants.size(); ++i)
            {
                json.begin_object();
                json.field("index", static_cast<std::uint64_t>(i));
                json.field("type",
                    std::holds_alternative<std::int64_t>(program.constants[i])      ? "int"
                        : std::holds_alternative<double>(program.constants[i]) ? "float"
                                                                               : "string");
                json.field("value", bytecode::constant_to_display(program.constants[i]));
                json.end_object();
            }
            json.end_array();
            json.key("structs").begin_array();
            for (std::size_t i = 0; i < program.structs.size(); ++i)
            {
                const StructLayout& layout = program.structs[i];
                json.begin_object();
                json.field("index", static_cast<std::uint64_t>(i));
                json.field("name", layout.name);
                json.key("fields").begin_array();
                for (std::size_t f = 0; f < layout.field_names.size(); ++f)
                {
                    json.begin_object();
                    json.field("name", layout.field_names[f]);
                    json.field("type", layout.field_types[f]);
                    json.end_object();
                }
                json.end_array();
                json.end_object();
            }
            json.end_array();
            json.key("globals").begin_array();
            for (const std::string& name : program.globals)
            {
                json.value(name);
            }
            json.end_array();
            json.key("functions").begin_array();
            for (std::size_t f = 0; f < program.functions.size(); ++f)
            {
                const bytecode::Function& function = program.functions[f];
                json.begin_object();
                json.field("index", static_cast<std::uint64_t>(f));
                json.field("name", function.name);
                json.field("arity", static_cast<unsigned>(function.arity));
                json.field("returns_value", function.returns_value);
                json.key("locals").begin_array();
                for (const std::string& name : function.local_names)
                {
                    json.value(name);
                }
                json.end_array();
                json.field("code_size", static_cast<std::uint64_t>(function.code.size()));
                json.key("instructions").begin_array();
                for (const bytecode::DisassembledInstruction& line : bytecode::disassemble(program, function))
                {
                    json.begin_object();
                    json.field("offset", line.offset);
                    json.field("bytes", line.bytes);
                    json.field("mnemonic", line.mnemonic);
                    json.field("operands", line.operands);
                    json.field("comment", line.comment);
                    json.field("line", line.position.line);
                    json.field("column", line.position.column);
                    if (const std::uint16_t file = function.file_at(line.offset); file != 0)
                    {
                        json.field("file", static_cast<unsigned>(file));
                    }
                    json.end_object();
                }
                json.end_array();
                json.end_object();
            }
            json.end_array();
            json.field("listing", bytecode::disassemble_text(program));
            json.end_object();
        }

        std::string to_hex(std::span<const std::uint8_t> bytes)
        {
            std::string hex;
            hex.reserve(bytes.size() * 2);
            constexpr const char* digits = "0123456789abcdef";
            for (const std::uint8_t b : bytes)
            {
                hex += digits[b >> 4];
                hex += digits[b & 0xF];
            }
            return hex;
        }

        void write_frames_and_locals(JsonWriter& json, const Vm& vm)
        {
            const bytecode::Program& program = vm.program();
            json.key("frames").begin_array();
            for (const CallFrame& frame : vm.frames())
            {
                const bytecode::Function& function = program.functions[frame.function];
                json.begin_object();
                json.field("function", function.name);
                json.field("base", static_cast<std::uint64_t>(frame.base));
                json.field("locals", static_cast<unsigned>(function.local_count));
                json.end_object();
            }
            json.end_array();

            const CallFrame& top = vm.frames().back();
            const bytecode::Function& function = program.functions[top.function];
            json.key("locals").begin_array();
            for (std::size_t slot = 0; slot < function.local_count; ++slot)
            {
                json.begin_object();
                json.field("name", slot < function.local_names.size() ? function.local_names[slot] : std::string("?"));
                json.field("value", vm.stack()[top.base + slot].to_debug(vm.locale()));
                json.end_object();
            }
            json.end_array();

            json.key("globals").begin_array();
            for (std::size_t slot = 0; slot < vm.globals().size(); ++slot)
            {
                json.begin_object();
                json.field("name", program.globals[slot]);
                json.field("value", vm.globals()[slot].to_debug(vm.locale()));
                json.end_object();
            }
            json.end_array();
        }
    }

    std::string compile_report_json(const CompileResult& result, const SourceFile& source, bool pretty)
    {
        const LanguagePack& language = *result.language;
        JsonWriter json(pretty);
        json.begin_object();
        json.field("tool", "thunderc");
        json.field("version", THUNDER_VERSION);
        json.field("file", source.name());
        json.key("language").begin_object();
        json.field("code", language.code);
        json.field("name", language.native_name);
        json.field("right_to_left", language.right_to_left);
        json.end_object();
        json.field("success", result.succeeded());
        json.field("internal_error", result.internal_error);
        json.field("error_count", static_cast<std::uint64_t>(result.diagnostics.error_count()));
        json.field("warning_count", static_cast<std::uint64_t>(result.diagnostics.warning_count()));

        json.key("phases").begin_array();
        for (const PhaseTiming& timing : result.timings)
        {
            json.begin_object();
            json.field("name", timing.phase);
            json.field("ran", timing.ran);
            json.field("succeeded", timing.succeeded);
            json.field("microseconds", timing.microseconds);
            json.end_object();
        }
        json.end_array();

        json.key("diagnostics").begin_array();
        for (const Diagnostic& diagnostic : result.diagnostics.items())
        {
            write_diagnostic(json, diagnostic, diagnostic.span.file == 0 ? source.name() : result.file_name(diagnostic.span.file),
                language);
        }
        json.end_array();

        // The imported modules (file ids 1, 2, ...), so tools can show where a span is.
        json.key("modules").begin_array();
        for (std::size_t i = 0; i < result.modules.size(); ++i)
        {
            json.begin_object();
            json.field("file", static_cast<std::uint64_t>(i + 1));
            json.field("name", result.modules[i].source->name());
            json.field("path", result.modules[i].path);
            json.end_object();
        }
        json.end_array();

        json.key("tokens").begin_array();
        for (const Token& token : result.tokens)
        {
            json.begin_object();
            json.field("kind", token_kind_name(token.kind));
            json.field("category", token_category_name(token_category(token.kind)));
            json.field("lexeme", token.lexeme);
            write_span(json, token.span);
            json.end_object();
        }
        json.end_array();

        json.key("ast");
        if (result.ast)
        {
            write_ast(json, describe(*result.ast, &language), "");
        }
        else
        {
            json.null();
        }

        json.key("symbols");
        if (result.model)
        {
            write_symbols(json, *result.model, language);
        }
        else
        {
            json.null();
        }

        json.key("bytecode");
        if (result.program)
        {
            write_bytecode(json, *result.program);
        }
        else
        {
            json.null();
        }

        json.key("binary");
        if (result.program)
        {
            std::vector<bytecode::Section> sections;
            const std::vector<std::uint8_t> bytes = bytecode::serialize(*result.program, &sections);
            json.begin_object();
            json.field("size", static_cast<std::uint64_t>(bytes.size()));
            json.field("hex", to_hex(bytes));
            json.key("sections").begin_array();
            for (const bytecode::Section& section : sections)
            {
                json.begin_object();
                json.field("name", section.name);
                json.field("offset", section.offset);
                json.field("size", section.size);
                json.end_object();
            }
            json.end_array();
            json.end_object();
        }
        else
        {
            json.null();
        }

        json.end_object();
        return json.str();
    }

    std::string language_info_json(const LanguagePack& language, bool pretty)
    {
        JsonWriter json(pretty);
        json.begin_object();
        json.field("code", language.code);
        json.field("name", language.display_name);
        json.field("native_name", language.native_name);
        json.field("right_to_left", language.right_to_left);

        // Every accepted spelling, with the preferred one first ("text").
        json.key("keywords").begin_array();
        for (std::size_t i = 0; i < token_kind_count; ++i)
        {
            const auto kind = static_cast<TokenKind>(i);
            if (!is_word_token(kind))
            {
                continue;
            }
            json.begin_object();
            json.field("kind", token_kind_name(kind));
            json.field("category", token_category_name(token_category(kind)));
            json.field("text", language.spelling(kind));
            json.key("spellings").begin_array();
            std::vector<std::string> spellings;
            for (const auto& [text, candidate] : language.keywords)
            {
                if (candidate == kind)
                {
                    spellings.push_back(text);
                }
            }
            std::ranges::sort(spellings);
            for (const std::string& text : spellings)
            {
                json.value(text);
            }
            json.end_array();
            json.end_object();
        }
        json.end_array();

        json.key("builtins").begin_array();
        for (const BuiltinInfo& builtin : all_builtins())
        {
            json.begin_object();
            json.field("name", language.spelling(builtin.id));
            json.field("canonical_name", builtin.canonical_name);
            json.field("module", module_canonical_name(builtin.module)); // "" = always available
            json.field("signature", builtin.signature);
            json.field("description", language.term("builtin." + std::string(builtin.canonical_name)) ==
                        "builtin." + std::string(builtin.canonical_name)
                    ? std::string(builtin.description)
                    : language.term("builtin." + std::string(builtin.canonical_name)));
            json.end_object();
        }
        json.end_array();

        // The built-in modules ("import os"), under their name in this language.
        json.key("modules").begin_array();
        for (const BuiltinModule module : {BuiltinModule::Os, BuiltinModule::Graphics})
        {
            json.begin_object();
            json.field("name", language.spelling(module));
            json.field("canonical_name", module_canonical_name(module));
            json.end_object();
        }
        json.end_array();

        json.key("punctuation").begin_array();
        std::vector<std::pair<char32_t, TokenKind>> punctuation(language.punctuation.begin(), language.punctuation.end());
        std::ranges::sort(punctuation);
        for (const auto& [character, kind] : punctuation)
        {
            std::string text;
            utf8::append(text, character);
            json.begin_object();
            json.field("text", text);
            json.field("kind", token_kind_name(kind));
            json.field("means", token_canonical_spelling(kind));
            json.end_object();
        }
        json.end_array();

        json.key("digits").begin_array();
        for (const char32_t digit : language.digits)
        {
            std::string text;
            utf8::append(text, digit);
            json.value(text);
        }
        json.end_array();

        json.key("quotes").begin_array();
        for (const QuotePair& pair : language.quotes)
        {
            std::string open;
            std::string close;
            utf8::append(open, pair.open);
            utf8::append(close, pair.close);
            json.begin_array().value(open).value(close).end_array();
        }
        json.end_array();

        json.key("display").begin_object();
        json.field("true", language.display.true_text);
        json.field("false", language.display.false_text);
        json.field("separator", language.display.separator);
        json.end_object();

        json.key("opcodes");
        write_opcodes(json);

        json.key("languages").begin_array();
        for (const LanguagePack* pack : available_language_packs())
        {
            json.begin_object();
            json.field("code", pack->code);
            json.field("name", pack->display_name);
            json.field("native_name", pack->native_name);
            json.field("right_to_left", pack->right_to_left);
            json.end_object();
        }
        json.end_array();

        json.end_object();
        return json.str();
    }

    std::string tokens_text(const CompileResult& result)
    {
        std::string out = std::format("{:<9} {:<14} {:<12} {}\n", "LINE:COL", "KIND", "CATEGORY", "LEXEME");
        for (const Token& token : result.tokens)
        {
            const std::string position = std::format("{}:{}", token.span.start.line, token.span.start.column);
            out += std::format("{:<9} {:<14} {:<12} {}\n", position, token_kind_name(token.kind),
                token_category_name(token_category(token.kind)), token.lexeme);
        }
        return out;
    }

    namespace
    {
        void append_scope_text(std::string& out, const ScopeInfo& scope, int depth, const LanguagePack& language)
        {
            const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
            out += indent + "scope " + scope.kind;
            if (!scope.name.empty())
            {
                out += " " + scope.name;
            }
            out += std::format(" (lines {}-{})\n", scope.span.start.line, scope.span.end.line);
            for (const Symbol* symbol : scope.symbols)
            {
                out += std::format("{}  {:<10} {:<12} {:<10} slot {}{}\n", indent, symbol_kind_name(symbol->kind),
                    symbol->name, symbol->type.to_string(&language), symbol->slot, symbol->is_const ? "  (const)" : "");
            }
            for (const ScopeInfo& child : scope.children)
            {
                append_scope_text(out, child, depth + 1, language);
            }
        }
    }

    std::string symbols_text(const SemanticModel& model, const LanguagePack& language)
    {
        std::string out = "functions:\n";
        for (std::size_t i = 0; i < model.functions.size(); ++i)
        {
            const FunctionInfo& function = model.functions[i];
            std::string params;
            for (const Type& type : function.parameter_types)
            {
                params += (params.empty() ? "" : ", ") + type.to_string(&language);
            }
            out += std::format("  #{} {}({}): {}  [{} local slot(s)]\n", i, function.name, params,
                function.return_type.to_string(&language), function.local_count);
        }
        if (!model.structs.empty())
        {
            out += "structs:\n";
            for (const auto& info : model.structs)
            {
                std::string fields;
                for (const FieldInfo& field : info->fields)
                {
                    fields += (fields.empty() ? "" : ", ") + field.name + ": " + field.type.to_string(&language);
                }
                out += std::format("  {} {{ {} }}\n", info->name, fields);
            }
        }
        append_scope_text(out, model.global_scope, 0, language);
        return out;
    }

    std::string hex_dump_text(std::span<const std::uint8_t> bytes, const std::vector<bytecode::Section>& sections)
    {
        std::string out;
        for (const bytecode::Section& section : sections)
        {
            out += std::format("; {} ({} bytes)\n", section.name, section.size);
            for (std::uint32_t row = 0; row < section.size; row += 16)
            {
                const std::uint32_t start = section.offset + row;
                out += std::format("{:08x}  ", start);
                std::string ascii;
                for (std::uint32_t i = 0; i < 16; ++i)
                {
                    if (row + i < section.size)
                    {
                        const std::uint8_t b = bytes[start + i];
                        out += std::format("{:02x} ", b);
                        ascii += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
                    }
                    else
                    {
                        out += "   ";
                    }
                }
                out += " " + ascii + "\n";
            }
        }
        return out;
    }

    std::string format_runtime_error(const RuntimeError& error, std::string_view file_name, const LanguagePack& language)
    {
        // Code from an imported module is reported with the module's file name.
        const std::string_view where = error.file.empty() ? file_name : std::string_view(error.file);
        std::string out = std::format("{}:{}:{}: {} {} [{}]: {}", where, error.position.line, error.position.column,
            language.phase_name(Phase::Runtime), language.severity_name(Severity::Error), error.code, error.message);
        for (const RuntimeError::Frame& frame : error.call_stack)
        {
            const std::string line = language.display.format_int(frame.position.line);
            // The top-level code is shown as <main> (<اصلي> in Pashto).
            const std::string name = frame.function == "<main>" ? "<" + language.main_name + ">" : frame.function;
            const std::string function = frame.file.empty() ? name : name + " [" + frame.file + "]";
            out += "\n    " + std::vformat(language.term("in {} (line {})"), std::make_format_args(function, line));
        }
        return out;
    }

    void write_runtime_error_json(JsonWriter& json, const RuntimeError& error)
    {
        json.begin_object();
        json.field("code", error.code);
        json.field("message", error.message);
        json.field("line", error.position.line);
        json.field("column", error.position.column);
        json.field("file_name", error.file);
        json.key("call_stack").begin_array();
        for (const RuntimeError::Frame& frame : error.call_stack)
        {
            json.begin_object();
            json.field("function", frame.function);
            json.field("line", frame.position.line);
            json.field("column", frame.position.column);
            json.field("file_name", frame.file);
            json.end_object();
        }
        json.end_array();
        json.end_object();
    }

    void write_stats_json(JsonWriter& json, const ExecStats& stats)
    {
        json.begin_object();
        json.field("steps", stats.steps);
        json.field("calls", stats.calls);
        json.field("max_stack_depth", static_cast<std::uint64_t>(stats.max_stack_depth));
        json.field("max_call_depth", static_cast<std::uint64_t>(stats.max_call_depth));
        json.field("frames", stats.frames);
        json.end_object();
    }

    // ------------------------------------------------------------------ tracing

    TraceRecorder::TraceRecorder(std::uint64_t max_steps)
        : max_steps_(max_steps)
    {
        steps_.begin_array();
        output_.begin_array();
    }

    void TraceRecorder::before_instruction(const Vm& vm, const bytecode::Instruction& instruction)
    {
        ++seen_;
        if (recorded_ >= max_steps_)
        {
            truncated_ = true;
            return;
        }
        const CallFrame& frame = vm.frames().back();
        const bytecode::Function& function = vm.program().functions[frame.function];
        const Position position = function.position_at(instruction.offset);

        steps_.begin_object();
        steps_.field("step", recorded_);
        steps_.field("function", function.name);
        steps_.field("offset", instruction.offset);
        steps_.field("line", position.line);
        steps_.field("column", position.column);
        if (const std::uint16_t file = function.file_at(instruction.offset); file != 0)
        {
            steps_.field("file", static_cast<unsigned>(file));
            steps_.field("file_name", vm.program().file_name(file));
        }
        steps_.field("instruction", bytecode::instruction_text(vm.program(), function, instruction));
        steps_.field("mnemonic", bytecode::opcode_info(instruction.op).mnemonic);
        steps_.key("stack").begin_array();
        for (const Value& value : vm.stack())
        {
            steps_.value(value.to_debug(vm.locale()));
        }
        steps_.end_array();
        write_frames_and_locals(steps_, vm);
        steps_.end_object();
        ++recorded_;
    }

    void TraceRecorder::on_output(std::string_view text)
    {
        // The output is attributed to the instruction that produced it (the last one seen).
        output_.begin_object();
        output_.field("step", seen_ == 0 ? 0 : seen_ - 1);
        output_.field("text", text);
        output_.end_object();
    }

    std::string TraceRecorder::finish(const ExecResult& result)
    {
        steps_.end_array();
        output_.end_array();

        JsonWriter json;
        json.begin_object();
        json.field("tool", "thundervm");
        json.field("version", THUNDER_VERSION);
        json.field("ok", result.ok);
        json.field("exit_code", static_cast<std::int64_t>(result.exit_code));
        json.field("truncated", truncated_);
        json.field("recorded_steps", recorded_);
        json.key("stats");
        write_stats_json(json, result.stats);
        json.key("error");
        if (result.error)
        {
            write_runtime_error_json(json, *result.error);
        }
        else
        {
            json.null();
        }
        json.end_object();
        std::string document = json.str();
        document.pop_back(); // reopen the object (drop its '}') to splice in the pre-rendered arrays
        document += ",\"output\":" + output_.str() + ",\"steps\":" + steps_.str() + "}";
        return document;
    }

    TextTracer::TextTracer(std::ostream& out)
        : out_(out)
    {
    }

    void TextTracer::before_instruction(const Vm& vm, const bytecode::Instruction& instruction)
    {
        const CallFrame& frame = vm.frames().back();
        const bytecode::Function& function = vm.program().functions[frame.function];
        std::string stack;
        for (std::size_t i = frame.base + function.local_count; i < vm.stack().size(); ++i)
        {
            stack += (stack.empty() ? "" : ", ") + vm.stack()[i].to_debug(vm.locale());
        }
        out_ << std::format("[{:>6}] {:>10}+{:04}  {:<36} stack: [{}]\n", vm.steps(), function.name, instruction.offset,
            bytecode::instruction_text(vm.program(), function, instruction), stack);
    }

    void TextTracer::on_output(std::string_view text)
    {
        (void)text; // program output already goes to standard output
    }
}
