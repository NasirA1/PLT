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
            if (left.is_integer() && right.is_integer())
            {
                return left.as_integer() < right.as_integer();
            }
            if (left.is_enum() && right.is_enum())
            {
                return left.as_enum().ordinal < right.as_enum().ordinal; // members sort by position
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
        // A byte is accepted wherever an int is: it widens, as the type checker promised.
        if (i >= args.size() || !args[i].is_integer())
        {
            fail(DiagId::InvalidBytecode, "a built-in expects an int");
        }
        return args[i].as_integer();
    }

    double Vm::number_arg(const std::vector<Value>& args, std::size_t i)
    {
        // Int arguments to float parameters were widened by the code generator, so a number of
        // either kind is accepted here.
        if (i >= args.size() || (!args[i].is_float() && !args[i].is_integer()))
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
            if (!std::ranges::all_of(elements,
                    [](const Value& e) { return e.is_integer() || e.is_float() || e.is_string() || e.is_enum(); }))
            {
                fail(DiagId::InvalidBytecode, "sort expects numbers, strings or enum members");
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
            if (!element.is_integer())
            {
                fail(DiagId::InvalidBytecode, "sum expects numbers");
            }
            const auto next = checked_add(total, element.as_integer());
            if (!next)
            {
                fail(DiagId::IntegerOverflow, total, "+", element.as_integer());
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
            // Only this task waits; the others run meanwhile (the same as task.sleep). When the
            // program has no tasks of its own, the program itself waits, as it always did.
            wait_milliseconds(int_arg(args, 0));
            return true;
        case BuiltinId::DateTime:
            push(Value::from_string(os::local_date_time()));
            return true;
        case BuiltinId::Platform:
        {
            // A member of the built-in Platform enum, which "import os" brings into every program
            // that can call platform(); the member names follow the English ones in order.
            const std::string name = os::platform_name();
            const auto& members = builtin_enum_info(BuiltinEnumId::Platform).members;
            std::uint16_t ordinal = static_cast<std::uint16_t>(members.size() - 1); // "unknown"
            for (std::size_t i = 0; i < members.size(); ++i)
            {
                if (members[i] == name)
                {
                    ordinal = static_cast<std::uint16_t>(i);
                }
            }
            for (const EnumLayout& layout : program_.enums)
            {
                if (layout.builtin == static_cast<std::int32_t>(BuiltinEnumId::Platform))
                {
                    push(Value::from_enum(&layout, ordinal));
                    return true;
                }
            }
            push(Value::from_string(name)); // a program compiled without the enum table (never, in practice)
            return true;
        }
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

    // ------------------------------------------------------------------ bytes

    Value Vm::bytes_value(std::string_view data)
    {
        check_array_size(data.size());
        std::vector<Value> elements;
        elements.reserve(data.size());
        for (const char c : data)
        {
            elements.push_back(Value::from_byte(static_cast<std::uint8_t>(c)));
        }
        return Value::from_array(std::move(elements));
    }

    std::string Vm::raw_bytes(const Value& array, const char* what)
    {
        if (!array.is_array())
        {
            fail(DiagId::InvalidBytecode, std::string(what) + " expects a byte array");
        }
        std::string out;
        out.reserve(array.as_array().elements.size());
        for (const Value& element : array.as_array().elements)
        {
            if (!element.is_integer())
            {
                fail(DiagId::InvalidBytecode, std::string(what) + " expects a byte array");
            }
            out.push_back(static_cast<char>(static_cast<std::uint8_t>(element.as_integer() & 0xFF)));
        }
        return out;
    }

    /** to_byte, string_to_bytes, bytes_to_string, bytes_to_int, int_to_bytes, read/write/append_bytes. */
    bool Vm::bytes_builtin(BuiltinId id, std::vector<Value>& args)
    {
        switch (id)
        {
        case BuiltinId::ToByte:
            push(Value::from_byte(static_cast<std::uint8_t>(static_cast<std::uint64_t>(int_arg(args, 0)) & 0xFF)));
            return true;
        case BuiltinId::StringToBytes:
            push(bytes_value(string_arg(args, 0)));
            return true;
        case BuiltinId::BytesToString:
        {
            // Decoded and re-encoded, so that what is not UTF-8 becomes U+FFFD and the result is
            // a well-formed string like every other.
            const std::string data = raw_bytes(args[0], "bytes_to_string");
            std::string text;
            std::size_t pos = 0;
            while (pos < data.size())
            {
                const utf8::Decoded decoded = utf8::decode(data, pos);
                utf8::append(text, decoded.code_point);
                pos += decoded.length;
            }
            push(Value::from_string(std::move(text)));
            return true;
        }
        case BuiltinId::BytesToInt:
        {
            const std::string data = raw_bytes(args[0], "bytes_to_int");
            const std::int64_t start = int_arg(args, 1);
            const std::int64_t count = int_arg(args, 2);
            if (count < 1 || count > 8)
            {
                fail(DiagId::ValueOutOfRange, language_->term("count"), language_->spelling(BuiltinId::BytesToInt), 1, 8, count);
            }
            const auto [from, to] = checked_range(start, start + count, data.size());
            const bool little_endian = args[3].is_bool() && args[3].as_bool();
            const bool is_signed = args[4].is_bool() && args[4].as_bool();
            std::uint64_t bits = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
            {
                const auto byte = static_cast<std::uint8_t>(data[little_endian ? to - 1 - i : from + i]);
                bits = (bits << 8) | byte;
            }
            if (is_signed && count < 8 && (bits >> (count * 8 - 1)) & 1)
            {
                bits |= ~std::uint64_t{0} << (count * 8); // sign-extend two's complement
            }
            push(Value::from_int(static_cast<std::int64_t>(bits)));
            return true;
        }
        case BuiltinId::IntToBytes:
        {
            const auto bits = static_cast<std::uint64_t>(int_arg(args, 0));
            const std::int64_t count = int_arg(args, 1);
            if (count < 1 || count > 8)
            {
                fail(DiagId::ValueOutOfRange, language_->term("count"), language_->spelling(BuiltinId::IntToBytes), 1, 8, count);
            }
            const bool little_endian = args[2].is_bool() && args[2].as_bool();
            std::string data(static_cast<std::size_t>(count), '\0');
            for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
            {
                const auto byte = static_cast<char>((bits >> (8 * i)) & 0xFF); // byte i, lowest first
                data[little_endian ? i : static_cast<std::size_t>(count) - 1 - i] = byte;
            }
            push(bytes_value(data));
            return true;
        }
        case BuiltinId::ReadBytes:
        {
            const std::string& path = string_arg(args, 0);
            auto contents = os::read_binary_file(path);
            if (!contents)
            {
                fail(DiagId::FileReadFailed, path, contents.error());
            }
            push(bytes_value(*contents));
            return true;
        }
        case BuiltinId::WriteBytes:
        case BuiltinId::AppendBytes:
        {
            const std::string& path = string_arg(args, 0);
            const std::string data = raw_bytes(args[1], id == BuiltinId::WriteBytes ? "write_bytes" : "append_bytes");
            if (const auto problem = os::write_text_file(path, data, id == BuiltinId::AppendBytes))
            {
                fail(DiagId::FileWriteFailed, path, *problem);
            }
            return true;
        }
        default:
            return false;
        }
    }

    // ------------------------------------------------------------------ enums

    /** ENUM_FROM: Color(2) or Color("red") - the member at a position, or with a name. */
    void Vm::enum_from(std::uint32_t enum_index)
    {
        if (enum_index >= program_.enums.size())
        {
            fail(DiagId::InvalidBytecode, "ENUM_FROM names an enum that does not exist");
        }
        const EnumLayout& layout = program_.enums[enum_index];
        const Value value = pop();
        if (value.is_integer())
        {
            const std::int64_t position = value.as_integer();
            if (position < 0 || static_cast<std::uint64_t>(position) >= layout.member_names.size())
            {
                fail(DiagId::EnumOutOfRange, position, layout.name, layout.member_names.size() - 1);
            }
            push(Value::from_enum(&layout, static_cast<std::uint16_t>(position)));
            return;
        }
        if (!value.is_string())
        {
            fail(DiagId::InvalidBytecode, "ENUM_FROM expects an int or a string");
        }
        const std::string wanted = fold_letters(value.as_string());
        for (std::size_t i = 0; i < layout.member_names.size(); ++i)
        {
            if (fold_letters(layout.member_names[i]) == wanted)
            {
                push(Value::from_enum(&layout, static_cast<std::uint16_t>(i)));
                return;
            }
        }
        if (layout.builtin >= 0)
        {
            // A built-in enum also answers to its other spellings ("grey") and its English names.
            for (const LanguagePack* pack : {language_, &english_pack()})
            {
                const auto member = pack->builtin_enum_member(wanted);
                if (member && static_cast<std::int32_t>(member->first) == layout.builtin && member->second < layout.member_names.size())
                {
                    push(Value::from_enum(&layout, member->second));
                    return;
                }
            }
        }
        fail(DiagId::EnumUnknownName, value.as_string(), layout.name);
    }

    /** ENUM_VALUES: every member of the enum, in order, as a new array. */
    void Vm::enum_values(std::uint32_t enum_index)
    {
        if (enum_index >= program_.enums.size())
        {
            fail(DiagId::InvalidBytecode, "ENUM_VALUES names an enum that does not exist");
        }
        const EnumLayout& layout = program_.enums[enum_index];
        std::vector<Value> members;
        members.reserve(layout.member_names.size());
        for (std::size_t i = 0; i < layout.member_names.size(); ++i)
        {
            members.push_back(Value::from_enum(&layout, static_cast<std::uint16_t>(i)));
        }
        push(Value::from_array(std::move(members)));
    }
}
