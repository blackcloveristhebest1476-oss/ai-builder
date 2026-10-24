# Standalone tests for the core library (no Geode SDK needed).
#   cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
cmake_minimum_required(VERSION 3.21)
project(FrameWindowAnalyzerTests CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/*.cpp)
add_executable(fwa_tests test_core.cpp ${CORE_SOURCES})
target_compile_definitions(fwa_tests PRIVATE FWA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if (MSVC)
    target_compile_options(fwa_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(fwa_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME core COMMAND fwa_tests)
