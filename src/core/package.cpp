#include "thunder/package.hpp"

#include "thunder/os.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

namespace thunder::package
{
    namespace fs = std::filesystem;

    namespace
    {
        void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
            }
        }

        std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t offset)
        {
            std::uint64_t value = 0;
            for (int i = 7; i >= 0; --i)
            {
                value = (value << 8) | bytes[offset + static_cast<std::size_t>(i)];
            }
            return value;
        }

        // Reads 'count' bytes from 'offset' in one go (an executable is tens of megabytes, so
        // reading it a character at a time through an iterator is far too slow).
        std::optional<std::vector<std::uint8_t>> read_part(std::ifstream& file, std::uint64_t offset,
            std::size_t count)
        {
            file.clear();
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            std::vector<std::uint8_t> bytes(count);
            if (count > 0)
            {
                file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
                if (file.gcount() != static_cast<std::streamsize>(count))
                {
                    return std::nullopt;
                }
            }
            return bytes;
        }

        std::optional<std::vector<std::uint8_t>> read_binary(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                return std::nullopt;
            }
            const std::streamoff size = file.tellg();
            if (size < 0)
            {
                return std::nullopt;
            }
            return read_part(file, 0, static_cast<std::size_t>(size));
        }
    }

    std::vector<std::uint8_t> build(std::span<const std::uint8_t> runtime, std::span<const std::uint8_t> program)
    {
        std::vector<std::uint8_t> bytes(runtime.begin(), runtime.end());
        const auto offset = static_cast<std::uint64_t>(bytes.size());
        bytes.insert(bytes.end(), program.begin(), program.end());
        append_u64(bytes, offset);
        append_u64(bytes, static_cast<std::uint64_t>(program.size()));
        bytes.insert(bytes.end(), magic.begin(), magic.end());
        return bytes;
    }

    std::optional<std::span<const std::uint8_t>> payload(std::span<const std::uint8_t> file)
    {
        if (file.size() < trailer_size)
        {
            return std::nullopt;
        }
        const std::size_t trailer = file.size() - trailer_size;
        if (!std::equal(magic.begin(), magic.end(), file.begin() + static_cast<std::ptrdiff_t>(trailer + 16)))
        {
            return std::nullopt;
        }
        const std::uint64_t offset = read_u64(file, trailer);
        const std::uint64_t size = read_u64(file, trailer + 8);
        // Never trust the numbers in a file: they must describe a region inside it.
        if (offset > trailer || size > trailer - offset)
        {
            return std::nullopt;
        }
        return file.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
    }

    std::optional<std::vector<std::uint8_t>> self_payload()
    {
        const std::string self = os::executable_path();
        if (self.empty())
        {
            return std::nullopt;
        }
        // Only the trailer and the program are read: the executable itself may be large, and
        // every run of the virtual machine goes through here.
        std::ifstream file(os::utf8_path(self), std::ios::binary | std::ios::ate);
        if (!file)
        {
            return std::nullopt;
        }
        const std::streamoff size = file.tellg();
        if (size < static_cast<std::streamoff>(trailer_size))
        {
            return std::nullopt;
        }
        const auto trailer = read_part(file, static_cast<std::uint64_t>(size) - trailer_size, trailer_size);
        if (!trailer || !std::equal(magic.begin(), magic.end(), trailer->begin() + 16))
        {
            return std::nullopt;
        }
        const std::uint64_t offset = read_u64(*trailer, 0);
        const std::uint64_t length = read_u64(*trailer, 8);
        const auto end = static_cast<std::uint64_t>(size) - trailer_size;
        if (offset > end || length > end - offset || length > max_payload_size)
        {
            return std::nullopt;
        }
        return read_part(file, offset, static_cast<std::size_t>(length));
    }

    std::string executable_file_name(const std::string& name)
    {
        if (os::platform_name() != "windows")
        {
            return name;
        }
        return name.ends_with(".exe") ? name : name + ".exe";
    }

    std::string default_runtime(const std::string& tool)
    {
        if (tool.empty())
        {
            return {};
        }
        const fs::path candidate = os::utf8_path(tool).parent_path() / os::utf8_path(executable_file_name("thundervm"));
        std::error_code error;
        return fs::exists(candidate, error) ? os::path_text(candidate) : std::string();
    }

    std::expected<Result, std::string> write(const std::string& output, const std::string& runtime,
        std::span<const std::uint8_t> program_bytes, const std::vector<std::string>& native_libraries,
        const std::vector<std::string>& search_paths)
    {
        const auto runtime_bytes = read_binary(os::utf8_path(runtime));
        if (!runtime_bytes || runtime_bytes->empty())
        {
            return std::unexpected(std::format("cannot read the virtual machine '{}'", runtime));
        }

        Result result;
        result.path = output;
        const std::vector<std::uint8_t> bytes = build(*runtime_bytes, program_bytes);
        result.size = static_cast<std::uint64_t>(bytes.size());

        std::error_code error;
        const fs::path path = os::utf8_path(output);
        if (path.has_parent_path())
        {
            fs::create_directories(path.parent_path(), error);
        }
        {
            // Truncating an executable that is running fails on Windows; removing it first does not.
            fs::remove(path, error);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                return std::unexpected(std::format("cannot write '{}'", output));
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
            {
                return std::unexpected(std::format("cannot write '{}'", output));
            }
        }
        // Make it runnable (a no-op on Windows, where the extension decides).
        fs::permissions(path,
            fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, error);

        // The program's native libraries have to travel with it: the packaged executable looks
        // for them in its own directory.
        for (const std::string& library : native_libraries)
        {
            const std::string file_name = os::library_file_name(library);
            bool copied = false;
            for (const std::string& directory : search_paths)
            {
                const fs::path source = os::utf8_path(directory) / os::utf8_path(file_name);
                if (!fs::exists(source, error))
                {
                    continue;
                }
                const fs::path target = path.parent_path() / os::utf8_path(file_name);
                if (fs::equivalent(source, target, error))
                {
                    copied = true; // it is already next to the executable
                    break;
                }
                fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
                if (error)
                {
                    return std::unexpected(std::format("cannot copy '{}': {}", file_name, error.message()));
                }
                copied = true;
                break;
            }
            if (!copied)
            {
                return std::unexpected(std::format("cannot find the native library '{}' to put beside the program",
                    file_name));
            }
            result.libraries.push_back(file_name);
        }
        return result;
    }
}
