#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/bytecode.hpp"

using namespace thunder;
using namespace thunder::bytecode;

namespace
{
    // A minimal valid program: <main> = HALT.
    Program halt_program()
    {
        Program program;
        program.source_name = "hand.th";
        Function main;
        main.name = "<main>";
        main.code = {static_cast<std::uint8_t>(Opcode::Halt)};
        program.functions.push_back(main);
        return program;
    }
}

TEST_CASE("bytecode: opcode table")
{
    CHECK_EQ(opcode_count(), 36u);
    CHECK_EQ(opcode_info(Opcode::JumpIfFalse).mnemonic, "JUMP_IF_FALSE");
    CHECK(opcode_info(Opcode::Jump).operand == OperandKind::Jump);
    CHECK_EQ(operand_size(OperandKind::Jump), 4u);
    CHECK_EQ(operand_size(OperandKind::Builtin), 2u);
    CHECK_EQ(operand_size(OperandKind::None), 0u);
    CHECK(opcode_from_byte(0) == Opcode::Halt);
    CHECK(!opcode_from_byte(200).has_value());
    for (std::size_t i = 0; i < opcode_count(); ++i)
    {
        const OpcodeInfo& op = opcode_info(static_cast<Opcode>(i));
        CHECK(static_cast<std::size_t>(op.opcode) == i);
        CHECK(!op.description.empty());
    }
}

TEST_CASE("bytecode: decode handles every operand layout and truncation")
{
    const std::vector<std::uint8_t> code{static_cast<std::uint8_t>(Opcode::Const), 0x34, 0x12,
        static_cast<std::uint8_t>(Opcode::Jump), 0x78, 0x56, 0x34, 0x12, static_cast<std::uint8_t>(Opcode::CallBuiltin),
        1, 2, static_cast<std::uint8_t>(Opcode::Add)};
    const auto constant = decode(code, 0);
    REQUIRE(constant.has_value());
    CHECK_EQ(constant->a, 0x1234u);
    CHECK_EQ(constant->size, 3u);
    const auto jump = decode(code, 3);
    REQUIRE(jump.has_value());
    CHECK_EQ(jump->a, 0x12345678u);
    const auto builtin = decode(code, 8);
    REQUIRE(builtin.has_value());
    CHECK_EQ(builtin->a, 1u);
    CHECK_EQ(builtin->b, 2u);
    CHECK(decode(code, 11).has_value());
    CHECK(!decode(code, 12).has_value()); // past the end

    const std::vector<std::uint8_t> truncated{static_cast<std::uint8_t>(Opcode::Jump), 1, 2};
    CHECK(!decode(truncated, 0).has_value());
    const std::vector<std::uint8_t> unknown{0xEE};
    CHECK(!decode(unknown, 0).has_value());
}

TEST_CASE("bytecode: serialize/deserialize round trip")
{
    const Program program = test::compile_ok(R"(
        var greeting = "hello\n";
        func square(x: int): int
        {
            return x * x;
        }
        println(greeting, square(-12), [true]);
    )");
    std::vector<Section> sections;
    const std::vector<std::uint8_t> bytes = serialize(program, &sections);
    CHECK(bytes.size() > 4);
    CHECK_EQ(std::string(bytes.begin(), bytes.begin() + 4), "THND");
    CHECK_EQ(sections.front().name, "header");
    CHECK_EQ(sections.back().offset + sections.back().size, static_cast<std::uint32_t>(bytes.size()));

    const auto loaded = deserialize(bytes);
    REQUIRE(loaded.has_value());
    CHECK_EQ(loaded->source_name, program.source_name);
    CHECK(loaded->constants == program.constants);
    CHECK(loaded->globals == program.globals);
    REQUIRE(loaded->functions.size() == program.functions.size());
    for (std::size_t i = 0; i < program.functions.size(); ++i)
    {
        CHECK_EQ(loaded->functions[i].name, program.functions[i].name);
        CHECK(loaded->functions[i].code == program.functions[i].code);
        CHECK(loaded->functions[i].lines == program.functions[i].lines);
        CHECK(loaded->functions[i].local_names == program.functions[i].local_names);
        CHECK_EQ(loaded->functions[i].returns_value, program.functions[i].returns_value);
    }
    CHECK(serialize(*loaded) == bytes);
}

TEST_CASE("bytecode: corrupt files are rejected, never trusted")
{
    const std::vector<std::uint8_t> bytes = serialize(test::compile_ok("var s = \"abc\"; println(s, 12345);"));

    CHECK_EQ(deserialize(std::vector<std::uint8_t>{}).error(), "not a Thunder bytecode file (bad magic number)");
    CHECK(!deserialize(std::vector<std::uint8_t>{'T', 'H', 'N', 'X', 1, 0}).has_value());

    std::vector<std::uint8_t> wrong_version = bytes;
    wrong_version[4] = 99;
    CHECK_CONTAINS(deserialize(wrong_version).error(), "unsupported bytecode version 99");

    // Every truncation of a valid file must be rejected cleanly.
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        const std::vector<std::uint8_t> prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
        CHECK(!deserialize(prefix).has_value());
    }

    std::vector<std::uint8_t> trailing = bytes;
    trailing.push_back(0);
    CHECK_CONTAINS(deserialize(trailing).error(), "unexpected data");

    // Flipping any single byte must never crash (it may still be a valid program).
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        std::vector<std::uint8_t> flipped = bytes;
        flipped[i] = static_cast<std::uint8_t>(flipped[i] ^ 0xFF);
        (void)deserialize(flipped);
    }
}

TEST_CASE("bytecode: unknown constant tag")
{
    Program program = halt_program();
    program.constants.emplace_back(std::int64_t{1});
    std::vector<std::uint8_t> bytes = serialize(program);
    // header: magic(4) version(2) name(4 + 7) language(4 + 2) then constant count(4) then the tag
    const std::size_t tag_offset = 4 + 2 + 4 + program.source_name.size() + 4 + program.language.size() + 4;
    REQUIRE(bytes[tag_offset] == 1);
    bytes[tag_offset] = 9;
    CHECK_CONTAINS(deserialize(bytes).error(), "unknown constant tag");
}

TEST_CASE("bytecode: the verifier rejects malformed programs")
{
    const auto op = [](Opcode o) { return static_cast<std::uint8_t>(o); };

    Program empty;
    CHECK_EQ(*verify(empty), "the program has no functions");

    Program bad_entry = halt_program();
    bad_entry.entry = 3;
    CHECK_CONTAINS(*verify(bad_entry), "entry function #3");

    Program entry_params = halt_program();
    entry_params.functions[0].arity = 1;
    entry_params.functions[0].local_count = 1;
    CHECK_CONTAINS(*verify(entry_params), "must not take parameters");

    Program arity = halt_program();
    Function f;
    f.name = "f";
    f.arity = 2;
    f.local_count = 1;
    f.code = {op(Opcode::ReturnVoid)};
    arity.functions.push_back(f);
    CHECK_CONTAINS(*verify(arity), "more parameters than local slots");

    Program empty_body = halt_program();
    empty_body.functions[0].code.clear();
    CHECK_CONTAINS(*verify(empty_body), "empty function body");

    const auto with_code = [&](std::vector<std::uint8_t> code)
    {
        Program program = halt_program();
        program.functions[0].code = std::move(code);
        return verify(program);
    };
    CHECK_CONTAINS(*with_code({0xEE}), "unknown opcode");
    CHECK_CONTAINS(*with_code({op(Opcode::Const), 5, 0, op(Opcode::Halt)}), "constant #5 does not exist");
    CHECK_CONTAINS(*with_code({op(Opcode::LoadLocal), 0, 0, op(Opcode::Halt)}), "local slot 0 does not exist");
    CHECK_CONTAINS(*with_code({op(Opcode::LoadGlobal), 0, 0, op(Opcode::Halt)}), "global slot 0 does not exist");
    CHECK_CONTAINS(*with_code({op(Opcode::Call), 7, 0, op(Opcode::Halt)}), "function #7 does not exist");
    CHECK_CONTAINS(*with_code({op(Opcode::CallBuiltin), 50, 0, op(Opcode::Halt)}), "built-in #50 does not exist");
    CHECK_CONTAINS(*with_code({op(Opcode::CallBuiltin), static_cast<std::uint8_t>(BuiltinId::Len), 3, op(Opcode::Halt)}),
        "wrong number of arguments");
    CHECK_CONTAINS(*with_code({op(Opcode::Jump), 2, 0, 0, 0, op(Opcode::Halt)}), "jump target 2");
    CHECK_CONTAINS(*with_code({op(Opcode::True), op(Opcode::Pop)}), "does not end with");
    CHECK(!with_code({op(Opcode::Jump), 0, 0, 0, 0}).has_value());
}

TEST_CASE("bytecode: disassembly")
{
    const Program program = test::compile_ok("var g = 1;\nfunc f(n: int): int\n{\n    return n + g;\n}\nprintln(f(2), \"s\");\n");
    const std::string listing = disassemble_text(program);
    CHECK_CONTAINS(listing, "; constants (");
    CHECK_CONTAINS(listing, "\"s\"");
    CHECK_CONTAINS(listing, "; globals (1)");
    CHECK_CONTAINS(listing, "function #1 f (params: 1, locals: 1, returns a value)");
    CHECK_CONTAINS(listing, "LOAD_LOCAL     0          ; n");
    CHECK_CONTAINS(listing, "LOAD_GLOBAL    0          ; g");
    CHECK_CONTAINS(listing, "CALL           1          ; f");
    CHECK_CONTAINS(listing, "CALL_BUILTIN   1 2        ; println");

    const auto lines = disassemble(program, program.functions[1]);
    REQUIRE(!lines.empty());
    CHECK_EQ(lines[0].mnemonic, "LOAD_LOCAL");
    CHECK_EQ(lines[0].bytes, "05 00 00");
    CHECK_EQ(lines[0].position.line, 4u);

    // Undecodable bytes are shown rather than skipped silently.
    Function broken;
    broken.code = {0xEE};
    const auto unknown = disassemble(program, broken);
    REQUIRE(unknown.size() == 1);
    CHECK_EQ(unknown[0].mnemonic, "???");

    Instruction jump;
    jump.op = Opcode::JumpIfFalse;
    jump.a = 12;
    CHECK_EQ(instruction_text(program, program.functions[0], jump), "JUMP_IF_FALSE 0012  ; -> 0012");
    Instruction out_of_range;
    out_of_range.op = Opcode::LoadGlobal;
    out_of_range.a = 99;
    CHECK_EQ(instruction_text(program, program.functions[0], out_of_range), "LOAD_GLOBAL 99");
}

TEST_CASE("bytecode: line table lookups")
{
    Function f;
    f.lines = {LineEntry{0, 1, 1}, LineEntry{5, 3, 7}};
    CHECK_EQ(f.position_at(0).line, 1u);
    CHECK_EQ(f.position_at(4).line, 1u);
    CHECK_EQ(f.position_at(5).line, 3u);
    CHECK_EQ(f.position_at(100).column, 7u);
    Function none;
    CHECK_EQ(none.position_at(0).line, 0u);
}

TEST_CASE("bytecode: constants display")
{
    CHECK_EQ(constant_to_display(Constant{std::int64_t{-5}}), "-5");
    CHECK_EQ(constant_to_display(Constant{std::string("a\"b\\\n\t\r", 7) + std::string(1, '\0')}),
        "\"a\\\"b\\\\\\n\\t\\r\\0\"");
}
