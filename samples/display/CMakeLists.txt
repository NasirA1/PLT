# The example display plug-ins (see README.md and docs/display.md).
#
# Both are plain C and include nothing of Thunder's but include/thunder/display.h, so they are
# also a template for a plug-in of your own: one source file, one include directory.

# The text display has no dependencies, so it is always built; the tests use it.
add_library(thunder_display_text MODULE text/thunder_display_text.c)
target_include_directories(thunder_display_text PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(thunder_display_text PROPERTIES
    C_VISIBILITY_PRESET hidden            # only thunder_display_init is exported
    POSITION_INDEPENDENT_CODE ON
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    set_target_properties(thunder_display_text PROPERTIES PREFIX "")
endif()
if(MSVC)
    # sscanf and friends are the plain C ones on purpose: a plug-in is meant to be portable C.
    target_compile_definitions(thunder_display_text PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The SDL display needs SDL2. It is built when SDL2 is installed and quietly skipped when it
# is not, so the toolchain never depends on it.
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
endif()

if(SDL2_FOUND)
    add_library(thunder_display_sdl MODULE sdl/thunder_display_sdl.c)
    target_include_directories(thunder_display_sdl PRIVATE ${PROJECT_SOURCE_DIR}/include ${SDL2_INCLUDE_DIRS})
    if(TARGET SDL2::SDL2)
        target_link_libraries(thunder_display_sdl PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(thunder_display_sdl PRIVATE ${SDL2_LIBRARIES})
        target_link_directories(thunder_display_sdl PRIVATE ${SDL2_LIBRARY_DIRS})
    endif()
    if(UNIX)
        target_link_libraries(thunder_display_sdl PRIVATE m)
    endif()
    set_target_properties(thunder_display_sdl PROPERTIES
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON
        PREFIX "lib"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    if(WIN32)
        set_target_properties(thunder_display_sdl PROPERTIES PREFIX "")
    endif()
    if(MSVC)
        target_compile_definitions(thunder_display_sdl PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    message(STATUS "SDL2 found: building the SDL display plug-in (thundervm --display sdl)")
else()
    message(STATUS "SDL2 not found: skipping the SDL display plug-in (see samples/display/README.md)")
endif()
