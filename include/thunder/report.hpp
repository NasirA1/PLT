// Machine-readable (JSON) and human-readable reports of compilation and execution.
// The IDE is driven entirely by these reports.
#pragma once

#include "thunder/compiler.hpp"
#include "thunder/json.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/source.hpp"
#include "thunder/vm.hpp"

#include <cstdint>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace thunder
{
    // Everything the compiler produced: diagnostics, tokens, AST, symbols, bytecode and a hex dump.
    std::string compile_report_json(const CompileResult& result, const SourceFile& source, bool pretty = false);

    // Keywords, built-ins and the instruction set, so tools never duplicate compiler knowledge.
    std::string language_info_json(const LanguagePack& language, bool pretty = false);

    // Text forms of phase artefacts for the command line.
    std::string tokens_text(const CompileResult& result);
    std::string symbols_text(const SemanticModel& model, const LanguagePack& language = english_pack());
    std::string hex_dump_text(std::span<const std::uint8_t> bytes, const std::vector<bytecode::Section>& sections);

    // "file:line:col: runtime error [R001]: message" plus a call-stack trace.
    std::string format_runtime_error(const RuntimeError& error, std::string_view file_name,
        const LanguagePack& language = english_pack());

    void write_runtime_error_json(JsonWriter& json, const RuntimeError& error);
    void write_stats_json(JsonWriter& json, const ExecStats& stats);

    // Records every executed instruction (up to a limit) with the machine state, for the
    // IDE's step-by-step debugger. Execution continues after the limit; only recording stops.
    class TraceRecorder final : public VmObserver
    {
    public:
        explicit TraceRecorder(std::uint64_t max_steps);

        void before_instruction(const Vm& vm, const bytecode::Instruction& instruction) override;
        void on_output(std::string_view text) override;

        // Completes the JSON document with the execution result.
        std::string finish(const ExecResult& result);

        std::uint64_t recorded_steps() const { return recorded_; }

    private:
        JsonWriter steps_;
        JsonWriter output_;
        std::uint64_t max_steps_;
        std::uint64_t recorded_ = 0;
        std::uint64_t seen_ = 0;
        bool truncated_ = false;
    };

    // Prints each executed instruction and the operand stack (thundervm --trace).
    class TextTracer final : public VmObserver
    {
    public:
        explicit TextTracer(std::ostream& out);

        void before_instruction(const Vm& vm, const bytecode::Instruction& instruction) override;
        void on_output(std::string_view text) override;

    private:
        std::ostream& out_;
    };
}
