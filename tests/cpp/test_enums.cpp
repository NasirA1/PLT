// Enums: a type whose values are its named members, each with a position. Color.red is a
// member; to_int gives its position, str its name; Color(2) and Color("red") find a member when
// the program runs; Color.values lists every member and Color.count counts them.
#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/translate.hpp"

using test::compile_ok;
using test::diagnostic_codes;
using test::run_output;
using Codes = std::vector<std::string>;
using namespace thunder;

namespace
{
    const char* const light = "enum Light\n    red\n    amber\n    green\n";

    std::string with_light(const std::string& code)
    {
        return std::string(light) + code;
    }
}

TEST_CASE("enums: members print by name, convert to and from int and string, and compare by position")
{
    CHECK_EQ(run_output(with_light("println(Light.red, \" \", str(Light.green), \" \", [Light.red, Light.green])")), "red green [Light.red, Light.green]\n");
    CHECK_EQ(run_output(with_light("println(to_int(Light.amber), \" \", Light(2), \" \", Light(\"amber\"), \" \", Light.count, \" \", Light.values)")),
        "1 green amber 3 [Light.red, Light.amber, Light.green]\n");
    CHECK_EQ(run_output(with_light("println(Light.red < Light.green, \" \", Light.red == Light(0), \" \", Light.red != Light.amber, \" \", Light.green >= Light.amber)")),
        "true true true true\n");
    CHECK_EQ(run_output(with_light("var l: Light\nprintln(l, \" \", l == Light.red)")), "red true\n");
    CHECK_EQ(run_output(with_light("for l in Light.values\n    print(to_int(l), \":\", l, \" \")\nprintln()")), "0:red 1:amber 2:green \n");
    CHECK_EQ(run_output(with_light("println(sort([Light.green, Light.red]), \" \", contains(Light.values, Light.amber), \" \", index_of(Light.values, Light.green))")),
        "[Light.red, Light.green] true 2\n");
    CHECK_EQ(run_output(with_light("var next: {Light: Light} = {Light.red: Light.green, Light.green: Light.amber, Light.amber: Light.red}\n"
                                   "var l = Light.red\nvar i = 0\nwhile i < 4\n    l = next[l]\n    print(l, \" \")\n    i += 1\nprintln(next)")),
        "green amber red green {Light.red: Light.green, Light.green: Light.amber, Light.amber: Light.red}\n");
}

TEST_CASE("enums: the mistakes are reported")
{
    CHECK_EQ(diagnostic_codes(with_light("println(Light.blue)")), Codes{"S062"});
    CHECK_EQ(diagnostic_codes(with_light("println(Light(true))")), Codes{"S063"});
    CHECK_EQ(diagnostic_codes(with_light("println(Light(1, 2))")), Codes{"S009"});
    CHECK_EQ(diagnostic_codes(with_light("var l: Light = 1")), Codes{"S005"});
    CHECK_EQ(diagnostic_codes(with_light("println(Light.red + 1)")), Codes{"S006"});
    CHECK_EQ(diagnostic_codes("func f()\n    enum E\n        a\n"), Codes{"S059"});
    CHECK_EQ(diagnostic_codes("enum E\n    a\n    a\n"), Codes{"S061"});
    CHECK_EQ(diagnostic_codes("struct Light\n    x: int\nenum Light\n    a\n"), Codes{"S028"});
    CHECK_EQ(run_output(with_light("println(Light(3))")), "ERROR R034: 3 is not a position in 'Light' (its members are numbered 0 to 2)");
    CHECK_EQ(run_output(with_light("println(Light(\"blue\"))")), "ERROR R035: \"blue\" is not the name of a member of 'Light'");
}

TEST_CASE("enums: the built-in Color and Platform enums")
{
    CHECK_EQ(diagnostic_codes("println(Color.red)"), Codes{"S001"}); // Color comes with "import graphics"
    CHECK_EQ(run_output("import graphics\nprintln(Color.red, \" \", Colour.blue, \" \", to_int(Color.black), \" \", Color.count, \" \", Color(\"grey\") == Color.gray)"),
        "red blue 4 13 true\n");
    // A drawing function takes a Color as well as a colour string.
    CHECK(diagnostic_codes("import graphics\ncanvas(10, 10)\nclear(Color.black)\nrect(1, 1, 2, 2, Color.red)\ntext(1, 1, \"hi\", Color.white)").empty());
    CHECK_EQ(diagnostic_codes("import graphics\ncanvas(10, 10)\ntext(1, 1, Color.white, \"hi\")"), Codes{"S010"}); // only where a colour is wanted
    CHECK_EQ(diagnostic_codes("import graphics\nprintln(to_upper(Color.red))"), Codes{"S010"});
    CHECK_EQ(run_output("import os\nvar p = platform()\nprintln(p == Platform.linux or p == Platform.windows or p == Platform.macos or p == Platform.unknown)"), "true\n");
    CHECK(diagnostic_codes("import os\nprintln(platform() == \"linux\")") == Codes{"S006"}); // no longer a string
    CHECK_EQ(run_output("import os\nprintln(contains([\"linux\", \"windows\", \"macos\", \"unknown\"], str(platform())))"), "true\n");
}

TEST_CASE("enums: in Pashto, with the Pashto colour names, and translated both ways")
{
    CHECK_EQ(run_output("# ژبه: ps\nګڼنه څراغ\n    سور\n    زرغون\nکرښه چاپ(څراغ.زرغون، \" \"، عدد کړه(څراغ.زرغون)، \" \"، څراغ(۰)، \" \"، څراغ.شمېر، \" \"، څراغ.ارزښتونه)"),
        "زرغون ۱ سور ۲ [څراغ.سور، څراغ.زرغون]\n");
    CHECK_EQ(run_output("# ژبه: ps\nراوړه ګرافیک\nکرښه چاپ(رنګونه.شین، \" \"، رنګونه.سپین، \" \"، رنګونه.تور، \" \"، عدد کړه(رنګونه.شین))"), "شین سپین تور ۲\n");
    const SourceFile source("c.th", "import graphics\ncanvas(10, 10)\nclear(Color.blue)\nenum Light\n    red\nprintln(Light.red)\n");
    const TranslationResult to_pashto = translate(source, english_pack(), pashto_pack());
    REQUIRE(to_pashto.ok);
    CHECK_CONTAINS(to_pashto.text, "ټاټ پاک کړه(رنګونه.شین)");
    CHECK_CONTAINS(to_pashto.text, "ګڼنه Light");
    const SourceFile pashto("c.th", to_pashto.text);
    const TranslationResult back = translate(pashto, english_pack(), english_pack());
    REQUIRE(back.ok);
    CHECK_CONTAINS(back.text, "clear(Color.blue)");
}

TEST_CASE("enums: the bytecode carries the enum table and the new instructions round-trip")
{
    const bytecode::Program program = compile_ok(with_light("var l = Light(1)\nprintln(l, Light.values, Light.count)\n"));
    REQUIRE(program.enums.size() == 1);
    CHECK_EQ(program.enums[0].name, "Light");
    CHECK_EQ(program.enums[0].member_names.size(), std::size_t{3});
    CHECK_EQ(program.enums[0].builtin, -1);
    const std::vector<std::uint8_t> bytes = bytecode::serialize(program);
    const auto loaded = bytecode::deserialize(bytes);
    REQUIRE(loaded.has_value());
    CHECK(loaded->enums == program.enums);
    CHECK(bytecode::serialize(*loaded) == bytes);
    CHECK(!bytecode::verify(*loaded).has_value());
    const std::string listing = bytecode::disassemble_text(program);
    CHECK_CONTAINS(listing, "ENUM_FROM");
    CHECK_CONTAINS(listing, "ENUM_VALUES");
    CHECK_CONTAINS(listing, "Light { red, amber, green }");
    const bytecode::Program colours = compile_ok("import graphics\nprintln(Color.red)\n");
    REQUIRE(colours.enums.size() == 1);
    CHECK_EQ(colours.enums[0].builtin, 0); // BuiltinEnumId::Color
    CHECK_CONTAINS(bytecode::disassemble_text(colours), "Color.red");
}
