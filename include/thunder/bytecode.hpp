// Bytecode: the instruction set of the Thunder virtual machine and the .thc file format.
//
// The VM is a stack machine. Each instruction is a one-byte opcode followed by zero or more
// little-endian operands. See docs/bytecode.md for the full reference.
#pragma once

#include "thunder/source.hpp"
#include "thunder/value.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace thunder::bytecode
{
    // How the bytes after an opcode are interpreted.
    enum class OperandKind : std::uint8_t
    {
        None,     // no operand
        Constant, // u16 index into the constant pool
        Local,    // u16 local slot in the current call frame
        Global,   // u16 global slot
        Jump,     // u32 absolute code offset within the current function
        Function, // u16 index into the function table
        Builtin,  // u8 built-in id, u8 argument count
        Count,    // u16 element count
        Struct,   // u16 index into the struct table
        Field,    // u16 field index within the struct on the stack
        Element,  // u16 element index within the tuple on the stack
        Native,   // u16 index into the native function table
    };

    // X(Enum, MNEMONIC, OperandKind, stack effect, description)
#define THUNDER_OPCODES(X)                                                                                         \
    X(Halt, "HALT", None, "->", "Stop the program.")                                                           \
    X(Const, "CONST", Constant, "-> value", "Push a constant from the constant pool.")                         \
    X(True, "TRUE", None, "-> true", "Push the boolean true.")                                                 \
    X(False, "FALSE", None, "-> false", "Push the boolean false.")                                             \
    X(Pop, "POP", None, "value ->", "Discard the value on top of the stack.")                                  \
    X(LoadLocal, "LOAD_LOCAL", Local, "-> value", "Push the value of a local variable.")                       \
    X(StoreLocal, "STORE_LOCAL", Local, "value ->", "Pop a value into a local variable.")                      \
    X(LoadGlobal, "LOAD_GLOBAL", Global, "-> value", "Push the value of a global variable.")                   \
    X(StoreGlobal, "STORE_GLOBAL", Global, "value ->", "Pop a value into a global variable.")                  \
    X(Add, "ADD", None, "a b -> a+b", "Add two numbers (runtime error on overflow).")                          \
    X(Sub, "SUB", None, "a b -> a-b", "Subtract two numbers (runtime error on overflow).")                     \
    X(Mul, "MUL", None, "a b -> a*b", "Multiply two numbers (runtime error on overflow).")                     \
    X(Div, "DIV", None, "a b -> a/b", "Divide two numbers (integers round toward zero; error if b is 0).")    \
    X(Mod, "MOD", None, "a b -> a%b", "Remainder of integer division (runtime error if b is 0).")             \
    X(Neg, "NEG", None, "a -> -a", "Negate a number.")                                                         \
    X(Not, "NOT", None, "a -> !a", "Logical NOT of a boolean.")                                                \
    X(Concat, "CONCAT", None, "a b -> ab", "Join two strings.")                                                \
    X(Equal, "EQ", None, "a b -> a==b", "Compare two values for equality.")                                    \
    X(NotEqual, "NE", None, "a b -> a!=b", "Compare two values for inequality.")                               \
    X(Less, "LT", None, "a b -> a<b", "Less than (numbers or strings).")                                      \
    X(LessEqual, "LE", None, "a b -> a<=b", "Less than or equal (numbers or strings).")                       \
    X(Greater, "GT", None, "a b -> a>b", "Greater than (numbers or strings).")                                \
    X(GreaterEqual, "GE", None, "a b -> a>=b", "Greater than or equal (numbers or strings).")                 \
    X(Jump, "JUMP", Jump, "->", "Continue execution at the target address.")                                   \
    X(JumpIfFalse, "JUMP_IF_FALSE", Jump, "cond ->", "Pop a boolean; jump to the target if it is false.")       \
    X(Call, "CALL", Function, "args -> result?", "Call a function; its arguments become its first locals.")   \
    X(CallBuiltin, "CALL_BUILTIN", Builtin, "args -> result?", "Call a built-in function.")                    \
    X(Return, "RETURN", None, "value ->", "Return the value on top of the stack to the caller.")               \
    X(ReturnVoid, "RETURN_VOID", None, "->", "Return to the caller without a value.")                          \
    X(MakeArray, "MAKE_ARRAY", Count, "v1..vn -> array", "Pop n values and push an array containing them.")    \
    X(IndexGet, "INDEX_GET", None, "a i -> a[i]", "Read an array element or a string character.")              \
    X(IndexSet, "INDEX_SET", None, "a i v ->", "Write an array element.")                                        \
    X(Dup, "DUP", None, "v -> v v", "Duplicate the value on top of the stack.")                                \
    X(Dup2, "DUP2", None, "a b -> a b a b", "Duplicate the two values on top of the stack (used by a[i] += v).") \
    X(IntToFloat, "INT_TO_FLOAT", None, "i -> f", "Convert an integer to a decimal number.")                \
    X(NewStruct, "NEW_STRUCT", Struct, "f1..fn -> s", "Pop the field values (in declaration order) and push a new struct.") \
    X(GetField, "GET_FIELD", Field, "s -> s.f", "Replace a struct by the value of one of its fields.")          \
    X(SetField, "SET_FIELD", Field, "s v ->", "Store a value in a field of a struct.")                            \
    X(MakeTuple, "MAKE_TUPLE", Count, "v1..vn -> tuple", "Pop n values and push a tuple containing them.")        \
    X(TupleGet, "TUPLE_GET", Element, "t -> t.i", "Replace a tuple by one of its elements.")                      \
    X(MakeMap, "MAKE_MAP", Count, "k1 v1..kn vn -> map", "Pop n key/value pairs and push a map containing them.") \
    X(FuncRef, "FUNC_REF", Function, "-> f", "Push a function value: a reference to a function, which can be stored, passed and called.") \
    X(CallValue, "CALL_VALUE", Count, "args f -> result?", "Pop a function value and call it with the n arguments below it.") \
    X(CallNative, "CALL_NATIVE", Native, "args -> result?", "Call a function in a native library (import native).")

    enum class Opcode : std::uint8_t
    {
#define THUNDER_OPCODE_ENUM(name, mnemonic, operand, effect, description) name,
        THUNDER_OPCODES(THUNDER_OPCODE_ENUM)
#undef THUNDER_OPCODE_ENUM
    };

    struct OpcodeInfo
    {
        Opcode opcode;
        std::string_view mnemonic;
        OperandKind operand;
        std::string_view stack_effect;
        std::string_view description;
    };

    // FUNC_REF with this operand pushes an unset function value (the default value of a
    // variable of function type); calling it is runtime error R020.
    inline constexpr std::uint16_t no_function = 0xFFFF;

    std::size_t opcode_count();
    std::optional<Opcode> opcode_from_byte(std::uint8_t byte);
    const OpcodeInfo& opcode_info(Opcode op);
    std::uint32_t operand_size(OperandKind kind);

    // ------------------------------------------------------------------ program representation

    using Constant = std::variant<std::int64_t, double, std::string>;

    // Maps the instructions starting at 'offset' (up to the next entry) to a source position.
    struct LineEntry
    {
        std::uint32_t offset = 0;
        std::uint32_t line = 0;
        std::uint32_t column = 0;
        std::uint16_t file = 0; // index into Program::source_files (0 = the program's own file)

        friend bool operator==(const LineEntry&, const LineEntry&) = default;
    };

    struct Function
    {
        std::string name;
        std::uint16_t arity = 0;
        std::uint16_t local_count = 0; // including the parameters
        bool returns_value = false;
        std::vector<std::string> local_names; // debug information, one per slot
        std::vector<std::uint8_t> code;
        std::vector<LineEntry> lines;

        // Source position of the instruction at 'offset' (line 0 if unknown).
        Position position_at(std::uint32_t offset) const;
        // The source file (an index into Program::source_files) of the instruction at 'offset'.
        std::uint16_t file_at(std::uint32_t offset) const;
    };

    // The most parameters a native function may have (keeps the call buffer on the stack small).
    inline constexpr std::uint16_t max_native_arity = 16;

    // A function a native library provides (see include/thunder/native.h and docs/native.md).
    struct NativeFunction
    {
        std::string name;              // the symbol the library registered
        std::uint16_t arity = 0;
        bool returns_value = false;
        // The kind of each parameter and of the result, as ThunderKind values from native.h
        // (1 int, 2 float, 3 bool, 4 string, 5 function; 0 void for a result).
        std::vector<std::uint8_t> parameter_kinds;
        std::uint8_t result_kind = 0;

        friend bool operator==(const NativeFunction&, const NativeFunction&) = default;
    };

    struct Program
    {
        std::string source_name;
        std::string language = "en"; // language pack of the source; decides how values and errors are shown
        // The files the code came from: [0] is the program itself, then the modules it imports.
        // Empty means just 'source_name'.
        std::vector<std::string> source_files;
        std::vector<Constant> constants;
        std::vector<StructLayout> structs;
        std::vector<std::string> globals; // names, one per global slot
        // Native libraries the program imports, in import order, and the functions it declared.
        std::vector<std::string> native_libraries;
        std::vector<NativeFunction> natives;
        std::vector<Function> functions;
        std::uint16_t entry = 0; // function executed first ("<main>")

        // The name of a source file by index (see LineEntry::file).
        const std::string& file_name(std::uint16_t file) const
        {
            return file < source_files.size() ? source_files[file] : source_name;
        }
    };

    std::string constant_to_display(const Constant& constant); // 42 or "text" (quoted, escaped)

    // ------------------------------------------------------------------ instructions

    struct Instruction
    {
        Opcode op = Opcode::Halt;
        std::uint32_t offset = 0;
        std::uint32_t size = 1;
        std::uint32_t a = 0; // first operand
        std::uint32_t b = 0; // second operand (CALL_BUILTIN argument count)
    };

    // Decodes the instruction at 'offset'. Returns nullopt for an unknown opcode or a truncated operand.
    std::optional<Instruction> decode(std::span<const std::uint8_t> code, std::uint32_t offset);

    struct DisassembledInstruction
    {
        std::uint32_t offset = 0;
        std::string bytes;    // hex, e.g. "01 03 00"
        std::string mnemonic; // e.g. "CONST"
        std::string operands; // e.g. "3"
        std::string comment;  // e.g. "42" or "x" or "-> 0012"
        Position position;
    };

    std::vector<DisassembledInstruction> disassemble(const Program& program, const Function& function);

    // One instruction formatted for display, e.g. "LOAD_LOCAL 0  ; n".
    std::string instruction_text(const Program& program, const Function& function, const Instruction& instruction);

    // A textual listing of the whole program (constants, globals and each function).
    std::string disassemble_text(const Program& program);

    // ------------------------------------------------------------------ the .thc file format

    inline constexpr std::string_view file_magic = "THND";
    inline constexpr std::uint16_t file_version = 6;

    // A named region of a serialised file (used to annotate hex dumps).
    struct Section
    {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    std::vector<std::uint8_t> serialize(const Program& program, std::vector<Section>* sections = nullptr);

    // Parses and verifies a .thc file. Never trusts its input: any inconsistency is reported as an error.
    std::expected<Program, std::string> deserialize(std::span<const std::uint8_t> bytes);

    // Checks the structural integrity of a program (valid opcodes, operands in range, jump targets
    // on instruction boundaries, every function ending in a terminator). Returns an error message.
    std::optional<std::string> verify(const Program& program);
}
