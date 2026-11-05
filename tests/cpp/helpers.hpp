// Shared helpers for the C++ tests.
#pragma once

#include "thunder/compiler.hpp"
#include "thunder/lexer.hpp"
#include "thunder/parser.hpp"
#include "thunder/vm.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace test
{
    // Tokens kinds of 'text', excluding the final EndOfFile.
    std::vector<thunder::TokenKind> token_kinds(const std::string& text);

    // Codes of every diagnostic reported for 'text', e.g. {"S005", "W001"}.
    std::vector<std::string> diagnostic_codes(const std::string& text);

    // Parses a single expression and returns its S-expression ("" on error).
    std::string parse_expr(const std::string& text);

    // Parses a whole program and returns its S-expression.
    std::string parse_program(const std::string& text);

    struct RunResult
    {
        bool compiled = false;
        std::vector<std::string> diagnostics; // short form, without the file name
        std::string output;
        std::optional<thunder::RuntimeError> error;
        thunder::ExecStats stats;
    };

    RunResult run(const std::string& text, thunder::VmOptions options = {});

    // Compiles and runs 'text', returning the output (with "ERROR <code>: <message>" appended
    // on a runtime error and the diagnostics if compilation fails).
    std::string run_output(const std::string& text);

    // Compiles 'text', which must succeed.
    thunder::bytecode::Program compile_ok(const std::string& text);
}
