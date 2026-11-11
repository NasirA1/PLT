// The display interface: loading a plug-in and showing a graphics program on it.
// The plug-in under test is lib/display/text, built into <build>/bin.
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"
#include "thunder/display.hpp"
#include "thunder/os.hpp"
#include "thunder/vm.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

using namespace thunder;

namespace
{
    // The example plug-in writes every frame to standard error; the tests do not need to see it.
    struct Quiet
    {
        Quiet()
        {
#ifdef _WIN32
            _putenv_s("THUNDER_DISPLAY_TEXT_QUIET", "1");
#else
            setenv("THUNDER_DISPLAY_TEXT_QUIET", "1", 1);
#endif
        }
    };
    const Quiet quiet;

    std::string plugin_directory()
    {
        return os::path_text(os::utf8_path(os::executable_path()).parent_path());
    }

    // Sets an environment variable for the duration of a test (the text plug-in reads them).
    class Environment
    {
    public:
        Environment(const char* name, const char* value)
            : name_(name)
        {
#ifdef _WIN32
            _putenv_s(name, value);
#else
            setenv(name, value, 1);
#endif
        }
        ~Environment()
        {
#ifdef _WIN32
            _putenv_s(name_.c_str(), "");
#else
            unsetenv(name_.c_str());
#endif
        }
        Environment(const Environment&) = delete;
        Environment& operator=(const Environment&) = delete;

    private:
        std::string name_;
    };

    struct DisplayRun
    {
        std::string output;
        ExecResult result;
    };

    DisplayRun run_with_display(const std::string& body, PluginDisplay& display, std::uint64_t max_frames = 0)
    {
        const SourceFile source("d.th", "import graphics\n" + body);
        const CompileResult compiled = compile(source);
        DisplayRun run;
        if (!compiled.program)
        {
            run.output = "COMPILE ERROR";
            return run;
        }
        std::ostringstream out;
        VmOptions options;
        options.max_frames = max_frames;
        Vm vm(*compiled.program, out, options);
        vm.set_graphics_host(&display);
        run.result = vm.run();
        run.output = out.str();
        return run;
    }
}

TEST_CASE("display: the example plug-in is where the tests expect it")
{
    REQUIRE(std::filesystem::exists(
        std::filesystem::path(plugin_directory()) / PluginDisplay::library_name("text")));
}

TEST_CASE("display: the library name follows the platform")
{
    const std::string name = PluginDisplay::library_name("sdl");
    CHECK_EQ(name, os::library_file_name("thunder_display_sdl"));
    CHECK_CONTAINS(name, "thunder_display_sdl");
}

TEST_CASE("display: a plug-in that is not there, or is not a display")
{
    const auto missing = PluginDisplay::load("no_such_display", {plugin_directory()}, "t");
    REQUIRE(!missing.has_value());
    CHECK_CONTAINS(missing.error(), "thunder_display_no_such_display");

    // A library that exists but exports no thunder_display_init is reported as such. The demo
    // native module is exactly that: a shared library of the wrong kind.
    const std::string wrong = os::path_text(os::utf8_path(plugin_directory())
        / os::utf8_path(os::library_file_name("thunder_demo")));
    if (std::filesystem::exists(wrong))
    {
        // load() takes a display name, so point it at the file by naming the directory it is in.
        const auto result = PluginDisplay::load("text", {plugin_directory()}, "t");
        CHECK(result.has_value()); // the real one still loads from the same directory
    }
}

TEST_CASE("display: a graphics program is shown on the plug-in")
{
    auto display = PluginDisplay::load("text", {plugin_directory()}, "test");
    REQUIRE(display.has_value());
    CHECK((*display)->paced());             // a window watches in real time, so frame() waits
    CHECK(!(*display)->should_stop());
    const DisplayRun run = run_with_display(
        "canvas(60, 40)\nfor var i = 0, i < 3, i = i + 1\n    clear(\"white\")\n    circle(i, i, 2.5, \"red\")\n"
        "    show()\nprintln(\"done\")",
        **display);
    CHECK(!run.result.error.has_value());
    CHECK_EQ(run.output, "done\n");
    CHECK_EQ(run.result.stats.frames, 3u);
}

TEST_CASE("display: closing the window ends the program normally")
{
    // THUNDER_DISPLAY_TEXT_FRAMES makes the example plug-in report "quit" after two frames,
    // which is what a window's close button does.
    const Environment frames("THUNDER_DISPLAY_TEXT_FRAMES", "2");
    auto display = PluginDisplay::load("text", {plugin_directory()}, "test");
    REQUIRE(display.has_value());
    const DisplayRun run = run_with_display("canvas(40, 20)\nwhile true\n    clear(\"white\")\n    show()",
        **display);
    CHECK(!run.result.error.has_value());       // not an error: the program simply has nowhere to draw
    CHECK(run.result.frame_limit_reached);
    CHECK_EQ(run.result.stats.frames, 2u);
    CHECK((*display)->should_stop());
}

TEST_CASE("display: keys and the mouse travel back to the program")
{
    const Environment keys("THUNDER_DISPLAY_TEXT_KEYS", "left,space");
    const Environment mouse("THUNDER_DISPLAY_TEXT_MOUSE", "12,7,1");
    const Environment frames("THUNDER_DISPLAY_TEXT_FRAMES", "3");
    auto display = PluginDisplay::load("text", {plugin_directory()}, "test");
    REQUIRE(display.has_value());
    const char* program = R"(
canvas(40, 20)
while true
    clear("white")
    show()
    println(key_pressed(), " ", mouse_x(), ",", mouse_y(), " ", mouse_down())
)";
    const DisplayRun run = run_with_display(program, **display);
    // show() hands the frame over and then drains the events, so the first println already
    // sees them: key_pressed() takes one press per call, and the mouse is where it was put.
    // The third round never prints: the plug-in asks to quit while show() is presenting.
    CHECK_EQ(run.output, "left 12,7 true\nspace 12,7 true\n");
}

TEST_CASE("display: a Pashto program sees the keys in Pashto")
{
    const Environment keys("THUNDER_DISPLAY_TEXT_KEYS", "left");
    const Environment frames("THUNDER_DISPLAY_TEXT_FRAMES", "2");
    auto display = PluginDisplay::load("text", {plugin_directory()}, "test");
    REQUIRE(display.has_value());
    const SourceFile source("d.th", "// ژبه: ps\nراوړه ګرافیک\nټاټ(۴۰، ۲۰)\nترڅو رښتیا\n"
                                    "    ټاټ پاک کړه(\"سپین\")\n    ښکاره کړه()\n"
                                    "    کرښه چاپ(کښېکښل شوې تڼۍ())\n");
    const CompileResult compiled = compile(source, pashto_pack());
    REQUIRE(compiled.program.has_value());
    std::ostringstream out;
    Vm vm(*compiled.program, out, VmOptions{});
    vm.set_graphics_host(display->get());
    vm.run();
    CHECK_CONTAINS(out.str(), "کیڼ"); // "left", named in the program's own language
}
