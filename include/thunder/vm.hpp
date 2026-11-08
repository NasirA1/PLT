// The Thunder virtual machine: a stack machine that executes verified bytecode.
#pragma once

#include "thunder/builtins.hpp"
#include "thunder/bytecode.hpp"
#include "thunder/diagnostics.hpp"
#include "thunder/graphics.hpp"
#include "thunder/value.hpp"

#include "thunder/native.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace thunder
{
    class Vm;
}

// The running program, as a native function sees it (see include/thunder/native.h).
struct ThunderEnv
{
    thunder::Vm* vm = nullptr;
};

namespace thunder
{
    struct VmOptions
    {
        std::uint64_t max_steps = 0;          // 0 = unlimited; otherwise stop after this many instructions
        std::size_t max_call_depth = 10'000;  // nested calls before "stack overflow"
        std::size_t max_stack = 4'000'000;    // values on the operand stack
        std::size_t max_array_size = 10'000'000;
        // Graphics: stop the program normally after this many frames have been shown (0 = never).
        // Used to run animations headless, e.g. in tests. Once a program shows frames, max_steps
        // limits the instructions *between two frames*, so an animation may run for ever.
        std::uint64_t max_frames = 0;
        // Seed of random_int (unset: a different sequence on every run).
        std::optional<std::uint64_t> random_seed;
        // Where "import native" looks for libraries, before the platform's own search. The
        // program's own directory belongs at the front (thundervm puts it there).
        std::vector<std::string> native_paths;
        // false forbids "import native" altogether: the program stops with R028 instead of
        // loading any C++ code.
        bool allow_native = true;
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
            std::string file; // the module the position is in ("" for the program's own file)
        };

        DiagId id = DiagId::InvalidBytecode;
        std::string code;    // e.g. "R001"
        std::string message;
        Position position;   // where the failing instruction came from
        std::string file;    // the module 'position' is in ("" for the program's own file)
        std::vector<Frame> call_stack; // innermost call first
    };

    struct ExecStats
    {
        std::uint64_t steps = 0; // instructions executed
        std::uint64_t calls = 0; // function calls (excluding built-ins)
        std::size_t max_stack_depth = 0;
        std::size_t max_call_depth = 0;
        std::uint64_t frames = 0; // graphics frames shown
    };

    struct ExecResult
    {
        bool ok = true;
        int exit_code = 0; // set by exit(code) (module os); 0 if the program ran to its end
        bool frame_limit_reached = false; // stopped after VmOptions::max_frames frames (not an error)
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
        // 'input' is what input() reads (null: the program gets no input at all).
        Vm(const bytecode::Program& program, std::ostream& output, VmOptions options = {}, std::istream* input = nullptr);

        // The command-line arguments returned by args() (module os).
        void set_arguments(std::vector<std::string> arguments) { arguments_ = std::move(arguments); }
        // Where frames are shown (module graphics); null: nowhere, the picture is only kept.
        void set_graphics_host(graphics::GraphicsHost* host) { host_ = host; }
        // Replaces the input stream for input() (used when input lines share it with events).
        void set_line_source(graphics::LineSource* source) { line_source_ = source; }
        // The picture the program drew (after run(): its final state).
        const graphics::Canvas& canvas() const { return canvas_; }

        ExecResult run(VmObserver* observer = nullptr);

        // Used by the C API a native module sees (include/thunder/native.h); not for other callers.
        void native_fail(const std::string& message);
        bool native_has_failed() const;
        Value native_callback(std::int32_t slot, const std::vector<Value>& arguments);

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

        struct ExitRequest // thrown by exit(code)
        {
            int code;
        };

        struct FrameLimit // thrown when VmOptions::max_frames frames have been shown
        {
        };

        // Runs instructions until the number of frames drops to 'stop_depth' (0: the program ends).
        void execute(VmObserver* observer, std::size_t stop_depth = 0);
        // Calls a function value with the given arguments from inside a built-in (map, filter, ...)
        // and returns its result (void for a function without one).
        Value invoke(const Value& function, std::vector<Value> arguments, VmObserver* observer);
        void call_value(std::uint32_t argument_count);
        void push_constant(std::uint32_t index);
        void arithmetic(bytecode::Opcode op);
        void float_arithmetic(bytecode::Opcode op);
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
        // The C++ native interface (vm_native.cpp).
        void load_native_libraries();                 // once, before the program starts
        void call_native(std::uint32_t index, VmObserver* observer);
        // The groups of built-ins (vm_builtins.cpp). Each returns false if 'id' is not one of its own.
        bool text_builtin(BuiltinId id, std::vector<Value>& args);
        bool list_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer);
        bool number_builtin(BuiltinId id, std::vector<Value>& args);
        bool os_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer);
        bool graphics_builtin(BuiltinId id, std::vector<Value>& args); // vm_graphics.cpp
        void present(bool wait);  // shows the canvas (frame() / show()); waits for the next frame if asked
        std::string color_argument(const Value& value);
        std::int64_t random_between(std::int64_t low, std::int64_t high);
        std::string read_line(); // the next input line without its line break ("" at the end)
        // The range start..end of a sequence of 'length' items, or runtime error R012.
        std::pair<std::size_t, std::size_t> checked_range(std::int64_t start, std::int64_t end, std::size_t length);
        void call_function(std::uint16_t index);

        Value pop();
        std::int64_t pop_int(std::string_view instruction);
        double pop_float(std::string_view instruction); // an int operand is widened
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
        std::istream* input_ = nullptr;
        std::vector<std::string> arguments_;
        std::chrono::steady_clock::time_point started_;
        graphics::Canvas canvas_;
        graphics::GraphicsHost* host_ = nullptr;
        graphics::LineSource* line_source_ = nullptr;
        std::chrono::steady_clock::time_point next_frame_;
        std::uint64_t steps_at_frame_ = 0; // instructions executed when the last frame was shown
        std::uint64_t random_state_ = 0;
        std::vector<Value> stack_;
        std::vector<CallFrame> frames_;
        std::vector<Value> globals_;
        std::uint32_t current_offset_ = 0;
        ExecStats stats_;
        std::unique_ptr<NativeModules> natives_;             // the loaded libraries (null until needed)
        std::vector<ThunderFunction> native_functions_;      // one per entry of Program::natives
        VmObserver* native_observer_ = nullptr;              // the observer during a native call
        std::vector<Value> native_callbacks_;                // function values a native call may use
        std::optional<Failure> native_failure_;              // set by thunder_fail / a failed callback
    };

    // Overflow-checked integer arithmetic (no undefined behaviour). Return nullopt on overflow.
    std::optional<std::int64_t> checked_add(std::int64_t a, std::int64_t b);
    std::optional<std::int64_t> checked_sub(std::int64_t a, std::int64_t b);
    std::optional<std::int64_t> checked_mul(std::int64_t a, std::int64_t b);

    // Parses a decimal integer with an optional sign, ignoring surrounding spaces.
    std::optional<std::int64_t> parse_int(std::string_view text);
    // The number a text denotes, in any supported script ("2.5", "−۲٫۵"), or nullopt.
    std::optional<double> parse_float(std::string_view text);
}
