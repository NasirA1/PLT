#include "thunder/codegen.hpp"

#include <format>
#include <limits>
#include <stdexcept>

namespace thunder
{
    using bytecode::Opcode;

    CodeGenerator::CodeGenerator(const SemanticModel& model, const LanguagePack& language)
        : model_(model)
        , language_(language)
    {
    }

    bytecode::Program CodeGenerator::generate(const ProgramNode& program, const std::string& source_name)
    {
        program_ = bytecode::Program{};
        program_.source_name = source_name;
        program_.language = language_.code;
        for (const auto& info : model_.structs)
        {
            StructLayout layout;
            layout.name = info->name;
            for (const FieldInfo& field : info->fields)
            {
                layout.field_names.push_back(field.name);
                layout.field_types.push_back(field.type.to_string(&language_));
            }
            program_.structs.push_back(std::move(layout));
        }
        for (const Symbol* global : model_.globals)
        {
            program_.globals.push_back(global->name);
        }

        // Create the function table first so calls can refer to any function by index.
        for (const FunctionInfo& info : model_.functions)
        {
            bytecode::Function function;
            function.name = info.name;
            function.arity = static_cast<std::uint16_t>(info.parameter_types.size());
            function.local_count = info.local_count;
            function.returns_value = !info.return_type.is_void();
            for (const Symbol* local : info.locals)
            {
                function.local_names.push_back(local->name);
            }
            program_.functions.push_back(std::move(function));
        }
        program_.entry = 0;

        // <main>: give every global its default value, run the top-level statements in order,
        // then call main() if the program defines one.
        function_ = &program_.functions[0];
        for (const Symbol* global : model_.globals)
        {
            gen_default_value(global->type, global->span);
            gen_store(*global, global->span);
        }
        // The program's own items and those of the modules it imports, modules first.
        for (const Stmt* item : model_.items)
        {
            if (item->kind != StmtKind::FuncDecl && item->kind != StmtKind::StructDecl)
            {
                gen_statement(*item);
            }
        }
        if (model_.main_function >= 0)
        {
            const FunctionInfo& main = model_.functions[static_cast<std::size_t>(model_.main_function)];
            emit(Opcode::Call, main.decl->name_span);
            emit_u16(static_cast<std::uint32_t>(model_.main_function), "functions");
        }
        Span end = program.span;
        end.start = end.end;
        emit(Opcode::Halt, end);

        for (const Stmt* item : model_.items)
        {
            if (item->kind == StmtKind::FuncDecl && static_cast<const FuncDeclStmt&>(*item).function_index >= 0)
            {
                generate_function(static_cast<const FuncDeclStmt&>(*item));
            }
        }
        return std::move(program_);
    }

    void CodeGenerator::generate_function(const FuncDeclStmt& func)
    {
        function_ = &program_.functions[static_cast<std::size_t>(func.function_index)];
        for (const StmtPtr& stmt : func.body->statements)
        {
            gen_statement(*stmt);
        }
        if (!func.return_type)
        {
            // Falling off the end of a function without a return type returns to the caller.
            Span end = func.body->span;
            end.start = end.end;
            emit(Opcode::ReturnVoid, end);
        }
    }

    // ------------------------------------------------------------------ statements

    void CodeGenerator::gen_statement(const Stmt& stmt)
    {
        switch (stmt.kind)
        {
        case StmtKind::VarDecl:
            gen_var_decl(static_cast<const VarDeclStmt&>(stmt));
            break;
        case StmtKind::Assign:
            gen_assign(static_cast<const AssignStmt&>(stmt));
            break;
        case StmtKind::ExprStmt:
        {
            const Expr& expr = *static_cast<const ExprStmt&>(stmt).expr;
            gen_expr(expr);
            if (!expr.type.is_void())
            {
                emit(Opcode::Pop, expr.span); // the value is not used
            }
            break;
        }
        case StmtKind::Block:
            for (const StmtPtr& child : static_cast<const BlockStmt&>(stmt).statements)
            {
                gen_statement(*child);
            }
            break;
        case StmtKind::If:
            gen_if(static_cast<const IfStmt&>(stmt));
            break;
        case StmtKind::While:
            gen_while(static_cast<const WhileStmt&>(stmt));
            break;
        case StmtKind::For:
            gen_for(static_cast<const ForStmt&>(stmt));
            break;
        case StmtKind::Break:
            loops_.back().break_jumps.push_back(emit_jump(Opcode::Jump, stmt.span));
            break;
        case StmtKind::Continue:
            loops_.back().continue_jumps.push_back(emit_jump(Opcode::Jump, stmt.span));
            break;
        case StmtKind::Pass:
            break;
        case StmtKind::Return:
        {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            if (ret.value)
            {
                gen_expr(*ret.value);
                emit(Opcode::Return, stmt.span);
            }
            else
            {
                emit(Opcode::ReturnVoid, stmt.span);
            }
            break;
        }
        case StmtKind::FuncDecl:
        case StmtKind::StructDecl:
        case StmtKind::Import:
            break; // functions are generated separately; structs only describe types; imports were resolved
        }
    }

    void CodeGenerator::gen_var_decl(const VarDeclStmt& decl)
    {
        if (!decl.names.empty())
        {
            // var (a, b) = pair: keep the tuple on the stack while taking each element.
            gen_expr(*decl.initializer);
            for (std::size_t i = 0; i < decl.symbols.size(); ++i)
            {
                if (i + 1 < decl.symbols.size())
                {
                    emit(Opcode::Dup, decl.name_spans[i]);
                }
                emit(Opcode::TupleGet, decl.name_spans[i]);
                emit_u16(static_cast<std::uint32_t>(i), "tuple elements");
                gen_store(*decl.symbols[i], decl.name_spans[i]);
            }
            return;
        }
        if (decl.initializer)
        {
            gen_expr(*decl.initializer);
        }
        else
        {
            gen_default_value(decl.symbol->type, decl.name_span);
        }
        gen_store(*decl.symbol, decl.name_span);
    }

    void CodeGenerator::gen_assign(const AssignStmt& assign)
    {
        if (assign.target->kind == ExprKind::Name)
        {
            gen_expr(*assign.value);
            gen_store(*static_cast<const NameExpr&>(*assign.target).symbol, assign.target->span);
            return;
        }
        if (assign.target->kind == ExprKind::Field)
        {
            // s.f = v  ->  push s, push v, SET_FIELD f
            const auto& field = static_cast<const FieldExpr&>(*assign.target);
            gen_expr(*field.target);
            gen_expr(*assign.value);
            emit(Opcode::SetField, field.field_span);
            emit_u16(static_cast<std::uint32_t>(field.field_index), "fields");
            return;
        }
        // a[i] = v  ->  push a, push i, push v, INDEX_SET
        const auto& index = static_cast<const IndexExpr&>(*assign.target);
        gen_expr(*index.target);
        gen_expr(*index.index);
        gen_expr(*assign.value);
        emit(Opcode::IndexSet, index.span);
    }

    void CodeGenerator::gen_if(const IfStmt& branch)
    {
        //     <condition>
        //     JUMP_IF_FALSE else
        //     <then>
        //     JUMP end            (omitted when <then> never falls through)
        // else:
        //     <else>
        // end:
        gen_expr(*branch.condition);
        const std::uint32_t to_else = emit_jump(Opcode::JumpIfFalse, branch.condition->span);
        gen_statement(*branch.then_branch);

        if (!branch.else_branch)
        {
            patch_jump(to_else, here());
            return;
        }

        const bool then_falls_through = !terminates(*branch.then_branch);
        std::uint32_t to_end = 0;
        if (then_falls_through)
        {
            Span end = branch.then_branch->span;
            end.start = end.end;
            to_end = emit_jump(Opcode::Jump, end);
        }
        patch_jump(to_else, here());
        gen_statement(*branch.else_branch);
        if (then_falls_through)
        {
            patch_jump(to_end, here());
        }
    }

    void CodeGenerator::gen_while(const WhileStmt& loop)
    {
        // start:
        //     <condition>
        //     JUMP_IF_FALSE end
        //     <body>
        //     JUMP start
        // end:
        const std::uint32_t start = here();
        gen_expr(*loop.condition);
        const std::uint32_t to_end = emit_jump(Opcode::JumpIfFalse, loop.condition->span);

        loops_.emplace_back();
        gen_statement(*loop.body);
        Span back = loop.body->span;
        back.start = back.end;
        patch_jump(emit_jump(Opcode::Jump, back), start);

        const std::uint32_t end = here();
        patch_jump(to_end, end);
        for (const std::uint32_t jump : loops_.back().break_jumps)
        {
            patch_jump(jump, end);
        }
        for (const std::uint32_t jump : loops_.back().continue_jumps)
        {
            patch_jump(jump, start);
        }
        loops_.pop_back();
    }

    void CodeGenerator::gen_for(const ForStmt& loop)
    {
        //     <init>
        // start:
        //     <condition>
        //     JUMP_IF_FALSE end
        //     <body>
        // continue:
        //     <step>
        //     JUMP start
        // end:
        if (loop.init)
        {
            gen_statement(*loop.init);
        }
        const std::uint32_t start = here();
        std::uint32_t to_end = 0;
        if (loop.condition)
        {
            gen_expr(*loop.condition);
            to_end = emit_jump(Opcode::JumpIfFalse, loop.condition->span);
        }

        loops_.emplace_back();
        gen_statement(*loop.body);

        const std::uint32_t continue_target = here();
        if (loop.step)
        {
            gen_statement(*loop.step);
        }
        Span back = loop.body->span;
        back.start = back.end;
        patch_jump(emit_jump(Opcode::Jump, back), start);

        const std::uint32_t end = here();
        if (loop.condition)
        {
            patch_jump(to_end, end);
        }
        for (const std::uint32_t jump : loops_.back().break_jumps)
        {
            patch_jump(jump, end);
        }
        for (const std::uint32_t jump : loops_.back().continue_jumps)
        {
            patch_jump(jump, continue_target);
        }
        loops_.pop_back();
    }

    void CodeGenerator::gen_store(const Symbol& symbol, const Span& span)
    {
        emit(symbol.kind == SymbolKind::Global ? Opcode::StoreGlobal : Opcode::StoreLocal, span);
        emit_u16(symbol.slot, "variables");
    }

    void CodeGenerator::gen_default_value(const Type& type, const Span& span)
    {
        if (type.is_struct())
        {
            // A struct whose fields all have their default values (structs are never recursive,
            // so this always terminates).
            const StructInfo& info = *model_.structs[static_cast<std::size_t>(type.struct_index)];
            for (const FieldInfo& field : info.fields)
            {
                gen_default_value(field.type, span);
            }
            emit(Opcode::NewStruct, span);
            emit_u16(static_cast<std::uint32_t>(type.struct_index), "structs");
        }
        else if (type.is_array())
        {
            emit(Opcode::MakeArray, span); // an empty array
            emit_u16(0, "elements");
        }
        else if (type.is_tuple())
        {
            for (const Type& element : type.elements())
            {
                gen_default_value(element, span);
            }
            emit(Opcode::MakeTuple, span);
            emit_u16(static_cast<std::uint32_t>(type.elements().size()), "tuple elements");
        }
        else if (type.is_map())
        {
            emit(Opcode::MakeMap, span); // an empty map
            emit_u16(0, "entries");
        }
        else if (type.is_function())
        {
            emit(Opcode::FuncRef, span); // "no function yet": calling it is a runtime error
            emit_u16(bytecode::no_function, "functions");
        }
        else if (type.is_bool())
        {
            emit(Opcode::False, span);
        }
        else if (type.is_string())
        {
            emit(Opcode::Const, span);
            emit_u16(constant(std::string()), "constants");
        }
        else
        {
            emit(Opcode::Const, span);
            emit_u16(constant(std::int64_t{0}), "constants");
        }
    }

    // ------------------------------------------------------------------ expressions

    void CodeGenerator::gen_expr(const Expr& expr)
    {
        switch (expr.kind)
        {
        case ExprKind::IntLiteral:
            emit(Opcode::Const, expr.span);
            emit_u16(constant(static_cast<const IntLiteralExpr&>(expr).value), "constants");
            break;
        case ExprKind::BoolLiteral:
            emit(static_cast<const BoolLiteralExpr&>(expr).value ? Opcode::True : Opcode::False, expr.span);
            break;
        case ExprKind::StringLiteral:
            emit(Opcode::Const, expr.span);
            emit_u16(constant(static_cast<const StringLiteralExpr&>(expr).value), "constants");
            break;
        case ExprKind::ArrayLiteral:
        {
            const auto& array = static_cast<const ArrayLiteralExpr&>(expr);
            for (const ExprPtr& element : array.elements)
            {
                gen_expr(*element);
            }
            emit(Opcode::MakeArray, expr.span);
            emit_u16(static_cast<std::uint32_t>(array.elements.size()), "array elements");
            break;
        }
        case ExprKind::TupleLiteral:
        {
            const auto& tuple = static_cast<const TupleLiteralExpr&>(expr);
            for (const ExprPtr& element : tuple.elements)
            {
                gen_expr(*element);
            }
            emit(Opcode::MakeTuple, expr.span);
            emit_u16(static_cast<std::uint32_t>(tuple.elements.size()), "tuple elements");
            break;
        }
        case ExprKind::MapLiteral:
        {
            const auto& map = static_cast<const MapLiteralExpr&>(expr);
            for (std::size_t i = 0; i < map.keys.size(); ++i)
            {
                gen_expr(*map.keys[i]);
                gen_expr(*map.values[i]);
            }
            emit(Opcode::MakeMap, expr.span);
            emit_u16(static_cast<std::uint32_t>(map.keys.size()), "map entries");
            break;
        }
        case ExprKind::TupleIndex:
        {
            const auto& element = static_cast<const TupleIndexExpr&>(expr);
            gen_expr(*element.target);
            emit(Opcode::TupleGet, element.index_span);
            emit_u16(static_cast<std::uint32_t>(element.index), "tuple elements");
            break;
        }
        case ExprKind::Name:
        {
            const auto& name = static_cast<const NameExpr&>(expr);
            if (name.function_index >= 0)
            {
                emit(Opcode::FuncRef, expr.span); // a function used as a value
                emit_u16(static_cast<std::uint32_t>(name.function_index), "functions");
                break;
            }
            const Symbol& symbol = *name.symbol;
            emit(symbol.kind == SymbolKind::Global ? Opcode::LoadGlobal : Opcode::LoadLocal, expr.span);
            emit_u16(symbol.slot, "variables");
            break;
        }
        case ExprKind::Unary:
        {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            gen_expr(*unary.operand);
            emit(unary.op == TokenKind::Minus ? Opcode::Neg : Opcode::Not, expr.span);
            break;
        }
        case ExprKind::Binary:
            gen_binary(static_cast<const BinaryExpr&>(expr));
            break;
        case ExprKind::Call:
            gen_call(static_cast<const CallExpr&>(expr));
            break;
        case ExprKind::Index:
        {
            const auto& index = static_cast<const IndexExpr&>(expr);
            gen_expr(*index.target);
            gen_expr(*index.index);
            emit(Opcode::IndexGet, expr.span);
            break;
        }
        case ExprKind::Field:
        {
            const auto& field = static_cast<const FieldExpr&>(expr);
            gen_expr(*field.target);
            emit(Opcode::GetField, field.field_span);
            emit_u16(static_cast<std::uint32_t>(field.field_index), "fields");
            break;
        }
        }
    }

    void CodeGenerator::gen_binary(const BinaryExpr& binary)
    {
        if (binary.op == TokenKind::KwAnd)
        {
            // Short-circuit: the right operand is only evaluated if the left one is true.
            gen_expr(*binary.left);
            const std::uint32_t to_false = emit_jump(Opcode::JumpIfFalse, binary.op_span);
            gen_expr(*binary.right);
            const std::uint32_t to_end = emit_jump(Opcode::Jump, binary.op_span);
            patch_jump(to_false, here());
            emit(Opcode::False, binary.op_span);
            patch_jump(to_end, here());
            return;
        }
        if (binary.op == TokenKind::KwOr)
        {
            // Short-circuit: the right operand is only evaluated if the left one is false.
            gen_expr(*binary.left);
            const std::uint32_t to_right = emit_jump(Opcode::JumpIfFalse, binary.op_span);
            emit(Opcode::True, binary.op_span);
            const std::uint32_t to_end = emit_jump(Opcode::Jump, binary.op_span);
            patch_jump(to_right, here());
            gen_expr(*binary.right);
            patch_jump(to_end, here());
            return;
        }

        gen_expr(*binary.left);
        gen_expr(*binary.right);
        Opcode op = Opcode::Add;
        switch (binary.op)
        {
        case TokenKind::Plus:
            op = binary.left->type.is_string() ? Opcode::Concat : Opcode::Add;
            break;
        case TokenKind::Minus:
            op = Opcode::Sub;
            break;
        case TokenKind::Star:
            op = Opcode::Mul;
            break;
        case TokenKind::Slash:
            op = Opcode::Div;
            break;
        case TokenKind::Percent:
            op = Opcode::Mod;
            break;
        case TokenKind::Equal:
            op = Opcode::Equal;
            break;
        case TokenKind::NotEqual:
            op = Opcode::NotEqual;
            break;
        case TokenKind::Less:
            op = Opcode::Less;
            break;
        case TokenKind::LessEqual:
            op = Opcode::LessEqual;
            break;
        case TokenKind::Greater:
            op = Opcode::Greater;
            break;
        case TokenKind::GreaterEqual:
            op = Opcode::GreaterEqual;
            break;
        default:
            throw std::logic_error("code generator: unexpected binary operator");
        }
        emit(op, binary.op_span);
    }

    void CodeGenerator::gen_construct(const CallExpr& call)
    {
        const auto struct_index = static_cast<std::uint32_t>(call.struct_index);
        if (!call.has_named_arguments())
        {
            // Point(1, 2): push the field values in order, then build the struct.
            for (const ExprPtr& argument : call.arguments)
            {
                gen_expr(*argument);
            }
            emit(Opcode::NewStruct, call.span);
            emit_u16(struct_index, "structs");
            return;
        }

        // Point(y: 2, x: 1): start from a struct with default values, then set each named field
        // in the order written, so the arguments are evaluated from left to right.
        const StructInfo& info = *model_.structs[struct_index];
        gen_default_value(Type::struct_type(call.struct_index, &info.name), call.span);
        for (std::size_t i = 0; i < call.arguments.size(); ++i)
        {
            emit(Opcode::Dup, call.arguments[i]->span);
            gen_expr(*call.arguments[i]);
            emit(Opcode::SetField, call.arguments[i]->span);
            emit_u16(static_cast<std::uint32_t>(call.argument_fields[i]), "fields");
        }
    }

    void CodeGenerator::gen_call(const CallExpr& call)
    {
        if (call.struct_index >= 0)
        {
            gen_construct(call);
            return;
        }
        for (const ExprPtr& argument : call.arguments)
        {
            gen_expr(*argument);
        }
        if (call.builtin)
        {
            emit(Opcode::CallBuiltin, call.span);
            emit_u8(static_cast<std::uint8_t>(*call.builtin));
            if (call.arguments.size() > std::numeric_limits<std::uint8_t>::max())
            {
                throw std::length_error("program too large: more than 255 arguments in a call");
            }
            emit_u8(static_cast<std::uint8_t>(call.arguments.size()));
            return;
        }
        if (call.callee_expr)
        {
            gen_expr(*call.callee_expr); // the function value, above its arguments
            emit(Opcode::CallValue, call.span);
            emit_u16(static_cast<std::uint32_t>(call.arguments.size()), "arguments");
            return;
        }
        if (call.callee_symbol != nullptr)
        {
            // f(x) where f is a variable: push the arguments, then the function value, then call it.
            const Symbol& symbol = *call.callee_symbol;
            emit(symbol.kind == SymbolKind::Global ? Opcode::LoadGlobal : Opcode::LoadLocal, call.callee_span);
            emit_u16(symbol.slot, "variables");
            emit(Opcode::CallValue, call.span);
            emit_u16(static_cast<std::uint32_t>(call.arguments.size()), "arguments");
            return;
        }
        emit(Opcode::Call, call.span);
        emit_u16(static_cast<std::uint32_t>(call.function_index), "functions");
    }

    // ------------------------------------------------------------------ emission helpers

    void CodeGenerator::emit(Opcode op, const Span& span)
    {
        const std::uint32_t offset = here();
        auto& lines = function_->lines;
        if (lines.empty() || lines.back().line != span.start.line || lines.back().column != span.start.column
            || lines.back().file != span.file)
        {
            const bytecode::LineEntry entry{offset, span.start.line, span.start.column, span.file};
            if (!lines.empty() && lines.back().offset == offset)
            {
                lines.back() = entry;
            }
            else
            {
                lines.push_back(entry);
            }
        }
        function_->code.push_back(static_cast<std::uint8_t>(op));
    }

    void CodeGenerator::emit_u8(std::uint8_t value)
    {
        function_->code.push_back(value);
    }

    void CodeGenerator::emit_u16(std::uint32_t value, const char* what)
    {
        if (value > std::numeric_limits<std::uint16_t>::max())
        {
            throw std::length_error(std::format("program too large: more than 65535 {}", what));
        }
        function_->code.push_back(static_cast<std::uint8_t>(value & 0xFF));
        function_->code.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    std::uint32_t CodeGenerator::emit_jump(Opcode op, const Span& span)
    {
        emit(op, span);
        const std::uint32_t operand = here();
        for (int i = 0; i < 4; ++i)
        {
            function_->code.push_back(0xFF); // placeholder, patched once the target is known
        }
        return operand;
    }

    void CodeGenerator::patch_jump(std::uint32_t operand_offset, std::uint32_t target)
    {
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            function_->code[operand_offset + i] = static_cast<std::uint8_t>((target >> (8 * i)) & 0xFF);
        }
    }

    std::uint32_t CodeGenerator::here() const
    {
        return static_cast<std::uint32_t>(function_->code.size());
    }

    std::uint32_t CodeGenerator::constant(const bytecode::Constant& value)
    {
        const auto found = constant_indexes_.find(value);
        if (found != constant_indexes_.end())
        {
            return found->second;
        }
        const auto index = static_cast<std::uint32_t>(program_.constants.size());
        program_.constants.push_back(value);
        constant_indexes_.emplace(value, index);
        return index;
    }
}
