#include "thunder/cli/cli_common.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW
#endif

namespace thunder::cli
{
    void prepare_console()
    {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif
    }

    std::vector<std::string> arguments(int argc, char* argv[])
    {
#ifdef _WIN32
        (void)argc;
        (void)argv;
        int count = 0;
        LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
        std::vector<std::string> args;
        if (wide != nullptr)
        {
            for (int i = 1; i < count; ++i)
            {
                const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
                std::string utf8(size > 0 ? static_cast<std::size_t>(size - 1) : 0, '\0');
                if (size > 1)
                {
                    WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, utf8.data(), size, nullptr, nullptr);
                }
                args.push_back(std::move(utf8));
            }
            LocalFree(wide);
        }
        return args;
#else
        return std::vector<std::string>(argv + 1, argv + argc);
#endif
    }

    std::optional<std::string> parse_arguments(const std::vector<std::string>& args,
        const std::vector<OptionSpec>& options, std::vector<std::string>& positional, bool rest_after_first)
    {
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string& arg = args[i];
            if ((rest_after_first && !positional.empty()) || arg == "--")
            {
                // Everything from here on is positional (e.g. the arguments of the program to run).
                const std::size_t first = arg == "--" ? i + 1 : i;
                positional.insert(positional.end(), args.begin() + static_cast<std::ptrdiff_t>(first), args.end());
                return std::nullopt;
            }
            const auto spec = std::ranges::find_if(options,
                [&](const OptionSpec& option) { return arg == option.name || (!option.alias.empty() && arg == option.alias); });

            if (spec == options.end())
            {
                if (arg.size() > 1 && arg[0] == '-')
                {
                    return "unknown option '" + arg + "'";
                }
                positional.push_back(arg);
                continue;
            }

            std::string value;
            if (spec->takes_value)
            {
                if (i + 1 >= args.size())
                {
                    return "missing value after " + arg;
                }
                value = args[++i];
            }
            if (auto error = spec->apply(value))
            {
                return error;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> read_file(const std::string& path)
    {
        if (path == "-")
        {
            std::ostringstream buffer;
            buffer << std::cin.rdbuf();
            return buffer.str();
        }
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return std::nullopt;
        }
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    bool write_file(const std::string& path, std::string_view data)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return false;
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(file);
    }

    std::optional<std::uint64_t> parse_count(std::string_view text)
    {
        std::uint64_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
        {
            return std::nullopt;
        }
        return value;
    }

    std::string replace_extension(const std::string& path, std::string_view extension)
    {
        const std::size_t slash = path.find_last_of("/\\");
        const std::size_t dot = path.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash) && dot != 0)
        {
            return path.substr(0, dot) + std::string(extension);
        }
        return path + std::string(extension);
    }
}
