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
#include <unordered_map>
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

    // ------------------------------------------------------------------ arguments
    //
    // Every call was type-checked, so a value of another kind means the bytecode is not thunderc's.

    const std::string& Vm::string_arg(const std::vector<Value>& args, std::size_t i)
    {
        if (i >= args.size() || !args[i].is_string())
        {
            fail(DiagId::InvalidBytecode, "a built-in expects a string");
        }
        return args[i].as_string();
    }

    std::int64_t Vm::int_arg(const std::vector<Value>& args, std::size_t i)
    {
        if (i >= args.size() || !args[i].is_int())
        {
            fail(DiagId::InvalidBytecode, "a built-in expects an int");
        }
        return args[i].as_int();
    }

    double Vm::number_arg(const std::vector<Value>& args, std::size_t i)
    {
        // Int arguments to float parameters were widened by the code generator, so a number of
        // either kind is accepted here.
        if (i >= args.size() || (!args[i].is_float() && !args[i].is_int()))
        {
            fail(DiagId::InvalidBytecode, "a built-in expects a number");
        }
        return args[i].as_number();
    }

    std::vector<Value>& Vm::array_arg(std::vector<Value>& args, std::size_t i)
    {
        if (i >= args.size() || !args[i].is_array())
        {
            fail(DiagId::InvalidBytecode, "a built-in expects an array");
        }
        return args[i].as_array().elements;
    }

    const Value& Vm::tuple_element_arg(const std::vector<Value>& args, std::size_t i, std::size_t element)
    {
        if (i >= args.size() || !args[i].is_tuple() || args[i].as_tuple().elements.size() <= element)
        {
            fail(DiagId::InvalidBytecode, "a tuple built-in expects a tuple");
        }
        return args[i].as_tuple().elements[element];
    }

    void Vm::check_array_size(std::uint64_t size)
    {
        if (size > options_.max_array_size)
        {
            fail(DiagId::ArrayTooLarge, size, options_.max_array_size);
        }
    }

    // ------------------------------------------------------------------ text

    bool Vm::text_builtin(BuiltinId id, std::vector<Value>& args)
    {
        switch (id)
        {
        case BuiltinId::Substring:
        {
            const std::vector<std::string> chars = utf8::code_points(string_arg(args, 0));
            const auto [begin, end] = checked_range(int_arg(args, 1), int_arg(args, 2), chars.size());
            push(Value::from_string(join_code_points(chars, begin, end)));
            return true;
        }
        case BuiltinId::Trim:
            push(Value::from_string(trimmed(string_arg(args, 0))));
            return true;
        case BuiltinId::ToUpper:
        case BuiltinId::ToLower:
            push(Value::from_string(ascii_case(string_arg(args, 0), id == BuiltinId::ToUpper)));
            return true;
        case BuiltinId::Split:
            push(split_text(string_arg(args, 0), string_arg(args, 1)));
            return true;
        case BuiltinId::Join:
            push(Value::from_string(join_strings(args)));
            return true;
        case BuiltinId::Replace:
            push(Value::from_string(replace_text(string_arg(args, 0), string_arg(args, 1), string_arg(args, 2))));
            return true;
        case BuiltinId::StartsWith:
            push(Value::from_bool(string_arg(args, 0).starts_with(string_arg(args, 1))));
            return true;
        case BuiltinId::EndsWith:
            push(Value::from_bool(string_arg(args, 0).ends_with(string_arg(args, 1))));
            return true;
        case BuiltinId::Repeat:
            push(Value::from_string(repeat_text(string_arg(args, 0), int_arg(args, 1))));
            return true;
        case BuiltinId::CharCode:
        {
            const std::string& c = string_arg(args, 0);
            if (utf8::length(c) != 1)
            {
                fail(DiagId::NotOneCharacter, c);
            }
            push(Value::from_int(static_cast<std::int64_t>(utf8::decode(c, 0).code_point)));
            return true;
        }
        case BuiltinId::FromCharCode:
        {
            const std::int64_t code = int_arg(args, 0);
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
            push(Value::from_bool(parse_int(string_arg(args, 0)).has_value()));
            return true;
        default:
            return false;
        }
    }

    std::string Vm::trimmed(const std::string& text)
    {
        std::string_view view = text;
        while (!view.empty() && is_trimmed(view.front()))
        {
            view.remove_prefix(1);
        }
        while (!view.empty() && is_trimmed(view.back()))
        {
            view.remove_suffix(1);
        }
        return std::string(view);
    }

    /** Only the English letters change: other scripts (such as Pashto) have no case. */
    std::string Vm::ascii_case(std::string text, bool upper)
    {
        for (char& c : text)
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
        return text;
    }

    /** split(text, separator): an empty separator splits into characters. */
    Value Vm::split_text(const std::string& source, const std::string& separator)
    {
        std::vector<Value> parts;
        if (separator.empty())
        {
            for (std::string& c : utf8::code_points(source))
            {
                parts.push_back(Value::from_string(std::move(c)));
            }
            return Value::from_array(std::move(parts));
        }
        std::size_t start = 0;
        while (true)
        {
            const std::size_t found = source.find(separator, start);
            parts.push_back(Value::from_string(source.substr(start, found - start)));
            if (found == std::string::npos)
            {
                return Value::from_array(std::move(parts));
            }
            start = found + separator.size();
        }
    }

    std::string Vm::join_strings(std::vector<Value>& args)
    {
        const std::string& separator = string_arg(args, 1);
        std::string result;
        bool first = true;
        for (const Value& part : array_arg(args, 0))
        {
            if (!part.is_string())
            {
                fail(DiagId::InvalidBytecode, "join expects an array of strings");
            }
            result += (first ? "" : separator) + part.as_string();
            first = false;
        }
        return result;
    }

    std::string Vm::replace_text(const std::string& source, const std::string& old_text, const std::string& new_text)
    {
        if (old_text.empty())
        {
            return source; // nothing to look for
        }
        std::string result;
        std::size_t start = 0;
        for (std::size_t found = source.find(old_text); found != std::string::npos; found = source.find(old_text, start))
        {
            result += source.substr(start, found - start) + new_text;
            start = found + old_text.size();
        }
        return result + source.substr(start);
    }

    std::string Vm::repeat_text(const std::string& part, std::int64_t count)
    {
        if (count < 0)
        {
            fail(DiagId::NegativeCount, language_->spelling(BuiltinId::Repeat), count);
        }
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
        return result;
    }

    // ------------------------------------------------------------------ lists, tuples and maps

    bool Vm::list_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer)
    {
        return sequence_builtin(id, args) || array_builtin(id, args) || functional_builtin(id, args, observer);
    }

    /** The built-ins that read a sequence (a string or an array), a tuple or a map. */
    bool Vm::sequence_builtin(BuiltinId id, std::vector<Value>& args)
    {
        switch (id)
        {
        case BuiltinId::Contains:
        case BuiltinId::IndexOf:
        {
            const std::int64_t position = find_in_sequence(args);
            push(id == BuiltinId::Contains ? Value::from_bool(position >= 0) : Value::from_int(position));
            return true;
        }
        case BuiltinId::Reverse:
            push(reversed(args));
            return true;
        case BuiltinId::Slice:
            push(sliced(args));
            return true;
        case BuiltinId::First:
            push(tuple_element_arg(args, 0, 0));
            return true;
        case BuiltinId::Second:
            push(tuple_element_arg(args, 0, 1));
            return true;
        case BuiltinId::Swap:
            push(Value::from_tuple({tuple_element_arg(args, 0, 1), tuple_element_arg(args, 0, 0)}));
            return true;
        case BuiltinId::Zip:
        {
            const std::vector<Value>& left = array_arg(args, 0);
            const std::vector<Value>& right = array_arg(args, 1);
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
            std::vector<Value> pairs;
            std::int64_t index = 0;
            for (const Value& element : array_arg(args, 0))
            {
                pairs.push_back(Value::from_tuple({Value::from_int(index++), element}));
            }
            push(Value::from_array(std::move(pairs)));
            return true;
        }
        case BuiltinId::Items:
        {
            std::vector<Value> pairs;
            for (const auto& [key, value] : map_arg(args, 0).entries)
            {
                pairs.push_back(Value::from_tuple({key, value}));
            }
            push(Value::from_array(std::move(pairs)));
            return true;
        }
        case BuiltinId::Get:
        {
            const Value* found = map_arg(args, 0).find(args[1]);
            push(found != nullptr ? *found : args[2]);
            return true;
        }
        default:
            return false;
        }
    }

    MapObject& Vm::map_arg(const std::vector<Value>& args, std::size_t i)
    {
        if (i >= args.size() || !args[i].is_map())
        {
            fail(DiagId::InvalidBytecode, "a map built-in expects a map");
        }
        return args[i].as_map();
    }

    /** contains / index_of: the code-point position of a part in a string, or of an element in an array. */
    std::int64_t Vm::find_in_sequence(std::vector<Value>& args)
    {
        if (args[0].is_string())
        {
            return find_text(args[0].as_string(), string_arg(args, 1));
        }
        const std::vector<Value>& elements = array_arg(args, 0);
        const auto found = std::ranges::find(elements, args[1]);
        return found == elements.end() ? -1 : static_cast<std::int64_t>(found - elements.begin());
    }

    Value Vm::reversed(std::vector<Value>& args)
    {
        if (args[0].is_string())
        {
            std::vector<std::string> chars = utf8::code_points(args[0].as_string());
            std::ranges::reverse(chars);
            return Value::from_string(join_code_points(chars, 0, chars.size()));
        }
        std::vector<Value> elements = array_arg(args, 0);
        std::ranges::reverse(elements);
        return Value::from_array(std::move(elements));
    }

    Value Vm::sliced(std::vector<Value>& args)
    {
        if (args[0].is_string())
        {
            const std::vector<std::string> chars = utf8::code_points(args[0].as_string());
            const auto [begin, end] = checked_range(int_arg(args, 1), int_arg(args, 2), chars.size());
            return Value::from_string(join_code_points(chars, begin, end));
        }
        const std::vector<Value>& elements = array_arg(args, 0);
        const auto [begin, end] = checked_range(int_arg(args, 1), int_arg(args, 2), elements.size());
        return Value::from_array(std::vector<Value>(elements.begin() + static_cast<std::ptrdiff_t>(begin),
            elements.begin() + static_cast<std::ptrdiff_t>(end)));
    }

    /** The built-ins that change or build an array. */
    bool Vm::array_builtin(BuiltinId id, std::vector<Value>& args)
    {
        switch (id)
        {
        case BuiltinId::Push:
        {
            std::vector<Value>& elements = array_arg(args, 0);
            check_array_size(elements.size() + 1);
            elements.push_back(std::move(args[1]));
            return true;
        }
        case BuiltinId::Pop:
        {
            std::vector<Value>& elements = array_arg(args, 0);
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
            std::vector<Value>& elements = array_arg(args, 0);
            const std::size_t index = checked_index(int_arg(args, 1), elements.size() + 1);
            check_array_size(elements.size() + 1);
            elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(index), std::move(args[2]));
            return true;
        }
        case BuiltinId::RemoveAt:
        {
            std::vector<Value>& elements = array_arg(args, 0);
            const auto position = elements.begin() + static_cast<std::ptrdiff_t>(checked_index(int_arg(args, 1), elements.size()));
            Value removed = std::move(*position);
            elements.erase(position);
            push(std::move(removed));
            return true;
        }
        case BuiltinId::Concat:
        {
            std::vector<Value> elements = array_arg(args, 0);
            const std::vector<Value>& more = array_arg(args, 1);
            check_array_size(static_cast<std::uint64_t>(elements.size()) + more.size());
            elements.insert(elements.end(), more.begin(), more.end());
            push(Value::from_array(std::move(elements)));
            return true;
        }
        case BuiltinId::Sort:
        {
            std::vector<Value> elements = array_arg(args, 0);
            if (!std::ranges::all_of(elements, [](const Value& e) { return e.is_int() || e.is_float() || e.is_string(); }))
            {
                fail(DiagId::InvalidBytecode, "sort expects numbers or strings");
            }
            std::ranges::stable_sort(elements, less_than);
            push(Value::from_array(std::move(elements)));
            return true;
        }
        case BuiltinId::Range:
            push(range_of(int_arg(args, 0), int_arg(args, 1)));
            return true;
        case BuiltinId::Sum:
            push(sum_of(array_arg(args, 0)));
            return true;
        default:
            return false;
        }
    }

    /** An index into an array of 'size' elements, or runtime error R011. */
    std::size_t Vm::checked_index(std::int64_t index, std::size_t size)
    {
        if (index < 0 || static_cast<std::uint64_t>(index) >= size)
        {
            fail(DiagId::ArrayIndexOutOfBounds, index, size);
        }
        return static_cast<std::size_t>(index);
    }

    Value Vm::range_of(std::int64_t start, std::int64_t end)
    {
        std::vector<Value> numbers;
        if (end > start)
        {
            // end - start may not fit in an int64; compare in unsigned arithmetic.
            check_array_size(static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start));
            numbers.reserve(static_cast<std::size_t>(end - start));
            for (std::int64_t n = start; n < end; ++n)
            {
                numbers.push_back(Value::from_int(n));
            }
        }
        return Value::from_array(std::move(numbers));
    }

    /** sum(): decimals add as decimals, whole numbers with overflow checked. */
    Value Vm::sum_of(const std::vector<Value>& elements)
    {
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
            return Value::from_float(total);
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
        return Value::from_int(total);
    }

    /** map, filter and reduce: the function is called back for every element. */
    bool Vm::functional_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer)
    {
        if (id != BuiltinId::Map && id != BuiltinId::Filter && id != BuiltinId::Reduce)
        {
            return false;
        }
        // Copy the elements first: the function may change the array while we go through it.
        const std::vector<Value> elements = array_arg(args, 0);
        if (id == BuiltinId::Reduce)
        {
            Value accumulator = args[1];
            const Value function = args[2];
            for (const Value& element : elements)
            {
                accumulator = invoke(function, {accumulator, element}, observer);
            }
            push(std::move(accumulator));
            return true;
        }
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

    // ------------------------------------------------------------------ numbers

    bool Vm::number_builtin(BuiltinId id, std::vector<Value>& args)
    {
        switch (id)
        {
        case BuiltinId::ToFloat:
            push(Value::from_float(args[0].is_string() ? parse_float_arg(args[0].as_string()) : number_arg(args, 0)));
            return true;
        case BuiltinId::IsNumber:
            push(Value::from_bool(parse_float(string_arg(args, 0)).has_value()));
            return true;
        case BuiltinId::Abs:
            push(absolute(args[0]));
            return true;
        case BuiltinId::Min:
        case BuiltinId::Max:
        {
            const bool first_smaller = args[0].is_string() && args[1].is_string()
                ? args[0].as_string() < args[1].as_string()
                : number_arg(args, 0) < number_arg(args, 1);
            push(first_smaller == (id == BuiltinId::Min) ? args[0] : args[1]);
            return true;
        }
        case BuiltinId::Round:
            push(whole_number(std::round(number_arg(args, 0))));
            return true;
        case BuiltinId::Floor:
            push(whole_number(std::floor(number_arg(args, 0))));
            return true;
        case BuiltinId::Ceil:
            push(whole_number(std::ceil(number_arg(args, 0))));
            return true;
        case BuiltinId::Pi:
            push(Value::from_float(3.141592653589793));
            return true;
        default:
            return maths_builtin(id, args);
        }
    }

    /** sqrt, log, pow, exp, sin, cos, tan: a decimal result, checked for the domain and for overflow. */
    bool Vm::maths_builtin(BuiltinId id, std::vector<Value>& args)
    {
        using Function = double (*)(double, double);
        static const std::unordered_map<BuiltinId, Function> functions = {
            {BuiltinId::Sqrt, [](double x, double) { return std::sqrt(x); }},
            {BuiltinId::Log, [](double x, double) { return std::log(x); }},
            {BuiltinId::Pow, [](double x, double y) { return std::pow(x, y); }},
            {BuiltinId::Exp, [](double x, double) { return std::exp(x); }},
            {BuiltinId::Sin, [](double x, double) { return std::sin(x); }},
            {BuiltinId::Cos, [](double x, double) { return std::cos(x); }},
            {BuiltinId::Tan, [](double x, double) { return std::tan(x); }},
        };
        const auto found = functions.find(id);
        if (found == functions.end())
        {
            return false;
        }
        const double x = number_arg(args, 0);
        const double y = args.size() > 1 ? number_arg(args, 1) : 0.0;
        const double result = found->second(x, y);
        const bool outside_domain = (id == BuiltinId::Sqrt && x < 0.0) || (id == BuiltinId::Log && x <= 0.0)
            || (id == BuiltinId::Pow && std::isnan(result));
        if (outside_domain)
        {
            fail(DiagId::MathDomainError, language_->spelling(id), locale().format_float(x));
        }
        if (!std::isfinite(result))
        {
            fail(DiagId::FloatOverflow, locale().format_float(x), language_->spelling(id), locale().format_float(y));
        }
        push(Value::from_float(result));
        return true;
    }

    double Vm::parse_float_arg(const std::string& text)
    {
        const auto number = parse_float(text);
        if (!number)
        {
            fail(DiagId::InvalidFloatConversion, text);
        }
        return *number;
    }

    Value Vm::absolute(const Value& value)
    {
        if (value.is_int())
        {
            const std::int64_t n = value.as_int();
            if (n == std::numeric_limits<std::int64_t>::min())
            {
                fail(DiagId::IntegerOverflow, 0, "-", n);
            }
            return Value::from_int(n < 0 ? -n : n);
        }
        if (!value.is_float())
        {
            fail(DiagId::InvalidBytecode, "abs expects a number");
        }
        return Value::from_float(std::abs(value.as_float()));
    }

    /** A decimal that must become an int (round, floor, ceil, to_int): R0xx when it cannot. */
    Value Vm::whole_number(double value)
    {
        // The whole numbers a double can hold exactly stop at 2^53, well inside an int64.
        if (!std::isfinite(value) || std::abs(value) > 9.007199254740992e15)
        {
            fail(DiagId::FloatOverflow, locale().format_float(value), "->", language_->spelling(TokenKind::KwInt));
        }
        return Value::from_int(static_cast<std::int64_t>(value));
    }

    // ------------------------------------------------------------------ the operating system

    bool Vm::os_builtin(BuiltinId id, std::vector<Value>& args, VmObserver* observer)
    {
        switch (id)
        {
        case BuiltinId::Args:
            push(strings_value(arguments_));
            return true;
        case BuiltinId::GetEnv:
            push(Value::from_string(os::environment_variable(string_arg(args, 0))));
            return true;
        case BuiltinId::Run:
        case BuiltinId::RunOutput:
            push(run_command(string_arg(args, 0), id == BuiltinId::Run, observer));
            return true;
        case BuiltinId::ReadFile:
        case BuiltinId::WriteFile:
        case BuiltinId::AppendFile:
        case BuiltinId::FileExists:
        case BuiltinId::RemoveFile:
        case BuiltinId::ListDir:
            return file_builtin(id, args);
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
            output_.flush();
            os::sleep_milliseconds(int_arg(args, 0));
            return true;
        case BuiltinId::DateTime:
            push(Value::from_string(os::local_date_time()));
            return true;
        case BuiltinId::Platform:
            push(Value::from_string(os::platform_name()));
            return true;
        case BuiltinId::Exit:
        {
            const std::int64_t code = int_arg(args, 0);
            output_.flush();
            throw ExitRequest{static_cast<int>(std::clamp<std::int64_t>(code, std::numeric_limits<int>::min(),
                std::numeric_limits<int>::max()))};
        }
        default:
            return false;
        }
    }

    Value Vm::strings_value(const std::vector<std::string>& items)
    {
        std::vector<Value> values;
        values.reserve(items.size());
        for (const std::string& item : items)
        {
            values.push_back(Value::from_string(item));
        }
        return Value::from_array(std::move(values));
    }

    /** run(command) gives the exit status and echoes the output; run_output(command) returns both. */
    Value Vm::run_command(const std::string& command, bool echo_output, VmObserver* observer)
    {
        output_.flush();
        const auto result = os::run_command(command);
        if (!result)
        {
            fail(DiagId::CommandFailed, command);
        }
        if (!echo_output)
        {
            return Value::from_tuple({Value::from_int(result->status), Value::from_string(result->output)});
        }
        // The command's output becomes part of the program's output (also in the IDE).
        write_output(result->output, true, observer);
        return Value::from_int(result->status);
    }

    bool Vm::file_builtin(BuiltinId id, std::vector<Value>& args)
    {
        const std::string& path = string_arg(args, 0);
        switch (id)
        {
        case BuiltinId::ReadFile:
        {
            auto contents = os::read_text_file(path);
            if (!contents)
            {
                fail(DiagId::FileReadFailed, path, contents.error());
            }
            push(Value::from_string(std::move(*contents)));
            return true;
        }
        case BuiltinId::WriteFile:
        case BuiltinId::AppendFile:
            if (const auto problem = os::write_text_file(path, string_arg(args, 1), id == BuiltinId::AppendFile))
            {
                fail(DiagId::FileWriteFailed, path, *problem);
            }
            return true;
        case BuiltinId::FileExists:
            push(Value::from_bool(os::path_exists(path)));
            return true;
        case BuiltinId::RemoveFile:
            if (const auto problem = os::remove_file(path))
            {
                fail(DiagId::FileWriteFailed, path, *problem);
            }
            return true;
        case BuiltinId::ListDir:
        {
            const auto names = os::list_directory(path);
            if (!names)
            {
                fail(DiagId::FileReadFailed, path, names.error());
            }
            push(strings_value(*names));
            return true;
        }
        default:
            return false;
        }
    }
}
