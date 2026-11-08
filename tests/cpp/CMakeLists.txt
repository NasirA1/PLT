add_executable(thunder_tests
    main.cpp
    helpers.cpp
    json_check.cpp
    test_lexer.cpp
    test_parser.cpp
    test_sema.cpp
    test_execution.cpp
    test_bytecode.cpp
    test_vm.cpp
    test_report.cpp
    test_samples.cpp
    test_structs.cpp
    test_pashto.cpp
    test_tuples_maps.cpp
    test_library.cpp
    test_modules.cpp
    test_graphics.cpp
    test_floats.cpp
    test_loops_assign.cpp
    test_native.cpp
    test_package.cpp
    test_display.cpp
)
target_link_libraries(thunder_tests PRIVATE thunder_core)
target_compile_definitions(thunder_tests PRIVATE THUNDER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    THUNDER_TEST_TEMP="${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME cpp_unit_tests COMMAND thunder_tests)
if(MSVC)
    # std::getenv is used to switch golden tests into update mode.
    target_compile_definitions(thunder_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
