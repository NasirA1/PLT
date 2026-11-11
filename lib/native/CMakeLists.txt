# The native modules built with the toolchain (see README.md and docs/native.md). Each is one
# source file and one include directory, and links against nothing of Thunder's, so this file
# is also a template for a module of your own.

# The demo module: the example the documentation walks through, and what the tests load.
add_library(thunder_demo MODULE thunder_demo.cpp)
target_include_directories(thunder_demo PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(thunder_demo PROPERTIES CXX_VISIBILITY_PRESET hidden) # only thunder_module_init is exported
thunder_sample_library(thunder_demo)

# The sockets module: TCP and UDP for Thunder programs (docs/sockets.md, sockets/README.md).
# It needs the platform's socket library and nothing else.
add_library(thunder_sockets MODULE sockets/thunder_sockets.cpp)
target_include_directories(thunder_sockets PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(thunder_sockets PROPERTIES CXX_VISIBILITY_PRESET hidden)
if(WIN32)
    target_link_libraries(thunder_sockets PRIVATE ws2_32)
endif()
thunder_sample_library(thunder_sockets)
