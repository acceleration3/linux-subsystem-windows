cmake_minimum_required(VERSION 3.20)
project(lsw_suites LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/LSWCompileOptions.cmake)

if(NOT WIN32)
    message(FATAL_ERROR "lsw-suites is a guest program: configure with a mingw-w64 toolchain")
endif()

add_subdirectory(core)
add_subdirectory(support)
add_subdirectory(selftest)
add_subdirectory(sanity)

add_executable(lsw-suites main.cpp)
target_link_libraries(lsw-suites PRIVATE suite_core suite_support suite_selftest suite_sanity LSW::CompileOptions)
math(EXPR LSW_SUITES_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
set_target_properties(lsw-suites PROPERTIES OUTPUT_NAME "lsw-suites${LSW_SUITES_BITS}")
