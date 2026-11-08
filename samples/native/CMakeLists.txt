# The demo native module (see README.md). It is built with the toolchain so that the tests and
# this directory's demo.th can load it, and it is also a self-contained example of how to build
# a module of your own: one source file, one include directory, no libraries to link.
add_library(thunder_demo MODULE thunder_demo.cpp)
target_include_directories(thunder_demo PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(thunder_demo PROPERTIES
    CXX_VISIBILITY_PRESET hidden        # only thunder_module_init is exported
    POSITION_INDEPENDENT_CODE ON
    PREFIX "lib"                        # libthunder_demo.so / libthunder_demo.dylib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
    # Windows does not use the "lib" prefix, and a MODULE library is a .dll there.
    set_target_properties(thunder_demo PROPERTIES PREFIX "")
endif()
