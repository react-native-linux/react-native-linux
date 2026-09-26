cmake_minimum_required(VERSION 3.4.1)
project(CppLibrary)

set (CMAKE_VERBOSE_MAKEFILE ON)

add_library(
    react-native-cpp-library
    STATIC
    ../cpp/CppLibraryImpl.cpp
)

set_target_properties(
    react-native-cpp-library PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_include_directories(
    react-native-cpp-library
    PUBLIC
    ../cpp
)

target_link_libraries(
    react-native-cpp-library
    jsi
    reactnative
    react_codegen_CppLibrarySpec
)
