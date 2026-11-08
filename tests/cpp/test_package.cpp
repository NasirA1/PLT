// Packaging a program into a native executable (thunderc --package).
//
// The unit tests here work on the package format itself; the end-to-end test that really runs a
// packaged executable is in tests/python/test_cli.py, where the built tools are available.
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/os.hpp"
#include "thunder/package.hpp"

#include <algorithm>

using namespace thunder;

namespace
{
    std::vector<std::uint8_t> bytes_of(std::string_view text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }
}

TEST_CASE("package: a program is appended to the runtime and found again")
{
    const std::vector<std::uint8_t> runtime = bytes_of("ELF...this stands for an executable...");
    const bytecode::Program program = test::compile_ok("println(6 * 7)");
    const std::vector<std::uint8_t> compiled = bytecode::serialize(program);

    const std::vector<std::uint8_t> packaged = package::build(runtime, compiled);
    CHECK_EQ(packaged.size(), runtime.size() + compiled.size() + package::trailer_size);
    // The runtime is unchanged, which is what lets the operating system still start the file.
    CHECK(std::equal(runtime.begin(), runtime.end(), packaged.begin()));

    const auto found = package::payload(packaged);
    REQUIRE(found.has_value());
    CHECK(std::equal(compiled.begin(), compiled.end(), found->begin()));

    // And the program inside really is the program.
    const auto restored = bytecode::deserialize(*found);
    REQUIRE(restored.has_value());
    CHECK_EQ(restored->functions.size(), program.functions.size());
}

TEST_CASE("package: an ordinary executable carries nothing")
{
    CHECK(!package::payload(bytes_of("")).has_value());
    CHECK(!package::payload(bytes_of("too short")).has_value());
    CHECK(!package::payload(bytes_of("a file that is long enough but has no trailer at all!!")).has_value());
}

TEST_CASE("package: a damaged trailer is refused, never trusted")
{
    const std::vector<std::uint8_t> runtime = bytes_of("runtime bytes");
    std::vector<std::uint8_t> packaged = package::build(runtime, bytes_of("payload"));
    REQUIRE(package::payload(packaged).has_value());

    // An offset or a size that points outside the file.
    std::vector<std::uint8_t> broken = packaged;
    broken[broken.size() - package::trailer_size] = 0xFF; // a huge offset
    CHECK(!package::payload(broken).has_value());

    broken = packaged;
    broken[broken.size() - package::trailer_size + 8] = 0xFF; // a huge size
    CHECK(!package::payload(broken).has_value());

    broken = packaged;
    broken.back() = 'X'; // the magic no longer matches
    CHECK(!package::payload(broken).has_value());
}

TEST_CASE("package: the executable name follows the platform")
{
    const std::string name = package::executable_file_name("hello");
    if (os::platform_name() == "windows")
    {
        CHECK_EQ(name, "hello.exe");
        CHECK_EQ(package::executable_file_name("hello.exe"), "hello.exe");
    }
    else
    {
        CHECK_EQ(name, "hello");
    }
}

TEST_CASE("package: writing one, and the errors it can report")
{
    const bytecode::Program program = test::compile_ok("println(1)");
    const std::vector<std::uint8_t> compiled = bytecode::serialize(program);

    // There is no such virtual machine to copy.
    const auto missing = package::write("out.app", "no/such/thundervm", compiled, {}, {});
    REQUIRE(!missing.has_value());
    CHECK_CONTAINS(missing.error(), "cannot read");

    // A program that needs a native library nobody can find is refused rather than packaged
    // into something that would fail at the user's end.
    const std::string runtime = os::executable_path();
    REQUIRE(!runtime.empty());
    const auto no_library = package::write(std::string(THUNDER_TEST_TEMP) + "/out.app", runtime, compiled,
        {"no_such_native_library"}, {});
    REQUIRE(!no_library.has_value());
    CHECK_CONTAINS(no_library.error(), "no_such_native_library");
}
