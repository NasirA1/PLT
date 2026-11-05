// Helpers shared by the command-line tools.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace thunder::cli
{
    // Exit codes shared by thunderc and thundervm.
    inline constexpr int exit_ok = 0;
    inline constexpr int exit_program_error = 1; // compile errors / runtime error
    inline constexpr int exit_usage = 2;         // bad arguments, unreadable files, invalid bytecode
    inline constexpr int exit_internal = 3;      // a bug in the toolchain

    // One command-line option. 'apply' receives the option's value (empty for flags) and returns an
    // error message if the value is unacceptable.
    struct OptionSpec
    {
        std::string_view name;  // e.g. "--output"
        std::string_view alias; // e.g. "-o" (may be empty)
        bool takes_value = false;
        std::function<std::optional<std::string>(const std::string&)> apply;
    };

    // Parses 'args' against 'options'; other arguments are collected in 'positional'.
    // Returns an error message on failure.
    std::optional<std::string> parse_arguments(const std::vector<std::string>& args,
        const std::vector<OptionSpec>& options, std::vector<std::string>& positional);

    // Makes the console display UTF-8 correctly (Windows); a no-op elsewhere.
    void prepare_console();

    // Reads a whole file ("-" means standard input). Returns nullopt if it cannot be read.
    std::optional<std::string> read_file(const std::string& path);
    bool write_file(const std::string& path, std::string_view data);

    std::optional<std::uint64_t> parse_count(std::string_view text);

    // Replaces the extension of 'path' (or appends one).
    std::string replace_extension(const std::string& path, std::string_view extension);
}
