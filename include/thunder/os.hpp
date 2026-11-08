// Operating-system services used by the built-in module "os" (import os).
//
// Everything platform-specific lives behind these functions, so the virtual machine itself stays
// portable. Errors are returned as text (the reason), never thrown.
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace thunder::os
{
    // Conversions between UTF-8 text and paths. (On Windows, a path made from a plain std::string
    // would read it in the ANSI code page and garble names in other scripts.)
    std::filesystem::path utf8_path(const std::string& text);
    std::string path_text(const std::filesystem::path& path); // UTF-8, with '/' separators

    std::expected<std::string, std::string> read_text_file(const std::string& path);
    std::optional<std::string> write_text_file(const std::string& path, const std::string& text, bool append);
    bool path_exists(const std::string& path);
    std::optional<std::string> remove_file(const std::string& path); // no error if it does not exist
    std::expected<std::vector<std::string>, std::string> list_directory(const std::string& path);
    std::string current_directory();

    std::string environment_variable(const std::string& name); // "" if not set

    // Reads one line (without its line break) from standard input, below the C stdio layer: a
    // thread blocked here holds no stream lock, so the process can still end normally (a blocked
    // std::getline(std::cin) would make exit() wait for ever). False at the end of the input.
    // Only one thread may use it.
    bool read_standard_input_line(std::string& line);

    struct CommandResult
    {
        int status = 0;     // the command's exit status
        std::string output; // what it wrote to its standard output
    };
    // Runs a command with the system shell (sh on Linux and macOS, cmd.exe on Windows).
    std::optional<CommandResult> run_command(const std::string& command);

    std::int64_t unix_time();          // seconds since 1970-01-01 00:00:00 UTC
    std::string local_date_time();     // "YYYY-MM-DD HH:MM:SS"
    void sleep_milliseconds(std::int64_t milliseconds);
    std::string platform_name();       // "linux", "windows", "macos" or "unknown"

    // ------------------------------------------------------------------ this program

    // The full path of the running executable ("" if it cannot be found).
    std::string executable_path();

    // ------------------------------------------------------------------ dynamic libraries
    //
    // Used by the C++ native interface (import native "name"); see docs/native.md.

    // The file a library called 'name' has on this platform: libname.so, name.dll, libname.dylib.
    std::string library_file_name(const std::string& name);

    class Library
    {
    public:
        Library() = default;
        ~Library();
        Library(const Library&) = delete;
        Library& operator=(const Library&) = delete;
        Library(Library&& other) noexcept;
        Library& operator=(Library&& other) noexcept;

        // Loads the library at 'path'; the error text on failure.
        static std::expected<Library, std::string> open(const std::string& path);

        // The address of an exported symbol, or nullptr.
        void* symbol(const std::string& name) const;
        bool is_open() const { return handle_ != nullptr; }
        const std::string& path() const { return path_; }

    private:
        void* handle_ = nullptr;
        std::string path_;
    };
}
