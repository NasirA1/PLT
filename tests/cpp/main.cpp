// Test runner for minitest. Usage: thunder_tests [-v] [name-filter]
#include "minitest.hpp"

#include <cstring>
#include <exception>
#include <iostream>
#include <print>

namespace minitest
{
    namespace
    {
        int failures_in_current_test = 0;
    }

    std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> tests;
        return tests;
    }

    void report_failure(const char* file, int line, const std::string& message)
    {
        ++failures_in_current_test;
        std::println(std::cerr, "    {}:{}: {}", file, line, message);
    }

    int run(int argc, char* argv[])
    {
        bool verbose = false;
        std::string filter;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "-v") == 0)
            {
                verbose = true;
            }
            else
            {
                filter = argv[i];
            }
        }

        int passed = 0;
        int failed = 0;
        for (const TestCase& test : registry())
        {
            if (!filter.empty() && std::string_view(test.name).find(filter) == std::string_view::npos)
            {
                continue;
            }
            failures_in_current_test = 0;
            try
            {
                test.function();
            }
            catch (const AbortTest&)
            {
            }
            catch (const std::exception& e)
            {
                report_failure(test.file, test.line, std::string("unexpected exception: ") + e.what());
            }
            catch (...)
            {
                report_failure(test.file, test.line, "unexpected non-standard exception");
            }

            if (failures_in_current_test == 0)
            {
                ++passed;
                if (verbose)
                {
                    std::println("[ PASS ] {}", test.name);
                }
            }
            else
            {
                ++failed;
                std::println("[ FAIL ] {}  ({}:{})", test.name, test.file, test.line);
            }
        }

        std::println("{} test case(s): {} passed, {} failed", passed + failed, passed, failed);
        return failed == 0 ? 0 : 1;
    }
}

int main(int argc, char* argv[])
{
    return minitest::run(argc, argv);
}
