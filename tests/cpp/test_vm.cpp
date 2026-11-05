#include "helpers.hpp"
#include "minitest.hpp"

#include "thunder/report.hpp"
#include "thunder/value.hpp"
#include "thunder/vm.hpp"

#include <limits>

using namespace thunder;
using namespace thunder::bytecode;

namespace
{
    std::uint8_t op(Opcode o)
    {
        return static_cast<std::uint8_t>(o);
    }

    // Runs hand-written <main> code (as if produced by a broken or malicious compiler).
    ExecResult run_code(std::vector<std::uint8_t> code, std::vector<Constant> constants = {}, std::uint16_t locals = 0)
    {
        static Program program; // the VM keeps a reference while running
        program = Program{};
        program.constants = std::move(constants);
        Function main;
        main.name = "<main>";
        main.local_count = locals;
        main.local_names.assign(locals, "t");
        main.code = std::move(code);
        program.functions.push_back(main);
        std::ostringstream out;
        Vm vm(program, out);
        return vm.run();
    }

    std::string error_message(const ExecResult& result)
    {
        return result.error ? result.error->code + ": " + result.error->message : "no error";
    }
}

TEST_CASE("vm: refuses to run a program that fails verification")
{
    const ExecResult result = run_code({op(Opcode::True)});
    REQUIRE(!result.ok);
    CHECK_EQ(result.error->code, "R010");
    CHECK_CONTAINS(result.error->message, "invalid bytecode");
}

TEST_CASE("vm: operand type confusion is detected, not undefined behaviour")
{
    CHECK_CONTAINS(error_message(run_code({op(Opcode::True), op(Opcode::True), op(Opcode::Add), op(Opcode::Halt)})),
        "ADD expects an int operand");
    CHECK_CONTAINS(error_message(run_code({op(Opcode::Const), 0, 0, op(Opcode::Not), op(Opcode::Halt)}, {std::int64_t{1}})),
        "NOT expects a bool operand");
    CHECK_CONTAINS(error_message(run_code({op(Opcode::Const), 0, 0, op(Opcode::Const), 0, 0, op(Opcode::Concat),
                                     op(Opcode::Halt)},
                       {std::int64_t{1}})),
        "CONCAT expects two strings");
    CHECK_CONTAINS(error_message(run_code({op(Opcode::True), op(Opcode::Const), 0, 0, op(Opcode::Less), op(Opcode::Halt)},
                       {std::int64_t{1}})),
        "comparison expects");
    CHECK_CONTAINS(error_message(run_code({op(Opcode::True), op(Opcode::Const), 0, 0, op(Opcode::IndexGet),
                                     op(Opcode::Halt)},
                       {std::int64_t{0}})),
        "INDEX_GET expects an array or a string");
    CHECK_CONTAINS(error_message(run_code({op(Opcode::True), op(Opcode::Const), 0, 0, op(Opcode::True),
                                     op(Opcode::IndexSet), op(Opcode::Halt)},
                       {std::int64_t{0}})),
        "INDEX_SET expects an array");
}

TEST_CASE("vm: built-ins validate their operands")
{
    const auto builtin = [](BuiltinId id, std::uint8_t argc)
    {
        return std::vector<std::uint8_t>{op(Opcode::CallBuiltin), static_cast<std::uint8_t>(id), argc};
    };
    auto code = std::vector<std::uint8_t>{op(Opcode::True)};
    auto len = builtin(BuiltinId::Len, 1);
    code.insert(code.end(), len.begin(), len.end());
    code.push_back(op(Opcode::Halt));
    CHECK_CONTAINS(error_message(run_code(code)), "len expects");

    code = {op(Opcode::True)};
    auto to_int = builtin(BuiltinId::ToInt, 1);
    code.insert(code.end(), to_int.begin(), to_int.end());
    code.push_back(op(Opcode::Halt));
    CHECK_CONTAINS(error_message(run_code(code)), "to_int expects");

    code = {op(Opcode::True), op(Opcode::True)};
    auto array = builtin(BuiltinId::Array, 2);
    code.insert(code.end(), array.begin(), array.end());
    code.push_back(op(Opcode::Halt));
    CHECK_CONTAINS(error_message(run_code(code)), "array expects");

    code = builtin(BuiltinId::Str, 1);
    code.push_back(op(Opcode::Halt));
    CHECK_CONTAINS(error_message(run_code(code)), "not enough arguments");
}

TEST_CASE("vm: stack underflow and local protection")
{
    CHECK_CONTAINS(error_message(run_code({op(Opcode::Pop), op(Opcode::Halt)})), "operand stack underflow");
    // Locals live on the stack below the operands; they must not be popped as operands.
    CHECK_CONTAINS(error_message(run_code({op(Opcode::Pop), op(Opcode::Halt)}, {}, 2)), "operand stack underflow");
    CHECK_CONTAINS(error_message(run_code({op(Opcode::MakeArray), 3, 0, op(Opcode::Halt)})), "operand stack underflow");
}

TEST_CASE("vm: calls validate the arguments on the stack")
{
    static Program program;
    program = Program{};
    Function main;
    main.name = "<main>";
    main.code = {op(Opcode::Call), 1, 0, op(Opcode::Halt)};
    Function f;
    f.name = "f";
    f.arity = 2;
    f.local_count = 2;
    f.code = {op(Opcode::ReturnVoid)};
    program.functions = {main, f};
    std::ostringstream out;
    Vm vm(program, out);
    const ExecResult result = vm.run();
    CHECK_CONTAINS(error_message(result), "not enough arguments on the stack for CALL");
}

TEST_CASE("vm: running past the end of a function")
{
    // A JUMP at the end passes verification but its target may still be the last instruction:
    // jumping to itself is an infinite loop, so use the step limit.
    static Program program;
    program = Program{};
    Function main;
    main.name = "<main>";
    main.code = {op(Opcode::Jump), 0, 0, 0, 0};
    program.functions.push_back(main);
    std::ostringstream out;
    VmOptions options;
    options.max_steps = 50;
    Vm vm(program, out, options);
    CHECK_EQ(vm.run().error->code, "R009");
}

TEST_CASE("vm: the entry function may return instead of halting")
{
    const ExecResult result = run_code({op(Opcode::ReturnVoid)});
    CHECK(result.ok);
    const ExecResult with_value = run_code({op(Opcode::True), op(Opcode::Return)});
    CHECK(with_value.ok);
}

TEST_CASE("vm: operand stack limit")
{
    static Program program;
    program = test::compile_ok("func f(n: int): int { return f(n + 1) + 1; } println(f(0));");
    std::ostringstream out;
    VmOptions options;
    options.max_stack = 100;
    options.max_call_depth = 1'000'000;
    Vm vm(program, out, options);
    CHECK_EQ(vm.run().error->code, "R008");
}

TEST_CASE("vm: checked arithmetic helpers")
{
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    CHECK(checked_add(max, 0) == max);
    CHECK(!checked_add(max, 1).has_value());
    CHECK(!checked_add(min, -1).has_value());
    CHECK(checked_sub(min, 0) == min);
    CHECK(!checked_sub(min, 1).has_value());
    CHECK(!checked_sub(max, -1).has_value());
    CHECK(checked_mul(0, max) == 0);
    CHECK(checked_mul(-1, max) == -max);
    CHECK(!checked_mul(-1, min).has_value());
    CHECK(!checked_mul(min, -1).has_value());
    CHECK(!checked_mul(max, 2).has_value());
    CHECK(!checked_mul(2, min).has_value());
    CHECK(!checked_mul(min, 2).has_value());
    CHECK(checked_mul(-3, -4) == 12);
    CHECK(!checked_mul(-3, min).has_value());
}

TEST_CASE("vm: parse_int")
{
    CHECK(parse_int("0") == 0);
    CHECK(parse_int("-۴۲") == -42); // Pashto digits
    CHECK(parse_int("٣") == 3);
    CHECK(parse_int("-9223372036854775808") == std::numeric_limits<std::int64_t>::min());
    CHECK(parse_int("9223372036854775807") == std::numeric_limits<std::int64_t>::max());
    CHECK(!parse_int("9223372036854775808").has_value());
    CHECK(!parse_int("-9223372036854775809").has_value());
    CHECK(parse_int(" \t12\r\n") == 12);
    CHECK(!parse_int("-").has_value());
    CHECK(!parse_int("1 2").has_value());
    CHECK(!parse_int("").has_value());
}

TEST_CASE("values: display, debug form, equality and deep copies")
{
    const Value nested = Value::from_array({Value::from_string("a\"b"), Value::from_array({Value::from_int(1)})});
    CHECK_EQ(nested.to_display(), "[\"a\\\"b\", [1]]");
    CHECK_EQ(Value::from_string("x\n").to_display(), "x\n");
    CHECK_EQ(Value::from_string("x\n\t\r\\").to_debug(), "\"x\\n\\t\\r\\\\\"");
    CHECK_EQ(Value::from_string(std::string(1, '\0')).to_debug(), "\"\\0\"");
    CHECK_EQ(Value{}.to_display(), "void");
    CHECK_EQ(Value::from_bool(false).to_debug(), "false");

    CHECK(Value::from_int(3) == Value::from_int(3));
    CHECK(!(Value::from_int(3) == Value::from_bool(true)));
    CHECK(Value::from_string("s") == Value::from_string("s"));
    CHECK(Value::from_bool(true) == Value::from_bool(true));
    CHECK(Value{} == Value{});
    CHECK(nested == nested);
    CHECK(!(nested == nested.deep_copy())); // arrays compare by identity
    const Value copy = nested.deep_copy();
    copy.as_array().elements[1].as_array().elements[0] = Value::from_int(9);
    CHECK_EQ(nested.to_display(), "[\"a\\\"b\", [1]]");
    CHECK_EQ(Value::from_int(5).deep_copy().as_int(), 5);

    CHECK_EQ(value_kind_name(Value::Kind::Array), "array");
    CHECK_EQ(value_kind_name(Value::Kind::Void), "void");
    CHECK_EQ(value_kind_name(Value::Kind::Int), "int");
    CHECK_EQ(value_kind_name(Value::Kind::Bool), "bool");
    CHECK_EQ(value_kind_name(Value::Kind::String), "string");
}

namespace
{
    class CountingObserver final : public VmObserver
    {
    public:
        void before_instruction(const Vm& vm, const Instruction& instruction) override
        {
            ++instructions;
            last_mnemonic = std::string(opcode_info(instruction.op).mnemonic);
            CHECK(!vm.frames().empty());
        }
        void on_output(std::string_view text) override { output += text; }

        int instructions = 0;
        std::string output;
        std::string last_mnemonic;
    };
}

TEST_CASE("vm: observers see every instruction and all output")
{
    const Program program = test::compile_ok("print(\"a\"); println(1 + 2);");
    std::ostringstream out;
    Vm vm(program, out);
    CountingObserver observer;
    const ExecResult result = vm.run(&observer);
    CHECK(result.ok);
    CHECK_EQ(observer.output, "a3\n");
    CHECK_EQ(out.str(), "a3\n");
    CHECK_EQ(static_cast<std::uint64_t>(observer.instructions), result.stats.steps);
    CHECK_EQ(observer.last_mnemonic, "HALT");
}

TEST_CASE("vm: text tracer prints instructions and operand stacks")
{
    const Program program = test::compile_ok("var x = 2 * 3;");
    std::ostringstream out;
    std::ostringstream trace;
    TextTracer tracer(trace);
    Vm vm(program, out);
    vm.run(&tracer);
    tracer.on_output("ignored");
    CHECK_CONTAINS(trace.str(), "<main>+0000");
    CHECK_CONTAINS(trace.str(), "MUL");
    CHECK_CONTAINS(trace.str(), "stack: [2, 3]");
}
