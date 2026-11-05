// minitest: a tiny self-contained unit-test framework (no external dependencies).
//
//   TEST_CASE("lexer: numbers") { CHECK_EQ(1 + 1, 2); }
//
// CHECK* records a failure and continues; REQUIRE stops the current test.
#pragma once

#include <format>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace minitest
{
    struct TestCase
    {
        const char* name;
        void (*function)();
        const char* file;
        int line;
    };

    std::vector<TestCase>& registry();

    struct Registrar
    {
        Registrar(const char* name, void (*function)(), const char* file, int line)
        {
            registry().push_back(TestCase{name, function, file, line});
        }
    };

    struct AbortTest
    {
    };

    void report_failure(const char* file, int line, const std::string& message);

    template <typename T>
    std::string show(const T& value)
    {
        if constexpr (std::formattable<T, char>)
        {
            return std::format("{}", value);
        }
        else if constexpr (requires(std::ostream& os, const T& v) { os << v; })
        {
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }
        else
        {
            return "<value>";
        }
    }
}

#define MT_CONCAT_INNER(a, b) a##b
#define MT_CONCAT(a, b) MT_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                                          \
    static void MT_CONCAT(mt_test_, __LINE__)();                                                                 \
    static const ::minitest::Registrar MT_CONCAT(mt_registrar_, __LINE__)(name, &MT_CONCAT(mt_test_, __LINE__), \
        __FILE__, __LINE__);                                                                                     \
    static void MT_CONCAT(mt_test_, __LINE__)()

#define CHECK(expr)                                                                   \
    do                                                                                \
    {                                                                                 \
        if (!(expr))                                                                  \
        {                                                                             \
            ::minitest::report_failure(__FILE__, __LINE__, "CHECK(" #expr ") failed"); \
        }                                                                             \
    } while (0)

#define REQUIRE(expr)                                                                   \
    do                                                                                  \
    {                                                                                   \
        if (!(expr))                                                                    \
        {                                                                               \
            ::minitest::report_failure(__FILE__, __LINE__, "REQUIRE(" #expr ") failed"); \
            throw ::minitest::AbortTest{};                                              \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                                           \
    do                                                                                                           \
    {                                                                                                            \
        const auto mt_left = (a);                                                                                \
        const auto mt_right = (b);                                                                               \
        if (!(mt_left == mt_right))                                                                              \
        {                                                                                                        \
            ::minitest::report_failure(__FILE__, __LINE__,                                                       \
                std::format("CHECK_EQ({}, {}) failed\n      left:  {}\n      right: {}", #a, #b,                  \
                    ::minitest::show(mt_left), ::minitest::show(mt_right)));                                     \
        }                                                                                                        \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                                                         \
    do                                                                                                           \
    {                                                                                                            \
        const std::string mt_haystack(haystack);                                                                 \
        const std::string mt_needle(needle);                                                                     \
        if (mt_haystack.find(mt_needle) == std::string::npos)                                                    \
        {                                                                                                        \
            ::minitest::report_failure(__FILE__, __LINE__,                                                       \
                std::format("CHECK_CONTAINS failed\n      text:     {}\n      expected: {}", mt_haystack, mt_needle)); \
        }                                                                                                        \
    } while (0)
