cmake_minimum_required(VERSION 3.28)
project(T3Highlights C CXX)

set(T3_HIGHLIGHTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(t3-highlights-lexer STATIC
    ${T3_HIGHLIGHTS_ROOT}/cpp/t3_highlights.c
    ${T3_HIGHLIGHTS_ROOT}/Vendor/highlights/highlights.c
    ${T3_HIGHLIGHTS_ROOT}/Vendor/wasm2c/wasm-rt-impl.c
    ${T3_HIGHLIGHTS_ROOT}/Vendor/wasm2c/wasm-rt-mem-impl.c)
target_include_directories(t3-highlights-lexer
    PUBLIC ${T3_HIGHLIGHTS_ROOT}/cpp
    PRIVATE ${T3_HIGHLIGHTS_ROOT}/Vendor ${T3_HIGHLIGHTS_ROOT}/Vendor/highlights ${T3_HIGHLIGHTS_ROOT}/Vendor/wasm2c)
target_compile_definitions(t3-highlights-lexer
    PRIVATE WASM_RT_USE_MMAP=0 WASM_RT_MEMCHECK_BOUNDS_CHECK=1 WASM_RT_SKIP_SIGNAL_RECOVERY=1)
target_compile_options(t3-highlights-lexer PRIVATE -O2)

add_library(t3-highlights STATIC T3Highlights.cpp ${T3_HIGHLIGHTS_ROOT}/cpp/Utf16ToUtf8.cpp)
set_target_properties(t3-highlights PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(t3-highlights PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(t3-highlights PUBLIC jsi reactnative PRIVATE t3-highlights-lexer)
