cmake_minimum_required(VERSION 3.28)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(react_native_linux LANGUAGES C CXX)

# ctest only recurses into subdirectories when testing was enabled at the top level. The
# `test` preset is the only configure that registers any tests; everywhere else this is inert.
enable_testing()

add_subdirectory(packages/core)
