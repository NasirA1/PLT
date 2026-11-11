#include "thunder/os.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// Only <windows.h> may be included directly: the inner SDK headers (libloaderapi.h and the
// rest) expect the architecture macros that <windows.h> sets up first, and including one of
// them on its own fails with winnt.h's "No Target Architecture".
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
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

    std::string executable_path()
    {
#if defined(_WIN32)
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return {};
        }
        buffer.resize(length);
        return path_text(fs::path(buffer));
#elif defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size); // asks how much room the path needs
        std::string buffer(size, '\0');
        if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0)
        {
            return {};
        }
        buffer.resize(std::strlen(buffer.c_str()));
        std::error_code error;
        const fs::path resolved = fs::weakly_canonical(utf8_path(buffer), error);
        return error ? buffer : path_text(resolved);
#else
        std::error_code error;
        const fs::path self = fs::read_symlink("/proc/self/exe", error); // Linux
        return error ? std::string() : path_text(self);
#endif
    }

    std::string library_file_name(const std::string& name)
    {
#if defined(_WIN32)
        return name + ".dll";
#elif defined(__APPLE__)
        return "lib" + name + ".dylib";
#else
        return "lib" + name + ".so";
#endif
    }

    Library::~Library()
    {
        if (handle_ != nullptr)
        {
#if defined(_WIN32)
            FreeLibrary(static_cast<HMODULE>(handle_));
#else
            dlclose(handle_);
#endif
        }
    }

    Library::Library(Library&& other) noexcept
        : handle_(other.handle_)
        , path_(std::move(other.path_))
    {
        other.handle_ = nullptr;
    }

    Library& Library::operator=(Library&& other) noexcept
    {
        if (this != &other)
        {
            Library old(std::move(*this));
            handle_ = other.handle_;
            path_ = std::move(other.path_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    std::expected<Library, std::string> Library::open(const std::string& path)
    {
        Library library;
        library.path_ = path;
#if defined(_WIN32)
        library.handle_ = LoadLibraryW(utf8_path(path).c_str());
        if (library.handle_ == nullptr)
        {
            // The system's own words for the error (e.g. "The specified module could not be
            // found."), with the file that was tried: a bare error number helps nobody.
            const DWORD code = GetLastError();
            std::string reason;
            wchar_t* buffer = nullptr;
            const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
            if (length > 0 && buffer != nullptr)
            {
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
                if (bytes > 0)
                {
                    reason.resize(static_cast<std::size_t>(bytes));
                    WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), reason.data(), bytes, nullptr, nullptr);
                }
                while (!reason.empty() && (reason.back() == '\r' || reason.back() == '\n' || reason.back() == ' '))
                {
                    reason.pop_back();
                }
            }
            if (buffer != nullptr)
            {
                LocalFree(buffer);
            }
            return std::unexpected(std::format("cannot load '{}': {} (Windows error {})", path,
                reason.empty() ? "unknown error" : reason, code));
        }
#else
        library.handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (library.handle_ == nullptr)
        {
            const char* reason = dlerror();
            return std::unexpected(reason != nullptr ? std::string(reason) : std::string("cannot load the library"));
        }
#endif
        return library;
    }

    void* Library::symbol(const std::string& name) const
    {
        if (handle_ == nullptr)
        {
            return nullptr;
        }
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name.c_str()));
#else
        return dlsym(handle_, name.c_str());
#endif
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
