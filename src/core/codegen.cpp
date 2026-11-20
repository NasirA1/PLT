#include "thunder/codegen.hpp"

#include "thunder/lexer.hpp"

#include <algorithm>
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
            for (const auto& [method, function] : info->methods)
            {
                layout.methods.emplace_back(method, static_cast<std::uint16_t>(function));
            }
            program_.structs.push_back(std::move(layout));
        }
        for (const auto& info : model_.enums)
        {
            EnumLayout layout;
            layout.name = info->name;
            layout.member_names = info->members;
            layout.builtin = info->builtin;
            program_.enums.push_back(std::move(layout));
        }
        for (const Symbol* global : model_.globals)
        {
            program_.globals.push_back(global->name);
        }
        program_.native_libraries = model_.native_libraries;
        for (const NativeInfo& native : model_.natives)
        {
            bytecode::NativeFunction entry;
            entry.name = native.name;
            entry.arity = static_cast<std::uint16_t>(native.parameter_types.size());
            entry.returns_value = !native.return_type.is_void();
            entry.result_kind = entry.returns_value ? native_kind(native.return_type, false).value_or(0) : 0;
            for (const Type& parameter : native.parameter_types)
            {
                entry.parameter_kinds.push_back(native_kind(parameter, true).value_or(0));
            }
            program_.natives.push_back(std::move(entry));
        }

        // Create the function table first so calls can refer to any function by index.
        for (const FunctionInfo& info : model_.functions)
        {
            bytecode::Function function;
            function.name = info.name;
            function.arity = static_cast<std::uint16_t>(info.parameter_types.size());
            function.local_count = info.local_count;
            function.capture_count = static_cast<std::uint16_t>(info.captured.size());
            function.returns_value = !info.return_type.is_void();
            for (const Symbol* local : info.locals)
            {
                function.local_names.push_back(local->name);
            }
            program_.functions.push_back(std::move(function));
        }
        program_.entry = 0;

        // <main>: give every global its default value, run the top-level statements in order,
        // then call main() if the program defines one. The default values belong to no statement,
        // so they carry the position of the first statement that runs: a debugger then starts
        // on the first line of the program rather than on the declaration of its last global.
        function_ = &program_.functions[0];
        const Span prologue = first_statement_span(program);
        for (const Symbol* global : model_.globals)
        {
            gen_default_value(global->type, prologue);
            gen_store(*global, prologue);
        }
        emit_box_prologue(model_.functions[0]);
        // The program's own items and those of the modules it imports, modules first.
        for (const Stmt* item : model_.items)
        {
            if (item->kind != StmtKind::FuncDecl && item->kind != StmtKind::StructDecl && item->kind != StmtKind::EnumDecl
                && item->kind != StmtKind::Interface)
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
            if (item->kind == StmtKind::FuncDecl && !static_cast<const FuncDeclStmt&>(*item).is_native
                && static_cast<const FuncDeclStmt&>(*item).function_index >= 0)
            {
                generate_function(static_cast<const FuncDeclStmt&>(*item));
            }
        }
        // The instances of generic functions (and of generic structs' methods), in the order
        // they were deduced.
        for (const StmtPtr& item : model_.instantiated)
        {
            if (item->kind == StmtKind::FuncDecl && static_cast<const FuncDeclStmt&>(*item).function_index >= 0)
            {
                generate_function(static_cast<const FuncDeclStmt&>(*item));
            }
        }
        // Function values written in place (closures), wherever they appear.
        for (const LambdaExpr* lambda : model_.lambdas)
        {
            if (lambda->function_index >= 0)
            {
                generate_lambda(*lambda);
            }
        }
        return std::move(program_);
    }

    Span CodeGenerator::end_of(const Stmt& stmt)
    {
        if (stmt.kind == StmtKind::Block && !static_cast<const BlockStmt&>(stmt).statements.empty())
        {
            return end_of(*static_cast<const BlockStmt&>(stmt).statements.back());
        }
        Span end = stmt.span;
        end.start = end.end;
        return end;
    }

    Span CodeGenerator::first_statement_span(const ProgramNode& program) const
    {
        for (const Stmt* item : model_.items)
        {
            if (item->kind != StmtKind::FuncDecl && item->kind != StmtKind::StructDecl && item->kind != StmtKind::EnumDecl
                && item->kind != StmtKind::Interface)
            {
                return item->span;
            }
        }
        if (model_.main_function >= 0)
        {
            return model_.functions[static_cast<std::size_t>(model_.main_function)].decl->name_span;
        }
        Span end = program.span;
        end.start = end.end;
        return end;
    }

    void CodeGenerator::generate_function(const FuncDeclStmt& func)
    {
        function_ = &program_.functions[static_cast<std::size_t>(func.function_index)];
        withs_.clear();
        emit_box_prologue(model_.functions[static_cast<std::size_t>(func.function_index)]);
        for (const StmtPtr& stmt : func.body->statements)
        {
            gen_statement(*stmt);
        }
        if (!func.return_type || func.is_constructor)
        {
            // Falling off the end of a function without a return type returns to the caller.
            Span end = func.body->span;
            end.start = end.end;
            emit(Opcode::ReturnVoid, end);
        }
    }

    void CodeGenerator::generate_lambda(const LambdaExpr& lambda)
    {
        function_ = &program_.functions[static_cast<std::size_t>(lambda.function_index)];
        withs_.clear();
        emit_box_prologue(model_.functions[static_cast<std::size_t>(lambda.function_index)]);
        for (const StmtPtr& stmt : lambda.body->statements)
        {
            gen_statement(*stmt);
        }
        if (!lambda.return_type)
        {
            Span end = lambda.body->span;
            end.start = end.end;
            emit(Opcode::ReturnVoid, end);
        }
    }

    void CodeGenerator::emit_box_prologue(const FunctionInfo& info)
    {
        // A local (or parameter) some function value captures lives in a heap box the closure
        // shares; the box is made at function entry, around whatever the slot holds.
        for (const Symbol* local : info.locals)
        {
            if (local->boxed)
            {
                emit(Opcode::LoadLocal, local->span);
                emit_u16(local->slot, "variables");
                emit(Opcode::NewBox, local->span);
                emit(Opcode::StoreLocal, local->span);
                emit_u16(local->slot, "variables");
            }
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
        case StmtKind::ForEach:
            gen_for_each(static_cast<const ForEachStmt&>(stmt));
            break;
        case StmtKind::Break:
            gen_cleanups(loops_.back().with_depth);
            loops_.back().break_jumps.push_back(emit_jump(Opcode::Jump, stmt.span));
            break;
        case StmtKind::Continue:
            gen_cleanups(loops_.back().with_depth);
            loops_.back().continue_jumps.push_back(emit_jump(Opcode::Jump, stmt.span));
            break;
        case StmtKind::With:
            gen_with(static_cast<const WithStmt&>(stmt));
            break;
        case StmtKind::Pass:
            break;
        case StmtKind::Return:
        {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            if (ret.value)
            {
                gen_expr(*ret.value); // computed first: it may still read the resources
                gen_cleanups(0);      // the value waits on the stack while they are closed
                emit(Opcode::Return, stmt.span);
            }
            else
            {
                gen_cleanups(0);
                emit(Opcode::ReturnVoid, stmt.span);
            }
            break;
        }
        case StmtKind::FuncDecl:
        case StmtKind::StructDecl:
        case StmtKind::EnumDecl:
        case StmtKind::Import:
        case StmtKind::Module:
        case StmtKind::Interface:
            break; // functions are generated separately; structs, interfaces and enums only describe types; imports were resolved
        }
    }

    void CodeGenerator::gen_with(const WithStmt& with)
    {
        gen_expr(*with.resource);
        gen_store(*with.symbol, with.span);
        withs_.push_back(&with);
        gen_statement(*with.body);
        withs_.pop_back();
        if (with.cleanup && !terminates(*with.body))
        {
            gen_statement(*with.cleanup); // the end of the block (if it can be reached)
        }
    }

    void CodeGenerator::gen_cleanups(std::size_t from)
    {
        for (std::size_t i = withs_.size(); i > from; --i)
        {
            if (withs_[i - 1]->cleanup)
            {
                gen_statement(*withs_[i - 1]->cleanup);
            }
        }
    }

    void CodeGenerator::enter_loop()
    {
        loops_.emplace_back();
        loops_.back().with_depth = withs_.size();
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
        // "x += v" reads the target, applies the operator and stores the result. The target itself
        // (and any index inside it) is evaluated only once, hence the DUP/DUP2.
        const bool compound = assign.compound_op.has_value();
        if (assign.target->kind == ExprKind::Name)
        {
            const Symbol& symbol = *static_cast<const NameExpr&>(*assign.target).symbol;
            if (compound)
            {
                gen_load(symbol, assign.target->span);
            }
            gen_expr(*assign.value);
            gen_compound_op(assign);
            gen_store(symbol, assign.target->span);
            return;
        }
        if (assign.target->kind == ExprKind::Field)
        {
            // s.f = v  ->  push s, push v, SET_FIELD f
            const auto& field = static_cast<const FieldExpr&>(*assign.target);
            gen_expr(*field.target);
            if (compound)
            {
                emit(Opcode::Dup, field.field_span);
                emit(Opcode::GetField, field.field_span);
                emit_u16(static_cast<std::uint32_t>(field.field_index), "fields");
            }
            gen_expr(*assign.value);
            gen_compound_op(assign);
            emit(Opcode::SetField, field.field_span);
            emit_u16(static_cast<std::uint32_t>(field.field_index), "fields");
            return;
        }
        // a[i] = v  ->  push a, push i, push v, INDEX_SET
        const auto& index = static_cast<const IndexExpr&>(*assign.target);
        gen_expr(*index.target);
        gen_expr(*index.index);
        if (compound)
        {
            emit(Opcode::Dup2, index.span);
            emit(Opcode::IndexGet, index.span);
        }
        gen_expr(*assign.value);
        gen_compound_op(assign);
        emit(Opcode::IndexSet, index.span);
    }

    void CodeGenerator::gen_compound_op(const AssignStmt& assign)
    {
        if (!assign.compound_op)
        {
            return;
        }
        const TokenKind op = *Lexer::compound_operator(*assign.compound_op);
        Opcode code = Opcode::Add;
        switch (op)
        {
        case TokenKind::Plus:
            code = assign.target->type.is_string() ? Opcode::Concat : Opcode::Add;
            break;
        case TokenKind::Minus:
            code = Opcode::Sub;
            break;
        case TokenKind::Star:
            code = Opcode::Mul;
            break;
        case TokenKind::Slash:
            code = Opcode::Div;
            break;
        default:
            code = Opcode::Mod;
            break;
        }
        emit(code, assign.span);
    }

    void CodeGenerator::gen_for_each(const ForEachStmt& loop)
    {
        //     <collection>           (keys(m) for a map)
        //     STORE_LOCAL sequence
        //     CONST 0 / STORE_LOCAL index
        // start:
        //     LOAD index, LOAD sequence, CALL_BUILTIN len, LT
        //     JUMP_IF_FALSE end
        //     LOAD sequence, LOAD index, INDEX_GET, STORE_LOCAL name
        //     <body>
        // continue:
        //     index = index + 1, JUMP start
        // end:
        const Span span = loop.collection->span;
        gen_expr(*loop.collection);
        if (loop.over_map_keys)
        {
            emit(Opcode::CallBuiltin, span); // walking a map walks its keys
            emit_u8(static_cast<std::uint8_t>(BuiltinId::Keys));
            emit_u8(1);
        }
        gen_store(*loop.sequence_symbol, span);
        emit(Opcode::Const, span);
        emit_u16(constant(std::int64_t{0}), "constants");
        gen_store(*loop.index_symbol, span);

        const std::uint32_t start = here();
        gen_load(*loop.index_symbol, span);
        gen_load(*loop.sequence_symbol, span);
        emit(Opcode::CallBuiltin, span);
        emit_u8(static_cast<std::uint8_t>(BuiltinId::Len));
        emit_u8(1);
        emit(Opcode::Less, span);
        const std::uint32_t to_end = emit_jump(Opcode::JumpIfFalse, span);

        gen_load(*loop.sequence_symbol, span);
        gen_load(*loop.index_symbol, span);
        emit(Opcode::IndexGet, span);
        if (loop.symbols.size() == 1)
        {
            gen_store(*loop.symbols[0], loop.name_spans[0]);
        }
        else
        {
            // for (key, value) in items(m): unpack the tuple into the loop variables.
            for (std::size_t i = 0; i < loop.symbols.size(); ++i)
            {
                if (i + 1 < loop.symbols.size())
                {
                    emit(Opcode::Dup, loop.name_spans[i]);
                }
                emit(Opcode::TupleGet, loop.name_spans[i]);
                emit_u16(static_cast<std::uint32_t>(i), "tuple elements");
                gen_store(*loop.symbols[i], loop.name_spans[i]);
            }
        }

        enter_loop();
        gen_statement(*loop.body);

        const std::uint32_t continue_target = here();
        gen_load(*loop.index_symbol, span);
        emit(Opcode::Const, span);
        emit_u16(constant(std::int64_t{1}), "constants");
        emit(Opcode::Add, span);
        gen_store(*loop.index_symbol, span);
        patch_jump(emit_jump(Opcode::Jump, span), start);

        const std::uint32_t end = here();
        patch_jump(to_end, end);
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
            // Skipping the else branch is the last thing the then branch does: the jump carries
            // the line of its last statement, so a debugger never shows a stop of its own for it.
            to_end = emit_jump(Opcode::Jump, end_of(*branch.then_branch));
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

        enter_loop();
        gen_statement(*loop.body);
        // The jump back belongs to the loop's own line, not to the line after the body: a
        // debugger then shows the condition being tested again, as the programmer imagines it.
        patch_jump(emit_jump(Opcode::Jump, loop.condition->span), start);

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

        enter_loop();
        gen_statement(*loop.body);

        const std::uint32_t continue_target = here();
        if (loop.step)
        {
            gen_statement(*loop.step);
        }
        // The jump back belongs to the loop's own line (its step, or its condition), not to the
        // line after the body.
        const Span& back = loop.step ? loop.step->span : (loop.condition ? loop.condition->span : loop.span);
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

    void CodeGenerator::gen_load(const Symbol& symbol, const Span& span)
    {
        if (symbol.kind == SymbolKind::Global)
        {
            emit(Opcode::LoadGlobal, span);
            emit_u16(symbol.slot, "variables");
            return;
        }
        if (symbol.kind == SymbolKind::Capture)
        {
            // A variable captured from an enclosing function: read it through its box.
            emit(Opcode::LoadCapture, span);
            emit_u16(symbol.slot, "captures");
            emit(Opcode::BoxGet, span);
            return;
        }
        emit(Opcode::LoadLocal, span);
        emit_u16(symbol.slot, "variables");
        if (symbol.boxed)
        {
            emit(Opcode::BoxGet, span); // the slot holds the box; the value lives inside
        }
    }

    void CodeGenerator::gen_store(const Symbol& symbol, const Span& span)
    {
        if (symbol.kind == SymbolKind::Global)
        {
            emit(Opcode::StoreGlobal, span);
            emit_u16(symbol.slot, "variables");
            return;
        }
        if (symbol.kind == SymbolKind::Capture)
        {
            emit(Opcode::LoadCapture, span);
            emit_u16(symbol.slot, "captures");
            emit(Opcode::BoxSet, span); // pops the box, then the value waiting below it
            return;
        }
        if (symbol.boxed)
        {
            emit(Opcode::LoadLocal, span);
            emit_u16(symbol.slot, "variables");
            emit(Opcode::BoxSet, span);
            return;
        }
        emit(Opcode::StoreLocal, span);
        emit_u16(symbol.slot, "variables");
    }

    void CodeGenerator::gen_default_value(const Type& type, const Span& span)
    {
        std::vector<std::int32_t> visiting;
        gen_default_value(type, span, visiting);
    }

    void CodeGenerator::gen_default_value(const Type& type, const Span& span, std::vector<std::int32_t>& visiting)
    {
        if (type.is_interface())
        {
            // No struct to build: the default of an interface-typed variable is nothing.
            emit(Opcode::NewStruct, span);
            emit_u16(bytecode::no_struct, "structs");
            return;
        }
        if (type.is_struct())
        {
            // A struct whose fields all have their default values. A field whose type leads
            // back to a struct already being built (a recursive struct, a linked node) defaults
            // to nothing, so the default value stays finite.
            if (std::find(visiting.begin(), visiting.end(), type.struct_index) != visiting.end())
            {
                emit(Opcode::NewStruct, span);
                emit_u16(bytecode::no_struct, "structs");
                return;
            }
            visiting.push_back(type.struct_index);
            const StructInfo& info = *model_.structs[static_cast<std::size_t>(type.struct_index)];
            for (const FieldInfo& field : info.fields)
            {
                gen_default_value(field.type, span, visiting);
            }
            visiting.pop_back();
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
                gen_default_value(element, span, visiting);
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
        else if (type.is_float())
        {
            emit(Opcode::Const, span);
            emit_u16(constant(0.0), "constants");
        }
        else if (type.is_byte())
        {
            emit(Opcode::Const, span);
            emit_u16(constant(std::int64_t{0}), "constants");
            emit(Opcode::IntToByte, span);
        }
        else if (type.is_enum())
        {
            // The first member: 0 is the default position, as 0 is the default int.
            emit(Opcode::EnumConst, span);
            emit_u16(static_cast<std::uint32_t>(type.struct_index), "enums");
            emit_u16(0, "enum members");
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
        gen_value(expr);
        if (expr.narrow_to_byte)
        {
            emit(Opcode::IntToByte, expr.span); // a literal written where a byte is wanted
        }
        if (expr.widen_to_int)
        {
            emit(Opcode::ByteToInt, expr.span); // a byte used where an int (or a float) is wanted
        }
        if (expr.widen_to_float)
        {
            emit(Opcode::IntToFloat, expr.span); // an int used where a float is wanted
        }
    }

    void CodeGenerator::gen_value(const Expr& expr)
    {
        switch (expr.kind)
        {
        case ExprKind::IntLiteral:
            emit(Opcode::Const, expr.span);
            emit_u16(constant(static_cast<const IntLiteralExpr&>(expr).value), "constants");
            break;
        case ExprKind::FloatLiteral:
            emit(Opcode::Const, expr.span);
            emit_u16(constant(static_cast<const FloatLiteralExpr&>(expr).value), "constants");
            break;
        case ExprKind::BoolLiteral:
            emit(static_cast<const BoolLiteralExpr&>(expr).value ? Opcode::True : Opcode::False, expr.span);
            break;
        case ExprKind::NothingLiteral:
            emit(Opcode::NewStruct, expr.span);
            emit_u16(bytecode::no_struct, "structs");
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
            gen_load(*name.symbol, expr.span);
            break;
        }
        case ExprKind::Lambda:
        {
            // A function value written in place: load the box of each captured variable (the box
            // itself, not its content), then bundle them with the function.
            const auto& lambda = static_cast<const LambdaExpr&>(expr);
            for (const Symbol* captured : lambda.capture_loads)
            {
                emit(captured->kind == SymbolKind::Capture ? Opcode::LoadCapture : Opcode::LoadLocal, expr.span);
                emit_u16(captured->slot, captured->kind == SymbolKind::Capture ? "captures" : "variables");
            }
            emit(Opcode::MakeClosure, expr.span);
            emit_u16(static_cast<std::uint32_t>(lambda.function_index), "functions");
            emit_u16(static_cast<std::uint32_t>(lambda.capture_loads.size()), "captures");
            break;
        }
        case ExprKind::Unary:
        {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            gen_expr(*unary.operand);
            emit(unary.op == TokenKind::Minus       ? Opcode::Neg
                     : unary.op == TokenKind::Tilde ? Opcode::BitNot
                                                    : Opcode::Not,
                expr.span);
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
            if (field.enum_use != FieldExpr::EnumUse::None)
            {
                // Color.red, Color.values, Color.count: the target is a type, so nothing is evaluated.
                const auto enum_index = static_cast<std::uint32_t>(field.enum_index);
                if (field.enum_use == FieldExpr::EnumUse::Member)
                {
                    emit(Opcode::EnumConst, expr.span);
                    emit_u16(enum_index, "enums");
                    emit_u16(static_cast<std::uint32_t>(field.enum_member), "enum members");
                }
                else if (field.enum_use == FieldExpr::EnumUse::Values)
                {
                    emit(Opcode::EnumValues, expr.span);
                    emit_u16(enum_index, "enums");
                }
                else
                {
                    emit(Opcode::Const, expr.span);
                    emit_u16(constant(static_cast<std::int64_t>(model_.enums[enum_index]->members.size())), "constants");
                }
                break;
            }
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
        case TokenKind::Amp:
            op = Opcode::BitAnd;
            break;
        case TokenKind::Pipe:
            op = Opcode::BitOr;
            break;
        case TokenKind::Caret:
            op = Opcode::BitXor;
            break;
        case TokenKind::ShiftLeft:
            op = Opcode::ShiftLeft;
            break;
        case TokenKind::ShiftRight:
            op = Opcode::ShiftRight;
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
        if (call.ctor_index >= 0)
        {
            // Button("OK"): build the struct with every field at its default, then run the
            // constructor on it (the duplicate is its hidden receiver; the arguments follow).
            const StructInfo& info = *model_.structs[struct_index];
            gen_default_value(Type::struct_type(call.struct_index, &info.name), call.span);
            emit(Opcode::Dup, call.span);
            for (const ExprPtr& argument : call.arguments)
            {
                gen_expr(*argument);
            }
            emit(Opcode::Call, call.span);
            emit_u16(static_cast<std::uint32_t>(call.ctor_index), "functions");
            return;
        }
        if (!call.has_named_arguments() && (!call.arguments.empty() || model_.structs[struct_index]->fields.empty()))
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
        if (call.self_expr)
        {
            // receiver.method(args): the receiver goes first (it becomes the method's hidden
            // first parameter), then the arguments.
            gen_expr(*call.self_expr);
            for (const ExprPtr& argument : call.arguments)
            {
                gen_expr(*argument);
            }
            if (!call.method_name.empty())
            {
                // The receiver is an interface value: the method is found when the program runs.
                emit(Opcode::CallMethod, call.span);
                emit_u16(constant(call.method_name), "constants");
                emit_u16(static_cast<std::uint32_t>(call.arguments.size()), "arguments");
            }
            else
            {
                emit(Opcode::Call, call.span);
                emit_u16(static_cast<std::uint32_t>(call.function_index), "functions");
            }
            return;
        }
        for (const ExprPtr& argument : call.arguments)
        {
            gen_expr(*argument);
        }
        if (call.enum_index >= 0)
        {
            emit(Opcode::EnumFrom, call.span); // Color(2) or Color("red"): the argument is on the stack
            emit_u16(static_cast<std::uint32_t>(call.enum_index), "enums");
            return;
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
        if (call.native_index >= 0)
        {
            emit(Opcode::CallNative, call.span);
            emit_u16(static_cast<std::uint32_t>(call.native_index), "native functions");
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
            // f(x) where f is a variable: push the arguments, then the function value, then call
            // it. The value is read the way any other read of that variable is - a global, a
            // local, a local in a box, or a variable captured from an enclosing function.
            gen_load(*call.callee_symbol, call.callee_span);
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
