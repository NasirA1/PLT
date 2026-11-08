# Core library: everything a Thunder program goes through, from source text to execution.
# Public headers live in include/thunder/, implementation files in src/core/.
add_library(thunder_core STATIC
    core/utf8.cpp
    core/source.cpp
    core/diagnostics.cpp
    core/token.cpp
    core/language_pack.cpp
    core/language_pashto.cpp
    core/lexer.cpp
    core/ast.cpp
    core/parser.cpp
    core/types.cpp
    core/builtins.cpp
    core/sema.cpp
    core/bytecode.cpp
    core/codegen.cpp
    core/value.cpp
    core/vm.cpp
    core/vm_builtins.cpp
    core/vm_graphics.cpp
    core/vm_native.cpp
    core/native.cpp
    core/package.cpp
    core/graphics.cpp
    core/os.cpp
    core/json.cpp
    core/compiler.cpp
    core/translate.cpp
    core/report.cpp
)
target_include_directories(thunder_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(thunder_core PUBLIC thunder_options ${CMAKE_DL_LIBS})
target_compile_definitions(thunder_core PUBLIC THUNDER_VERSION="${PROJECT_VERSION}")

# Command-line front ends.
add_library(thunder_cli_common STATIC cli/cli_common.cpp)
target_link_libraries(thunder_cli_common PUBLIC thunder_core)

add_executable(thunderc cli/thunderc.cpp)
target_link_libraries(thunderc PRIVATE thunder_cli_common)

add_executable(thundervm cli/thundervm.cpp)
target_link_libraries(thundervm PRIVATE thunder_cli_common)
