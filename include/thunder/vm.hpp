// The Thunder virtual machine: a stack machine that executes verified bytecode.
#pragma once

#include "thunder/bytecode.hpp"
#include "thunder/diagnostics.hpp"
#include "thunder/value.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace thunder
{
    struct VmOptions
    {
        std::uint64_t max_steps = 0;          // 0 = unlimited; otherwise stop after this many instructions
        std::size_t max_call_depth = 10'000;  // nested calls before "stack overflow"
        std::size_t max_stack = 4'000'000;    // values on the operand stack
        std::size_t max_array_size = 10'000'000;
    };

    // One activation record. Its local variables live on the operand stack starting at 'base'
    // (parameters first); the values above them are the frame's temporary operands.
    struct CallFrame
    {
        std::uint16_t function = 0;
        std::uint32_t ip = 0; // offset of the next instruction
        std::size_t base = 0;
    };

    struct RuntimeError
    {
        struct Frame
        {
            std::string function;
            Position position;
        };

        DiagId id = DiagId::InvalidBytecode;
        std::string code;    // e.g. "R001"
        std::string message;
        Position position;   // where the failing instruction came from
        std::vector<Frame> call_stack; // innermost call first
    };

    struct ExecStats
    {
        std::uint64_t steps = 0; // instructions executed
        std::uint64_t calls = 0; // function calls (excluding built-ins)
        std::size_t max_stack_depth = 0;
        std::size_t max_call_depth = 0;
    };

    struct ExecResult
    {
        bool ok = true;
        std::optional<RuntimeError> error;
        ExecStats stats;
    };

    class Vm;

    // Lets tools (tracers, debuggers) watch execution.
    class VmObserver
    {
    public:
        virtual ~VmObserver() = default;
        // Called before each instruction executes; frames().back().ip is the instruction's offset.
        virtual void before_instruction(const Vm& vm, const bytecode::Instruction& instruction) = 0;
        // Called whenever the program writes output.
        virtual void on_output(std::string_view text) = 0;
    };

    class Vm
    {
    public:
        Vm(const bytecode::Program& program, std::ostream& output, VmOptions options = {});

        ExecResult run(VmObserver* observer = nullptr);

        // Introspection (meaningful during observer callbacks and after run()).
        const bytecode::Program& program() const { return program_; }
        const std::vector<Value>& stack() const { return stack_; }
        const std::vector<CallFrame>& frames() const { return frames_; }
        const std::vector<Value>& globals() const { return globals_; }
        std::uint64_t steps() const { return stats_.steps; }
        const LanguagePack& language() const { return *language_; }
        const DisplayLocale& locale() const { return language_->display; }

    private:
        struct Failure
        {
            DiagId id;
            std::string message;
        };

        void execute(VmObserver* observer);
        void push_constant(std::uint32_t index);
        void arithmetic(bytecode::Opcode op);
        void division(bytecode::Opcode op);
        void comparison(bytecode::Opcode op);
        bool return_from_function(bool has_value); // false when the entry function returned
        void make_array(std::uint32_t count);
        void make_tuple(std::uint32_t count);
        void make_map(std::uint32_t count);
        void tuple_get(std::uint32_t index);
        std::vector<Value> pop_values(std::size_t count);
        void make_struct(std::uint32_t index);
        void get_field(std::uint32_t index);
        void set_field(std::uint32_t index);
        std::size_t operand_floor() const; // stack index of the current frame's first operand
        void index_get();
        void index_set();
        void call_builtin(std::uint8_t id, std::uint32_t argument_count, VmObserver* observer);
        void call_function(std::uint16_t index);

        Value pop();
        std::int64_t pop_int(std::string_view instruction);
        bool pop_bool(std::string_view instruction);
        void push(Value value);
        void write_output(const std::string& text, bool flush, VmObserver* observer);

        template <typename... Args>
        [[noreturn]] void fail(DiagId id, const Args&... args)
        {
            throw Failure{id, language_->format(id, std::vector<std::string>{to_argument(args)...})};
        }

        RuntimeError make_error(const Failure& failure) const;

        const bytecode::Program& program_;
        const LanguagePack* language_; // from the program; decides how values and messages are shown
        std::ostream& output_;
        VmOptions options_;
        std::vector<Value> stack_;
        std::vector<CallFrame> frames_;
        std::vector<Value> globals_;
        std::uint32_t current_offset_ = 0;
        ExecStats stats_;
    };

    // Overflow-checked integer arithmetic (no undefined behaviour). Return nullopt on overflow.
    std::optional<std::int64_t> checked_add(std::int64_t a, std::int64_t b);
    std::optional<std::int64_t> checked_sub(std::int64_t a, std::int64_t b);
    std::optional<std::int64_t> checked_mul(std::int64_t a, std::int64_t b);

    // Parses a decimal integer with an optional sign, ignoring surrounding spaces.
    std::optional<std::int64_t> parse_int(std::string_view text);
}
