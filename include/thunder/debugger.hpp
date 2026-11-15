// The live debugger: stops the virtual machine at breakpoints and steps, and answers questions
// about its state over a channel of JSON lines (thundervm --debug; the protocol is described in
// docs/debugging.md).
//
// The debugger is a VmObserver: it sees every instruction before it runs and, when the program
// should stop there, reads commands from the channel until one of them resumes it. While the
// program runs it also keeps a bounded history of the lines it executed - the call stack and the
// variables at each - so a client can step *backwards* as well as forwards.
#pragma once

#include "thunder/bytecode.hpp"
#include "thunder/graphics.hpp"
#include "thunder/json.hpp"
#include "thunder/vm.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace thunder
{
    // The two ends of the conversation: lines the client sends and lines the debugger answers.
    class DebugChannel
    {
    public:
        virtual ~DebugChannel() = default;
        // The next line from the client; waits for it. nullopt once the client has gone.
        virtual std::optional<std::string> read_line() = 0;
        // True if a line is waiting. Called between instructions, so it must be cheap.
        virtual bool has_line() = 0;
        virtual void write_line(std::string_view line) = 0;
    };

    // A channel over an output stream and a line reader, read on a background thread so that a
    // running program can be paused. The reader returns false at the end of the input.
    class StreamChannel final : public DebugChannel
    {
    public:
        using LineReader = std::function<bool(std::string& line)>;
        StreamChannel(LineReader next_line, std::ostream& out);
        ~StreamChannel() override;
        StreamChannel(const StreamChannel&) = delete;
        StreamChannel& operator=(const StreamChannel&) = delete;

        std::optional<std::string> read_line() override;
        bool has_line() override;
        void write_line(std::string_view line) override;

    private:
        struct State
        {
            std::mutex mutex;
            std::condition_variable ready;
            std::deque<std::string> lines;
            std::atomic<std::size_t> pending{0};
            bool finished = false;
        };
        static void run(const LineReader& next_line, const std::shared_ptr<State>& state);

        std::shared_ptr<State> state_;
        std::ostream& out_;
        std::thread thread_;
    };

    struct DebuggerOptions
    {
        // Lines kept for stepping back (0: no history, so no stepping back). Each entry holds the
        // call stack and every variable as text, so the limit bounds memory, not time.
        std::size_t history_limit = 10'000;
    };

    class Debugger final : public VmObserver, public graphics::LineSource
    {
    public:
        Debugger(DebugChannel& channel, const bytecode::Program& program, DebuggerOptions options = {});

        // VmObserver: every instruction, and every piece of output.
        void before_instruction(const Vm& vm, const bytecode::Instruction& instruction) override;
        void on_output(std::string_view text) override;

        // LineSource: input() asks the client for a line (an "input" event, answered by an
        // "input" command) instead of reading the stream the protocol travels on.
        std::optional<std::string> read_line() override;
        bool at_end() override;

        // Reports how the program ended. A runtime error is reported as a stop first, so the
        // client can inspect the state the program failed in before the "exited" event.
        void finish(const Vm& vm, const ExecResult& result);

    private:
        enum class Mode
        {
            Run,      // until a breakpoint
            StepIn,   // until the next line, wherever it is
            StepOver, // until the next line at this depth or above
            StepOut,  // until the calling function
            Pause,    // the client asked to stop: at the next instruction
        };

        struct NamedValue
        {
            std::string name;
            std::string value;
            std::string type;
            bool expandable = false; // has elements or fields of its own (live values only)
        };

        struct SnapshotFrame
        {
            std::string function;
            std::uint16_t file = 0;
            Position position;
            std::vector<NamedValue> locals;
        };

        // The state of the machine at one executed line, as the history keeps it.
        struct Snapshot
        {
            std::uint64_t step = 0;
            std::vector<SnapshotFrame> frames; // innermost first
            std::vector<NamedValue> globals;
            std::vector<std::string> stack; // the operands of the innermost frame
        };

        struct Breakpoint
        {
            std::uint32_t line = 0;
            std::string condition;
            std::string hit_condition;
            std::uint64_t hits = 0;
        };

        struct Location
        {
            std::uint16_t file = 0;
            std::uint32_t line = 0;

            friend bool operator==(const Location&, const Location&) = default;
        };

        // A frame as the client sees it, live or from a snapshot.
        struct FrameView
        {
            std::string function;
            std::uint16_t file = 0;
            Position position;
        };

        // What a request asks the program to do next.
        enum class Action
        {
            Stay,      // answered; the program stays as it is
            Resume,    // run on (the mode says how far)
            Terminate, // end the program now
        };

        // ---------------------------------------------------------------- deciding when to stop
        bool is_line_start(std::size_t depth, const Location& location);
        // Set when a breakpoint on this line fires; the text is empty unless something is worth
        // telling the client (a condition that could not be evaluated).
        std::optional<std::string> breakpoint_hit(const Vm& vm, const Location& location);
        static bool hit_condition_met(std::string_view condition, std::uint64_t hits);
        void stop(const Vm& vm, std::string_view reason, std::string_view description = {}, std::string_view text = {});

        // ---------------------------------------------------------------- the conversation
        // Reads commands until one resumes the program (or ends it, which throws ExecutionAborted
        // while the program runs and returns once it has finished).
        void command_loop(const Vm& vm);
        Action handle(const Vm& vm, const JsonValue& request, bool running);
        void handle_pending(const Vm& vm);
        Action resume(const Vm& vm, const JsonValue& request);
        // True when a stepping command was satisfied inside the history (a stop was reported).
        bool step_in_history(std::string_view command);
        void send_event(std::string_view name, const std::function<void(JsonWriter&)>& body = {});
        void respond(const JsonValue& request, const std::function<void(JsonWriter&)>& body = {});
        void respond_error(const JsonValue& request, std::string_view message);
        void send_stopped(const Vm& vm, std::string_view reason, std::string_view description = {}, std::string_view text = {});

        void set_breakpoints(const Vm& vm, const JsonValue& request);
        void stack_trace(const Vm& vm, const JsonValue& request);
        void variables(const Vm& vm, const JsonValue& request);
        void evaluate(const Vm& vm, const JsonValue& request);

        // ---------------------------------------------------------------- the state as the client sees it
        // Live (the machine now) or historic (the snapshot at the cursor), as the client views it.
        bool viewing_history() const { return historic_; }
        void view(std::size_t index);
        std::vector<FrameView> frames_view(const Vm& vm) const;
        std::vector<NamedValue> locals_view(const Vm& vm, std::size_t frame) const;
        std::vector<NamedValue> globals_view(const Vm& vm) const;
        std::vector<std::string> stack_view(const Vm& vm) const;
        std::size_t depth_view(const Vm& vm) const;
        // The machine now, whatever the client views.
        std::vector<FrameView> live_frames(const Vm& vm) const;
        std::vector<NamedValue> live_locals(const Vm& vm, std::size_t frame) const;
        std::vector<NamedValue> live_globals(const Vm& vm) const;
        std::vector<std::string> live_stack(const Vm& vm) const;
        // The live value a path names in a frame ("point.x", "items[2]"); nullopt when there is none.
        std::optional<Value> resolve(const Vm& vm, std::size_t frame, std::string_view path, std::string_view scope) const;
        std::optional<Value> variable_named(const Vm& vm, std::size_t frame, std::string_view name, std::string_view scope) const;
        std::vector<NamedValue> children(const Vm& vm, const Value& value) const;
        NamedValue describe(const Vm& vm, std::string name, const Value& value) const;
        static bool expandable(const Value& value);
        std::optional<bool> condition_holds(const Vm& vm, std::string_view condition) const;
        std::optional<Value> literal(std::string_view text) const;
        std::optional<std::string> history_value(std::string_view name) const;

        // ---------------------------------------------------------------- the history
        Snapshot take_snapshot(const Vm& vm) const;
        void record(const Vm& vm);
        Location location_of_snapshot(std::size_t index) const;
        bool snapshot_has_breakpoint(std::size_t index) const;

        // ---------------------------------------------------------------- helpers
        Position frame_position(const CallFrame& frame, bool executing) const;
        std::uint16_t frame_file(const CallFrame& frame, bool executing) const;
        std::string type_text(const Value& value) const;

        DebugChannel& channel_;
        const bytecode::Program& program_;
        DebuggerOptions options_;
        std::uint64_t step_ = 0;         // instructions seen
        std::uint64_t sequence_ = 1;     // of the messages sent
        bool started_ = false;
        bool finishing_ = false;         // finish() is running: the top frame's ip is past its instruction
        bool input_ended_ = false;
        Mode mode_ = Mode::Run;
        std::size_t mode_depth_ = 0;
        std::vector<std::optional<Location>> line_of_depth_;
        std::map<std::uint16_t, std::vector<Breakpoint>> breakpoints_;
        std::set<std::pair<std::uint16_t, std::uint32_t>> executable_lines_;
        std::deque<Snapshot> history_;
        std::size_t cursor_ = 0;  // the snapshot shown while historic_
        bool historic_ = false;   // the client looks at a snapshot rather than at the machine
        const Vm* vm_ = nullptr; // the machine, once it has started (for input() requests)
        bytecode::Instruction current_;  // the instruction about to run (or that failed)
        Location current_location_;
        bool at_entry_ = false;          // stopped before the first instruction
    };
}
