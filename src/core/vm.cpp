#include "thunder/vm.hpp"

#include "thunder/builtins.hpp"
#include "thunder/utf8.hpp"

#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace thunder
{
    using bytecode::Opcode;

    namespace
    {
        constexpr std::int64_t int_min = std::numeric_limits<std::int64_t>::min();
        constexpr std::int64_t int_max = std::numeric_limits<std::int64_t>::max();
    }

    std::optional<std::int64_t> checked_add(std::int64_t a, std::int64_t b)
    {
        if ((b > 0 && a > int_max - b) || (b < 0 && a < int_min - b))
        {
            return std::nullopt;
        }
        return a + b;
    }

    std::optional<std::int64_t> checked_sub(std::int64_t a, std::int64_t b)
    {
        if ((b < 0 && a > int_max + b) || (b > 0 && a < int_min + b))
        {
            return std::nullopt;
        }
        return a - b;
    }

    std::optional<std::int64_t> checked_mul(std::int64_t a, std::int64_t b)
    {
        if (a == 0 || b == 0)
        {
            return 0;
        }
        if (a > 0)
        {
            if (b > 0 ? a > int_max / b : b < int_min / a)
            {
                return std::nullopt;
            }
        }
        else if (b > 0 ? a < int_min / b : b < int_max / a)
        {
            return std::nullopt;
        }
        return a * b;
    }

    std::optional<double> parse_float(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        {
            text.remove_prefix(1);
        }
        while (!text.empty()
            && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r'))
        {
            text.remove_suffix(1);
        }

        // Rewrite the text with ASCII digits and an ASCII point, so that from_chars can read it.
        // Digits may come from any supported script and the point may be '.' or the Arabic '٫'.
        std::string plain;
        bool any_digit = false;
        bool seen_point = false;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            const utf8::Decoded decoded = utf8::decode(text, pos);
            pos += decoded.length;
            if (pos == decoded.length && (decoded.code_point == U'-' || decoded.code_point == U'+'))
            {
                plain += static_cast<char>(decoded.code_point);
                continue;
            }
            if ((decoded.code_point == U'.' || decoded.code_point == U'\u066B') && !seen_point)
            {
                seen_point = true;
                plain += '.';
                continue;
            }
            const auto digit = digit_value(decoded.code_point);
            if (!digit)
            {
                return std::nullopt;
            }
            any_digit = true;
            plain += static_cast<char>('0' + *digit);
        }
        if (!any_digit)
        {
            return std::nullopt;
        }

        double value = 0.0;
        const auto [end, error] = std::from_chars(plain.data(), plain.data() + plain.size(), value);
        if (error != std::errc{} || end != plain.data() + plain.size() || !std::isfinite(value))
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<std::int64_t> parse_int(std::string_view text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r'))
        {
            text.remove_suffix(1);
        }

        bool negative = false;
        if (!text.empty() && (text.front() == '-' || text.front() == '+'))
        {
            negative = text.front() == '-';
            text.remove_prefix(1);
        }
        if (text.empty())
        {
            return std::nullopt;
        }

        // Accumulate as a negative number so that the most negative int can be represented.
        // Digits may come from any supported script (e.g. Pashto ۴۲).
        std::int64_t value = 0;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            const utf8::Decoded decoded = utf8::decode(text, pos);
            pos += decoded.length;
            const auto digit = digit_value(decoded.code_point);
            if (!digit)
            {
                return std::nullopt;
            }
            const auto shifted = checked_mul(value, 10);
            if (!shifted)
            {
                return std::nullopt;
            }
            const auto next = checked_sub(*shifted, *digit);
            if (!next)
            {
                return std::nullopt;
            }
            value = *next;
        }
        if (!negative)
        {
            if (value == int_min)
            {
                return std::nullopt;
            }
            value = -value;
        }
        return value;
    }

    Vm::Vm(const bytecode::Program& program, std::ostream& output, VmOptions options, std::istream* input)
        : program_(program)
        , language_(find_language_pack(program.language) != nullptr ? find_language_pack(program.language)
                                                                     : &english_pack())
        , output_(output)
        , options_(options)
        , input_(input)
    {
    }

    ExecResult Vm::run(VmObserver* observer)
    {
        stack_.clear();
        frames_.clear();
        stats_ = ExecStats{};
        current_offset_ = 0;

        ExecResult result;
        if (auto problem = bytecode::verify(program_))
        {
            result.ok = false;
            RuntimeError error;
            error.id = DiagId::InvalidBytecode;
            error.code = info(error.id).code;
            error.message = language_->format(error.id, {*problem});
            result.error = std::move(error);
            return result;
        }

        globals_.assign(program_.globals.size(), Value{});
        started_ = std::chrono::steady_clock::now();
        next_frame_ = started_;
        steps_at_frame_ = 0;
        canvas_ = graphics::Canvas{};
        random_state_ = options_.random_seed
            ? *options_.random_seed
            : static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        try
        {
            load_native_libraries(); // import native "...": before anything runs
            call_function(program_.entry);
            execute(observer);
        }
        catch (const FrameLimit&)
        {
            result.frame_limit_reached = true; // VmOptions::max_frames: a normal end
        }
        catch (const Failure& failure)
        {
            result.ok = false;
            result.error = make_error(failure);
        }
        catch (const ExitRequest& request)
        {
            result.exit_code = request.code; // exit(code): a normal end, not an error
        }
        output_.flush();
        if (canvas_.changed() && !result.frame_limit_reached)
        {
            // A picture that was drawn but not shown yet is shown when the program ends (also after a
            // runtime error, so students see how far the drawing got).
            if (host_ != nullptr)
            {
                host_->present(canvas_, canvas_.kept());
            }
            canvas_.presented();
        }
        stats_.frames = canvas_.frames();
        result.stats = stats_;
        return result;
    }

    RuntimeError Vm::make_error(const Failure& failure) const
    {
        RuntimeError error;
        error.id = failure.id;
        error.code = info(failure.id).code;
        error.message = failure.message;
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
        {
            const bytecode::Function& function = program_.functions[it->function];
            // The innermost frame failed at current_offset_; callers are paused just after their CALL.
            const std::uint32_t offset = it == frames_.rbegin() ? current_offset_ : (it->ip > 0 ? it->ip - 1 : 0);
            const std::uint16_t file = function.file_at(offset);
            error.call_stack.push_back(RuntimeError::Frame{function.name, function.position_at(offset),
                file == 0 ? std::string() : program_.file_name(file)});
        }
        if (!error.call_stack.empty())
        {
            error.position = error.call_stack.front().position;
            error.file = error.call_stack.front().file;
        }
        return error;
    }

    // ------------------------------------------------------------------ stack helpers

    Value Vm::pop()
    {
        const CallFrame& frame = frames_.back();
        const std::size_t floor = frame.base + program_.functions[frame.function].local_count;
        if (stack_.size() <= floor)
        {
            fail(DiagId::InvalidBytecode, "operand stack underflow");
        }
        Value value = std::move(stack_.back());
        stack_.pop_back();
        return value;
    }

    std::int64_t Vm::pop_int(std::string_view instruction)
    {
        const Value value = pop();
        if (!value.is_int())
        {
            fail(DiagId::InvalidBytecode, std::string(instruction) + " expects an int operand");
        }
        return value.as_int();
    }

    double Vm::pop_float(std::string_view instruction)
    {
        const Value value = pop();
        if (!value.is_float() && !value.is_int())
        {
            fail(DiagId::InvalidBytecode, std::string(instruction) + " expects a number operand");
        }
        return value.as_number();
    }

    bool Vm::pop_bool(std::string_view instruction)
    {
        const Value value = pop();
        if (!value.is_bool())
        {
            fail(DiagId::InvalidBytecode, std::string(instruction) + " expects a bool operand");
        }
        return value.as_bool();
    }

    void Vm::push(Value value)
    {
        if (stack_.size() >= options_.max_stack)
        {
            fail(DiagId::CallStackOverflow, options_.max_call_depth);
        }
        stack_.push_back(std::move(value));
        if (stack_.size() > stats_.max_stack_depth)
        {
            stats_.max_stack_depth = stack_.size();
        }
    }

    void Vm::write_output(const std::string& text, bool flush, VmObserver* observer)
    {
        if (host_ != nullptr && text.find(graphics::record_separator) != std::string::npos)
        {
            // Frames share the output with the program's text: a record separator in the text
            // would start a bogus frame, so it is written as U+FFFD instead.
            std::string safe;
            for (const char c : text)
            {
                safe += c == graphics::record_separator ? std::string("\xEF\xBF\xBD") : std::string(1, c);
            }
            output_ << safe;
        }
        else
        {
            output_ << text;
        }
        if (flush)
        {
            output_.flush();
        }
        if (observer != nullptr)
        {
            observer->on_output(text);
        }
    }

    void Vm::call_function(std::uint16_t index)
    {
        if (frames_.size() >= options_.max_call_depth)
        {
            fail(DiagId::CallStackOverflow, options_.max_call_depth);
        }
        const bytecode::Function& function = program_.functions[index];

        // The arguments are already on the stack; they become the callee's first local slots.
        std::size_t base = stack_.size();
        if (!frames_.empty())
        {
            const CallFrame& caller = frames_.back();
            const std::size_t floor = caller.base + program_.functions[caller.function].local_count;
            if (stack_.size() < floor + function.arity)
            {
                fail(DiagId::InvalidBytecode, "not enough arguments on the stack for CALL");
            }
            base = stack_.size() - function.arity;
        }
        for (std::size_t i = function.arity; i < function.local_count; ++i)
        {
            push(Value{});
        }
        frames_.push_back(CallFrame{index, 0, base});
        ++stats_.calls;
        if (frames_.size() > stats_.max_call_depth)
        {
            stats_.max_call_depth = frames_.size();
        }
    }

    // ------------------------------------------------------------------ the interpreter loop

    void Vm::execute(VmObserver* observer, std::size_t stop_depth)
    {
        while (true)
        {
            CallFrame& frame = frames_.back();
            const bytecode::Function& function = program_.functions[frame.function];
            const auto instruction = bytecode::decode(function.code, frame.ip);
            current_offset_ = frame.ip;
            if (!instruction)
            {
                fail(DiagId::InvalidBytecode, "execution ran past the end of the function");
            }
            if (options_.max_steps != 0 && stats_.steps - steps_at_frame_ >= options_.max_steps)
            {
                fail(DiagId::StepLimitExceeded, options_.max_steps);
            }
            if (observer != nullptr)
            {
                observer->before_instruction(*this, *instruction);
            }
            ++stats_.steps;
            frame.ip += instruction->size;

            // 'frame' may be invalidated by instructions that push or pop frames, so the handlers
            // below look up frames_.back() themselves when they need it.
            const std::uint32_t a = instruction->a;
            switch (instruction->op)
            {
            case Opcode::Halt:
                return;
            case Opcode::Const:
                push_constant(a);
                break;
            case Opcode::True:
            case Opcode::False:
                push(Value::from_bool(instruction->op == Opcode::True));
                break;
            case Opcode::Pop:
                pop();
                break;
            case Opcode::LoadLocal:
                push(stack_[frame.base + a]);
                break;
            case Opcode::StoreLocal:
            {
                Value value = pop();
                stack_[frames_.back().base + a] = std::move(value);
                break;
            }
            case Opcode::LoadGlobal:
                push(globals_[a]);
                break;
            case Opcode::StoreGlobal:
                globals_[a] = pop();
                break;
            case Opcode::Add:
            case Opcode::Sub:
            case Opcode::Mul:
            case Opcode::Neg:
                arithmetic(instruction->op);
                break;
            case Opcode::Div:
            case Opcode::Mod:
                division(instruction->op);
                break;
            case Opcode::Not:
                push(Value::from_bool(!pop_bool("NOT")));
                break;
            case Opcode::Concat:
            {
                const Value right = pop();
                const Value left = pop();
                if (!left.is_string() || !right.is_string())
                {
                    fail(DiagId::InvalidBytecode, "CONCAT expects two strings");
                }
                push(Value::from_string(left.as_string() + right.as_string()));
                break;
            }
            case Opcode::Equal:
            case Opcode::NotEqual:
            case Opcode::Less:
            case Opcode::LessEqual:
            case Opcode::Greater:
            case Opcode::GreaterEqual:
                comparison(instruction->op);
                break;
            case Opcode::Jump:
                frame.ip = a;
                break;
            case Opcode::JumpIfFalse:
                if (!pop_bool("JUMP_IF_FALSE"))
                {
                    frames_.back().ip = a;
                }
                break;
            case Opcode::Call:
                call_function(static_cast<std::uint16_t>(a));
                break;
            case Opcode::CallBuiltin:
                call_builtin(static_cast<std::uint8_t>(a), instruction->b, observer);
                break;
            case Opcode::CallNative:
                call_native(a, observer);
                break;
            case Opcode::Return:
            case Opcode::ReturnVoid:
                if (!return_from_function(instruction->op == Opcode::Return) || frames_.size() == stop_depth)
                {
                    return; // the program ended, or a function called by a built-in (map, ...) returned
                }
                break;
            case Opcode::MakeArray:
                make_array(a);
                break;
            case Opcode::IndexGet:
                index_get();
                break;
            case Opcode::IndexSet:
                index_set();
                break;
            case Opcode::Dup:
            {
                if (stack_.size() <= operand_floor())
                {
                    fail(DiagId::InvalidBytecode, "operand stack underflow");
                }
                Value top = stack_.back();
                push(std::move(top));
                break;
            }
            case Opcode::Dup2:
            {
                if (stack_.size() < operand_floor() + 2)
                {
                    fail(DiagId::InvalidBytecode, "operand stack underflow");
                }
                const Value first = stack_[stack_.size() - 2];
                const Value second = stack_.back();
                push(first);
                push(second);
                break;
            }
            case Opcode::IntToFloat:
            {
                const Value value = pop();
                if (!value.is_int())
                {
                    fail(DiagId::InvalidBytecode, "INT_TO_FLOAT expects an int operand");
                }
                push(Value::from_float(static_cast<double>(value.as_int())));
                break;
            }
            case Opcode::NewStruct:
                make_struct(a);
                break;
            case Opcode::GetField:
                get_field(a);
                break;
            case Opcode::SetField:
                set_field(a);
                break;
            case Opcode::MakeTuple:
                make_tuple(a);
                break;
            case Opcode::TupleGet:
                tuple_get(a);
                break;
            case Opcode::MakeMap:
                make_map(a);
                break;
            case Opcode::FuncRef:
                push(a == bytecode::no_function
                        ? Value::from_function(bytecode::no_function, nullptr)
                        : Value::from_function(static_cast<std::uint16_t>(a), &program_.functions[a].name));
                break;
            case Opcode::CallValue:
                call_value(a);
                break;
            }
        }
    }

    void Vm::call_value(std::uint32_t argument_count)
    {
        const Value callee = pop();
        if (!callee.is_function())
        {
            fail(DiagId::InvalidBytecode, "CALL_VALUE expects a function value");
        }
        const std::uint16_t index = callee.as_function().index;
        if (index == bytecode::no_function)
        {
            fail(DiagId::FunctionNotSet);
        }
        if (program_.functions[index].arity != argument_count)
        {
            fail(DiagId::InvalidBytecode, "CALL_VALUE: wrong number of arguments for the function");
        }
        call_function(index);
    }

    Value Vm::invoke(const Value& function, std::vector<Value> arguments, VmObserver* observer)
    {
        const std::uint32_t saved_offset = current_offset_; // errors after the call must point at the built-in
        const std::size_t depth = frames_.size();
        const auto count = static_cast<std::uint32_t>(arguments.size());
        for (Value& argument : arguments)
        {
            push(std::move(argument));
        }
        push(function);
        call_value(count);
        execute(observer, depth);
        current_offset_ = saved_offset;
        if (!program_.functions[function.as_function().index].returns_value)
        {
            return Value{};
        }
        return pop();
    }

    void Vm::push_constant(std::uint32_t index)
    {
        const bytecode::Constant& constant = program_.constants[index];
        if (const auto* number = std::get_if<std::int64_t>(&constant))
        {
            push(Value::from_int(*number));
        }
        else if (const auto* decimal = std::get_if<double>(&constant))
        {
            push(Value::from_float(*decimal));
        }
        else
        {
            push(Value::from_string(std::get<std::string>(constant)));
        }
    }

    void Vm::arithmetic(Opcode op)
    {
        const std::string_view name = bytecode::opcode_info(op).mnemonic;
        if (op == Opcode::Neg)
        {
            if (!stack_.empty() && stack_.back().is_float())
            {
                push(Value::from_float(-pop().as_float()));
                return;
            }
            const std::int64_t value = pop_int(name);
            if (value == int_min)
            {
                fail(DiagId::IntegerOverflow, 0, "-", value);
            }
            push(Value::from_int(-value));
            return;
        }

        // The type checker guarantees that both operands have the same type, so one float means two.
        if (stack_.size() >= 2 && (stack_.back().is_float() || stack_[stack_.size() - 2].is_float()))
        {
            float_arithmetic(op);
            return;
        }

        const std::int64_t right = pop_int(name);
        const std::int64_t left = pop_int(name);
        std::optional<std::int64_t> result;
        const char* symbol = "+";
        if (op == Opcode::Add)
        {
            result = checked_add(left, right);
        }
        else if (op == Opcode::Sub)
        {
            result = checked_sub(left, right);
            symbol = "-";
        }
        else
        {
            result = checked_mul(left, right);
            symbol = "*";
        }
        if (!result)
        {
            fail(DiagId::IntegerOverflow, left, symbol, right);
        }
        push(Value::from_int(*result));
    }

    void Vm::float_arithmetic(Opcode op)
    {
        const std::string_view name = bytecode::opcode_info(op).mnemonic;
        const double right = pop_float(name);
        const double left = pop_float(name);
        double result = 0.0;
        const char* symbol = "+";
        if (op == Opcode::Add)
        {
            result = left + right;
        }
        else if (op == Opcode::Sub)
        {
            result = left - right;
            symbol = "-";
        }
        else if (op == Opcode::Mul)
        {
            result = left * right;
            symbol = "*";
        }
        else
        {
            if (right == 0.0)
            {
                fail(DiagId::DivisionByZero);
            }
            result = left / right;
            symbol = "/";
        }
        if (!std::isfinite(result))
        {
            // Thunder has no infinities or NaN: a result that cannot be represented is an error.
            fail(DiagId::FloatOverflow, locale().format_float(left), symbol,
                locale().format_float(right));
        }
        push(Value::from_float(result));
    }

    void Vm::division(Opcode op)
    {
        const bool divide = op == Opcode::Div;
        const std::string_view name = bytecode::opcode_info(op).mnemonic;
        if (divide && stack_.size() >= 2 && (stack_.back().is_float() || stack_[stack_.size() - 2].is_float()))
        {
            float_arithmetic(op);
            return;
        }
        const std::int64_t right = pop_int(name);
        const std::int64_t left = pop_int(name);
        if (right == 0)
        {
            fail(DiagId::DivisionByZero);
        }
        if (left == int_min && right == -1)
        {
            // The quotient does not fit in 64 bits; the remainder is simply 0.
            if (divide)
            {
                fail(DiagId::IntegerOverflow, left, "/", right);
            }
            push(Value::from_int(0));
            return;
        }
        push(Value::from_int(divide ? left / right : left % right));
    }

    void Vm::comparison(Opcode op)
    {
        const Value right = pop();
        const Value left = pop();
        if (op == Opcode::Equal || op == Opcode::NotEqual)
        {
            const bool equal = left == right;
            push(Value::from_bool(op == Opcode::Equal ? equal : !equal));
            return;
        }

        int order = 0; // negative, zero or positive, like strcmp
        if (left.is_int() && right.is_int())
        {
            order = left.as_int() < right.as_int() ? -1 : (left.as_int() > right.as_int() ? 1 : 0);
        }
        else if ((left.is_float() || left.is_int()) && (right.is_float() || right.is_int()))
        {
            const double a = left.as_number();
            const double b = right.as_number();
            order = a < b ? -1 : (a > b ? 1 : 0);
        }
        else if (left.is_string() && right.is_string())
        {
            // Byte order of UTF-8 equals code point order.
            const int c = left.as_string().compare(right.as_string());
            order = c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
        else
        {
            fail(DiagId::InvalidBytecode, "comparison expects two numbers or two strings");
        }

        bool result = order >= 0; // GreaterEqual
        if (op == Opcode::Less)
        {
            result = order < 0;
        }
        else if (op == Opcode::LessEqual)
        {
            result = order <= 0;
        }
        else if (op == Opcode::Greater)
        {
            result = order > 0;
        }
        push(Value::from_bool(result));
    }

    bool Vm::return_from_function(bool has_value)
    {
        Value result = has_value ? pop() : Value{};
        stack_.resize(frames_.back().base); // discard the callee's locals and operands
        frames_.pop_back();
        if (frames_.empty())
        {
            return false;
        }
        if (has_value)
        {
            push(std::move(result));
        }
        return true;
    }

    std::vector<Value> Vm::pop_values(std::size_t count)
    {
        if (stack_.size() < operand_floor() + count)
        {
            fail(DiagId::InvalidBytecode, "operand stack underflow");
        }
        std::vector<Value> values(std::make_move_iterator(stack_.end() - static_cast<std::ptrdiff_t>(count)),
            std::make_move_iterator(stack_.end()));
        stack_.resize(stack_.size() - count);
        return values;
    }

    void Vm::make_array(std::uint32_t count)
    {
        push(Value::from_array(pop_values(count)));
    }

    void Vm::make_tuple(std::uint32_t count)
    {
        push(Value::from_tuple(pop_values(count)));
    }

    void Vm::make_map(std::uint32_t count)
    {
        std::vector<Value> flat = pop_values(static_cast<std::size_t>(count) * 2);
        std::vector<std::pair<Value, Value>> entries;
        entries.reserve(count);
        for (std::size_t i = 0; i < flat.size(); i += 2)
        {
            if (!flat[i].is_int() && !flat[i].is_bool() && !flat[i].is_string())
            {
                fail(DiagId::InvalidBytecode, "MAKE_MAP expects int, bool or string keys");
            }
            entries.emplace_back(std::move(flat[i]), std::move(flat[i + 1]));
        }
        push(Value::from_map(std::move(entries)));
    }

    void Vm::tuple_get(std::uint32_t index)
    {
        const Value target = pop();
        if (!target.is_tuple() || index >= target.as_tuple().elements.size())
        {
            fail(DiagId::InvalidBytecode, "TUPLE_GET expects a tuple with that element");
        }
        push(target.as_tuple().elements[index]);
    }

    std::size_t Vm::operand_floor() const
    {
        const CallFrame& current = frames_.back();
        return current.base + program_.functions[current.function].local_count;
    }

    void Vm::make_struct(std::uint32_t index)
    {
        const StructLayout& layout = program_.structs[index];
        const std::size_t count = layout.field_names.size();
        if (stack_.size() < operand_floor() + count)
        {
            fail(DiagId::InvalidBytecode, "operand stack underflow");
        }
        std::vector<Value> fields(std::make_move_iterator(stack_.end() - static_cast<std::ptrdiff_t>(count)),
            std::make_move_iterator(stack_.end()));
        stack_.resize(stack_.size() - count);
        push(Value::from_struct(&layout, std::move(fields)));
    }

    void Vm::get_field(std::uint32_t index)
    {
        const Value target = pop();
        if (!target.is_struct() || index >= target.as_struct().fields.size())
        {
            fail(DiagId::InvalidBytecode, "GET_FIELD expects a struct with that field");
        }
        push(target.as_struct().fields[index]);
    }

    void Vm::set_field(std::uint32_t index)
    {
        Value value = pop();
        const Value target = pop();
        if (!target.is_struct() || index >= target.as_struct().fields.size())
        {
            fail(DiagId::InvalidBytecode, "SET_FIELD expects a struct with that field");
        }
        target.as_struct().fields[index] = std::move(value);
    }

    void Vm::index_get()
    {
        const Value key = pop();
        const Value target = pop();
        if (target.is_map())
        {
            const Value* found = target.as_map().find(key);
            if (found == nullptr)
            {
                fail(DiagId::KeyNotFound, key.to_debug(locale()));
            }
            push(*found);
            return;
        }
        if (!key.is_int())
        {
            fail(DiagId::InvalidBytecode, "INDEX_GET expects an int index");
        }
        const std::int64_t index = key.as_int();
        if (target.is_array())
        {
            const auto& elements = target.as_array().elements;
            if (index < 0 || static_cast<std::uint64_t>(index) >= elements.size())
            {
                fail(DiagId::ArrayIndexOutOfBounds, index, elements.size());
            }
            push(elements[static_cast<std::size_t>(index)]);
        }
        else if (target.is_string())
        {
            const std::vector<std::string> chars = utf8::code_points(target.as_string());
            if (index < 0 || static_cast<std::uint64_t>(index) >= chars.size())
            {
                fail(DiagId::StringIndexOutOfBounds, index, chars.size());
            }
            push(Value::from_string(chars[static_cast<std::size_t>(index)]));
        }
        else
        {
            fail(DiagId::InvalidBytecode, "INDEX_GET expects an array or a string");
        }
    }

    void Vm::index_set()
    {
        Value value = pop();
        const Value key = pop();
        const Value target = pop();
        if (target.is_map())
        {
            if (!key.is_int() && !key.is_bool() && !key.is_string())
            {
                fail(DiagId::InvalidBytecode, "INDEX_SET expects an int, bool or string key");
            }
            target.as_map().set(key, std::move(value));
            return;
        }
        if (!target.is_array() || !key.is_int())
        {
            fail(DiagId::InvalidBytecode, "INDEX_SET expects an array and an int index");
        }
        const std::int64_t index = key.as_int();
        auto& elements = target.as_array().elements;
        if (index < 0 || static_cast<std::uint64_t>(index) >= elements.size())
        {
            fail(DiagId::ArrayIndexOutOfBounds, index, elements.size());
        }
        elements[static_cast<std::size_t>(index)] = std::move(value);
    }

    void Vm::call_builtin(std::uint8_t id, std::uint32_t argument_count, VmObserver* observer)
    {
        const CallFrame& frame = frames_.back();
        const std::size_t floor = frame.base + program_.functions[frame.function].local_count;
        if (stack_.size() < floor + argument_count)
        {
            fail(DiagId::InvalidBytecode, "not enough arguments on the stack for CALL_BUILTIN");
        }
        std::vector<Value> args(std::make_move_iterator(stack_.end() - argument_count), std::make_move_iterator(stack_.end()));
        stack_.resize(stack_.size() - argument_count);

        const auto builtin = builtin_from_index(id);
        if (!builtin)
        {
            fail(DiagId::InvalidBytecode, "unknown built-in function");
        }
        switch (*builtin)
        {
        case BuiltinId::Print:
        case BuiltinId::Println:
        {
            std::string text;
            for (const Value& arg : args)
            {
                text += arg.to_display(locale());
            }
            const bool newline = *builtin == BuiltinId::Println;
            if (newline)
            {
                text += '\n';
            }
            write_output(text, newline, observer);
            break;
        }
        case BuiltinId::Len:
            if (args[0].is_string())
            {
                push(Value::from_int(static_cast<std::int64_t>(utf8::length(args[0].as_string()))));
            }
            else if (args[0].is_array())
            {
                push(Value::from_int(static_cast<std::int64_t>(args[0].as_array().elements.size())));
            }
            else if (args[0].is_tuple())
            {
                push(Value::from_int(static_cast<std::int64_t>(args[0].as_tuple().elements.size())));
            }
            else if (args[0].is_map())
            {
                push(Value::from_int(static_cast<std::int64_t>(args[0].as_map().entries.size())));
            }
            else
            {
                fail(DiagId::InvalidBytecode, "len expects a string, an array, a tuple or a map");
            }
            break;
        case BuiltinId::Has:
        case BuiltinId::Remove:
        case BuiltinId::Keys:
        case BuiltinId::Values:
        {
            if (!args[0].is_map())
            {
                fail(DiagId::InvalidBytecode, "a map built-in expects a map");
            }
            MapObject& map = args[0].as_map();
            if (*builtin == BuiltinId::Has)
            {
                push(Value::from_bool(map.find(args[1]) != nullptr));
            }
            else if (*builtin == BuiltinId::Remove)
            {
                map.erase(args[1]);
            }
            else
            {
                std::vector<Value> items;
                items.reserve(map.entries.size());
                for (const auto& [key, value] : map.entries)
                {
                    items.push_back(*builtin == BuiltinId::Keys ? key : value);
                }
                push(Value::from_array(std::move(items)));
            }
            break;
        }
        case BuiltinId::Str:
            push(Value::from_string(args[0].to_display(locale())));
            break;
        case BuiltinId::Inspect:
            push(Value::from_string(args[0].inspect(locale())));
            break;
        case BuiltinId::Dump:
            write_output(args[0].inspect(locale()) + "\n", true, observer);
            break;
        case BuiltinId::Copy:
            push(args[0].deep_copy());
            break;
        case BuiltinId::ToInt:
        {
            if (args[0].is_float())
            {
                // Converting a decimal to a whole number drops the fractional part: to_int(2.9) is 2.
                const double value = args[0].as_float();
                if (!std::isfinite(value) || std::abs(value) > 9.007199254740992e15)
                {
                    fail(DiagId::FloatOverflow, locale().format_float(value), "->",
                        language_->spelling(TokenKind::KwInt));
                }
                push(Value::from_int(static_cast<std::int64_t>(value)));
                break;
            }
            if (args[0].is_int())
            {
                push(args[0]);
                break;
            }
            if (!args[0].is_string())
            {
                fail(DiagId::InvalidBytecode, "to_int expects a string or a number");
            }
            const auto number = parse_int(args[0].as_string());
            if (!number)
            {
                fail(DiagId::InvalidIntConversion, args[0].as_string());
            }
            push(Value::from_int(*number));
            break;
        }
        case BuiltinId::Input:
            if (!args.empty())
            {
                write_output(args[0].to_display(locale()), true, observer); // the prompt
            }
            output_.flush();
            push(Value::from_string(read_line()));
            break;
        case BuiltinId::EndOfInput:
            push(Value::from_bool(line_source_ != nullptr ? line_source_->at_end()
                                                          : input_ == nullptr || input_->peek() == std::char_traits<char>::eof()));
            break;
        case BuiltinId::Array:
        {
            if (!args[0].is_int())
            {
                fail(DiagId::InvalidBytecode, "array expects an int size");
            }
            const std::int64_t size = args[0].as_int();
            if (size < 0)
            {
                fail(DiagId::NegativeArraySize, size);
            }
            if (static_cast<std::uint64_t>(size) > options_.max_array_size)
            {
                fail(DiagId::ArrayTooLarge, size, options_.max_array_size);
            }
            std::vector<Value> elements;
            elements.reserve(static_cast<std::size_t>(size));
            for (std::int64_t i = 0; i < size; ++i)
            {
                elements.push_back(args[1].deep_copy());
            }
            push(Value::from_array(std::move(elements)));
            break;
        }
        default:
            if (!text_builtin(*builtin, args) && !list_builtin(*builtin, args, observer)
                && !number_builtin(*builtin, args) && !os_builtin(*builtin, args, observer)
                && !graphics_builtin(*builtin, args))
            {
                fail(DiagId::InvalidBytecode, "unknown built-in function");
            }
            break;
        }
    }

    std::string Vm::read_line()
    {
        std::string line;
        if (line_source_ != nullptr)
        {
            line = line_source_->read_line().value_or(std::string());
        }
        else if (input_ == nullptr || !std::getline(*input_, line))
        {
            return {};
        }
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back(); // a Windows line ending
        }
        return line;
    }
}
