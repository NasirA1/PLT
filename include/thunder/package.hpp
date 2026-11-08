// Packaging: turning a compiled program into an executable the operating system can run on
// its own, without thunderc, thundervm or the IDE.
//
// A package is simply the virtual machine's own executable with the program's bytecode stuck
// on the end and a 24-byte trailer that says where it starts:
//
//     [ the thundervm executable, byte for byte ] [ program.thc ] [ trailer ]
//     trailer = u64 payload offset, u64 payload size, "THNDPACK"   (little-endian)
//
// Both ELF (Linux) and PE (Windows) executables ignore bytes after the image, so the copy
// still starts normally; at start-up it looks at its own file, finds the trailer and runs the
// program it carries. See docs/packaging.md.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace thunder::package
{
    inline constexpr std::string_view magic = "THNDPACK";
    inline constexpr std::size_t trailer_size = 8 + 8 + 8;
    // A sanity limit on what a trailer may claim to carry, so a damaged file cannot ask for a
    // huge allocation.
    inline constexpr std::uint64_t max_payload_size = 512ull * 1024 * 1024;

    // 'runtime' followed by 'program' and the trailer.
    std::vector<std::uint8_t> build(std::span<const std::uint8_t> runtime, std::span<const std::uint8_t> program);

    // The bytecode carried by an executable, or nullopt if it carries none.
    std::optional<std::span<const std::uint8_t>> payload(std::span<const std::uint8_t> file);

    // Reads this program's own executable and returns the bytecode it carries, if any.
    // (Returns nullopt both when there is no payload and when the file cannot be read.)
    std::optional<std::vector<std::uint8_t>> self_payload();

    struct Result
    {
        std::string path;                      // the executable that was written
        std::vector<std::string> libraries;    // native libraries copied next to it
        std::uint64_t size = 0;
    };

    // Writes a package for 'program_bytes' to 'output', copying 'runtime' and any native
    // libraries named in 'native_libraries' (looked for in 'search_paths'). The error text on
    // failure.
    std::expected<Result, std::string> write(const std::string& output, const std::string& runtime,
        std::span<const std::uint8_t> program_bytes, const std::vector<std::string>& native_libraries,
        const std::vector<std::string>& search_paths);

    // The file name an executable called 'name' has on this platform (adds .exe on Windows).
    std::string executable_file_name(const std::string& name);

    // The virtual machine next to 'tool' (the running thunderc), or "" if it is not there.
    std::string default_runtime(const std::string& tool);
}
