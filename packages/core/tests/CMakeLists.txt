# The GoogleTest binary for @react-native-linux/core.
#
# This directory is reached only from RNL_BUILD_TESTS, which is also what keeps Hermes out of
# the configure: Hermes' bundled llvh defines the `gtest` and `gtest_main` target names
# unconditionally, so upstream googletest has nowhere to go in the default build. See
# docs/cpp-toolchain.md.
#
# The sources under test are compiled into the test executable rather than linked from
# rnl_react_core, because rnl_react_core is a Hermes-linked target and because the coverage
# instrumentation below then applies to exactly the files the coverage gate scores.

# Pinned by commit rather than by tag: a tag is a mutable ref, and this is the only dependency
# in the build that is fetched purely to run tests.
set(RNL_GOOGLETEST_VERSION 1.18.0)
set(RNL_GOOGLETEST_COMMIT 063de7e9578f82b369302001269680b4b1553359)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

message(STATUS "googletest: FetchContent ${RNL_GOOGLETEST_VERSION}")

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${RNL_GOOGLETEST_COMMIT})
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(rnl_core_tests
    ImageTest.cpp
    ImeTest.cpp
    InputTest.cpp
    SceneTest.cpp
    ScrollTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ImageContent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/InputPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/LinuxMountingManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RetainedScene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ScrollPhysics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/TextInputV3State.cpp)
target_compile_options(rnl_core_tests PRIVATE -Wall -Wextra -fexceptions -frtti)
target_include_directories(rnl_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(rnl_core_tests
    PRIVATE
        GTest::gtest_main
        jsi
        rnl_jsidynamic
        folly_runtime
        ${RNL_REACT_COMMON_TARGETS}
        atomic)
set_target_properties(rnl_core_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# LLVM source-based coverage, scoped to this target so the profile carries the files the gate
# scores and nothing else. scripts/cpp-coverage.ts reads what comes out of it.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(rnl_core_tests PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(rnl_core_tests PRIVATE -fprofile-instr-generate)
endif()

gtest_discover_tests(rnl_core_tests)
