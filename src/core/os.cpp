#include "thunder/os.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <io.h>
#define THUNDER_POPEN _popen
#define THUNDER_PCLOSE _pclose
#else
#include <sys/wait.h>
#include <unistd.h>
#define THUNDER_POPEN popen
#define THUNDER_PCLOSE pclose
#endif

namespace thunder::os
{
    namespace fs = std::filesystem;

    fs::path utf8_path(const std::string& text)
    {
        return fs::path(std::u8string(text.begin(), text.end()));
    }

    std::string path_text(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }

    namespace
    {
        fs::path to_path(const std::string& text)
        {
            return utf8_path(text);
        }

        std::string from_path(const fs::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }
    }

    std::expected<std::string, std::string> read_text_file(const std::string& path)
    {
        std::error_code error;
        if (fs::is_directory(to_path(path), error))
        {
            return std::unexpected(std::string("it is a directory"));
        }
        std::ifstream file(to_path(path), std::ios::binary);
        if (!file)
        {
            return std::unexpected(std::string("no such file, or it cannot be opened"));
        }
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::erase(text, '\r'); // Windows line endings: programs always see "\n"
        return text;
    }

    std::optional<std::string> write_text_file(const std::string& path, const std::string& text, bool append)
    {
        std::ofstream file(to_path(path), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!file)
        {
            return std::string("the file cannot be created (does the directory exist?)");
        }
        file << text;
        if (!file)
        {
            return std::string("the disk is full or the file is not writable");
        }
        return std::nullopt;
    }

    bool path_exists(const std::string& path)
    {
        std::error_code error;
        return fs::exists(to_path(path), error);
    }

    std::optional<std::string> remove_file(const std::string& path)
    {
        std::error_code error;
        if (fs::is_directory(to_path(path), error))
        {
            return std::string("it is a directory");
        }
        fs::remove(to_path(path), error);
        if (error)
        {
            return error.message();
        }
        return std::nullopt;
    }

    std::expected<std::vector<std::string>, std::string> list_directory(const std::string& path)
    {
        std::error_code error;
        fs::directory_iterator it(to_path(path), error);
        if (error)
        {
            return std::unexpected(error.message());
        }
        std::vector<std::string> names;
        for (const fs::directory_entry& entry : it)
        {
            names.push_back(from_path(entry.path().filename()));
        }
        std::ranges::sort(names);
        return names;
    }

    std::string current_directory()
    {
        std::error_code error;
        const fs::path path = fs::current_path(error);
        return error ? std::string() : from_path(path);
    }

    std::string environment_variable(const std::string& name)
    {
#ifdef _MSC_VER
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, name.c_str()) != 0 || value == nullptr)
        {
            return {};
        }
        std::string result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name.c_str()); // NOLINT(concurrency-mt-unsafe): the VM is single-threaded
        return value != nullptr ? std::string(value) : std::string();
#endif
    }

    bool read_standard_input_line(std::string& line)
    {
        static std::string pending; // bytes read but not yet returned (used by one thread only)
        static bool ended = false;
        while (true)
        {
            const std::size_t end = pending.find('\n');
            if (end != std::string::npos)
            {
                line = pending.substr(0, end);
                pending.erase(0, end + 1);
                return true;
            }
            if (ended)
            {
                if (pending.empty())
                {
                    return false;
                }
                line = std::move(pending); // a last line without a line break
                pending.clear();
                return true;
            }
            char buffer[4096];
#ifdef _WIN32
            const int count = _read(0, buffer, sizeof buffer);
#else
            const ssize_t count = ::read(0, buffer, sizeof buffer);
#endif
            if (count <= 0)
            {
                ended = true; // the end of the input (or an error, which ends it too)
            }
            else
            {
                pending.append(buffer, static_cast<std::size_t>(count));
            }
        }
    }

    std::optional<CommandResult> run_command(const std::string& command)
    {
        std::fflush(nullptr); // output written so far must come before the command's output
        FILE* pipe = THUNDER_POPEN(command.c_str(), "r");
        if (pipe == nullptr)
        {
            return std::nullopt;
        }
        CommandResult result;
        char buffer[4096];
        std::size_t count = 0;
        while ((count = std::fread(buffer, 1, sizeof buffer, pipe)) > 0)
        {
            result.output.append(buffer, count);
        }
        const int status = THUNDER_PCLOSE(pipe);
#ifdef _WIN32
        result.status = status;
#else
        result.status = status == -1 ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
#endif
        std::erase(result.output, '\r');
        return result;
    }

    std::int64_t unix_time()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    std::string local_date_time()
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
            local.tm_hour, local.tm_min, local.tm_sec);
    }

    void sleep_milliseconds(std::int64_t milliseconds)
    {
        if (milliseconds > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        }
    }

    std::string platform_name()
    {
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#elif defined(__linux__)
        return "linux";
#else
        return "unknown";
#endif
    }
}
