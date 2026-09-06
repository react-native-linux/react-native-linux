# The Hermes-linked GoogleTest binary (#228).
#
# rnl_core_tests cannot host these suites: they construct a real Hermes runtime, and Hermes' bundled llvh
# defines the `gtest` and `gtest_main` target names unconditionally, so upstream googletest's own CMakeLists
# has nowhere to go in a configure that contains Hermes. llvh's copy is not a substitute either — it predates
# MOCK_METHOD and INSTANTIATE_TEST_SUITE_P, both of which these upstream files use — so googletest is fetched
# at the same pinned commit rnl_core_tests uses and compiled here under target names of ours. SOURCE_SUBDIR
# names a directory googletest does not have, which is CMake's documented way to populate the tree without
# calling add_subdirectory() on it. See docs/cpp-toolchain.md.
set(RNL_GOOGLETEST_VERSION 1.18.0)
set(RNL_GOOGLETEST_COMMIT 063de7e9578f82b369302001269680b4b1553359)

message(STATUS "googletest for the Hermes-linked tests: FetchContent ${RNL_GOOGLETEST_VERSION}")

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${RNL_GOOGLETEST_COMMIT}
    SOURCE_SUBDIR googletest-is-not-added-as-a-subdirectory)
FetchContent_MakeAvailable(googletest)

# gmock is built here and not in rnl_core_tests because ReactInstanceTest and RuntimeExecutorShutdownTest are
# the first suites that need it.
add_library(rnl_hermes_googletest STATIC
    ${googletest_SOURCE_DIR}/googletest/src/gtest-all.cc
    ${googletest_SOURCE_DIR}/googlemock/src/gmock-all.cc)
target_include_directories(rnl_hermes_googletest
    PUBLIC
        ${googletest_SOURCE_DIR}/googletest/include
        ${googletest_SOURCE_DIR}/googlemock/include
    PRIVATE
        ${googletest_SOURCE_DIR}/googletest
        ${googletest_SOURCE_DIR}/googlemock)
target_link_libraries(rnl_hermes_googletest PUBLIC Threads::Threads)
set_target_properties(rnl_hermes_googletest PROPERTIES CXX_STANDARD 20)

include(GoogleTest)

# BridgingTest.h includes <ReactCommon/TestCallInvoker.h>, but upstream keeps that fixture in
# callinvoker/ReactCommon/tests/, which no include root can spell as ReactCommon/TestCallInvoker.h. A
# configure-time symlink in the build tree gives the directory the name the vendored header expects, so the
# header stays unedited and a version bump keeps following upstream's copy.
set(RNL_UPSTREAM_FIXTURE_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/upstream-fixtures)
file(MAKE_DIRECTORY ${RNL_UPSTREAM_FIXTURE_INCLUDE_DIR})

if(NOT IS_SYMLINK ${RNL_UPSTREAM_FIXTURE_INCLUDE_DIR}/ReactCommon)
    file(CREATE_LINK ${REACT_COMMON_DIR}/callinvoker/ReactCommon/tests
        ${RNL_UPSTREAM_FIXTURE_INCLUDE_DIR}/ReactCommon SYMBOLIC)
endif()

# The upstream suites, each compiled unmodified from its upstream path — link, don't copy — so a version bump
# re-runs them at the new SHA. jsi/jsi/test/testlib.cpp is upstream's JSI conformance suite; the only source of
# ours in this binary is HermesRuntimeFactory.cpp, which supplies the runtimeGenerators() it declares but does
# not define, exactly as react-native-windows' JsiRuntimeGenerators.cpp does.
add_executable(rnl_core_hermes_tests
    HermesRuntimeFactory.cpp
    ${googletest_SOURCE_DIR}/googlemock/src/gmock_main.cc
    ${REACT_COMMON_DIR}/jsi/jsi/test/testlib.cpp
    ${REACT_COMMON_DIR}/react/bridging/tests/BridgingTest.cpp
    ${REACT_COMMON_DIR}/react/bridging/tests/ClassTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/EventQueueProcessorTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/EventTargetTests.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/RawPropsTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/RawValueTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/runtimescheduler/tests/RuntimeSchedulerTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/scheduler/tests/SchedulerDelegateInvalidationTest.cpp
    ${REACT_COMMON_DIR}/react/runtime/tests/cxx/ReactInstanceTest.cpp
    ${REACT_COMMON_DIR}/react/runtime/tests/cxx/RuntimeExecutorShutdownTest.cpp)
target_compile_options(rnl_core_hermes_tests PRIVATE -Wall -Wextra -fexceptions -frtti)
target_include_directories(rnl_core_hermes_tests
    PRIVATE ${REACT_COMMON_DIR}/jsi ${RNL_UPSTREAM_FIXTURE_INCLUDE_DIR})
# rnl_react_core rather than RNL_REACT_COMMON_TARGETS directly: the Linux TextLayoutManager and ImageManager
# swaps put our TextPipeline.cpp and ImagePipeline.cpp inside two of those upstream object libraries, and the
# rest of what those two call — ImageContent, JsErrorReporter — is in rnl_react_core.
target_link_libraries(rnl_core_hermes_tests
    PRIVATE
        rnl_hermes_googletest
        rnl_react_core
        react_renderer_element
        ${RNL_REACT_COMMON_TARGETS}
        atomic)
set_target_properties(rnl_core_hermes_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

gtest_discover_tests(rnl_core_hermes_tests)
