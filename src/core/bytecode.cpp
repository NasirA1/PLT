#include "thunder/bytecode.hpp"

#include "thunder/builtins.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <set>

namespace thunder::bytecode
{
    namespace
    {
        constexpr std::array opcode_table{
#define THUNDER_OPCODE_INFO(name, mnemonic, operand, effect, description) \
    OpcodeInfo{Opcode::name, mnemonic, OperandKind::operand, effect, description},
            THUNDER_OPCODES(THUNDER_OPCODE_INFO)
#undef THUNDER_OPCODE_INFO
        };

        std::string escape_text(std::string_view text)
        {
            std::string out;
            for (const char c : text)
            {
                switch (c)
                {
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\0':
                    out += "\\0";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                default:
                    out += c;
                    break;
                }
            }
            return out;
        }
    }

    std::size_t opcode_count()
    {
        return opcode_table.size();
    }

    std::optional<Opcode> opcode_from_byte(std::uint8_t byte)
    {
        if (byte >= opcode_table.size())
        {
            return std::nullopt;
        }
        return static_cast<Opcode>(byte);
    }

    const OpcodeInfo& opcode_info(Opcode op)
    {
        return opcode_table.at(static_cast<std::size_t>(op));
    }

    std::uint32_t operand_size(OperandKind kind)
    {
        switch (kind)
        {
        case OperandKind::None:
            return 0;
        case OperandKind::Constant:
        case OperandKind::Local:
        case OperandKind::Global:
        case OperandKind::Function:
        case OperandKind::Count:
        case OperandKind::Builtin:
        case OperandKind::Struct:
        case OperandKind::Field:
            return 2;
        case OperandKind::Jump:
            return 4;
        }
        return 0;
    }

    Position Function::position_at(std::uint32_t offset) const
    {
        Position position{0, 0};
        for (const LineEntry& entry : lines)
        {
            if (entry.offset > offset)
            {
                break;
            }
            position = Position{entry.line, entry.column};
        }
        return position;
    }

    std::string constant_to_display(const Constant& constant)
    {
        if (const auto* number = std::get_if<std::int64_t>(&constant))
        {
            return std::to_string(*number);
        }
        return "\"" + escape_text(std::get<std::string>(constant)) + "\"";
    }

    // ------------------------------------------------------------------ instructions

    std::optional<Instruction> decode(std::span<const std::uint8_t> code, std::uint32_t offset)
    {
        if (offset >= code.size())
        {
            return std::nullopt;
        }
        const auto op = opcode_from_byte(code[offset]);
        if (!op)
        {
            return std::nullopt;
        }

        Instruction instruction;
        instruction.op = *op;
        instruction.offset = offset;
        const OperandKind kind = opcode_info(*op).operand;
        const std::uint32_t operand_bytes = operand_size(kind);
        instruction.size = 1 + operand_bytes;
        if (static_cast<std::size_t>(offset) + instruction.size > code.size())
        {
            return std::nullopt;
        }

        const auto byte = [&](std::uint32_t i) { return static_cast<std::uint32_t>(code[offset + 1 + i]); };
        switch (kind)
        {
        case OperandKind::None:
            break;
        case OperandKind::Builtin:
            instruction.a = byte(0);
            instruction.b = byte(1);
            break;
        case OperandKind::Jump:
            instruction.a = byte(0) | (byte(1) << 8) | (byte(2) << 16) | (byte(3) << 24);
            break;
        default:
            instruction.a = byte(0) | (byte(1) << 8);
            break;
        }
        return instruction;
    }

    namespace
    {
        std::string operand_comment(const Program& program, const Function& function, const Instruction& instruction)
        {
            switch (opcode_info(instruction.op).operand)
            {
            case OperandKind::Constant:
                if (instruction.a < program.constants.size())
                {
                    return constant_to_display(program.constants[instruction.a]);
                }
                break;
            case OperandKind::Local:
                if (instruction.a < function.local_names.size())
                {
                    return function.local_names[instruction.a];
                }
                break;
            case OperandKind::Global:
                if (instruction.a < program.globals.size())
                {
                    return program.globals[instruction.a];
                }
                break;
            case OperandKind::Function:
                if (instruction.a < program.functions.size())
                {
                    return program.functions[instruction.a].name;
                }
                break;
            case OperandKind::Builtin:
                if (const auto id = builtin_from_index(static_cast<std::uint8_t>(instruction.a)))
                {
                    return std::string(builtin_info(*id).canonical_name);
                }
                break;
            case OperandKind::Jump:
                return std::format("-> {:04}", instruction.a);
            case OperandKind::Struct:
                if (instruction.a < program.structs.size())
                {
                    return program.structs[instruction.a].name;
                }
                break;
            case OperandKind::Field:
                return std::format("field #{}", instruction.a);
            case OperandKind::Count:
            case OperandKind::None:
                break;
            }
            return {};
        }

        std::string operand_text(const Instruction& instruction)
        {
            switch (opcode_info(instruction.op).operand)
            {
            case OperandKind::None:
                return {};
            case OperandKind::Builtin:
                return std::format("{} {}", instruction.a, instruction.b);
            case OperandKind::Jump:
                return std::format("{:04}", instruction.a);
            default:
                return std::to_string(instruction.a);
            }
        }
    }

    std::vector<DisassembledInstruction> disassemble(const Program& program, const Function& function)
    {
        std::vector<DisassembledInstruction> result;
        std::uint32_t offset = 0;
        while (offset < function.code.size())
        {
            DisassembledInstruction line;
            line.offset = offset;
            line.position = function.position_at(offset);
            const auto instruction = decode(function.code, offset);
            if (!instruction)
            {
                line.bytes = std::format("{:02x}", function.code[offset]);
                line.mnemonic = "???";
                result.push_back(std::move(line));
                ++offset;
                continue;
            }

            for (std::uint32_t i = 0; i < instruction->size; ++i)
            {
                if (i > 0)
                {
                    line.bytes += ' ';
                }
                line.bytes += std::format("{:02x}", function.code[offset + i]);
            }
            line.mnemonic = std::string(opcode_info(instruction->op).mnemonic);
            line.operands = operand_text(*instruction);
            line.comment = operand_comment(program, function, *instruction);
            result.push_back(std::move(line));
            offset += instruction->size;
        }
        return result;
    }

    std::string instruction_text(const Program& program, const Function& function, const Instruction& instruction)
    {
        std::string text(opcode_info(instruction.op).mnemonic);
        const std::string operands = operand_text(instruction);
        if (!operands.empty())
        {
            text += " " + operands;
        }
        const std::string comment = operand_comment(program, function, instruction);
        if (!comment.empty())
        {
            text += "  ; " + comment;
        }
        return text;
    }

    std::string disassemble_text(const Program& program)
    {
        std::string out;
        out += std::format("; constants ({})\n", program.constants.size());
        for (std::size_t i = 0; i < program.constants.size(); ++i)
        {
            out += std::format(";   #{:<3} {}\n", i, constant_to_display(program.constants[i]));
        }
        out += std::format("; structs ({})\n", program.structs.size());
        for (std::size_t i = 0; i < program.structs.size(); ++i)
        {
            const StructLayout& layout = program.structs[i];
            std::string fields;
            for (std::size_t f = 0; f < layout.field_names.size(); ++f)
            {
                fields += std::format("{}#{} {}: {}", f == 0 ? "" : ", ", f, layout.field_names[f], layout.field_types[f]);
            }
            out += std::format(";   ${} {} {{ {} }}\n", i, layout.name, fields);
        }
        out += std::format("; globals ({})\n", program.globals.size());
        for (std::size_t i = 0; i < program.globals.size(); ++i)
        {
            out += std::format(";   [{}] {}\n", i, program.globals[i]);
        }

        for (std::size_t f = 0; f < program.functions.size(); ++f)
        {
            const Function& function = program.functions[f];
            out += std::format("\nfunction #{} {} (params: {}, locals: {}{})\n", f, function.name, function.arity,
                function.local_count, function.returns_value ? ", returns a value" : "");
            std::uint32_t last_line = 0;
            for (const DisassembledInstruction& line : disassemble(program, function))
            {
                std::string source_line = "    ";
                if (line.position.line != last_line && line.position.line != 0)
                {
                    source_line = std::format("{:>4}", line.position.line);
                    last_line = line.position.line;
                }
                std::string text = std::format("  {}  {:04}  {:<14} {:<10}", source_line, line.offset, line.mnemonic,
                    line.operands);
                if (!line.comment.empty())
                {
                    text += " ; " + line.comment;
                }
                while (!text.empty() && text.back() == ' ')
                {
                    text.pop_back();
                }
                out += text + "\n";
            }
        }
        return out;
    }

    // ------------------------------------------------------------------ serialisation

    namespace
    {
        enum class ConstantTag : std::uint8_t
        {
            Int = 1,
            String = 2,
        };

        class Writer
        {
        public:
            void u8(std::uint8_t value) { bytes.push_back(value); }
            void u16(std::uint16_t value)
            {
                u8(static_cast<std::uint8_t>(value & 0xFF));
                u8(static_cast<std::uint8_t>(value >> 8));
            }
            void u32(std::uint32_t value)
            {
                for (int shift = 0; shift < 32; shift += 8)
                {
                    u8(static_cast<std::uint8_t>((value >> shift) & 0xFF));
                }
            }
            void i64(std::int64_t value)
            {
                const auto bits = static_cast<std::uint64_t>(value);
                for (int shift = 0; shift < 64; shift += 8)
                {
                    u8(static_cast<std::uint8_t>((bits >> shift) & 0xFF));
                }
            }
            void str(std::string_view text)
            {
                u32(static_cast<std::uint32_t>(text.size()));
                bytes.insert(bytes.end(), text.begin(), text.end());
            }
            void raw(std::span<const std::uint8_t> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }
            std::uint32_t size() const { return static_cast<std::uint32_t>(bytes.size()); }

            std::vector<std::uint8_t> bytes;
        };

        class Reader
        {
        public:
            explicit Reader(std::span<const std::uint8_t> data)
                : data_(data)
            {
            }

            bool ok() const { return ok_; }
            bool at_end() const { return pos_ == data_.size(); }

            std::uint8_t u8()
            {
                if (!need(1))
                {
                    return 0;
                }
                return data_[pos_++];
            }
            std::uint16_t u16()
            {
                const std::uint16_t lo = u8();
                const std::uint16_t hi = u8();
                return static_cast<std::uint16_t>(lo | (hi << 8));
            }
            std::uint32_t u32()
            {
                std::uint32_t value = 0;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    value |= static_cast<std::uint32_t>(u8()) << shift;
                }
                return value;
            }
            std::int64_t i64()
            {
                std::uint64_t value = 0;
                for (int shift = 0; shift < 64; shift += 8)
                {
                    value |= static_cast<std::uint64_t>(u8()) << shift;
                }
                return static_cast<std::int64_t>(value);
            }
            std::string str()
            {
                const std::uint32_t size = u32();
                if (!need(size))
                {
                    return {};
                }
                std::string text(reinterpret_cast<const char*>(data_.data() + pos_), size);
                pos_ += size;
                return text;
            }
            std::vector<std::uint8_t> raw(std::uint32_t size)
            {
                if (!need(size))
                {
                    return {};
                }
                std::vector<std::uint8_t> out(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                    data_.begin() + static_cast<std::ptrdiff_t>(pos_ + size));
                pos_ += size;
                return out;
            }
            // Guards element counts against absurd values in corrupt files.
            bool plausible_count(std::uint32_t count, std::uint32_t min_bytes_each)
            {
                if (static_cast<std::uint64_t>(count) * min_bytes_each > data_.size() - pos_)
                {
                    ok_ = false;
                }
                return ok_;
            }

        private:
            bool need(std::size_t count)
            {
                if (!ok_ || count > data_.size() - pos_)
                {
                    ok_ = false;
                    return false;
                }
                return true;
            }

            std::span<const std::uint8_t> data_;
            std::size_t pos_ = 0;
            bool ok_ = true;
        };
    }

    std::vector<std::uint8_t> serialize(const Program& program, std::vector<Section>* sections)
    {
        Writer w;
        std::uint32_t section_start = 0;
        const auto end_section = [&](std::string name)
        {
            if (sections != nullptr)
            {
                sections->push_back(Section{std::move(name), section_start, w.size() - section_start});
            }
            section_start = w.size();
        };

        w.raw(std::span(reinterpret_cast<const std::uint8_t*>(file_magic.data()), file_magic.size()));
        w.u16(file_version);
        w.str(program.source_name);
        w.str(program.language);
        end_section("header");

        w.u32(static_cast<std::uint32_t>(program.constants.size()));
        for (const Constant& constant : program.constants)
        {
            if (const auto* number = std::get_if<std::int64_t>(&constant))
            {
                w.u8(static_cast<std::uint8_t>(ConstantTag::Int));
                w.i64(*number);
            }
            else
            {
                w.u8(static_cast<std::uint8_t>(ConstantTag::String));
                w.str(std::get<std::string>(constant));
            }
        }
        end_section("constants");

        w.u16(static_cast<std::uint16_t>(program.structs.size()));
        for (const StructLayout& layout : program.structs)
        {
            w.str(layout.name);
            w.u16(static_cast<std::uint16_t>(layout.field_names.size()));
            for (std::size_t f = 0; f < layout.field_names.size(); ++f)
            {
                w.str(layout.field_names[f]);
                w.str(layout.field_types[f]);
            }
        }
        end_section("structs");

        w.u16(static_cast<std::uint16_t>(program.globals.size()));
        for (const std::string& name : program.globals)
        {
            w.str(name);
        }
        end_section("globals");

        w.u16(static_cast<std::uint16_t>(program.functions.size()));
        w.u16(program.entry);
        end_section("function table");

        for (const Function& function : program.functions)
        {
            w.str(function.name);
            w.u16(function.arity);
            w.u16(function.local_count);
            w.u8(function.returns_value ? 1 : 0);
            w.u16(static_cast<std::uint16_t>(function.local_names.size()));
            for (const std::string& name : function.local_names)
            {
                w.str(name);
            }
            w.u32(static_cast<std::uint32_t>(function.code.size()));
            end_section("function " + function.name + ": header");
            w.raw(function.code);
            end_section("function " + function.name + ": code");
            w.u32(static_cast<std::uint32_t>(function.lines.size()));
            for (const LineEntry& entry : function.lines)
            {
                w.u32(entry.offset);
                w.u32(entry.line);
                w.u32(entry.column);
            }
            end_section("function " + function.name + ": line table");
        }
        return std::move(w.bytes);
    }

    std::expected<Program, std::string> deserialize(std::span<const std::uint8_t> bytes)
    {
        Reader r(bytes);
        const std::vector<std::uint8_t> magic = r.raw(static_cast<std::uint32_t>(file_magic.size()));
        if (!r.ok() || std::string_view(reinterpret_cast<const char*>(magic.data()), magic.size()) != file_magic)
        {
            return std::unexpected("not a Thunder bytecode file (bad magic number)");
        }
        const std::uint16_t version = r.u16();
        if (r.ok() && version != file_version)
        {
            return std::unexpected(std::format("unsupported bytecode version {} (expected {})", version, file_version));
        }

        Program program;
        program.source_name = r.str();
        program.language = r.str();

        const std::uint32_t constant_count = r.u32();
        if (!r.plausible_count(constant_count, 2))
        {
            return std::unexpected("truncated file (constant pool)");
        }
        for (std::uint32_t i = 0; i < constant_count && r.ok(); ++i)
        {
            const std::uint8_t tag = r.u8(); // untrusted: compare before interpreting it
            if (tag == static_cast<std::uint8_t>(ConstantTag::Int))
            {
                program.constants.emplace_back(r.i64());
            }
            else if (tag == static_cast<std::uint8_t>(ConstantTag::String))
            {
                program.constants.emplace_back(r.str());
            }
            else if (r.ok())
            {
                return std::unexpected(std::format("unknown constant tag {}", tag));
            }
        }

        const std::uint16_t struct_count = r.u16();
        if (!r.plausible_count(struct_count, 6))
        {
            return std::unexpected("truncated file (structs)");
        }
        for (std::uint16_t i = 0; i < struct_count && r.ok(); ++i)
        {
            StructLayout layout;
            layout.name = r.str();
            const std::uint16_t field_count = r.u16();
            if (!r.plausible_count(field_count, 8))
            {
                break;
            }
            for (std::uint16_t f = 0; f < field_count && r.ok(); ++f)
            {
                layout.field_names.push_back(r.str());
                layout.field_types.push_back(r.str());
            }
            program.structs.push_back(std::move(layout));
        }

        const std::uint16_t global_count = r.u16();
        if (!r.plausible_count(global_count, 4))
        {
            return std::unexpected("truncated file (globals)");
        }
        for (std::uint16_t i = 0; i < global_count && r.ok(); ++i)
        {
            program.globals.push_back(r.str());
        }

        const std::uint16_t function_count = r.u16();
        program.entry = r.u16();
        if (!r.plausible_count(function_count, 17))
        {
            return std::unexpected("truncated file (function table)");
        }
        for (std::uint16_t f = 0; f < function_count && r.ok(); ++f)
        {
            Function function;
            function.name = r.str();
            function.arity = r.u16();
            function.local_count = r.u16();
            function.returns_value = r.u8() != 0;
            const std::uint16_t name_count = r.u16();
            if (!r.plausible_count(name_count, 4))
            {
                break;
            }
            for (std::uint16_t i = 0; i < name_count && r.ok(); ++i)
            {
                function.local_names.push_back(r.str());
            }
            function.code = r.raw(r.u32());
            const std::uint32_t line_count = r.u32();
            if (!r.plausible_count(line_count, 12))
            {
                break;
            }
            for (std::uint32_t i = 0; i < line_count && r.ok(); ++i)
            {
                LineEntry entry;
                entry.offset = r.u32();
                entry.line = r.u32();
                entry.column = r.u32();
                function.lines.push_back(entry);
            }
            program.functions.push_back(std::move(function));
        }

        if (!r.ok())
        {
            return std::unexpected("truncated or corrupt bytecode file");
        }
        if (!r.at_end())
        {
            return std::unexpected("unexpected data after the end of the program");
        }
        if (auto error = verify(program))
        {
            return std::unexpected(*error);
        }
        return program;
    }

    namespace
    {
        // Checks one instruction's operand against the program's tables (jumps are checked later,
        // once every instruction boundary is known).
        std::optional<std::string> check_operand(const Program& program, const Function& function,
            const Instruction& instruction)
        {
            const std::uint32_t a = instruction.a;
            switch (opcode_info(instruction.op).operand)
            {
            case OperandKind::Constant:
                if (a >= program.constants.size())
                {
                    return std::format("constant #{} does not exist", a);
                }
                break;
            case OperandKind::Local:
                if (a >= function.local_count)
                {
                    return std::format("local slot {} does not exist", a);
                }
                break;
            case OperandKind::Global:
                if (a >= program.globals.size())
                {
                    return std::format("global slot {} does not exist", a);
                }
                break;
            case OperandKind::Function:
                if (a >= program.functions.size())
                {
                    return std::format("function #{} does not exist", a);
                }
                break;
            case OperandKind::Builtin:
            {
                const auto id = builtin_from_index(static_cast<std::uint8_t>(a));
                if (!id)
                {
                    return std::format("built-in #{} does not exist", a);
                }
                const int arity = builtin_info(*id).arity;
                if (arity != variadic && static_cast<std::uint32_t>(arity) != instruction.b)
                {
                    return std::string("wrong number of arguments for a built-in");
                }
                break;
            }
            case OperandKind::Struct:
                if (a >= program.structs.size())
                {
                    return std::format("struct ${} does not exist", a);
                }
                break;
            case OperandKind::Field:
            {
                std::size_t widest = 0;
                for (const StructLayout& layout : program.structs)
                {
                    widest = std::max(widest, layout.field_names.size());
                }
                if (a >= widest)
                {
                    return std::format("field #{} does not exist in any struct", a);
                }
                break;
            }
            case OperandKind::Jump:
            case OperandKind::Count:
            case OperandKind::None:
                break;
            }
            return std::nullopt;
        }

        std::optional<std::string> verify_function(const Program& program, const Function& function)
        {
            const auto fail = [&](std::uint32_t offset, const std::string& message)
            {
                return std::format("in function '{}' at offset {}: {}", function.name, offset, message);
            };

            if (function.arity > function.local_count)
            {
                return fail(0, "more parameters than local slots");
            }
            if (function.code.empty())
            {
                return fail(0, "empty function body");
            }

            std::set<std::uint32_t> boundaries;
            std::vector<Instruction> jumps;
            std::uint32_t offset = 0;
            Opcode last = Opcode::Halt;
            while (offset < function.code.size())
            {
                const auto instruction = decode(function.code, offset);
                if (!instruction)
                {
                    return fail(offset, "unknown opcode or truncated operand");
                }
                if (auto problem = check_operand(program, function, *instruction))
                {
                    return fail(offset, *problem);
                }
                if (opcode_info(instruction->op).operand == OperandKind::Jump)
                {
                    jumps.push_back(*instruction);
                }
                boundaries.insert(offset);
                last = instruction->op;
                offset += instruction->size;
            }

            for (const Instruction& jump : jumps)
            {
                if (!boundaries.contains(jump.a))
                {
                    return fail(jump.offset, std::format("jump target {} is not the start of an instruction", jump.a));
                }
            }
            if (last != Opcode::Halt && last != Opcode::Return && last != Opcode::ReturnVoid && last != Opcode::Jump)
            {
                return fail(offset, "the function does not end with HALT, RETURN, RETURN_VOID or JUMP");
            }
            return std::nullopt;
        }
    }

    std::optional<std::string> verify(const Program& program)
    {
        if (program.functions.empty())
        {
            return "the program has no functions";
        }
        if (program.entry >= program.functions.size())
        {
            return std::format("entry function #{} does not exist", program.entry);
        }
        if (program.functions[program.entry].arity != 0)
        {
            return "the entry function must not take parameters";
        }
        for (const StructLayout& layout : program.structs)
        {
            if (layout.field_names.size() != layout.field_types.size())
            {
                return std::format("struct '{}' has inconsistent field information", layout.name);
            }
        }
        for (const Function& function : program.functions)
        {
            if (auto problem = verify_function(program, function))
            {
                return problem;
            }
        }
        return std::nullopt;
    }
}
