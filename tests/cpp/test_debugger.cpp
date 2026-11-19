// The live debugger (thundervm --debug): the conversation over a scripted channel.
#include "helpers.hpp"
#include "json_check.hpp"
#include "minitest.hpp"
#include "thunder/debugger.hpp"
#include "thunder/json.hpp"

#include <deque>
#include <sstream>
#include <string>
#include <vector>

using namespace thunder;

namespace
{
    // A channel whose lines are scripted in advance. A line may be held back until the debugger
    // has looked for one a number of times (has_line), which is how a pause arrives mid-run.
    class ScriptedChannel final : public DebugChannel
    {
    public:
        void script(std::string line, std::size_t after_polls = 0)
        {
            lines_.push_back({std::move(line), after_polls, after_polls > 0});
        }

        std::optional<std::string> read_line() override
        {
            if (lines_.empty())
            {
                return std::nullopt; // the client has gone
            }
            std::string line = std::move(lines_.front().text);
            lines_.pop_front();
            return line;
        }

        bool has_line() override
        {
            if (lines_.empty() || !lines_.front().pushed)
            {
                return false; // a scripted line waits to be asked for, unless it is "pushed"
            }
            if (lines_.front().after_polls > 0)
            {
                --lines_.front().after_polls;
                return false;
            }
            return true;
        }

        void write_line(std::string_view line) override
        {
            sent.emplace_back(line);
        }

        // Every message sent, parsed.
        std::vector<JsonValue> messages() const
        {
            std::vector<JsonValue> parsed;
            for (const std::string& line : sent)
            {
                auto value = parse_json(line);
                REQUIRE(value.has_value());
                parsed.push_back(*value);
            }
            return parsed;
        }

        std::vector<JsonValue> events(std::string_view name) const
        {
            std::vector<JsonValue> found;
            for (const JsonValue& message : messages())
            {
                if (message.string_or("type") == "event" && message.string_or("event") == name)
                {
                    found.push_back(message);
                }
            }
            return found;
        }

        std::optional<JsonValue> response_to(std::int64_t seq) const
        {
            for (const JsonValue& message : messages())
            {
                if (message.string_or("type") == "response" && message.int_or("request_seq") == seq)
                {
                    return message;
                }
            }
            return std::nullopt;
        }

        std::vector<std::string> sent;

    private:
        struct Line
        {
            std::string text;
            std::size_t after_polls;
            bool pushed; // arrives on its own (has_line) rather than when asked for (read_line)
        };
        std::deque<Line> lines_;
    };

    // Runs 'source' under the debugger with a scripted conversation. Requests are numbered 1, 2, ...
    struct Session
    {
        explicit Session(const std::string& source, DebuggerOptions options = {})
            : program(test::compile_ok(source))
            , options_(options)
        {
        }

        Session& request(std::string body, std::size_t after_polls = 0)
        {
            channel.script("{\"seq\":" + std::to_string(next_seq++) + "," + body + "}", after_polls);
            return *this;
        }

        ExecResult run()
        {
            std::ostringstream out;
            Vm vm(program, out);
            Debugger debugger(channel, program, options_);
            vm.set_line_source(&debugger);
            // As thundervm wires it: the debugger is the graphics host too (frames travel as
            // "frame" events), unless a --display plug-in has a window of its own.
            vm.set_graphics_host(&debugger);
            const ExecResult result = vm.run(&debugger);
            debugger.finish(vm, result);
            return result;
        }

        std::vector<std::string> stop_lines() const
        {
            std::vector<std::string> lines;
            for (const JsonValue& stopped : channel.events("stopped"))
            {
                lines.push_back(stopped.string_or("reason") + "@" + std::to_string(stopped.int_or("line")));
            }
            return lines;
        }

        std::string output() const
        {
            std::string text;
            for (const JsonValue& event : channel.events("output"))
            {
                text += event.string_or("text");
            }
            return text;
        }

        bytecode::Program program;
        ScriptedChannel channel;
        int next_seq = 1;

    private:
        DebuggerOptions options_;
    };

    const std::string PROGRAM =
        "func twice(n: int): int\n"    // 1
        "    var doubled = n * 2\n"    // 2
        "    return doubled\n"         // 3
        "\n"                           // 4
        "var answer = twice(21)\n"     // 5
        "println(answer)\n"            // 6
        "var more = twice(answer)\n"   // 7
        "println(more)\n";             // 8
}

// ---------------------------------------------------------------------- the JSON reader

TEST_CASE("json: the reader understands every kind of value")
{
    const auto parsed = parse_json(R"({"name":"Ada","age":36,"ok":true,"none":null,"list":[1,2.5,-3e2],"nested":{"k":"v"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->is_object());
    CHECK_EQ(parsed->string_or("name"), "Ada");
    CHECK_EQ(parsed->int_or("age"), 36);
    CHECK(parsed->bool_or("ok"));
    CHECK(parsed->get("none")->is_null());
    CHECK_EQ(parsed->array_or_empty("list").size(), 3u);
    CHECK_EQ(parsed->array_or_empty("list")[1].as_number(), 2.5);
    CHECK_EQ(parsed->array_or_empty("list")[2].as_number(), -300.0);
    CHECK_EQ(parsed->get("nested")->string_or("k"), "v");
    CHECK_EQ(parsed->string_or("missing", "fallback"), "fallback");
    CHECK_EQ(parsed->int_or("name", 7), 7); // the wrong type falls back too
    CHECK(parsed->array_or_empty("age").empty());
}

TEST_CASE("json: the reader decodes escapes and rejects broken documents")
{
    const auto text = parse_json(R"("a\"b\\c\n\u00e9\ud83d\ude00")");
    REQUIRE(text.has_value());
    CHECK_EQ(text->as_string(), std::string("a\"b\\c\n\xC3\xA9\xF0\x9F\x98\x80"));
    for (const std::string_view broken : {"", "{", "[1,]", "{\"a\" 1}", "tru", "\"open", "1 2", "{\"a\":1,}", "\"\\x\""})
    {
        CHECK(!parse_json(broken).has_value());
    }
    const auto spaced = parse_json(" \n\t[ ] ");
    REQUIRE(spaced.has_value());
    CHECK(spaced->is_array());
}

TEST_CASE("json: what the writer writes, the reader reads")
{
    JsonWriter writer;
    writer.begin_object();
    writer.field("text", "line\nbreak \"quoted\" \\ پښتو");
    writer.field("n", std::int64_t(-42));
    writer.field("f", 0.5);
    writer.end_object();
    const auto parsed = parse_json(writer.str());
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->string_or("text"), "line\nbreak \"quoted\" \\ پښتو");
    CHECK_EQ(parsed->int_or("n"), -42);
    CHECK_EQ(parsed->get("f")->as_number(), 0.5);
}

// ---------------------------------------------------------------------- the conversation

TEST_CASE("debugger: a session starts stopped at the entry and runs to a breakpoint")
{
    Session session(PROGRAM);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":7}])")
        .request(R"("command":"continue")")
        .request(R"("command":"stackTrace")")
        .request(R"("command":"variables","scope":"globals")")
        .request(R"("command":"continue")");
    const ExecResult result = session.run();
    CHECK(result.ok);
    CHECK(!result.aborted);
    for (const std::string& line : session.channel.sent)
    {
        CHECK(test::is_valid_json(line));
    }
    CHECK_EQ(session.channel.events("ready").size(), 1u);
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@5", "breakpoint@7"}));
    CHECK_EQ(session.output(), "42\n84\n");

    const auto breakpoints = session.channel.response_to(1);
    REQUIRE(breakpoints.has_value());
    CHECK(breakpoints->get("body")->array_or_empty("breakpoints")[0].bool_or("verified"));

    const auto stack = session.channel.response_to(3);
    REQUIRE(stack.has_value());
    const auto& frames = stack->get("body")->array_or_empty("frames");
    REQUIRE(frames.size() == 1u);
    CHECK_EQ(frames[0].string_or("function"), "<main>");
    CHECK_EQ(frames[0].int_or("line"), 7);

    const auto globals = session.channel.response_to(4);
    REQUIRE(globals.has_value());
    const auto& variables = globals->get("body")->array_or_empty("variables");
    REQUIRE(variables.size() == 2u);
    CHECK_EQ(variables[0].string_or("name"), "answer");
    CHECK_EQ(variables[0].string_or("value"), "42");
    CHECK_EQ(variables[0].string_or("type"), "int");

    const auto exited = session.channel.events("exited");
    REQUIRE(exited.size() == 1u);
    CHECK(exited[0].bool_or("ok"));
    CHECK_EQ(exited[0].int_or("exit_code"), 0);
}

TEST_CASE("debugger: stepping over a call stays in the frame, stepping in enters it, stepping out returns")
{
    Session session(PROGRAM);
    session.request(R"("command":"next")")   // 5 -> 6 (over the call)
        .request(R"("command":"next")")      // 6 -> 7
        .request(R"("command":"stepIn")")    // 7 -> 2 (inside twice)
        .request(R"("command":"stackTrace")")
        .request(R"("command":"variables","scope":"locals","frame":0)")
        .request(R"("command":"stepOut")")   // back on line 7, after the call
        .request(R"("command":"next")")      // -> 8
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.stop_lines(),
        (std::vector<std::string>{"entry@5", "step@6", "step@7", "step@2", "step@7", "step@8"}));

    const auto stack = session.channel.response_to(4);
    REQUIRE(stack.has_value());
    const auto& frames = stack->get("body")->array_or_empty("frames");
    REQUIRE(frames.size() == 2u);
    CHECK_EQ(frames[0].string_or("function"), "twice");
    CHECK_EQ(frames[0].int_or("line"), 2);
    CHECK_EQ(frames[1].string_or("function"), "<main>");
    CHECK_EQ(frames[1].int_or("line"), 7); // the caller is shown on the line of the call

    const auto locals = session.channel.response_to(5);
    REQUIRE(locals.has_value());
    const auto& variables = locals->get("body")->array_or_empty("variables");
    REQUIRE(variables.size() == 2u);
    CHECK_EQ(variables[0].string_or("name"), "n");
    CHECK_EQ(variables[0].string_or("value"), "42");
    CHECK_EQ(variables[1].string_or("name"), "doubled");
}

TEST_CASE("debugger: a breakpoint on the first line fires when the entry stop is continued")
{
    Session session(PROGRAM);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":5}])")
        .request(R"("command":"continue")")
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@5", "breakpoint@5"}));
}

TEST_CASE("debugger: a breakpoint on a line without code moves to the next line that has some")
{
    Session session(PROGRAM);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":4},{"line":40}])")
        .request(R"("command":"continue")")
        .request(R"("command":"continue")");
    session.run();
    const auto response = session.channel.response_to(1);
    REQUIRE(response.has_value());
    const auto& placed = response->get("body")->array_or_empty("breakpoints");
    REQUIRE(placed.size() == 2u);
    CHECK_EQ(placed[0].int_or("line"), 5);
    CHECK(placed[0].bool_or("verified"));
    CHECK(!placed[1].bool_or("verified"));
    CHECK_CONTAINS(placed[1].string_or("message"), "no code");
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@5", "breakpoint@5"}));
}

TEST_CASE("debugger: a condition decides whether a breakpoint stops")
{
    const std::string loop = "var total = 0\nfor var i = 0, i < 10, i = i + 1\n    total = total + i\nprintln(total)\n";
    Session session(loop);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":3,"condition":"i >= 8"}])")
        .request(R"("command":"continue")")
        .request(R"("command":"evaluate","expression":"i")")
        .request(R"("command":"continue")")
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@1", "breakpoint@3", "breakpoint@3"}));
    const auto evaluated = session.channel.response_to(3);
    REQUIRE(evaluated.has_value());
    CHECK_EQ(evaluated->get("body")->string_or("value"), "8");
    CHECK_EQ(session.output(), "45\n");
}

TEST_CASE("debugger: a condition that cannot be evaluated stops anyway and says why")
{
    const std::string loop = "var total = 0\nfor var i = 0, i < 10, i = i + 1\n    total = total + i\nprintln(total)\n";
    Session session(loop);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":4,"condition":"nobody == 1"}])")
        .request(R"("command":"continue")")
        .request(R"("command":"continue")");
    session.run();
    const auto stops = session.channel.events("stopped");
    REQUIRE(stops.size() == 2u);
    CHECK_EQ(stops[1].string_or("reason"), "breakpoint");
    CHECK_CONTAINS(stops[1].string_or("description"), "could not be evaluated");
}

TEST_CASE("debugger: a hit count decides how often a breakpoint stops")
{
    const std::string loop = "var total = 0\nfor var i = 0, i < 10, i = i + 1\n    total = total + i\nprintln(total)\n";
    Session session(loop);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":3,"hitCondition":"% 3"}])");
    for (int i = 0; i < 5; ++i)
    {
        session.request(R"("command":"continue")");
    }
    session.run();
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@1", "breakpoint@3", "breakpoint@3", "breakpoint@3"}));
    CHECK_EQ(session.output(), "45\n");
}

TEST_CASE("debugger: variables are described with their type and expanded by path")
{
    const std::string source =
        "struct Point\n    x: int\n    y: int\n"
        "var p = Point(1, 2)\nvar items = [p, Point(3, 4)]\nvar scores = {\"ada\": 10}\nvar pair = (1, \"one\")\n"
        "println(items[1].y)\n";
    Session session(source);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":8}])")
        .request(R"("command":"continue")")
        .request(R"("command":"variables","scope":"globals")")
        .request(R"("command":"variables","scope":"globals","path":"items")")
        .request(R"("command":"variables","scope":"globals","path":"items[1]")")
        .request(R"("command":"evaluate","expression":"items[1].y")")
        .request(R"("command":"evaluate","expression":"scores[\"ada\"]")")
        .request(R"("command":"evaluate","expression":"pair[1]")")
        .request(R"("command":"evaluate","expression":"items[7]")")
        .request(R"("command":"evaluate","expression":"items[1].y > 3")")
        .request(R"("command":"variables","scope":"stack")")
        .request(R"("command":"variables","scope":"execution")")
        .request(R"("command":"continue")");
    session.run();
    const auto globals = session.channel.response_to(3);
    REQUIRE(globals.has_value());
    const auto& variables = globals->get("body")->array_or_empty("variables");
    REQUIRE(variables.size() == 4u);
    CHECK_EQ(variables[0].string_or("type"), "Point");
    CHECK(variables[0].bool_or("expandable"));
    CHECK_EQ(variables[1].string_or("type"), "Point[]");
    CHECK_EQ(variables[2].string_or("type"), "{string: int}");
    CHECK_EQ(variables[3].string_or("type"), "(int, string)");

    const auto elements = session.channel.response_to(4);
    REQUIRE(elements.has_value());
    const auto& items = elements->get("body")->array_or_empty("variables");
    REQUIRE(items.size() == 2u);
    CHECK_EQ(items[1].string_or("name"), "[1]");
    CHECK_EQ(items[1].string_or("value"), "Point(x: 3, y: 4)");

    const auto fields = session.channel.response_to(5);
    REQUIRE(fields.has_value());
    const auto& members = fields->get("body")->array_or_empty("variables");
    REQUIRE(members.size() == 2u);
    CHECK_EQ(members[0].string_or("name"), "x");
    CHECK_EQ(members[1].string_or("value"), "4");

    CHECK_EQ(session.channel.response_to(6)->get("body")->string_or("value"), "4");
    CHECK_EQ(session.channel.response_to(7)->get("body")->string_or("value"), "10");
    CHECK_EQ(session.channel.response_to(8)->get("body")->string_or("value"), "\"one\"");
    CHECK(!session.channel.response_to(9)->bool_or("success"));
    CHECK_EQ(session.channel.response_to(10)->get("body")->string_or("value"), "true");

    const auto stack = session.channel.response_to(11);
    REQUIRE(stack.has_value());
    CHECK(stack->get("body")->array_or_empty("variables").empty()); // nothing pushed yet on line 8

    const auto execution = session.channel.response_to(12);
    REQUIRE(execution.has_value());
    const auto& facts = execution->get("body")->array_or_empty("variables");
    REQUIRE(facts.size() >= 4u);
    CHECK_EQ(facts[0].string_or("name"), "step");
    CHECK_EQ(facts[1].string_or("name"), "instruction");
    CHECK_CONTAINS(facts[1].string_or("value"), "LOAD_GLOBAL");
}

TEST_CASE("debugger: the history lets the client step back and then forwards again")
{
    // The history holds every line that ran, the lines inside twice included:
    // 5 (entry), 2, 3, 6, 7 after the two steps over.
    Session session(PROGRAM);
    session.request(R"("command":"next")")     // 6
        .request(R"("command":"next")")        // 7
        .request(R"("command":"stepBack")")    // back to 6 (history)
        .request(R"("command":"stackTrace")")
        .request(R"("command":"variables","scope":"globals")")
        .request(R"("command":"evaluate","expression":"answer")")
        .request(R"("command":"stepBack")")    // 3, inside twice
        .request(R"("command":"stackTrace")")
        .request(R"("command":"stepBack")")    // 2
        .request(R"("command":"stepBack")")    // 5
        .request(R"("command":"stepBack")")    // stays at 5: the beginning
        .request(R"("command":"next")")        // forwards in history, over the call: 6
        .request(R"("command":"stepIn")")      // 7: the live state again
        .request(R"("command":"next")")        // live: 8
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@5", "step@6", "step@7", "step@6", "step@3",
        "step@2", "step@5", "step@5", "step@6", "step@7", "step@8"}));
    const auto stops = session.channel.events("stopped");
    CHECK(stops[3].bool_or("historic"));
    CHECK(stops[8].bool_or("historic"));
    CHECK(!stops[9].bool_or("historic"));
    CHECK(!stops[10].bool_or("historic"));
    const auto inside = session.channel.response_to(8);
    REQUIRE(inside.has_value());
    const auto& frames = inside->get("body")->array_or_empty("frames");
    REQUIRE(frames.size() == 2u);
    CHECK_EQ(frames[0].string_or("function"), "twice");
    CHECK_EQ(frames[1].int_or("line"), 5);
    const auto stack = session.channel.response_to(4);
    REQUIRE(stack.has_value());
    CHECK(stack->get("body")->bool_or("historic"));
    CHECK_EQ(stack->get("body")->array_or_empty("frames")[0].int_or("line"), 6);
    const auto globals = session.channel.response_to(5);
    REQUIRE(globals.has_value());
    const auto& variables = globals->get("body")->array_or_empty("variables");
    REQUIRE(variables.size() == 2u);
    CHECK_EQ(variables[0].string_or("value"), "42");
    CHECK(!variables[0].bool_or("expandable"));
    CHECK_EQ(session.channel.response_to(6)->get("body")->string_or("value"), "42");
    // The output was sent once, when it happened, not again while stepping through the history.
    CHECK_EQ(session.output(), "42\n84\n");
}

TEST_CASE("debugger: reverse continue finds an earlier breakpoint, continue in the history a later one")
{
    const std::string loop = "var total = 0\nfor var i = 0, i < 4, i = i + 1\n    total = total + i\nprintln(total)\n";
    Session session(loop);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":3}])")
        .request(R"("command":"continue")")        // i = 0
        .request(R"("command":"continue")")        // i = 1
        .request(R"("command":"continue")")        // i = 2
        .request(R"("command":"reverseContinue")") // back to i = 1
        .request(R"("command":"evaluate","expression":"i")")
        .request(R"("command":"reverseContinue")") // i = 0
        .request(R"("command":"reverseContinue")") // nothing earlier: the entry
        .request(R"("command":"continue")")        // forwards in history: i = 0
        .request(R"("command":"continue")")        // i = 1
        .request(R"("command":"continue")")        // i = 2 (the live state)
        .request(R"("command":"continue")")        // live: i = 3
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@1", "breakpoint@3", "breakpoint@3", "breakpoint@3",
        "breakpoint@3", "breakpoint@3", "entry@1", "breakpoint@3", "breakpoint@3", "breakpoint@3", "breakpoint@3"}));
    CHECK_EQ(session.channel.response_to(6)->get("body")->string_or("value"), "1");
    CHECK_EQ(session.output(), "6\n");
}

TEST_CASE("debugger: without a history stepping back stays where it is")
{
    Session session(PROGRAM, DebuggerOptions{0});
    session.request(R"("command":"next")")
        .request(R"("command":"stepBack")")
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.stop_lines(), (std::vector<std::string>{"entry@5", "step@6", "step@6"}));
}

TEST_CASE("debugger: the history is bounded")
{
    const std::string loop = "var total = 0\nfor var i = 0, i < 50, i = i + 1\n    total = total + i\nprintln(total)\n";
    Session session(loop, DebuggerOptions{5});
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":4}])")
        .request(R"("command":"continue")")
        .request(R"("command":"stackTrace")")
        .request(R"("command":"continue")");
    session.run();
    const auto stopped = session.channel.events("stopped");
    REQUIRE(stopped.size() == 2u);
    CHECK_EQ(stopped[1].get("history")->int_or("size"), 5);
}

TEST_CASE("debugger: terminate ends the program at once")
{
    Session session(PROGRAM);
    session.request(R"("command":"next")").request(R"("command":"terminate")");
    const ExecResult result = session.run();
    CHECK(result.aborted);
    CHECK(result.ok);
    CHECK_EQ(session.output(), ""); // line 6 never ran
    const auto exited = session.channel.events("exited");
    REQUIRE(exited.size() == 1u);
    CHECK(exited[0].bool_or("aborted"));
}

TEST_CASE("debugger: a client that goes away ends the program")
{
    Session session(PROGRAM);
    const ExecResult result = session.run(); // no commands at all: the entry stop reads nothing
    CHECK(result.aborted);
    CHECK_EQ(session.channel.events("exited").size(), 1u);
}

TEST_CASE("debugger: a pause stops the program where it is")
{
    const std::string loop = "var i = 0\nwhile i < 100000\n    i = i + 1\nprintln(i)\n";
    Session session(loop);
    session.request(R"("command":"continue")")
        .request(R"("command":"pause")", 500) // arrives after 500 instructions
        .request(R"("command":"evaluate","expression":"i")")
        .request(R"("command":"continue")");
    session.run();
    const auto stops = session.channel.events("stopped");
    REQUIRE(stops.size() == 2u);
    CHECK_EQ(stops[1].string_or("reason"), "pause");
    const auto evaluated = session.channel.response_to(3);
    REQUIRE(evaluated.has_value());
    const std::string value = evaluated->get("body")->string_or("value");
    CHECK(value != "0" && value != "100000");
    CHECK_EQ(session.output(), "100000\n");
}

TEST_CASE("debugger: a runtime error stops the program in the state it failed in")
{
    const std::string source = "func divide(a: int, b: int): int\n    return a / b\nprintln(divide(1, 0))\n";
    Session session(source);
    session.request(R"("command":"continue")")
        .request(R"("command":"stackTrace")")
        .request(R"("command":"variables","scope":"locals","frame":0)")
        .request(R"("command":"continue")");
    const ExecResult result = session.run();
    CHECK(!result.ok);
    const auto stops = session.channel.events("stopped");
    REQUIRE(stops.size() == 2u);
    CHECK_EQ(stops[1].string_or("reason"), "exception");
    CHECK_CONTAINS(stops[1].string_or("description"), "R0");
    CHECK_CONTAINS(stops[1].string_or("text"), "divide");
    CHECK_EQ(stops[1].int_or("line"), 2);
    const auto stack = session.channel.response_to(2);
    REQUIRE(stack.has_value());
    const auto& frames = stack->get("body")->array_or_empty("frames");
    REQUIRE(frames.size() == 2u);
    CHECK_EQ(frames[0].string_or("function"), "divide");
    CHECK_EQ(frames[0].int_or("line"), 2);
    CHECK_EQ(frames[1].int_or("line"), 3);
    const auto locals = session.channel.response_to(3);
    REQUIRE(locals.has_value());
    CHECK_EQ(locals->get("body")->array_or_empty("variables")[1].string_or("value"), "0");
    const auto exited = session.channel.events("exited");
    REQUIRE(exited.size() == 1u);
    CHECK(!exited[0].bool_or("ok"));
    CHECK(exited[0].get("error")->is_object());
}

TEST_CASE("debugger: input() asks the client for a line")
{
    const std::string source = "var name = input(\"Name? \")\nprintln(\"Hello, \", name)\nprintln(end_of_input())\n";
    Session session(source);
    session.request(R"("command":"continue")")
        .request(R"("command":"stackTrace")") // allowed while the program waits for input
        .request(R"("command":"input","text":"Ada")")
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.channel.events("input").size(), 1u);
    CHECK(session.channel.response_to(2).has_value());
    CHECK_EQ(session.output(), "Name? Hello, Ada\nfalse\n");
    // The end of the input is a command too.
    Session ended(source);
    ended.request(R"("command":"continue")").request(R"("command":"input","end":true)");
    ended.run();
    CHECK_EQ(ended.output(), "Name? Hello, \ntrue\n");
}

TEST_CASE("debugger: a graphics program's frames travel as events, and the canvas answers back")
{
    Session session(
        "import graphics\n"
        "graphics.canvas(50, 40)\n"
        "graphics.rect(5, 6, 10, 10, \"red\")\n"
        "graphics.frame()\n"
        "var waited = input()\n" // holds the program while the canvas's events are scripted
        "println(graphics.mouse_x(), \" \", graphics.mouse_y(), \" \", graphics.key_pressed())\n");
    session.request(R"("command":"continue")")
        .request(R"("command":"canvasEvent","text":"mouse 20 30 1")") // handled while waiting
        .request(R"("command":"canvasEvent","text":"key down left")")
        .request(R"("command":"canvasEvent","text":"not an event at all")") // quietly ignored
        .request(R"("command":"input","text":"go")");
    session.run();
    const std::vector<JsonValue> frames = session.channel.events("frame");
    REQUIRE(!frames.empty());
    const JsonValue* record = frames.front().get("record");
    REQUIRE(record != nullptr);
    CHECK_EQ(record->string_or("type"), "frame");
    CHECK_EQ(record->int_or("width"), 50);
    CHECK_EQ(record->int_or("height"), 40);
    bool rect = false;
    for (const JsonValue& shape : record->array_or_empty("shapes"))
    {
        rect = rect || (shape.is_array() && !shape.as_array().empty()
            && shape.as_array().front().is_string() && shape.as_array().front().as_string() == "rect");
    }
    CHECK(rect);
    // The canvas's mouse and key arrived through the protocol.
    CHECK_EQ(session.output(), "20 30 left\n");
}

TEST_CASE("debugger: a function's frame shows the caller on the line of the call, and locals shadow correctly")
{
    const std::string source =
        "func outer(n: int): int\n"
        "    var k = n + 1\n"
        "    return inner(k)\n"
        "func inner(k: int): int\n"
        "    return k * 2\n"
        "println(outer(1))\n";
    Session session(source);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":5}])")
        .request(R"("command":"continue")")
        .request(R"("command":"stackTrace")")
        .request(R"("command":"variables","scope":"locals","frame":1)")
        .request(R"("command":"evaluate","expression":"k","frame":1)")
        .request(R"("command":"evaluate","expression":"k","frame":0)")
        .request(R"("command":"continue")");
    session.run();
    const auto stack = session.channel.response_to(3);
    REQUIRE(stack.has_value());
    const auto& frames = stack->get("body")->array_or_empty("frames");
    REQUIRE(frames.size() == 3u);
    CHECK_EQ(frames[0].string_or("function"), "inner");
    CHECK_EQ(frames[1].string_or("function"), "outer");
    CHECK_EQ(frames[1].int_or("line"), 3);
    CHECK_EQ(frames[2].int_or("line"), 6);
    const auto locals = session.channel.response_to(4);
    REQUIRE(locals.has_value());
    const auto& variables = locals->get("body")->array_or_empty("variables");
    REQUIRE(variables.size() == 2u);
    CHECK_EQ(variables[0].string_or("name"), "n");
    CHECK_EQ(variables[1].string_or("name"), "k");
    CHECK_EQ(variables[1].string_or("value"), "2");
    CHECK_EQ(session.channel.response_to(5)->get("body")->string_or("value"), "2");
    CHECK_EQ(session.channel.response_to(6)->get("body")->string_or("value"), "2");
}

TEST_CASE("debugger: unknown and malformed requests are answered, not fatal")
{
    Session session(PROGRAM);
    session.channel.script("this is not json");
    session.channel.script("[1, 2]");
    session.request(R"("command":"dance")")
        .request(R"("command":"continue")");
    session.run();
    CHECK_EQ(session.channel.events("error").size(), 2u);
    const auto response = session.channel.response_to(1);
    REQUIRE(response.has_value());
    CHECK(!response->bool_or("success"));
    CHECK_CONTAINS(response->string_or("message"), "unknown command");
    CHECK_EQ(session.output(), "42\n84\n");
}

TEST_CASE("debugger: hit conditions are read every way VS Code writes them")
{
    for (const auto& [condition, expected] : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"3", {"entry@1", "breakpoint@3"}},
             {"== 2", {"entry@1", "breakpoint@3"}},
             {">= 3", {"entry@1", "breakpoint@3", "breakpoint@3"}},
             {"> 3", {"entry@1", "breakpoint@3"}},
             {"< 2", {"entry@1", "breakpoint@3"}},
             {"<= 2", {"entry@1", "breakpoint@3", "breakpoint@3"}},
             {"% 2", {"entry@1", "breakpoint@3", "breakpoint@3"}},
             {"nonsense", {"entry@1", "breakpoint@3", "breakpoint@3", "breakpoint@3", "breakpoint@3"}},
         })
    {
        const std::string loop = "var total = 0\nfor var i = 0, i < 4, i = i + 1\n    total = total + i\nprintln(total)\n";
        Session session(loop);
        session.request("\"command\":\"setBreakpoints\",\"file\":0,\"breakpoints\":[{\"line\":3,\"hitCondition\":\"" + condition + "\"}]");
        for (std::size_t i = 0; i < 6; ++i)
        {
            session.request(R"("command":"continue")");
        }
        session.run();
        CHECK_EQ(session.stop_lines(), expected);
    }
}

TEST_CASE("debugger: a Pashto program's types are named in Pashto")
{
    Session session("# ژبه: ps\nمتغیر نوم = \"ناصر\"\nمتغیر شمېرې = [۱، ۲]\nمتغیر جوړه = (۱، \"یو\")\nکرښه چاپ(نوم)\n");
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":5}])")
        .request(R"("command":"continue")")
        .request(R"("command":"variables","scope":"globals")")
        .request(R"("command":"evaluate","expression":"نوم")")
        .request(R"("command":"continue")");
    session.run();
    const auto globals = session.channel.response_to(3);
    REQUIRE(globals.has_value());
    const auto& variables = globals->get("body")->array_or_empty("variables");
    REQUIRE(variables.size() == 3u);
    CHECK_EQ(variables[0].string_or("type"), "متن");
    CHECK_EQ(variables[1].string_or("type"), "عدد[]");
    CHECK_EQ(variables[2].string_or("type"), "(عدد، متن)");
    CHECK_EQ(session.channel.response_to(4)->get("body")->string_or("type"), "متن");
}

TEST_CASE("debugger: a module's variable is evaluated by its qualified name")
{
    const std::string source = "module geometry\n    struct Point\n        x: int\n    var made = 2\n    var origin = Point(7)\n"
                               "println(geometry.made)\n";
    Session session(source);
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":6}])")
        .request(R"("command":"continue")")
        .request(R"("command":"evaluate","expression":"geometry.made")")
        .request(R"("command":"evaluate","expression":"geometry.origin.x")")
        .request(R"("command":"continue")");
    session.run();
    const auto made = session.channel.response_to(3);
    REQUIRE(made.has_value());
    CHECK_EQ(made->get("body")->string_or("value"), "2");
    const auto x = session.channel.response_to(4);
    REQUIRE(x.has_value());
    CHECK_EQ(x->get("body")->string_or("value"), "7");
}

TEST_CASE("debugger: a large value is remembered in brief, a small one in full")
{
    // Every line's snapshot writes its variables out: a large array is kept as its first
    // elements and "…", so a program holding a grid does not spend its time on the history.
    const std::string program =
        "var small = [1, 2, 3]\nvar grid = array(100, 7)\nvar row = [array(10, 1), array(10, 2)]\nprintln(len(grid))\nprintln(1)\n";
    Session session(program);
    session.request(R"("command":"next")")
        .request(R"("command":"next")")
        .request(R"("command":"next")")
        .request(R"("command":"next")")       // 5: three lines remembered with grid in them
        .request(R"("command":"stepBack")")   // 4, historic
        .request(R"("command":"variables","scope":"globals")")
        .request(R"("command":"next")")       // 5: live again
        .request(R"("command":"variables","scope":"globals")")
        .request(R"("command":"continue")");
    session.run();
    const auto historic = session.channel.response_to(6)->get("body")->array_or_empty("variables");
    REQUIRE(historic.size() == 3u);
    CHECK_EQ(historic[0].string_or("value"), "[1, 2, 3]");
    CHECK_EQ(historic[1].string_or("value"), "[7, 7, 7, 7, 7, 7, 7, 7, …]");
    CHECK_EQ(historic[1].string_or("type"), "int[]");
    CHECK_EQ(historic[2].string_or("value"), "[[1, 1, 1, 1, 1, 1, 1, 1, 1, 1], [2, 2, 2, 2, 2, 2, 2, 2, 2, 2]]");
    // The live value is whole, as the Variables view shows it.
    const auto live = session.channel.response_to(8)->get("body")->array_or_empty("variables");
    REQUIRE(live.size() == 3u);
    CHECK(live[1].string_or("value").size() > 200);
}

TEST_CASE("debugger: a drawing program's history keeps to a time budget, a stop is always in it")
{
    // An animation runs many lines per frame; running freely, its history may take a tenth of
    // the time, so it keeps far fewer lines than ran - but the line a breakpoint stopped on is
    // there, and a program that does not draw keeps them all.
    const std::string drawing =
        "import graphics\ngraphics.canvas(20, 20)\ngraphics.frame()\nvar total = 0\n"
        "for var frame = 0, frame < 3, frame = frame + 1\n"
        "    for var i = 0, i < 3000, i = i + 1\n        total = total + i\n"
        "    graphics.frame()\n"
        "println(total)\n";
    Session session(drawing, DebuggerOptions{100000});
    session.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":9}])")
        .request(R"("command":"continue")")
        .request(R"("command":"continue")");
    session.run();
    const auto stopped = session.channel.events("stopped");
    REQUIRE(stopped.size() == 2u);
    CHECK_EQ(stopped[1].string_or("reason"), "breakpoint");
    const auto kept = stopped[1].get("history")->int_or("size");
    CHECK(kept >= 2);         // the entry and the breakpoint's line at least
    CHECK(kept < 9000);       // well under half of the ~18000 lines that ran
    // Without drawing, every line is remembered.
    const std::string counting = "var total = 0\nfor var i = 0, i < 300, i = i + 1\n    total = total + i\nprintln(total)\n";
    Session plain(counting, DebuggerOptions{100000});
    plain.request(R"("command":"setBreakpoints","file":0,"breakpoints":[{"line":4}])")
        .request(R"("command":"continue")")
        .request(R"("command":"continue")");
    plain.run();
    const auto plain_stops = plain.channel.events("stopped");
    REQUIRE(plain_stops.size() == 2u);
    CHECK(plain_stops[1].get("history")->int_or("size") >= 600);
}
