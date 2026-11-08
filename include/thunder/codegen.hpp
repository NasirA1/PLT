// Code generation: turns the type-checked AST into bytecode for the virtual machine.
#pragma once

#include "thunder/ast.hpp"
#include "thunder/bytecode.hpp"
#include "thunder/language_pack.hpp"
#include "thunder/sema.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace thunder
{
    class CodeGenerator
    {
    public:
        CodeGenerator(const SemanticModel& model, const LanguagePack& language);

        // 'program' must have been analysed without errors.
        bytecode::Program generate(const ProgramNode& program, const std::string& source_name);

    private:
        struct Loop
        {
            std::vector<std::uint32_t> break_jumps;    // operand offsets to patch with the loop exit
            std::vector<std::uint32_t> continue_jumps; // operand offsets to patch with the continue target
        };

        void generate_function(const FuncDeclStmt& func);

        // Statements
        void gen_statement(const Stmt& stmt);
        void gen_var_decl(const VarDeclStmt& decl);
        void gen_assign(const AssignStmt& assign);
        void gen_if(const IfStmt& branch);
        void gen_while(const WhileStmt& loop);
        void gen_for(const ForStmt& loop);
        void gen_for_each(const ForEachStmt& loop);
        void gen_compound_op(const AssignStmt& assign); // the operator of "x += v", if any
        void gen_load(const Symbol& symbol, const Span& span);
        void gen_store(const Symbol& symbol, const Span& span);
        void gen_default_value(const Type& type, const Span& span);

        // Expressions
        void gen_expr(const Expr& expr);  // the value, then the int -> float widening if needed
        void gen_value(const Expr& expr); // the value itself
        void gen_binary(const BinaryExpr& binary);
        void gen_call(const CallExpr& call);
        void gen_construct(const CallExpr& call);

        // Emission helpers
        void emit(bytecode::Opcode op, const Span& span);
        void emit_u8(std::uint8_t value);
        void emit_u16(std::uint32_t value, const char* what);
        std::uint32_t emit_jump(bytecode::Opcode op, const Span& span); // returns the operand offset
        void patch_jump(std::uint32_t operand_offset, std::uint32_t target);
        std::uint32_t here() const;
        std::uint32_t constant(const bytecode::Constant& value);

        const SemanticModel& model_;
        const LanguagePack& language_;
        bytecode::Program program_;
        bytecode::Function* function_ = nullptr;
        std::map<bytecode::Constant, std::uint32_t> constant_indexes_;
        std::vector<Loop> loops_;
    };
}
