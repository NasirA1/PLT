// The byte type: one byte, 0..255, the element of a byte[] (raw data). A byte widens to an int
// wherever an int is wanted, a whole-number literal from 0 to 255 written where a byte is
// wanted is that byte, and nothing narrows by itself (to_byte does, keeping the low 8 bits).
#include "helpers.hpp"
#include "minitest.hpp"

#include <filesystem>
#include <fstream>

using test::diagnostic_codes;
using test::run_output;
using Codes = std::vector<std::string>;

TEST_CASE("bytes: a byte is a number from 0 to 255 that widens to an int")
{
    CHECK_EQ(run_output("var b: byte = 200\nprintln(b, \" \", b + 1, \" \", b * 2, \" \", -b, \" \", ~b)"), "200 201 400 -200 -201\n");
    CHECK_EQ(run_output("var b: byte = 200\nvar i: int = b\nvar f: float = b\nprintln(i, \" \", f, \" \", b / 2.0)"), "200 200.0 100.0\n");
    CHECK_EQ(run_output("var b: byte = 7\nprintln(b == 7, \" \", b < 10, \" \", b & 0b11, \" \", b << 1, \" \", b % 4)"), "true true 3 14 3\n");
    CHECK_EQ(run_output("var a: byte = 3\nvar b: byte = 250\nprintln(a + b, \" \", max(a, b), \" \", min(a, 2), \" \", abs(-a))"), "253 250 2 3\n");
    CHECK_EQ(run_output("var b: byte = 171\nprintln(hex(b), \" \", bin(b), \" \", format(\"{x} {b} {}\", b, b, b), \" \", str(b), \" \", to_int(b) + 1)"),
        "0xab 0b10101011 0xab 0b10101011 171 171 172\n");
    CHECK_EQ(run_output("var b: byte\nprintln(b, \" \", [b])"), "0 [0]\n");
}

TEST_CASE("bytes: to_byte keeps the low 8 bits; literals narrow, other ints do not")
{
    CHECK_EQ(run_output("println(to_byte(255), \" \", to_byte(256), \" \", to_byte(-1), \" \", to_byte(0x1234))"), "255 0 255 52\n");
    CHECK_EQ(run_output("var data: byte[] = [1, 2, 255]\ndata[0] = 7\npush(data, 9)\nprintln(data, \" \", len(data), \" \", sum(data), \" \", sort(data))"),
        "[7, 2, 255, 9] 4 273 [2, 7, 9, 255]\n");
    // 256 is not a byte, and an int variable is not narrowed by itself.
    CHECK_EQ(diagnostic_codes("var b: byte = 256"), Codes{"S005"});
    CHECK_EQ(diagnostic_codes("var n = 5\nvar b: byte = n"), Codes{"S005"});
    CHECK_EQ(diagnostic_codes("var n = 5\nvar b: byte = to_byte(n)\nvar data: byte[] = [n]"), Codes{"S005"}); // the array element too
    CHECK_EQ(diagnostic_codes("var data: byte[] = [1, 2]\ndata[0] = data[1] + 1"), Codes{"S005"}); // byte + int is an int
    CHECK(diagnostic_codes("var data: byte[] = [1, 2]\ndata[0] = to_byte(data[1] + 1)\nvar b: byte = 1\nvar i = 0\ni += b").empty());
}

TEST_CASE("bytes: byte arrays work with every array built-in and as map keys")
{
    CHECK_EQ(run_output("var data = array(3, to_byte(0))\ndata[2] = 200\nprintln(data, \" \", contains(data, to_byte(200)), \" \", index_of(data, to_byte(200)), \" \", reverse(data), \" \", slice(data, 1, 3))"),
        "[0, 0, 200] true 2 [200, 0, 0] [0, 200]\n");
    CHECK_EQ(run_output("var data: byte[] = [10, 20]\nfor b in data\n    print(b * 2, \" \")\nprintln(len(copy(data)), \" \", concat(data, [30]))"), "20 40 2 [10, 20, 30]\n");
    CHECK_EQ(run_output("var counts: {byte: int} = {}\nvar k: byte = 65\ncounts[k] = 1\ncounts[k] = counts[k] + 1\nprintln(counts, \" \", has(counts, to_byte(65)))"), "{65: 2} true\n");
}

TEST_CASE("bytes: text and bytes convert through UTF-8")
{
    CHECK_EQ(run_output("println(string_to_bytes(\"Az\"), \" \", string_to_bytes(\"ژ\"))"), "[65, 122] [218, 152]\n");
    CHECK_EQ(run_output("println(bytes_to_string(string_to_bytes(\"سلام, world\")))"), "سلام, world\n");
    // A byte that is not UTF-8 becomes the replacement character, and the result is a well-formed string.
    CHECK_EQ(run_output("var bad: byte[] = [0xff, 65]\nvar text = bytes_to_string(bad)\nprintln(len(text), \" \", char_code(text[0]), \" \", text[1])"), "2 65533 A\n");
}

TEST_CASE("bytes: whole numbers are read from and written to bytes in either byte order")
{
    const char* program = R"(
var le = int_to_bytes(0x1234, 2, true)
var be = int_to_bytes(0x1234, 2, false)
println(le, " ", be)
println(bytes_to_int(le, 0, 2, true, false), " ", bytes_to_int(be, 0, 2, false, false), " ", bytes_to_int(le, 0, 2, false, false))
var minus = int_to_bytes(-2, 2, true)
println(minus, " ", bytes_to_int(minus, 0, 2, true, true), " ", bytes_to_int(minus, 0, 2, true, false))
var header = concat(string_to_bytes("RIFF"), int_to_bytes(44100, 4, true))
println(bytes_to_string(slice(header, 0, 4)), " ", bytes_to_int(header, 4, 4, true, false))
println(bytes_to_int(int_to_bytes(-1, 8, true), 0, 8, true, true), " ", int_to_bytes(255, 1, true))
)";
    CHECK_EQ(run_output(program),
        "[52, 18] [18, 52]\n4660 4660 13330\n[254, 255] -2 65534\nRIFF 44100\n-1 [255]\n");
    CHECK_EQ(run_output("println(int_to_bytes(1, 9, true))"), "ERROR R022: the count given to 'int_to_bytes' must be from 1 to 8 (got 9)");
    CHECK_EQ(run_output("println(bytes_to_int([to_byte(1)], 0, 2, true, false))"), "ERROR R012: range 0..2 is out of bounds for a length of 1");
}

TEST_CASE("bytes: read_bytes and write_bytes keep every byte, carriage returns included")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "thunder_test_bytes.bin";
    const std::string file = path.generic_string();
    const std::string program = "import os\nvar data: byte[] = [0, 13, 10, 255, 128]\nwrite_bytes(\"" + file + "\", data)\n"
        "append_bytes(\"" + file + "\", [to_byte(7)])\nvar back = read_bytes(\"" + file + "\")\nprintln(back, \" \", len(back))\n"
        "println(len(read_file(\"" + file + "\")))\nremove_file(\"" + file + "\")\n";
    // read_file drops the carriage return (13) and reads 255/128 as one replacement character
    // of three bytes... but counts characters: 0, \n, U+FFFD, U+FFFD, 7 = 5.
    CHECK_EQ(run_output(program), "[0, 13, 10, 255, 128, 7] 6\n5\n");
    CHECK(!std::filesystem::exists(path));
}

TEST_CASE("bytes: a byte[] may cross the native boundary")
{
    CHECK(diagnostic_codes("import native \"demo\"\nnative func checksum(data: byte[]): byte[]\n").empty());
    CHECK_EQ(diagnostic_codes("import native \"demo\"\nnative func f(data: byte): int\n"), Codes{"S053"}); // a lone byte does not
}

TEST_CASE("bytes: the Pashto spellings")
{
    const std::string program = "# ژبه: ps\nمتغیر ب: بایټ = ۲۰۰\nمتغیر ټوټې = متن بایټونه کړه(\"A\")\n"
        "کرښه چاپ(ب، \" \"، ټوټې، \" \"، بایټ کړه(۲۵۶)، \" \"، بایټونه متن کړه(ټوټې)، \" \"، عدد بایټونه کړه(۱، ۲، رښتیا))\n";
    CHECK_EQ(run_output(program), "۲۰۰ [۶۵] ۰ A [۱، ۰]\n");
}
