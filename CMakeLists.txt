cmake_minimum_required(VERSION 3.16)
project(redch3psp_recovery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(psp_render_safety_test
    tests/psp_render_safety_test.cpp
)

target_include_directories(psp_render_safety_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/psp
)

add_test(NAME psp_render_safety COMMAND psp_render_safety_test)
