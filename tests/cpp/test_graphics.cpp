// The graphics module: colors, the canvas, presenting frames to a host, events, random numbers.
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/compiler.hpp"
#include "thunder/graphics.hpp"
#include "thunder/language_pack.hpp"

#include <chrono>
#include <sstream>

using namespace thunder;
using namespace thunder::graphics;
using test::diagnostic_codes;
using Codes = std::vector<std::string>;

namespace
{
    // A host that records every frame it is shown.
    class RecordingHost final : public GraphicsHost
    {
    public:
        void present(const Canvas& canvas, std::size_t keep) override
        {
            keeps.push_back(keep);
            sizes.push_back(canvas.shapes().size());
            records.push_back(frame_json(canvas, keep));
        }
        bool paced() const override { return pace; }
        Events* events() override { return &input; }

        std::vector<std::size_t> keeps;
        std::vector<std::size_t> sizes;
        std::vector<std::string> records;
        Events input;
        bool pace = false;
    };

    struct Run
    {
        std::string output;
        std::string error; // runtime error code
        ExecResult result;
        Canvas canvas;
    };

    Run run_graphics(const std::string& body, GraphicsHost* host = nullptr, VmOptions options = {},
        const std::string& input = "")
    {
        const SourceFile source("g.th", "import graphics\n" + body);
        const CompileResult compiled = compile(source);
        Run run;
        if (!compiled.program)
        {
            run.error = "COMPILE";
            for (const Diagnostic& diagnostic : compiled.diagnostics.items())
            {
                run.error += " " + diagnostic.code;
            }
            return run;
        }
        std::ostringstream out;
        std::istringstream in(input);
        Vm vm(*compiled.program, out, options, &in);
        vm.set_graphics_host(host);
        run.result = vm.run();
        run.output = out.str();
        run.error = run.result.error ? run.result.error->code : std::string();
        run.canvas = vm.canvas();
        return run;
    }
}

TEST_CASE("graphics: colors")
{
    CHECK(parse_color("red") == std::optional<std::string>("#ff0000"));
    CHECK(parse_color("Sky Blue") == std::nullopt);
    CHECK(parse_color(" RED ") == std::optional<std::string>("#ff0000"));
    CHECK(parse_color("#F80") == std::optional<std::string>("#ff8800"));
    CHECK(parse_color("#12aBcD") == std::optional<std::string>("#12abcd"));
    CHECK(parse_color("سور") == std::optional<std::string>("#ff0000")); // Pashto names too
    for (const char* bad : {"", "#", "#12", "#12345", "#gggggg", "reddish", "#1234567"})
    {
        CHECK(!parse_color(bad).has_value());
    }
    CHECK_EQ(rgb(255, 136, 0), "#ff8800");
    CHECK_EQ(rgb(-5, 300, 16), "#00ff10"); // clamped
    const auto names = color_names();
    CHECK(names.size() > 20);
    for (const std::string_view name : names)
    {
        CHECK(parse_color(name).has_value());
    }
}

TEST_CASE("graphics: frames are sent to the host incrementally")
{
    RecordingHost host;
    const Run run = run_graphics(R"(canvas(100, 50)
clear("black")
rect(1, 2, 3, 4, "red")
frame()
circle(5, 6, 7, "#00ff00")
frame()
show()
clear("white")
text(1, 1, "a \"q\"", "blue")
)", &host);
    CHECK_EQ(run.error, "");
    // Three frames shown by the program, and one more when it ends (it drew after the last one).
    CHECK((host.keeps == std::vector<std::size_t>{0, 2, 3, 0}));
    CHECK((host.sizes == std::vector<std::size_t>{2, 3, 3, 2}));
    CHECK_EQ(host.records[0],
        R"({"type":"frame","n":1,"width":100,"height":50,"keep":0,"shapes":[["clear","#000000"],["rect",1,2,3,4,"#ff0000"]]})");
    CHECK_EQ(host.records[1], R"({"type":"frame","n":2,"width":100,"height":50,"keep":2,"shapes":[["circle",5,6,7,"#00ff00"]]})");
    CHECK_EQ(host.records[2], R"({"type":"frame","n":3,"width":100,"height":50,"keep":3,"shapes":[]})");
    CHECK_EQ(host.records[3],
        R"({"type":"frame","n":4,"width":100,"height":50,"keep":0,"shapes":[["clear","#ffffff"],["text",1,1,"a \"q\"","#0000ff",16]]})");
    CHECK_EQ(run.canvas.frames(), 4u);
    CHECK_EQ(run.result.stats.frames, 4u);

    // Nothing drawn: nothing shown.
    RecordingHost idle;
    CHECK_EQ(run_graphics("var x = 1\nprintln(x)", &idle).error, "");
    CHECK(idle.records.empty());
}

TEST_CASE("graphics: every shape, in JSON, SVG and text")
{
    RecordingHost host;
    const Run run = run_graphics(R"(canvas(60, 40)
pixel(1, 2, "red")
line_width(3)
line(0, 0, 10, 10, "blue")
rect(10, 10, -4, -6, "green")
rect_outline(1, 1, 5, 5, "navy")
circle(20, 20, 5, "gold")
circle_outline(30, 20, 4, "white")
text_size(12)
text(2, 3, "x < y & z", "black")
show())", &host);
    REQUIRE(run.error.empty());
    CHECK_EQ(host.records.back(),
        R"({"type":"frame","n":1,"width":60,"height":40,"keep":0,"shapes":[["pixel",1,2,"#ff0000"],["line",0,0,10,10,"#0000ff",3],)"
        R"(["rect",6,4,4,6,"#008000"],["rect_outline",1,1,5,5,"#000080",3],["circle",20,20,5,"#ffd700"],)"
        R"(["circle_outline",30,20,4,"#ffffff",3],["text",2,3,"x < y & z","#000000",12]]})");
    const std::string svg = to_svg(run.canvas);
    CHECK_CONTAINS(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"60\" height=\"40\"");
    CHECK_CONTAINS(svg, "<rect x=\"6\" y=\"4\" width=\"4\" height=\"6\" fill=\"#008000\"/>");
    CHECK_CONTAINS(svg, "<circle cx=\"30\" cy=\"20\" r=\"4\" fill=\"none\" stroke=\"#ffffff\" stroke-width=\"3\"/>");
    CHECK_CONTAINS(svg, "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"10\" stroke=\"#0000ff\" stroke-width=\"3\"");
    CHECK_CONTAINS(svg, ">x &lt; y &amp; z</text>");
    CHECK_CONTAINS(svg, "<rect x=\"1\" y=\"2\" width=\"1\" height=\"1\" fill=\"#ff0000\"/>");
    CHECK_CONTAINS(svg, "fill=\"none\" stroke=\"#000080\"");
    CHECK_CONTAINS(svg, "<circle cx=\"20\" cy=\"20\" r=\"5\" fill=\"#ffd700\"/>");
    CHECK_EQ(to_text(run.canvas), "canvas 60 x 40, 1 frame(s) shown\n"
                                  "pixel 1 2 #ff0000\n"
                                  "line 0 0 10 10 #0000ff (3)\n"
                                  "rect 6 4 4 6 #008000\n"
                                  "rect_outline 1 1 5 5 #000080 (3)\n"
                                  "circle 20 20 5 #ffd700\n"
                                  "circle_outline 30 20 4 #ffffff (3)\n"
                                  "text 2 3 \"x < y & z\" #000000 (12)\n");
    Canvas cleared;
    cleared.clear("#123456");
    CHECK_CONTAINS(to_svg(cleared), "<rect width=\"400\" height=\"300\" fill=\"#123456\"/>");
    CHECK_EQ(to_text(cleared), "canvas 400 x 300, 0 frame(s) shown\nclear #123456\n");
    CHECK_EQ(shape_name(static_cast<ShapeKind>(99)), "?");
}

TEST_CASE("graphics: the canvas without a host, and the default canvas")
{
    const Run run = run_graphics("rect(0, 0, 10, 10, \"red\")\nframe()\nframe()");
    CHECK_EQ(run.error, "");
    CHECK(run.canvas.used());
    CHECK_EQ(run.canvas.width(), 400);
    CHECK_EQ(run.canvas.height(), 300);
    CHECK_EQ(run.canvas.frames(), 2u);
    CHECK(!run_graphics("println(rgb(1, 2, 3))").canvas.used());
    CHECK_EQ(run_graphics("println(rgb(1, 2, 3))").output, "#010203\n");
}

TEST_CASE("graphics: runtime errors")
{
    CHECK_EQ(run_graphics("clear(\"blurple\")").error, "R021");
    CHECK_EQ(run_graphics("rect(0, 0, 1, 1, \"\")").error, "R021");
    CHECK_EQ(run_graphics("canvas(0, 10)").error, "R022");
    CHECK_EQ(run_graphics("canvas(10, 2001)").error, "R022");
    CHECK_EQ(run_graphics("frame_rate(0)").error, "R022");
    CHECK_EQ(run_graphics("frame_rate(121)").error, "R022");
    CHECK_EQ(run_graphics("text_size(0)").error, "R022");
    CHECK_EQ(run_graphics("line_width(101)").error, "R022");
    CHECK_EQ(run_graphics("circle(1, 1, -1, \"red\")").error, "R022");
    CHECK_EQ(test::run_output("import graphics\ncanvas(5000, 10)"),
        "ERROR R022: the width given to 'canvas' must be from 1 to 2000 (got 5000)");
    CHECK_EQ(test::run_output("import graphics\nclear(\"blurple\")"),
        "ERROR R021: \"blurple\" is not a color (use a name such as \"red\", \"#ff8800\" or rgb(255, 136, 0))");
    CHECK_EQ(run_graphics("while true\n    pixel(1, 1, \"red\")").error, "R024");
    // After clear() the canvas may fill up again.
    CHECK_EQ(run_graphics("for var f = 0, f < 3, f = f + 1\n    clear(\"black\")\n    for var i = 0, i < 60000, i = i + 1\n        pixel(i, 1, \"red\")").error,
        "");
    // A picture drawn before a runtime error is still shown.
    RecordingHost host;
    CHECK_EQ(run_graphics("rect(0, 0, 5, 5, \"red\")\nprintln(1 / 0)", &host).error, "R001");
    CHECK_EQ(host.records.size(), 1u);
}

TEST_CASE("graphics: the functions need 'import graphics' and are type-checked")
{
    CHECK(diagnostic_codes("rect(0, 0, 1, 1, \"red\")") == Codes{"S049"});
    CHECK(diagnostic_codes("import graphics\nrect(0, 0, 1, \"red\")") == Codes{"S009"});
    CHECK(diagnostic_codes("import graphics\nrect(0, 0, 1, 1, 2)") == Codes{"S010"});
    CHECK(diagnostic_codes("import graphics\nvar k: int = key_pressed()") == Codes{"S005"});
    CHECK(diagnostic_codes("import graphics\nimport os\nvar line = 1\nprintln(line)").empty()); // a variable may share a name
    CHECK(diagnostic_codes("import graphics\nfunc circle()\n    pass") == Codes{"S004"});
    CHECK(diagnostic_codes("func circle()\n    pass").empty()); // without the import, an ordinary name
    CHECK(diagnostic_codes("import ګرافیک\nshow()").empty());
    CHECK(find_builtin_module("graphics") == BuiltinModule::Graphics);
    CHECK_EQ(module_canonical_name(BuiltinModule::Graphics), "graphics");
    CHECK_EQ(pashto_pack().spelling(BuiltinModule::Graphics), "ګرافیک");
}

TEST_CASE("graphics: frame limits, step limits and pacing")
{
    // An endless animation stops normally after max_frames frames.
    VmOptions limited;
    limited.max_frames = 5;
    const Run endless = run_graphics("var n = 0\nwhile true\n    clear(\"black\")\n    n = n + 1\n    frame()", nullptr, limited);
    CHECK_EQ(endless.error, "");
    CHECK(endless.result.frame_limit_reached);
    CHECK_EQ(endless.canvas.frames(), 5u);

    // Once frames are shown, the step limit counts the instructions between two frames.
    VmOptions steps;
    steps.max_steps = 2'000;
    steps.max_frames = 50;
    const Run animation = run_graphics("while true\n    for var i = 0, i < 100, i = i + 1\n        pass\n    frame()", nullptr, steps);
    CHECK_EQ(animation.error, "");
    CHECK(animation.result.stats.steps > 2'000);
    CHECK_EQ(run_graphics("frame()\nwhile true\n    pass", nullptr, steps).error, "R009");

    // A paced host makes frame() wait: 6 frames at 100 frames per second take at least 50 ms.
    RecordingHost paced;
    paced.pace = true;
    const auto start = std::chrono::steady_clock::now();
    CHECK_EQ(run_graphics("frame_rate(100)\nfor var i = 0, i < 6, i = i + 1\n    frame()", &paced).error, "");
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(50));
    // show() never waits, and neither does a host that is not watched in real time.
    RecordingHost unpaced;
    const auto quick = std::chrono::steady_clock::now();
    run_graphics("frame_rate(1)\nfor var i = 0, i < 5, i = i + 1\n    frame()", &unpaced);
    CHECK(std::chrono::steady_clock::now() - quick < std::chrono::milliseconds(900));
}

TEST_CASE("graphics: keys and the mouse")
{
    RecordingHost host;
    CHECK(host.input.apply("key down left"));
    CHECK(host.input.apply("key down space"));
    CHECK(host.input.apply("key up space"));
    CHECK(host.input.apply("mouse 12 34 1"));
    CHECK(!host.input.apply("mouse 1 2"));
    CHECK(!host.input.apply("mouse a 2 1"));
    CHECK(!host.input.apply("key sideways q"));
    CHECK(!host.input.apply(""));
    const Run run = run_graphics(R"(println(key_pressed(), ",", key_pressed(), ",", key_pressed(), ".")
println(key_down("left"), key_down("space"), mouse_x(), ",", mouse_y(), mouse_down()))", &host);
    CHECK_EQ(run.output, "left,space,.\ntruefalse12,34true\n");
    // Without a host (or events) there are no keys and no mouse.
    CHECK_EQ(run_graphics("println(key_pressed() == \"\", key_down(\"a\"), mouse_x(), mouse_y(), mouse_down())").output,
        "truefalse00false\n");
    // A program that never asks for keys does not make the queue grow for ever.
    Events events;
    for (int i = 0; i < 200; ++i)
    {
        events.apply("key down " + std::to_string(i));
    }
    CHECK_EQ(events.next_key(), "136");
}

TEST_CASE("graphics: events and input lines share one stream")
{
    auto events = std::make_shared<Events>();
    std::istringstream stream("first\n\x1ekey down up\r\nsecond\r\n\x1emouse 5 6 0\n\x1egarbage\nthird");
    {
        EventReader reader(stream, events);
        CHECK(!reader.at_end());
        CHECK(reader.read_line() == std::optional<std::string>("first"));
        CHECK(reader.read_line() == std::optional<std::string>("second"));
        CHECK(reader.read_line() == std::optional<std::string>("third"));
        CHECK(reader.at_end());
        CHECK(!reader.read_line().has_value());
    }
    CHECK_EQ(events->next_key(), "up");
    CHECK_EQ(events->mouse_x(), 5);

    // The VM reads input() lines from the reader.
    const SourceFile source("g.th", "import graphics\nvar a = input()\nprintln(a, \"|\", end_of_input(), \"|\", input(), \"|\", end_of_input())");
    const CompileResult compiled = compile(source);
    REQUIRE(compiled.succeeded());
    std::istringstream mixed("\x1ekey down a\nhello\n\x1ekey down b\nworld\n");
    auto shared = std::make_shared<Events>();
    EventReader reader(mixed, shared);
    std::ostringstream out;
    Vm vm(*compiled.program, out);
    vm.set_line_source(&reader);
    vm.run();
    CHECK_EQ(out.str(), "hello|false|world|true\n");
}

TEST_CASE("graphics: the stream host and the record separator")
{
    std::ostringstream stream;
    Events events;
    StreamHost host(stream, &events);
    CHECK(host.paced());
    CHECK_EQ(host.events(), &events);
    Canvas canvas;
    canvas.resize(2, 3);
    host.present(canvas, 0);
    CHECK_EQ(stream.str(), "\x1e{\"type\":\"frame\",\"n\":1,\"width\":2,\"height\":3,\"keep\":0,\"shapes\":[]}\n");

    // Program text cannot start a bogus frame: the separator is replaced while a host listens.
    RecordingHost listening;
    CHECK_EQ(run_graphics("println(\"a\" + from_char_code(30) + \"b\")", &listening).output, "a\xEF\xBF\xBD" "b\n");
    CHECK_EQ(run_graphics("println(\"a\" + from_char_code(30) + \"b\")").output, "a\x1e" "b\n");
}

TEST_CASE("random numbers")
{
    VmOptions seeded;
    seeded.random_seed = 42;
    const std::string program = "import graphics\nvar xs: int[] = []\nfor var i = 0, i < 8, i = i + 1\n    push(xs, random_int(1, 6))\nprintln(xs)";
    const auto once = [&](VmOptions options)
    {
        const CompileResult compiled = compile(SourceFile("r.th", program));
        std::ostringstream out;
        Vm vm(*compiled.program, out, options);
        vm.run();
        return out.str();
    };
    const std::string first = once(seeded);
    CHECK_EQ(first, once(seeded)); // the same seed gives the same numbers
    CHECK_EQ(first, "[2, 2, 1, 1, 5, 1, 2, 3]\n"); // and the same on every platform (our own generator)
    seeded.random_seed = 43;
    CHECK(first != once(seeded));

    CHECK_EQ(test::run_output("seed_random(7)\nvar a = random_int(0, 1000000)\nseed_random(7)\nprintln(a == random_int(0, 1000000))"),
        "true\n");
    CHECK_EQ(test::run_output("var ok = true\nfor var i = 0, i < 2000, i = i + 1\n    var n = random_int(-3, 3)\n    ok = ok and n >= -3 and n <= 3\nprintln(ok, random_int(5, 5))"),
        "true5\n");
    CHECK_EQ(test::run_output("var n = random_int(-9223372036854775807 - 1, 9223372036854775807)\nprintln(n == n)"), "true\n");
    CHECK_EQ(test::run_output("println(random_int(3, 2))"),
        "ERROR R023: random_int(3, 2): the lowest number must not be greater than the highest");
}

TEST_CASE("graphics: Pashto key names and built-in names")
{
    RecordingHost host;
    host.input.apply("key down left");
    host.input.apply("key down q");
    host.input.apply("mouse 3 4 1");
    const SourceFile source("p.th", "// ژبه: ps\nراوړه ګرافیک\nکرښه چاپ(کښېکښل شوې تڼۍ()، \" \"، کښېکښل شوې تڼۍ()، \" \"، "
                                     "تڼۍ کښېکښل شوې ده(\"کیڼ\")، \" \"، تڼۍ کښېکښل شوې ده(\"left\")، \" \"، "
                                     "د موږک افقي ځای()، د موږک عمودي ځای()، موږک کښېکښل شوی())\n"
                                     "تصادفي بنسټ(۱)\n");
    const CompileResult compiled = compile(source);
    REQUIRE(compiled.succeeded());
    std::ostringstream out;
    Vm vm(*compiled.program, out);
    vm.set_graphics_host(&host);
    vm.run();
    // Keys are named in the program's language; key_down accepts either name.
    CHECK_EQ(out.str(), "کیڼ q رښتیا رښتیا ۳۴رښتیا\n");
    CHECK_EQ(english_pack().key_name("left"), "left");
    CHECK_EQ(pashto_pack().key_name("space"), "تشه");
    CHECK_EQ(pashto_pack().key_name("a"), "a");
    CHECK_EQ(pashto_pack().english_key("ښي"), "right");
    CHECK_EQ(pashto_pack().english_key("x"), "x");
}

TEST_CASE("graphics: Pashto programs")
{
    const SourceFile source("p.th", "// ژبه: ps\nراوړه ګرافیک\nټاټ(۱۰, ۱۰)\nټاټ پاک کړه(\"تور\")\nدایره(۵, ۵, ۲, \"سور\")\nښکاره کړه()\n");
    const CompileResult compiled = compile(source);
    REQUIRE(compiled.succeeded());
    std::ostringstream out;
    RecordingHost host;
    Vm vm(*compiled.program, out);
    vm.set_graphics_host(&host);
    vm.run();
    REQUIRE(host.records.size() == 1u);
    CHECK_CONTAINS(host.records[0], "[\"clear\",\"#000000\"],[\"circle\",5,5,2,\"#ff0000\"]");
    CHECK_EQ(test::run_output("// ژبه: ps\nراوړه ګرافیک\nټاټ(۰, ۱۰)"),
        "ERROR R022: هغه پلنوالی چې 'ټاټ' ته ورکړل شوی باید له ۱ تر ۲۰۰۰ وي (۰ ورکړل شو)");
}
