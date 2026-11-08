// The virtual machine's library built-ins: text, list, tuple and operating-system functions.
// (The core built-ins - print, len, str, ... - are in vm.cpp.)
//
// Semantic analysis has checked every call, so a value of an unexpected kind here means the
// bytecode was not produced by thunderc; it is reported as invalid bytecode, never trusted.

#include "thunder/os.hpp"
#include "thunder/utf8.hpp"
#include "thunder/vm.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace thunder
{
    namespace
    {
        std::string join_code_points(const std::vector<std::string>& chars, std::size_t begin, std::size_t end)
        {
            std::string out;
            for (std::size_t i = begin; i < end; ++i)
            {
                out += chars[i];
            }
            return out;
        }

        // The code-point position of the first 'part' in 'text', or -1.
        std::int64_t find_text(const std::string& text, const std::string& part)
        {
            const std::size_t byte = text.find(part);
            if (byte == std::string::npos)
            {
                return -1;
            }
            return static_cast<std::int64_t>(utf8::length(std::string_view(text).substr(0, byte)));
        }

        bool is_trimmed(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        // Ordering for sort(): ints numerically, strings by code point.
        bool less_than(const Value& left, const Value& right)
        {
            if (left.is_int() && right.is_int())
            {
                return left.as_int() < right.as_int();
            }
            if (left.is_float() || right.is_float())
            {
                return left.as_number() < right.as_number();
            }
            return left.as_string() < right.as_string(); // UTF-8 byte order is code point order
        }
    }

    std::pair<std::size_t, std::size_t> Vm::checked_range(std::int64_t start, std::int64_t end, std::size_t length)
    {
        if (start < 0 || end < start || static_cast<std::uint64_t>(end) > length)
        {
            fail(DiagId::RangeOutOfBounds, start, end, length);
        }
        return {static_cast<std::size_t>(start), static_cast<std::size_t>(end)};
    }

    bool Vm::text_builtin(BuiltinId id, std::vector<Value>& args)
    {
        const auto text = [&](std::size_t i) -> const std::string&
        {
            if (i >= args.size() || !args[i].is_string())
            {
                fail(DiagId::InvalidBytecode, "a text built-in expects a string");
            }
            return args[i].as_string();
        };
        const auto number = [&](std::size_t i)
        {
            if (i >= args.size() || !args[i].is_int())
            {
                fail(DiagId::InvalidBytecode, "a text built-in expects an int");
            }
            return args[i].as_int();
        };

        switch (id)
        {
        case BuiltinId::Substring:
        {
            const std::vector<std::string> chars = utf8::code_points(text(0));
            const auto [begin, end] = checked_range(number(1), number(2), chars.size());
            push(Value::from_string(join_code_points(chars, begin, end)));
            return true;
        }
        case BuiltinId::Trim:
        {
            std::string_view view = text(0);
            while (!view.empty() && is_trimmed(view.front()))
            {
                view.remove_prefix(1);
            }
            while (!view.empty() && is_trimmed(view.back()))
            {
                view.remove_suffix(1);
            }
            push(Value::from_string(std::string(view)));
            return true;
        }
        case BuiltinId::ToUpper:
        case BuiltinId::ToLower:
        {
            // Only the English letters change: other scripts (such as Pashto) have no case.
            std::string result = text(0);
            const bool upper = id == BuiltinId::ToUpper;
            for (char& c : result)
            {
                if (upper && c >= 'a' && c <= 'z')
                {
                    c = static_cast<char>(c - 'a' + 'A');
                }
                else if (!upper && c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            push(Value::from_string(std::move(result)));
            return true;
        }
        case BuiltinId::Split:
        {
            const std::string& source = text(0);
            const std::string& separator = text(1);
            std::vector<Value> parts;
            if (separator.empty())
            {
                for (std::string& c : utf8::code_points(source))
                {
                    parts.push_back(Value::from_string(std::move(c)));
                }
            }
            else
            {
                std::size_t start = 0;
                while (true)
                {
                    const std::size_t found = source.find(separator, start);
                    parts.push_back(Value::from_string(source.substr(start, found - start)));
                    if (found == std::string::npos)
                    {
                        break;
                    }
                    start = found + separator.size();
                }
            }
            push(Value::from_array(std::move(parts)));
            return true;
        }
        case BuiltinId::Join:
        {
            if (!args[0].is_array())
            {
                fail(DiagId::InvalidBytecode, "join expects an array of strings");
            }
            const std::string& separator = text(1);
            std::string result;
            bool first = true;
            for (const Value& part : args[0].as_array().elements)
            {
                if (!part.is_string())
                {
                    fail(DiagId::InvalidBytecode, "join expects an array of strings");
                }
                result += (first ? "" : separator) + part.as_string();
                first = false;
            }
            push(Value::from_string(std::move(result)));
            return true;
        }
        case BuiltinId::Replace:
        {
            const std::string& source = text(0);
            const std::string& old_text = text(1);
            const std::string& new_text = text(2);
            if (old_text.empty())
            {
                push(args[0]); // nothing to look for
                return true;
            }
            std::string result;
            std::size_t start = 0;
            for (std::size_t found = source.find(old_text); found != std::string::npos;
                 found = source.find(old_text, start))
            {
                result += source.substr(start, found - start) + new_text;
                start = found + old_text.size();
            }
            result += source.substr(start);
            push(Value::from_string(std::move(result)));
            return true;
        }
        case BuiltinId::StartsWith:
            push(Value::from_bool(text(0).starts_with(text(1))));
            return true;
        case BuiltinId::EndsWith:
            push(Value::from_bool(text(0).ends_with(text(1))));
            return true;
        case BuiltinId::Repeat:
        {
            const std::int64_t count = number(1);
            if (count < 0)
            {
                fail(DiagId::NegativeCount, language_->spelling(id), count);
            }
            const std::string& part = text(0);
            if (!part.empty() && static_cast<std::uint64_t>(count) > options_.max_array_size / part.size())
            {
                fail(DiagId::ArrayTooLarge, count, options_.max_array_size);
            }
            std::string result;
            result.reserve(part.size() * static_cast<std::size_t>(count));
            for (std::int64_t i = 0; i < count; ++i)
            {
                result += part;
            }
            push(Value::from_string(std::move(result)));
            return true;
        }
        case BuiltinId::CharCode:
        {
            const std::string& c = text(0);
            if (utf8::length(c) != 1)
            {
                fail(DiagId::NotOneCharacter, c);
            }
            push(Value::from_int(static_cast<std::int64_t>(utf8::decode(c, 0).code_point)));
            return true;
        }
        case BuiltinId::FromCharCode:
        {
            const std::int64_t code = number(0);
            if (code < 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
            {
                fail(DiagId::InvalidCharCode, code);
            }
            std::string result;
            utf8::append(result, static_cast<char32_t>(code));
            push(Value::from_string(std::move(result)));
            return true;
        }
        case BuiltinId::IsInt:
            push(Value::from_bool(parse_int(text(0)).has_value()));
            return true;
        default:
            return false;
        }
    }

    bool Vm::list_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer)
    {
        const auto array = [&](std::size_t i) -> std::vector<Value>&
        {
            if (i >= args.size() || !args[i].is_array())
            {
                fail(DiagId::InvalidBytecode, "a list built-in expects an array");
            }
            return args[i].as_array().elements;
        };
        const auto number = [&](std::size_t i)
        {
            if (i >= args.size() || !args[i].is_int())
            {
                fail(DiagId::InvalidBytecode, "a list built-in expects an int");
            }
            return args[i].as_int();
        };
        const auto tuple = [&](std::size_t i, std::size_t element) -> const Value&
        {
            if (!args[i].is_tuple() || args[i].as_tuple().elements.size() <= element)
            {
                fail(DiagId::InvalidBytecode, "a tuple built-in expects a tuple");
            }
            return args[i].as_tuple().elements[element];
        };
        const auto check_size = [&](std::uint64_t size)
        {
            if (size > options_.max_array_size)
            {
                fail(DiagId::ArrayTooLarge, size, options_.max_array_size);
            }
        };

        switch (id)
        {
        case BuiltinId::Contains:
        case BuiltinId::IndexOf:
        {
            std::int64_t position = -1;
            if (args[0].is_string())
            {
                if (!args[1].is_string())
                {
                    fail(DiagId::InvalidBytecode, "searching a string needs a string");
                }
                position = find_text(args[0].as_string(), args[1].as_string());
            }
            else
            {
                const std::vector<Value>& elements = array(0);
                const auto found = std::ranges::find(elements, args[1]);
                position = found == elements.end() ? -1 : static_cast<std::int64_t>(found - elements.begin());
            }
            push(id == BuiltinId::Contains ? Value::from_bool(position >= 0) : Value::from_int(position));
            return true;
        }
        case BuiltinId::Reverse:
            if (args[0].is_string())
            {
                std::vector<std::string> chars = utf8::code_points(args[0].as_string());
                std::ranges::reverse(chars);
                push(Value::from_string(join_code_points(chars, 0, chars.size())));
            }
            else
            {
                std::vector<Value> elements = array(0);
                std::ranges::reverse(elements);
                push(Value::from_array(std::move(elements)));
            }
            return true;
        case BuiltinId::Slice:
            if (args[0].is_string())
            {
                const std::vector<std::string> chars = utf8::code_points(args[0].as_string());
                const auto [begin, end] = checked_range(number(1), number(2), chars.size());
                push(Value::from_string(join_code_points(chars, begin, end)));
            }
            else
            {
                const std::vector<Value>& elements = array(0);
                const auto [begin, end] = checked_range(number(1), number(2), elements.size());
                push(Value::from_array(std::vector<Value>(elements.begin() + static_cast<std::ptrdiff_t>(begin),
                    elements.begin() + static_cast<std::ptrdiff_t>(end))));
            }
            return true;
        case BuiltinId::First:
            push(tuple(0, 0));
            return true;
        case BuiltinId::Second:
            push(tuple(0, 1));
            return true;
        case BuiltinId::Swap:
            push(Value::from_tuple({tuple(0, 1), tuple(0, 0)}));
            return true;
        case BuiltinId::Zip:
        {
            const std::vector<Value>& left = array(0);
            const std::vector<Value>& right = array(1);
            std::vector<Value> pairs;
            for (std::size_t i = 0; i < left.size() && i < right.size(); ++i)
            {
                pairs.push_back(Value::from_tuple({left[i], right[i]}));
            }
            push(Value::from_array(std::move(pairs)));
            return true;
        }
        case BuiltinId::Enumerate:
        {
            const std::vector<Value>& elements = array(0);
            std::vector<Value> pairs;
            for (std::size_t i = 0; i < elements.size(); ++i)
            {
                pairs.push_back(Value::from_tuple({Value::from_int(static_cast<std::int64_t>(i)), elements[i]}));
            }
            push(Value::from_array(std::move(pairs)));
            return true;
        }
        case BuiltinId::Items:
        {
            if (!args[0].is_map())
            {
                fail(DiagId::InvalidBytecode, "items expects a map");
            }
            std::vector<Value> pairs;
            for (const auto& [key, value] : args[0].as_map().entries)
            {
                pairs.push_back(Value::from_tuple({key, value}));
            }
            push(Value::from_array(std::move(pairs)));
            return true;
        }
        case BuiltinId::Get:
        {
            if (!args[0].is_map())
            {
                fail(DiagId::InvalidBytecode, "get expects a map");
            }
            const Value* found = args[0].as_map().find(args[1]);
            push(found != nullptr ? *found : args[2]);
            return true;
        }
        case BuiltinId::Push:
        {
            std::vector<Value>& elements = array(0);
            check_size(elements.size() + 1);
            elements.push_back(std::move(args[1]));
            return true;
        }
        case BuiltinId::Pop:
        {
            std::vector<Value>& elements = array(0);
            if (elements.empty())
            {
                fail(DiagId::EmptyArray, language_->spelling(id));
            }
            Value last = std::move(elements.back());
            elements.pop_back();
            push(std::move(last));
            return true;
        }
        case BuiltinId::Insert:
        {
            std::vector<Value>& elements = array(0);
            const std::int64_t index = number(1);
            if (index < 0 || static_cast<std::uint64_t>(index) > elements.size())
            {
                fail(DiagId::ArrayIndexOutOfBounds, index, elements.size());
            }
            check_size(elements.size() + 1);
            elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(index), std::move(args[2]));
            return true;
        }
        case BuiltinId::RemoveAt:
        {
            std::vector<Value>& elements = array(0);
            const std::int64_t index = number(1);
            if (index < 0 || static_cast<std::uint64_t>(index) >= elements.size())
            {
                fail(DiagId::ArrayIndexOutOfBounds, index, elements.size());
            }
            const auto position = elements.begin() + static_cast<std::ptrdiff_t>(index);
            Value removed = std::move(*position);
            elements.erase(position);
            push(std::move(removed));
            return true;
        }
        case BuiltinId::Concat:
        {
            std::vector<Value> elements = array(0);
            const std::vector<Value>& more = array(1);
            check_size(static_cast<std::uint64_t>(elements.size()) + more.size());
            elements.insert(elements.end(), more.begin(), more.end());
            push(Value::from_array(std::move(elements)));
            return true;
        }
        case BuiltinId::Sort:
        {
            std::vector<Value> elements = array(0);
            for (const Value& element : elements)
            {
                if (!element.is_int() && !element.is_float() && !element.is_string())
                {
                    fail(DiagId::InvalidBytecode, "sort expects numbers or strings");
                }
            }
            std::ranges::stable_sort(elements, less_than);
            push(Value::from_array(std::move(elements)));
            return true;
        }
        case BuiltinId::Range:
        {
            const std::int64_t start = number(0);
            const std::int64_t end = number(1);
            std::vector<Value> numbers;
            if (end > start)
            {
                // end - start may not fit in an int64; compare in unsigned arithmetic.
                check_size(static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start));
                numbers.reserve(static_cast<std::size_t>(end - start));
                for (std::int64_t n = start; n < end; ++n)
                {
                    numbers.push_back(Value::from_int(n));
                }
            }
            push(Value::from_array(std::move(numbers)));
            return true;
        }
        case BuiltinId::Sum:
        {
            const std::vector<Value>& elements = array(0);
            if (!elements.empty() && elements.front().is_float())
            {
                double total = 0.0;
                for (const Value& element : elements)
                {
                    total += element.as_number();
                }
                if (!std::isfinite(total))
                {
                    fail(DiagId::FloatOverflow, locale().format_float(total), "+", locale().format_float(0.0));
                }
                push(Value::from_float(total));
                return true;
            }
            std::int64_t total = 0;
            for (const Value& element : elements)
            {
                if (!element.is_int())
                {
                    fail(DiagId::InvalidBytecode, "sum expects numbers");
                }
                const auto next = checked_add(total, element.as_int());
                if (!next)
                {
                    fail(DiagId::IntegerOverflow, total, "+", element.as_int());
                }
                total = *next;
            }
            push(Value::from_int(total));
            return true;
        }
        case BuiltinId::Map:
        case BuiltinId::Filter:
        {
            // Copy the elements first: the function may change the array while we go through it.
            const std::vector<Value> elements = array(0);
            const Value function = args[1];
            std::vector<Value> results;
            for (const Value& element : elements)
            {
                Value result = invoke(function, {element}, observer);
                if (id == BuiltinId::Map)
                {
                    results.push_back(std::move(result));
                }
                else if (!result.is_bool())
                {
                    fail(DiagId::InvalidBytecode, "the function given to filter must return a bool");
                }
                else if (result.as_bool())
                {
                    results.push_back(element);
                }
            }
            push(Value::from_array(std::move(results)));
            return true;
        }
        case BuiltinId::Reduce:
        {
            const std::vector<Value> elements = array(0);
            Value accumulator = args[1];
            const Value function = args[2];
            for (const Value& element : elements)
            {
                accumulator = invoke(function, {accumulator, element}, observer);
            }
            push(std::move(accumulator));
            return true;
        }
        default:
            return false;
        }
    }

    bool Vm::number_builtin(BuiltinId id, std::vector<Value>& args)
    {
        // Every argument has been type-checked, and int arguments to float parameters were widened
        // by the code generator, so 'decimal' only has to accept what the checker allows.
        const auto decimal = [&](std::size_t i)
        {
            if (i >= args.size() || (!args[i].is_float() && !args[i].is_int()))
            {
                fail(DiagId::InvalidBytecode, "a maths built-in expects a number");
            }
            return args[i].as_number();
        };
        const auto whole = [&](double value)
        {
            // The whole numbers a double can hold exactly stop at 2^53, well inside an int64.
            if (!std::isfinite(value) || std::abs(value) > 9.007199254740992e15)
            {
                fail(DiagId::FloatOverflow, locale().format_float(value), "->",
                     language_->spelling(TokenKind::KwInt));
            }
            push(Value::from_int(static_cast<std::int64_t>(value)));
            return true;
        };
        const auto checked = [&](double value)
        {
            if (!std::isfinite(value))
            {
                fail(DiagId::FloatOverflow, locale().format_float(decimal(0)),
                     language_->spelling(id), locale().format_float(args.size() > 1 ? decimal(1) : 0.0));
            }
            push(Value::from_float(value));
            return true;
        };

        switch (id)
        {
        case BuiltinId::ToFloat:
            if (args[0].is_string())
            {
                const auto number = parse_float(args[0].as_string());
                if (!number)
                {
                    fail(DiagId::InvalidFloatConversion, args[0].as_string());
                }
                push(Value::from_float(*number));
                return true;
            }
            push(Value::from_float(decimal(0)));
            return true;
        case BuiltinId::IsNumber:
            if (!args[0].is_string())
            {
                fail(DiagId::InvalidBytecode, "is_number expects a string");
            }
            push(Value::from_bool(parse_float(args[0].as_string()).has_value()));
            return true;
        case BuiltinId::Abs:
            if (args[0].is_int())
            {
                const std::int64_t value = args[0].as_int();
                if (value == std::numeric_limits<std::int64_t>::min())
                {
                    fail(DiagId::IntegerOverflow, 0, "-", value);
                }
                push(Value::from_int(value < 0 ? -value : value));
                return true;
            }
            push(Value::from_float(std::abs(decimal(0))));
            return true;
        case BuiltinId::Min:
        case BuiltinId::Max:
        {
            const bool take_min = id == BuiltinId::Min;
            if (args[0].is_string() && args[1].is_string())
            {
                const bool first = args[0].as_string() < args[1].as_string();
                push(first == take_min ? args[0] : args[1]);
                return true;
            }
            const bool first = decimal(0) < decimal(1);
            push(first == take_min ? args[0] : args[1]);
            return true;
        }
        case BuiltinId::Round:
            return whole(std::round(decimal(0)));
        case BuiltinId::Floor:
            return whole(std::floor(decimal(0)));
        case BuiltinId::Ceil:
            return whole(std::ceil(decimal(0)));
        case BuiltinId::Sqrt:
            if (decimal(0) < 0.0)
            {
                fail(DiagId::MathDomainError, language_->spelling(id), locale().format_float(decimal(0)));
            }
            return checked(std::sqrt(decimal(0)));
        case BuiltinId::Log:
            if (decimal(0) <= 0.0)
            {
                fail(DiagId::MathDomainError, language_->spelling(id), locale().format_float(decimal(0)));
            }
            return checked(std::log(decimal(0)));
        case BuiltinId::Pow:
        {
            const double result = std::pow(decimal(0), decimal(1));
            if (std::isnan(result))
            {
                fail(DiagId::MathDomainError, language_->spelling(id), locale().format_float(decimal(0)));
            }
            return checked(result);
        }
        case BuiltinId::Exp:
            return checked(std::exp(decimal(0)));
        case BuiltinId::Sin:
            return checked(std::sin(decimal(0)));
        case BuiltinId::Cos:
            return checked(std::cos(decimal(0)));
        case BuiltinId::Tan:
            return checked(std::tan(decimal(0)));
        case BuiltinId::Pi:
            push(Value::from_float(3.141592653589793));
            return true;
        default:
            return false;
        }
    }

    bool Vm::os_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer)
    {
        const auto text = [&](std::size_t i) -> const std::string&
        {
            if (i >= args.size() || !args[i].is_string())
            {
                fail(DiagId::InvalidBytecode, "an os built-in expects a string");
            }
            return args[i].as_string();
        };
        const auto strings = [](const std::vector<std::string>& items)
        {
            std::vector<Value> values;
            values.reserve(items.size());
            for (const std::string& item : items)
            {
                values.push_back(Value::from_string(item));
            }
            return Value::from_array(std::move(values));
        };

        switch (id)
        {
        case BuiltinId::Args:
            push(strings(arguments_));
            return true;
        case BuiltinId::GetEnv:
            push(Value::from_string(os::environment_variable(text(0))));
            return true;
        case BuiltinId::Run:
        case BuiltinId::RunOutput:
        {
            output_.flush();
            const auto result = os::run_command(text(0));
            if (!result)
            {
                fail(DiagId::CommandFailed, text(0));
            }
            if (id == BuiltinId::Run)
            {
                // The command's output becomes part of the program's output (also in the IDE).
                write_output(result->output, true, observer);
                push(Value::from_int(result->status));
            }
            else
            {
                push(Value::from_tuple({Value::from_int(result->status), Value::from_string(result->output)}));
            }
            return true;
        }
        case BuiltinId::ReadFile:
        {
            auto contents = os::read_text_file(text(0));
            if (!contents)
            {
                fail(DiagId::FileReadFailed, text(0), contents.error());
            }
            push(Value::from_string(std::move(*contents)));
            return true;
        }
        case BuiltinId::WriteFile:
        case BuiltinId::AppendFile:
            if (const auto problem = os::write_text_file(text(0), text(1), id == BuiltinId::AppendFile))
            {
                fail(DiagId::FileWriteFailed, text(0), *problem);
            }
            return true;
        case BuiltinId::FileExists:
            push(Value::from_bool(os::path_exists(text(0))));
            return true;
        case BuiltinId::RemoveFile:
            if (const auto problem = os::remove_file(text(0)))
            {
                fail(DiagId::FileWriteFailed, text(0), *problem);
            }
            return true;
        case BuiltinId::ListDir:
        {
            const auto names = os::list_directory(text(0));
            if (!names)
            {
                fail(DiagId::FileReadFailed, text(0), names.error());
            }
            push(strings(*names));
            return true;
        }
        case BuiltinId::CurrentDir:
            push(Value::from_string(os::current_directory()));
            return true;
        case BuiltinId::Time:
            push(Value::from_int(os::unix_time()));
            return true;
        case BuiltinId::ClockMs:
        {
            const auto elapsed = std::chrono::steady_clock::now() - started_;
            push(Value::from_int(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
            return true;
        }
        case BuiltinId::Sleep:
            if (!args[0].is_int())
            {
                fail(DiagId::InvalidBytecode, "sleep expects an int");
            }
            output_.flush();
            os::sleep_milliseconds(args[0].as_int());
            return true;
        case BuiltinId::DateTime:
            push(Value::from_string(os::local_date_time()));
            return true;
        case BuiltinId::Platform:
            push(Value::from_string(os::platform_name()));
            return true;
        case BuiltinId::Exit:
        {
            if (!args[0].is_int())
            {
                fail(DiagId::InvalidBytecode, "exit expects an int");
            }
            const std::int64_t code = args[0].as_int();
            output_.flush();
            throw ExitRequest{static_cast<int>(std::clamp<std::int64_t>(code, std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max()))};
        }
        default:
            return false;
        }
    }
}
