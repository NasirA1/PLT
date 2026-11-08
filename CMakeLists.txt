# Thunder - an educational programming language toolchain (compiler + virtual machine).
# The IDE is written in Python with Qt (PySide6; see ide/) and drives the executables built here.
cmake_minimum_required(VERSION 3.25)

project(Thunder
    VERSION 0.2.0
    DESCRIPTION "Thunder educational programming language: compiler and virtual machine"
    LANGUAGES CXX)

# C++23: the newest standard that builds with both GCC 15 and Visual Studio 2026 (C++26 mode does not).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(THUNDER_BUILD_TESTS "Build the C++ unit tests" ON)
option(THUNDER_COVERAGE "Instrument the build for gcov coverage (GCC/Clang only)" OFF)
option(THUNDER_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang only)" OFF)
option(THUNDER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Put every executable in <build>/bin, also for multi-config generators such as Visual Studio
# (the generator expression stops CMake from appending a per-configuration subdirectory).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin$<0:>")

# Common warning/instrumentation settings shared by every Thunder target.
add_library(thunder_options INTERFACE)
if(MSVC)
    target_compile_options(thunder_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    if(THUNDER_WARNINGS_AS_ERRORS)
        target_compile_options(thunder_options INTERFACE /WX)
    endif()
else()
    target_compile_options(thunder_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor)
    if(THUNDER_WARNINGS_AS_ERRORS)
        target_compile_options(thunder_options INTERFACE -Werror)
    endif()
    if(THUNDER_COVERAGE)
        target_compile_options(thunder_options INTERFACE --coverage -O0 -g)
        target_link_options(thunder_options INTERFACE --coverage)
    endif()
    if(THUNDER_SANITIZERS)
        target_compile_options(thunder_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(thunder_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

add_subdirectory(src)

# The demo native module (samples/native): an example of the C++ native interface, and what the
# native-interface tests load. It deliberately uses nothing but include/thunder/native.h.
add_subdirectory(samples/native)

# The example display plug-ins (samples/display): a window for a graphics program run outside
# the IDE. They are plain C - the display interface is a C ABI, so a plug-in can be written in
# any language - and are skipped if no C compiler is at hand. The text one is always built
# (the tests use it); the SDL one when SDL2 is installed.
include(CheckLanguage)
check_language(C)
if(CMAKE_C_COMPILER)
    enable_language(C)
    add_subdirectory(samples/display)
else()
    message(STATUS "No C compiler: skipping the display plug-ins (see samples/display/README.md)")
endif()

if(THUNDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/cpp)
endif()
