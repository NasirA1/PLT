#include "thunder/debugger.hpp"

#include "thunder/report.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <ostream>

namespace thunder
{
    // ------------------------------------------------------------------ the channel

    StreamChannel::StreamChannel(LineReader next_line, std::ostream& out)
        : state_(std::make_shared<State>())
        , out_(out)
    {
        thread_ = std::thread([next_line = std::move(next_line), state = state_] { run(next_line, state); });
    }

    StreamChannel::~StreamChannel()
    {
        // The reader may be blocked on input that never comes (a client that keeps the pipe open):
        // give it a moment to finish, otherwise let it go. It owns everything it touches.
        std::unique_lock lock(state_->mutex);
        const bool finished = state_->ready.wait_for(lock, std::chrono::milliseconds(200), [this] { return state_->finished; });
        lock.unlock();
        if (finished)
        {
            thread_.join();
        }
        else
        {
            thread_.detach();
        }
    }

    void StreamChannel::run(const LineReader& next_line, const std::shared_ptr<State>& state)
    {
        std::string line;
        while (next_line(line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            const std::lock_guard lock(state->mutex);
            state->lines.push_back(std::move(line));
            state->pending.fetch_add(1);
            state->ready.notify_all();
        }
        const std::lock_guard lock(state->mutex);
        state->finished = true;
        state->ready.notify_all();
    }

    std::optional<std::string> StreamChannel::read_line()
    {
        std::unique_lock lock(state_->mutex);
        state_->ready.wait(lock, [this] { return !state_->lines.empty() || state_->finished; });
        if (state_->lines.empty())
        {
            return std::nullopt;
        }
        std::string line = std::move(state_->lines.front());
        state_->lines.pop_front();
        state_->pending.fetch_sub(1);
        return line;
    }

    bool StreamChannel::has_line()
    {
        return state_->pending.load() > 0;
    }

    void StreamChannel::write_line(std::string_view line)
    {
        out_ << line << '\n';
        out_.flush();
    }

    // ------------------------------------------------------------------ small helpers

    namespace
    {
        std::string_view trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        // The comparison operator of a condition and where it is, if the text has one outside
        // quotes and brackets: "n == 3" -> ("==", 2).
        struct Comparison
        {
            std::string_view op;
            std::size_t at = std::string_view::npos;
        };

        Comparison find_comparison(std::string_view text)
        {
            int depth = 0;
            bool quoted = false;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (c == '"')
                {
                    quoted = !quoted;
                }
                else if (!quoted && (c == '[' || c == '('))
                {
                    ++depth;
                }
                else if (!quoted && (c == ']' || c == ')'))
                {
                    --depth;
                }
                else if (!quoted && depth == 0)
                {
                    for (const std::string_view op : {"==", "!=", "<=", ">=", "<", ">"})
                    {
                        if (text.substr(i, op.size()) == op)
                        {
                            return {op, i};
                        }
                    }
                }
            }
            return {};
        }

        // The value of a comparison between two values; nullopt when they cannot be compared.
        std::optional<bool> compare(const Value& left, std::string_view op, const Value& right)
        {
            const auto numeric = [](const Value& value) { return value.is_int() || value.is_float(); };
            const auto ordered = [&](auto less, auto equal) -> std::optional<bool>
            {
                if (op == "==")
                {
                    return equal;
                }
                if (op == "!=")
                {
                    return !equal;
                }
                if (op == "<")
                {
                    return less;
                }
                if (op == "<=")
                {
                    return less || equal;
                }
                if (op == ">")
                {
                    return !less && !equal;
                }
                return !less; // ">="
            };
            if (numeric(left) && numeric(right))
            {
                if (left.is_int() && right.is_int())
                {
                    return ordered(left.as_int() < right.as_int(), left.as_int() == right.as_int());
                }
                return ordered(left.as_number() < right.as_number(), left.as_number() == right.as_number());
            }
            if (left.is_string() && right.is_string())
            {
                return ordered(left.as_string() < right.as_string(), left.as_string() == right.as_string());
            }
            if (op != "==" && op != "!=")
            {
                return std::nullopt; // only equality is defined for the other kinds
            }
            if (left.kind() != right.kind())
            {
                return op == "!=";
            }
            return op == "==" ? left == right : !(left == right);
        }

        std::string error_text(const RuntimeError& error)
        {
            return error.code + ": " + error.message;
        }
    }

    // ------------------------------------------------------------------ construction

    Debugger::Debugger(DebugChannel& channel, const bytecode::Program& program, DebuggerOptions options)
        : channel_(channel)
        , program_(program)
        , options_(options)
    {
        for (const bytecode::Function& function : program_.functions)
        {
            for (const bytecode::LineEntry& entry : function.lines)
            {
                if (entry.line != 0)
                {
                    executable_lines_.emplace(entry.file, entry.line);
                }
            }
        }
        send_event("ready", [&](JsonWriter& json)
        {
            json.field("program", program_.source_name);
            json.key("files").begin_array();
            if (program_.source_files.empty())
            {
                json.value(program_.source_name);
            }
            for (const std::string& file : program_.source_files)
            {
                json.value(file);
            }
            json.end_array();
        });
    }

    // ------------------------------------------------------------------ watching the program

    void Debugger::before_instruction(const Vm& vm, const bytecode::Instruction& instruction)
    {
        vm_ = &vm;
        current_ = instruction;
        const CallFrame& top = vm.frames().back();
        const bytecode::Function& function = program_.functions[top.function];
        current_location_ = {function.file_at(instruction.offset), function.position_at(instruction.offset).line};
        const std::size_t depth = vm.frames().size();
        const bool line_start = current_location_.line != 0 && is_line_start(depth, current_location_);

        if (!started_)
        {
            // Every session begins stopped, so the client can place its breakpoints before the
            // first instruction runs; a client that does not want to show this stop continues.
            started_ = true;
            at_entry_ = true;
            stop(vm, "entry");
            at_entry_ = false;
            ++step_;
            return;
        }
        if (channel_.has_line())
        {
            handle_pending(vm); // a pause, new breakpoints, or the end of the session
        }

        std::optional<std::string> reason;
        std::string description;
        if (line_start)
        {
            record(vm);
            if (const auto hit = breakpoint_hit(vm, current_location_))
            {
                reason = "breakpoint";
                description = *hit;
            }
            else if (mode_ == Mode::StepIn || (mode_ == Mode::StepOver && depth <= mode_depth_))
            {
                reason = "step";
            }
        }
        if (!reason && mode_ == Mode::StepOut && depth < mode_depth_)
        {
            reason = "step"; // back in the caller, right after the call
        }
        if (!reason && mode_ == Mode::Pause)
        {
            reason = "pause";
        }
        if (reason)
        {
            stop(vm, *reason, description);
        }
        ++step_;
    }

    void Debugger::on_output(std::string_view text)
    {
        send_event("output", [&](JsonWriter& json) { json.field("text", text); });
    }

    std::optional<std::string> Debugger::read_line()
    {
        if (input_ended_ || vm_ == nullptr)
        {
            return std::nullopt;
        }
        send_event("input");
        while (true)
        {
            const std::optional<std::string> line = channel_.read_line();
            if (!line)
            {
                throw ExecutionAborted{};
            }
            const auto request = parse_json(*line);
            if (!request || !request->is_object())
            {
                continue;
            }
            if (request->string_or("command") == "input")
            {
                respond(*request);
                if (request->bool_or("end"))
                {
                    input_ended_ = true;
                    return std::nullopt;
                }
                return request->string_or("text");
            }
            if (handle(*vm_, *request, true) == Action::Terminate)
            {
                throw ExecutionAborted{};
            }
        }
    }

    bool Debugger::at_end()
    {
        return input_ended_;
    }

    void Debugger::finish(const Vm& vm, const ExecResult& result)
    {
        finishing_ = true;
        if (!result.aborted && result.error && vm_ != nullptr)
        {
            const RuntimeError& error = *result.error;
            stop(vm, "exception", error_text(error), format_runtime_error(error, program_.source_name, vm.language()));
        }
        send_event("exited", [&](JsonWriter& json)
        {
            json.field("ok", result.ok);
            json.field("exit_code", static_cast<std::int64_t>(result.exit_code));
            json.field("aborted", result.aborted);
            json.field("steps", result.stats.steps);
            json.key("error");
            if (result.error)
            {
                write_runtime_error_json(json, *result.error);
            }
            else
            {
                json.null();
            }
        });
    }

    // ------------------------------------------------------------------ deciding when to stop

    /*
     * The first instruction of a source line *in its own frame*. The test is not simply "the line
     * changed": `var answer = twice(21)` is one line whose instructions are interrupted by the
     * whole of `twice`; when the call returns, the next instruction is on that line again, and
     * stopping there a second time would make a breakpoint fire twice for one execution of the
     * line. So the line each frame was last on is remembered per depth, and a frame that is left
     * forgets its line, so that a second call to the same function does stop again.
     */
    bool Debugger::is_line_start(std::size_t depth, const Location& location)
    {
        line_of_depth_.resize(depth + 1); // deeper frames are gone
        std::optional<Location>& previous = line_of_depth_[depth];
        const bool start = !previous || *previous != location;
        previous = location;
        return start;
    }

    std::optional<std::string> Debugger::breakpoint_hit(const Vm& vm, const Location& location)
    {
        const auto found = breakpoints_.find(location.file);
        if (found == breakpoints_.end())
        {
            return std::nullopt;
        }
        for (Breakpoint& breakpoint : found->second)
        {
            if (breakpoint.line != location.line)
            {
                continue;
            }
            ++breakpoint.hits;
            std::string description;
            if (!breakpoint.condition.empty())
            {
                const std::optional<bool> holds = condition_holds(vm, breakpoint.condition);
                if (holds.has_value() && !*holds)
                {
                    continue;
                }
                if (!holds)
                {
                    description = std::format("the condition '{}' could not be evaluated here", breakpoint.condition);
                }
            }
            if (!breakpoint.hit_condition.empty() && !hit_condition_met(breakpoint.hit_condition, breakpoint.hits))
            {
                continue;
            }
            return description;
        }
        return std::nullopt;
    }

    /** "3" or "== 3": the third time; ">= 3", "> 3", "< 3", "<= 3"; "% 3": every third time. */
    bool Debugger::hit_condition_met(std::string_view condition, std::uint64_t hits)
    {
        std::string_view text = trim(condition);
        std::string_view op = "==";
        for (const std::string_view candidate : {"==", ">=", "<=", ">", "<", "%"})
        {
            if (text.starts_with(candidate))
            {
                op = candidate;
                text = trim(text.substr(candidate.size()));
                break;
            }
        }
        const std::optional<std::int64_t> number = parse_int(text);
        if (!number || *number < 0)
        {
            return true; // not a hit condition we understand: do not hide the stop
        }
        const auto count = static_cast<std::uint64_t>(*number);
        if (op == "==")
        {
            return hits == count;
        }
        if (op == ">=")
        {
            return hits >= count;
        }
        if (op == "<=")
        {
            return hits <= count;
        }
        if (op == ">")
        {
            return hits > count;
        }
        if (op == "<")
        {
            return hits < count;
        }
        return count != 0 && hits % count == 0;
    }

    void Debugger::stop(const Vm& vm, std::string_view reason, std::string_view description, std::string_view text)
    {
        record(vm); // a stop between line starts (a pause, a step out) is in the history too
        view(history_.size());
        mode_ = Mode::Run;
        send_stopped(vm, reason, description, text);
        command_loop(vm);
    }

    // ------------------------------------------------------------------ the conversation

    void Debugger::command_loop(const Vm& vm)
    {
        while (true)
        {
            const std::optional<std::string> line = channel_.read_line();
            if (!line)
            {
                if (finishing_)
                {
                    return; // the client has gone: nothing left to tell it
                }
                throw ExecutionAborted{};
            }
            if (trim(*line).empty())
            {
                continue;
            }
            const auto request = parse_json(*line);
            if (!request || !request->is_object())
            {
                send_event("error", [&](JsonWriter& json)
                {
                    json.field("message", request ? "a request must be a JSON object" : request.error());
                });
                continue;
            }
            switch (handle(vm, *request, false))
            {
            case Action::Stay:
                break;
            case Action::Resume:
                return;
            case Action::Terminate:
                if (finishing_)
                {
                    return;
                }
                throw ExecutionAborted{};
            }
        }
    }

    void Debugger::handle_pending(const Vm& vm)
    {
        while (channel_.has_line())
        {
            const std::optional<std::string> line = channel_.read_line();
            if (!line)
            {
                throw ExecutionAborted{};
            }
            const auto request = parse_json(*line);
            if (request && request->is_object() && handle(vm, *request, true) == Action::Terminate)
            {
                throw ExecutionAborted{};
            }
        }
    }

    Debugger::Action Debugger::handle(const Vm& vm, const JsonValue& request, bool running)
    {
        // The questions, answered whatever the program is doing.
        using Query = void (Debugger::*)(const Vm&, const JsonValue&);
        static const std::map<std::string_view, Query> queries = {
            {"setBreakpoints", &Debugger::set_breakpoints},
            {"stackTrace", &Debugger::stack_trace},
            {"variables", &Debugger::variables},
            {"evaluate", &Debugger::evaluate},
            {"canvasEvent", &Debugger::canvas_event},
        };
        static const std::set<std::string_view> stepping = {
            "continue", "next", "stepIn", "stepOut", "stepBack", "reverseContinue"};

        const std::string command = request.string_or("command");
        if (const auto query = queries.find(command); query != queries.end())
        {
            (this->*query->second)(vm, request);
            return Action::Stay;
        }
        if (command == "terminate" || command == "disconnect")
        {
            respond(request);
            return Action::Terminate;
        }
        if (command == "pause" || command == "input")
        {
            if (running && command == "pause")
            {
                mode_ = Mode::Pause; // an "input" line nobody asked for is simply dropped
            }
            respond(request);
            return Action::Stay;
        }
        if (!stepping.contains(command))
        {
            respond_error(request, std::format("unknown command '{}'", command));
            return Action::Stay;
        }
        if (running)
        {
            respond_error(request, "the program is running");
            return Action::Stay;
        }
        return resume(vm, request);
    }

    Debugger::Action Debugger::resume(const Vm& vm, const JsonValue& request)
    {
        const std::string command = request.string_or("command");
        respond(request);
        if (finishing_)
        {
            return Action::Resume; // the program has ended: any step lets it go
        }
        const std::size_t depth = depth_view(vm);
        if (command == "continue")
        {
            mode_ = Mode::Run;
        }
        else if (command == "next")
        {
            mode_ = Mode::StepOver;
            mode_depth_ = depth;
        }
        else if (command == "stepIn")
        {
            mode_ = Mode::StepIn;
        }
        else if (command == "stepOut")
        {
            mode_ = Mode::StepOut;
            mode_depth_ = depth;
        }
        if (step_in_history(command))
        {
            return Action::Stay;
        }
        if (at_entry_ && command == "continue")
        {
            // Breakpoints were placed while stopped at the entry: one on the first line fires now,
            // otherwise the first line would be the only one a breakpoint can never stop on.
            if (const auto hit = breakpoint_hit(vm, current_location_))
            {
                at_entry_ = false; // the next continue runs on
                mode_ = Mode::Run;
                send_stopped(vm, "breakpoint", *hit);
                return Action::Stay;
            }
        }
        send_event("continued");
        return Action::Resume;
    }

    /*
     * Stepping while the history is shown moves through the history first. Forwards, a step that
     * runs out of history goes live with the mode already set; backwards, the history is all
     * there is.
     */
    bool Debugger::step_in_history(std::string_view command)
    {
        const Vm& vm = *vm_;
        if (command == "stepBack")
        {
            view(cursor_ > 0 ? cursor_ - 1 : 0);
            send_stopped(vm, "step");
            return true;
        }
        if (command == "reverseContinue")
        {
            for (std::size_t i = cursor_; i-- > 0;)
            {
                if (snapshot_has_breakpoint(i))
                {
                    view(i);
                    send_stopped(vm, "breakpoint");
                    return true;
                }
            }
            view(0);
            send_stopped(vm, !history_.empty() && history_.front().step == 0 ? "entry" : "step");
            return true;
        }
        if (!viewing_history())
        {
            return false;
        }
        const std::size_t depth = history_[cursor_].frames.size();
        for (std::size_t i = cursor_ + 1; i < history_.size(); ++i)
        {
            const std::size_t at = history_[i].frames.size();
            const bool stops = (command == "stepIn") || (command == "next" && at <= depth)
                || (command == "stepOut" && at < depth) || (command == "continue" && snapshot_has_breakpoint(i));
            if (stops)
            {
                view(i);
                send_stopped(vm, command == "continue" ? "breakpoint" : "step");
                return true;
            }
        }
        view(history_.size()); // back to the live state, then on from there
        return false;
    }

    void Debugger::present(const graphics::Canvas& canvas, std::size_t keep)
    {
        // The frame record (graphics::frame_json) is already JSON, so the event line is
        // assembled around it: JsonWriter cannot embed a ready-made object.
        channel_.write_line(std::format("{{\"type\":\"event\",\"seq\":{},\"event\":\"frame\",\"record\":{}}}",
            sequence_++, graphics::frame_json(canvas, keep)));
    }

    void Debugger::canvas_event(const Vm&, const JsonValue& request)
    {
        // A key or mouse line from the client's canvas, worded as on the --canvas input stream
        // ("key down left", "mouse 20 30 1"); anything else is quietly ignored, as there.
        events_.apply(request.string_or("text"));
        respond(request);
    }

    void Debugger::send_event(std::string_view name, const std::function<void(JsonWriter&)>& body)
    {
        JsonWriter json;
        json.begin_object();
        json.field("type", "event");
        json.field("seq", sequence_++);
        json.field("event", name);
        if (body)
        {
            body(json);
        }
        json.end_object();
        channel_.write_line(json.str());
    }

    void Debugger::respond(const JsonValue& request, const std::function<void(JsonWriter&)>& body)
    {
        JsonWriter json;
        json.begin_object();
        json.field("type", "response");
        json.field("seq", sequence_++);
        json.field("request_seq", request.int_or("seq"));
        json.field("command", request.string_or("command"));
        json.field("success", true);
        json.key("body").begin_object();
        if (body)
        {
            body(json);
        }
        json.end_object();
        json.end_object();
        channel_.write_line(json.str());
    }

    void Debugger::respond_error(const JsonValue& request, std::string_view message)
    {
        JsonWriter json;
        json.begin_object();
        json.field("type", "response");
        json.field("seq", sequence_++);
        json.field("request_seq", request.int_or("seq"));
        json.field("command", request.string_or("command"));
        json.field("success", false);
        json.field("message", message);
        json.end_object();
        channel_.write_line(json.str());
    }

    void Debugger::send_stopped(const Vm& vm, std::string_view reason, std::string_view description, std::string_view text)
    {
        const std::vector<FrameView> frames = frames_view(vm);
        send_event("stopped", [&](JsonWriter& json)
        {
            json.field("reason", reason);
            if (!description.empty())
            {
                json.field("description", description);
            }
            if (!text.empty())
            {
                json.field("text", text);
            }
            json.field("step", viewing_history() ? history_[cursor_].step : step_);
            json.field("historic", viewing_history());
            json.key("history").begin_object();
            json.field("index", static_cast<std::uint64_t>(cursor_));
            json.field("size", static_cast<std::uint64_t>(history_.size()));
            json.end_object();
            if (!frames.empty())
            {
                json.field("function", frames.front().function);
                json.field("file", static_cast<unsigned>(frames.front().file));
                json.field("line", frames.front().position.line);
                json.field("column", frames.front().position.column);
            }
        });
    }

    // ------------------------------------------------------------------ breakpoints

    void Debugger::set_breakpoints(const Vm& /*vm*/, const JsonValue& request)
    {
        const auto file = static_cast<std::uint16_t>(request.int_or("file"));
        std::vector<Breakpoint> placed;
        JsonWriter verified;
        verified.begin_array();
        for (const JsonValue& wanted : request.array_or_empty("breakpoints"))
        {
            const auto line = static_cast<std::uint32_t>(std::max<std::int64_t>(0, wanted.int_or("line")));
            // A line without instructions (a comment, a blank, a `struct` header) can never stop
            // the program: the breakpoint moves down to the next line that has code, as in every
            // other debugger, and is reported as unverified when there is none.
            const auto next = executable_lines_.lower_bound({file, line});
            const bool found = next != executable_lines_.end() && next->first == file;
            verified.begin_object();
            verified.field("line", found ? next->second : line);
            verified.field("verified", found);
            if (!found)
            {
                verified.field("message", "there is no code on this line, so the program never stops here");
            }
            verified.end_object();
            if (found)
            {
                Breakpoint breakpoint;
                breakpoint.line = next->second;
                breakpoint.condition = wanted.string_or("condition");
                breakpoint.hit_condition = wanted.string_or("hitCondition");
                // A breakpoint that was already there keeps its hit count.
                const auto previous = breakpoints_.find(file);
                if (previous != breakpoints_.end())
                {
                    const auto same = std::ranges::find_if(previous->second,
                        [&](const Breakpoint& b) { return b.line == breakpoint.line && b.condition == breakpoint.condition; });
                    if (same != previous->second.end())
                    {
                        breakpoint.hits = same->hits;
                    }
                }
                placed.push_back(std::move(breakpoint));
            }
        }
        verified.end_array();
        breakpoints_[file] = std::move(placed);
        respond(request, [&](JsonWriter& json)
        {
            json.key("breakpoints").raw_number(verified.str());
        });
    }

    // ------------------------------------------------------------------ the state as the client sees it

    void Debugger::stack_trace(const Vm& vm, const JsonValue& request)
    {
        const std::vector<FrameView> frames = frames_view(vm);
        respond(request, [&](JsonWriter& json)
        {
            json.field("historic", viewing_history());
            json.field("step", viewing_history() ? history_[cursor_].step : step_);
            json.key("frames").begin_array();
            for (std::size_t i = 0; i < frames.size(); ++i)
            {
                json.begin_object();
                json.field("index", static_cast<std::uint64_t>(i));
                json.field("function", frames[i].function);
                json.field("file", static_cast<unsigned>(frames[i].file));
                json.field("line", frames[i].position.line);
                json.field("column", frames[i].position.column);
                json.end_object();
            }
            json.end_array();
        });
    }

    void Debugger::variables(const Vm& vm, const JsonValue& request)
    {
        const auto frame = static_cast<std::size_t>(std::max<std::int64_t>(0, request.int_or("frame")));
        const std::string scope = request.string_or("scope", "locals");
        const std::string path = request.string_or("path");
        std::vector<NamedValue> values;
        std::string message;
        if (scope == "execution")
        {
            const std::vector<FrameView> frames = frames_view(vm);
            values.push_back({"step", std::to_string(viewing_history() ? history_[cursor_].step : step_), "int"});
            if (!viewing_history())
            {
                const bytecode::Function& function = program_.functions[vm.frames().back().function];
                values.push_back({"instruction", bytecode::instruction_text(program_, function, current_), "instruction"});
            }
            if (!frames.empty())
            {
                values.push_back({"function", frames.front().function, "function"});
                values.push_back({"line", std::to_string(frames.front().position.line), "int"});
                values.push_back({"file", program_.file_name(frames.front().file), "file"});
            }
            if (viewing_history())
            {
                values.push_back({"history", std::format("step {} of the {} remembered", cursor_ + 1, history_.size()), "history"});
            }
        }
        else if (scope == "stack")
        {
            std::size_t index = 0;
            for (const std::string& value : stack_view(vm))
            {
                values.push_back({std::format("[{}]", index++), value, ""});
            }
        }
        else if (!path.empty())
        {
            if (viewing_history())
            {
                message = "an earlier step keeps its values as text: step forwards to the live state to expand them";
            }
            else if (const std::optional<Value> value = resolve(vm, frame, path, scope))
            {
                values = children(vm, *value);
            }
            else
            {
                message = std::format("'{}' is not a variable here", path);
            }
        }
        else if (scope == "globals")
        {
            values = globals_view(vm);
        }
        else
        {
            values = locals_view(vm, frame);
        }
        respond(request, [&](JsonWriter& json)
        {
            if (!message.empty())
            {
                json.field("message", message);
            }
            json.key("variables").begin_array();
            for (const NamedValue& value : values)
            {
                json.begin_object();
                json.field("name", value.name);
                json.field("value", value.value);
                json.field("type", value.type);
                json.field("expandable", value.expandable && !viewing_history());
                json.end_object();
            }
            json.end_array();
        });
    }

    void Debugger::evaluate(const Vm& vm, const JsonValue& request)
    {
        const std::string expression(trim(request.string_or("expression")));
        const auto frame = static_cast<std::size_t>(std::max<std::int64_t>(0, request.int_or("frame")));
        if (expression.empty())
        {
            respond_error(request, "nothing to evaluate");
            return;
        }
        if (viewing_history())
        {
            if (const std::optional<std::string> value = history_value(expression))
            {
                respond(request, [&](JsonWriter& json)
                {
                    json.field("value", *value);
                    json.field("type", "");
                    json.field("expandable", false);
                });
                return;
            }
            respond_error(request, std::format("'{}' is not a variable at this step", expression));
            return;
        }
        if (find_comparison(expression).at != std::string_view::npos)
        {
            if (const std::optional<bool> holds = condition_holds(vm, expression))
            {
                respond(request, [&](JsonWriter& json)
                {
                    json.field("value", *holds ? vm.locale().true_text : vm.locale().false_text);
                    json.field("type", "bool");
                    json.field("expandable", false);
                });
                return;
            }
            respond_error(request, std::format("'{}' cannot be evaluated here (compare a variable with a value)", expression));
            return;
        }
        const std::optional<Value> value = resolve(vm, frame, expression, "evaluate");
        if (!value)
        {
            respond_error(request, std::format(
                "'{}' is not a variable here (the debugger evaluates variables, fields, elements and comparisons)", expression));
            return;
        }
        const NamedValue described = describe(vm, expression, *value);
        respond(request, [&](JsonWriter& json)
        {
            json.field("value", described.value);
            json.field("type", described.type);
            json.field("expandable", expandable(*value));
        });
    }

    /** Shows the snapshot at 'index'; an index at or past the end shows the machine itself. */
    void Debugger::view(std::size_t index)
    {
        historic_ = index + 1 < history_.size();
        cursor_ = historic_ ? index : (history_.empty() ? 0 : history_.size() - 1);
    }

    std::vector<Debugger::FrameView> Debugger::frames_view(const Vm& vm) const
    {
        if (!historic_)
        {
            return live_frames(vm);
        }
        std::vector<FrameView> frames;
        for (const SnapshotFrame& frame : history_[cursor_].frames)
        {
            frames.push_back({frame.function, frame.file, frame.position});
        }
        return frames;
    }

    std::vector<Debugger::NamedValue> Debugger::locals_view(const Vm& vm, std::size_t frame) const
    {
        if (!historic_)
        {
            return live_locals(vm, frame);
        }
        const Snapshot& snapshot = history_[cursor_];
        return frame < snapshot.frames.size() ? snapshot.frames[frame].locals : std::vector<NamedValue>{};
    }

    std::vector<Debugger::NamedValue> Debugger::globals_view(const Vm& vm) const
    {
        return historic_ ? history_[cursor_].globals : live_globals(vm);
    }

    std::vector<std::string> Debugger::stack_view(const Vm& vm) const
    {
        return historic_ ? history_[cursor_].stack : live_stack(vm);
    }

    std::vector<Debugger::FrameView> Debugger::live_frames(const Vm& vm) const
    {
        std::vector<FrameView> frames;
        const std::vector<CallFrame>& live = vm.frames();
        for (std::size_t i = live.size(); i-- > 0;)
        {
            const bool executing = i + 1 == live.size() && !finishing_;
            frames.push_back({program_.functions[live[i].function].name, frame_file(live[i], executing),
                frame_position(live[i], executing)});
        }
        return frames;
    }

    std::vector<Debugger::NamedValue> Debugger::live_locals(const Vm& vm, std::size_t frame) const
    {
        const std::vector<CallFrame>& live = vm.frames();
        if (frame >= live.size())
        {
            return {};
        }
        const CallFrame& call = live[live.size() - 1 - frame];
        const bytecode::Function& function = program_.functions[call.function];
        std::vector<NamedValue> values;
        for (std::size_t slot = 0; slot < function.local_count && call.base + slot < vm.stack().size(); ++slot)
        {
            const std::string name = slot < function.local_names.size() ? function.local_names[slot] : "?";
            values.push_back(describe(vm, name, vm.stack()[call.base + slot]));
        }
        return values;
    }

    std::vector<Debugger::NamedValue> Debugger::live_globals(const Vm& vm) const
    {
        std::vector<NamedValue> values;
        for (std::size_t slot = 0; slot < vm.globals().size() && slot < program_.globals.size(); ++slot)
        {
            values.push_back(describe(vm, program_.globals[slot], vm.globals()[slot]));
        }
        return values;
    }

    std::vector<std::string> Debugger::live_stack(const Vm& vm) const
    {
        std::vector<std::string> values;
        if (vm.frames().empty())
        {
            return values;
        }
        const CallFrame& top = vm.frames().back();
        const bytecode::Function& function = program_.functions[top.function];
        for (std::size_t i = top.base + function.local_count; i < vm.stack().size(); ++i)
        {
            values.push_back(vm.stack()[i].to_debug(vm.locale()));
        }
        return values;
    }

    std::size_t Debugger::depth_view(const Vm& vm) const
    {
        return historic_ ? history_[cursor_].frames.size() : vm.frames().size();
    }

    std::optional<Value> Debugger::variable_named(const Vm& vm, std::size_t frame, std::string_view name, std::string_view scope) const
    {
        const std::vector<CallFrame>& live = vm.frames();
        if (scope != "globals" && frame < live.size())
        {
            const CallFrame& call = live[live.size() - 1 - frame];
            const bytecode::Function& function = program_.functions[call.function];
            // The last slot with the name wins: an inner block's variable shadows an outer one.
            for (std::size_t slot = std::min<std::size_t>(function.local_count, function.local_names.size()); slot-- > 0;)
            {
                if (function.local_names[slot] == name && call.base + slot < vm.stack().size())
                {
                    return vm.stack()[call.base + slot];
                }
            }
        }
        if (scope != "locals")
        {
            for (std::size_t slot = 0; slot < program_.globals.size() && slot < vm.globals().size(); ++slot)
            {
                if (program_.globals[slot] == name)
                {
                    return vm.globals()[slot];
                }
            }
        }
        return std::nullopt;
    }

    /** "point.x", "items[2]", "scores["ada"]", "pairs[0][1]": a variable, then fields and elements. */
    std::optional<Value> Debugger::resolve(const Vm& vm, std::size_t frame, std::string_view path, std::string_view scope) const
    {
        std::string_view rest = trim(path);
        const std::size_t end = rest.find_first_of(".[");
        const std::string_view name = trim(rest.substr(0, end));
        if (name.empty())
        {
            return std::nullopt;
        }
        std::optional<Value> current = variable_named(vm, frame, name, scope);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
        while (current && !rest.empty())
        {
            if (rest.front() == '.')
            {
                rest.remove_prefix(1);
                const std::size_t next = rest.find_first_of(".[");
                const std::string_view field = trim(rest.substr(0, next));
                rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next);
                if (!current->is_struct() || current->is_nothing())
                {
                    return std::nullopt;
                }
                const StructObject& object = current->as_struct();
                const auto& names = object.layout->field_names;
                const auto found = std::ranges::find(names, field);
                if (found == names.end())
                {
                    return std::nullopt;
                }
                current = object.fields[static_cast<std::size_t>(found - names.begin())];
                continue;
            }
            if (rest.front() != '[')
            {
                return std::nullopt;
            }
            const std::size_t close = rest.find(']');
            if (close == std::string_view::npos)
            {
                return std::nullopt;
            }
            const std::optional<Value> key = literal(trim(rest.substr(1, close - 1)));
            rest = rest.substr(close + 1);
            if (!key)
            {
                return std::nullopt;
            }
            if (current->is_map())
            {
                const Value* entry = current->as_map().find(*key);
                if (entry == nullptr)
                {
                    return std::nullopt;
                }
                current = *entry;
                continue;
            }
            if (!key->is_int())
            {
                return std::nullopt;
            }
            const std::vector<Value>* elements = current->is_array() ? &current->as_array().elements
                : current->is_tuple()                                 ? &current->as_tuple().elements
                                                                      : nullptr;
            if (elements == nullptr || key->as_int() < 0 || static_cast<std::size_t>(key->as_int()) >= elements->size())
            {
                return std::nullopt;
            }
            current = (*elements)[static_cast<std::size_t>(key->as_int())];
        }
        return current;
    }

    std::vector<Debugger::NamedValue> Debugger::children(const Vm& vm, const Value& value) const
    {
        std::vector<NamedValue> values;
        const auto elements = [&](const std::vector<Value>& items)
        {
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                values.push_back(describe(vm, std::format("[{}]", i), items[i]));
            }
        };
        if (value.is_array())
        {
            elements(value.as_array().elements);
        }
        else if (value.is_tuple())
        {
            elements(value.as_tuple().elements);
        }
        else if (value.is_struct() && !value.is_nothing())
        {
            const StructObject& object = value.as_struct();
            for (std::size_t i = 0; i < object.fields.size(); ++i)
            {
                const std::string name = i < object.layout->field_names.size() ? object.layout->field_names[i] : "?";
                values.push_back(describe(vm, name, object.fields[i]));
            }
        }
        else if (value.is_map())
        {
            for (const auto& [key, entry] : value.as_map().entries)
            {
                values.push_back(describe(vm, std::format("[{}]", key.to_debug(vm.locale())), entry));
            }
        }
        return values;
    }

    Debugger::NamedValue Debugger::describe(const Vm& vm, std::string name, const Value& value) const
    {
        return {std::move(name), value.to_debug(vm.locale()), type_text(value), expandable(value)};
    }

    bool Debugger::expandable(const Value& value)
    {
        return (value.is_array() && !value.as_array().elements.empty())
            || (value.is_tuple() && !value.as_tuple().elements.empty())
            || (value.is_struct() && !value.is_nothing() && !value.as_struct().fields.empty())
            || (value.is_map() && !value.as_map().entries.empty());
    }

    /*
     * A value's type as the program's own language writes it: متن rather than string in a Pashto
     * program, and an array, a tuple or a map by what it holds (عدد[], (عدد، متن), {متن: عدد}).
     * Values carry no static type, so an empty array is [] and an empty map {}.
     */
    std::string Debugger::type_text(const Value& value) const
    {
        const LanguagePack& language = vm_ != nullptr ? vm_->language() : english_pack();
        const std::string& separator = language.display.separator;
        switch (value.kind())
        {
        case Value::Kind::Int:
            return language.spelling(TokenKind::KwInt);
        case Value::Kind::Float:
            return language.spelling(TokenKind::KwFloat);
        case Value::Kind::Bool:
            return language.spelling(TokenKind::KwBool);
        case Value::Kind::String:
            return language.spelling(TokenKind::KwString);
        case Value::Kind::Function:
            return language.spelling(TokenKind::KwFunc);
        case Value::Kind::Struct:
            if (value.is_nothing())
            {
                return language.display.nothing_text;
            }
            return value.as_struct().layout != nullptr ? value.as_struct().layout->name : "struct";
        case Value::Kind::Array:
        {
            const auto& elements = value.as_array().elements;
            return elements.empty() ? "[]" : type_text(elements.front()) + "[]";
        }
        case Value::Kind::Tuple:
        {
            std::string text = "(";
            for (const Value& element : value.as_tuple().elements)
            {
                text += (text.size() > 1 ? separator : "") + type_text(element);
            }
            return text + ")";
        }
        case Value::Kind::Map:
        {
            const auto& entries = value.as_map().entries;
            return entries.empty() ? "{}" : "{" + type_text(entries.front().first) + ": " + type_text(entries.front().second) + "}";
        }
        case Value::Kind::Void:
            break;
        }
        return std::string(value_kind_name(value.kind()));
    }

    std::optional<bool> Debugger::condition_holds(const Vm& vm, std::string_view condition) const
    {
        const Comparison comparison = find_comparison(condition);
        if (comparison.at == std::string_view::npos)
        {
            // A bare expression: true when it is a true bool.
            const std::optional<Value> value = resolve(vm, 0, condition, "evaluate");
            if (!value || !value->is_bool())
            {
                return std::nullopt;
            }
            return value->as_bool();
        }
        const std::string_view left_text = trim(condition.substr(0, comparison.at));
        const std::string_view right_text = trim(condition.substr(comparison.at + comparison.op.size()));
        const auto operand = [&](std::string_view text) -> std::optional<Value>
        {
            if (const std::optional<Value> value = literal(text))
            {
                return value;
            }
            return resolve(vm, 0, text, "evaluate");
        };
        const std::optional<Value> left = operand(left_text);
        const std::optional<Value> right = operand(right_text);
        if (!left || !right)
        {
            return std::nullopt;
        }
        return compare(*left, comparison.op, *right);
    }

    /** 42, -1, 2.5, "text", true, false (also in the program's own language). */
    std::optional<Value> Debugger::literal(std::string_view text) const
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        {
            return Value::from_string(std::string(text.substr(1, text.size() - 2)));
        }
        const DisplayLocale* locale = vm_ != nullptr ? &vm_->locale() : nullptr;
        if (text == "true" || (locale != nullptr && text == locale->true_text))
        {
            return Value::from_bool(true);
        }
        if (text == "false" || (locale != nullptr && text == locale->false_text))
        {
            return Value::from_bool(false);
        }
        if (const std::optional<std::int64_t> number = parse_int(text))
        {
            return Value::from_int(*number);
        }
        if (const std::optional<double> number = parse_float(text))
        {
            return Value::from_float(*number);
        }
        return std::nullopt;
    }

    std::optional<std::string> Debugger::history_value(std::string_view name) const
    {
        const Snapshot& snapshot = history_[cursor_];
        if (!snapshot.frames.empty())
        {
            for (const NamedValue& local : snapshot.frames.front().locals)
            {
                if (local.name == name)
                {
                    return local.value;
                }
            }
        }
        for (const NamedValue& global : snapshot.globals)
        {
            if (global.name == name)
            {
                return global.value;
            }
        }
        return std::nullopt;
    }

    // ------------------------------------------------------------------ the history

    Debugger::Snapshot Debugger::take_snapshot(const Vm& vm) const
    {
        Snapshot snapshot;
        snapshot.step = step_;
        const std::vector<FrameView> frames = live_frames(vm);
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            snapshot.frames.push_back({frames[i].function, frames[i].file, frames[i].position, live_locals(vm, i)});
        }
        snapshot.globals = live_globals(vm);
        snapshot.stack = live_stack(vm);
        return snapshot;
    }

    void Debugger::record(const Vm& vm)
    {
        if (options_.history_limit == 0 || (!history_.empty() && history_.back().step == step_))
        {
            return;
        }
        while (history_.size() >= options_.history_limit)
        {
            history_.pop_front();
        }
        history_.push_back(take_snapshot(vm));
    }

    Debugger::Location Debugger::location_of_snapshot(std::size_t index) const
    {
        const Snapshot& snapshot = history_[index];
        if (snapshot.frames.empty())
        {
            return {};
        }
        return {snapshot.frames.front().file, snapshot.frames.front().position.line};
    }

    bool Debugger::snapshot_has_breakpoint(std::size_t index) const
    {
        const Location location = location_of_snapshot(index);
        const auto found = breakpoints_.find(location.file);
        if (found == breakpoints_.end())
        {
            return false;
        }
        return std::ranges::any_of(found->second, [&](const Breakpoint& b) { return b.line == location.line; });
    }

    // ------------------------------------------------------------------ helpers

    /*
     * A frame's ip is the offset of its *next* instruction: for the frame that is about to execute
     * one (the observer is called first) that is the instruction itself, for every other frame -
     * and for the top frame once the program has ended - it is the one after the call or the
     * failure, so the byte before it belongs to the instruction wanted.
     */
    Position Debugger::frame_position(const CallFrame& frame, bool executing) const
    {
        const bytecode::Function& function = program_.functions[frame.function];
        return function.position_at(executing || frame.ip == 0 ? frame.ip : frame.ip - 1);
    }

    std::uint16_t Debugger::frame_file(const CallFrame& frame, bool executing) const
    {
        const bytecode::Function& function = program_.functions[frame.function];
        return function.file_at(executing || frame.ip == 0 ? frame.ip : frame.ip - 1);
    }
}
