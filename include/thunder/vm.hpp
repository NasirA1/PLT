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
#include <unordered_set>
#include <optional>
#include <ostream>
#include <format>
#include <span>
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
        // A closure's captured boxes, handed over with the function value (LOAD_CAPTURE reads them).
        std::shared_ptr<std::vector<Value>> captures;
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
        bool aborted = false;             // an observer ended the program early (a debugger's "terminate")
        std::optional<RuntimeError> error;
        ExecStats stats;
    };

    class Vm;

    // Thrown by an observer to end the program at once (a debugger's "terminate"); run() reports
    // it as ExecResult::aborted, not as an error.
    struct ExecutionAborted
    {
    };

    // Lets tools (tracers, debuggers) watch execution.
    class VmObserver
    {
    public:
        virtual ~VmObserver() = default;
        // Called before each instruction executes; frames().back().ip is the instruction's offset.
        // May throw ExecutionAborted to stop the program.
        virtual void before_instruction(const Vm& vm, const bytecode::Instruction& instruction) = 0;
        // Called whenever the program writes output.
        virtual void on_output(std::string_view text) = 0;
    };

    class Vm
    {
    public:
        // 'input' is what input() reads (null: the program gets no input at all).
        Vm(const bytecode::Program& program, std::ostream& output, VmOptions options = {}, std::istream* input = nullptr);
        // Recursive structs may form cycles of references, which reference counting alone
        // would leak: the registry of every struct the program made is swept on shutdown.
        ~Vm();

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
        // The instruction groups execute() delegates to; each returns false if 'op' is not its own.
        bool execute_data(bytecode::Opcode op, std::uint32_t a);
        bool execute_arithmetic(bytecode::Opcode op);
        bool execute_aggregate(bytecode::Opcode op, std::uint32_t a);
        // Calls a function value with the given arguments from inside a built-in (map, filter, ...)
        // and returns its result (void for a function without one).
        Value invoke(const Value& function, std::vector<Value> arguments, VmObserver* observer);
        void call_value(std::uint32_t argument_count);
        // CALL_METHOD: the receiver sits below the arguments; 'name_constant' names the member.
        void call_method(std::uint32_t name_constant, std::uint32_t argument_count);
        void make_closure(std::uint32_t function, std::uint32_t capture_count);
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
        // The core built-ins (vm.cpp).
        bool value_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer);
        void print_values(const std::vector<Value>& args, bool newline, VmObserver* observer);
        std::int64_t length_of(const Value& value);
        void map_builtin(BuiltinId id, std::vector<Value>& args);
        static std::string hex_or_bin(std::int64_t value, bool hex);
        Value to_int(const Value& value);
        bool bytes_builtin(BuiltinId id, std::vector<Value>& args);
        Value bytes_value(std::string_view data);                    // a byte[] from raw bytes
        std::string raw_bytes(const Value& array, const char* what); // the raw bytes of a byte[]
        void enum_from(std::uint32_t enum_index);
        void enum_values(std::uint32_t enum_index);
        Value array_of_copies(std::int64_t size, const Value& value);
        // The C++ native interface (vm_native.cpp).
        void load_native_libraries();                 // once, before the program starts
        void call_native(std::uint32_t index, VmObserver* observer);
        // The groups of built-ins (vm_builtins.cpp). Each returns false if 'id' is not one of its own.
        bool text_builtin(BuiltinId id, std::vector<Value>& args);
        bool list_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer);
        bool sequence_builtin(BuiltinId id, std::vector<Value>& args);
        bool array_builtin(BuiltinId id, std::vector<Value>& args);
        bool functional_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer);
        bool number_builtin(BuiltinId id, std::vector<Value>& args);
        bool maths_builtin(BuiltinId id, std::vector<Value>& args);
        bool os_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer);
        bool file_builtin(BuiltinId id, std::vector<Value>& args);
        bool graphics_builtin(BuiltinId id, std::vector<Value>& args); // vm_graphics.cpp
        bool drawing_builtin(BuiltinId id, std::vector<Value>& args);  // shapes and canvas settings
        bool event_builtin(BuiltinId id, std::vector<Value>& args);    // keys and the mouse
        // The arguments of a built-in: the checker guarantees their kinds, so another kind is
        // invalid bytecode.
        const std::string& string_arg(const std::vector<Value>& args, std::size_t i);
        std::int64_t int_arg(const std::vector<Value>& args, std::size_t i);
        double number_arg(const std::vector<Value>& args, std::size_t i);
        std::vector<Value>& array_arg(std::vector<Value>& args, std::size_t i);
        MapObject& map_arg(const std::vector<Value>& args, std::size_t i);
        const Value& tuple_element_arg(const std::vector<Value>& args, std::size_t i, std::size_t element);
        void check_array_size(std::uint64_t size);
        std::size_t checked_index(std::int64_t index, std::size_t size);
        // The work of individual built-ins.
        static std::string trimmed(const std::string& text);
        static std::string ascii_case(std::string text, bool upper);
        static Value split_text(const std::string& source, const std::string& separator);
        static std::string replace_text(const std::string& source, const std::string& old_text, const std::string& new_text);
        static Value strings_value(const std::vector<std::string>& items);
        std::string join_strings(std::vector<Value>& args);
        std::string repeat_text(const std::string& part, std::int64_t count);
        std::int64_t find_in_sequence(std::vector<Value>& args);
        Value reversed(std::vector<Value>& args);
        Value sliced(std::vector<Value>& args);
        Value range_of(std::int64_t start, std::int64_t end);
        Value sum_of(const std::vector<Value>& elements);
        double parse_float_arg(const std::string& text);
        Value absolute(const Value& value);
        Value whole_number(double value);
        Value run_command(const std::string& command, bool echo_output, VmObserver* observer);
        void present(bool wait);  // shows the canvas (frame() / show()); waits for the next frame if asked
        // A setting such as the canvas width must lie in a range (R022).
        std::int64_t int_within(const std::vector<Value>& args, std::size_t i, BuiltinId id, const char* what,
            std::int64_t low, std::int64_t high);
        double number_within(const std::vector<Value>& args, std::size_t i, BuiltinId id, const char* what,
            double low, double high);
        void add_shape(graphics::Shape shape);
        graphics::Events* events() const;
        std::string color_argument(const Value& value);
        std::int64_t random_between(std::int64_t low, std::int64_t high);
        std::string read_line(); // the next input line without its line break ("" at the end)
        // The range start..end of a sequence of 'length' items, or runtime error R012.
        std::pair<std::size_t, std::size_t> checked_range(std::int64_t start, std::int64_t end, std::size_t length);
        void call_function(std::uint16_t index);

        Value pop();
        std::int64_t pop_int(std::string_view instruction);
        void bitwise(bytecode::Opcode op);
        std::string format_values(const std::string& template_text, std::span<const Value> values);
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
        std::vector<std::weak_ptr<StructObject>> struct_registry_; // to break reference cycles at the end
        void register_structs(const Value& value, std::unordered_set<const void*>& visited);
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
