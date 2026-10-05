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

# #85: rn-tester's NativeCxxModuleExample, upstream's own C++ TurboModule over the module type surface (enums,
# unions, nested and recursive objects, maps, sets, ArrayBuffers, callbacks, promises, event emitters), with its
# spec, implementation and GoogleTest suite. rn-tester is outside the sparse checkout of third_party/react-native,
# so the four files are fetched at the vendored tag and pinned by hash: a React Native bump fails here until the
# hashes are re-read. The spec runs through @react-native/codegen at build time, so the generated header always
# matches the pinned generator and spec.
file(READ ${CMAKE_SOURCE_DIR}/scripts/vendor.lock.json RNL_VENDOR_LOCK)
string(JSON RNL_REACT_NATIVE_TAG GET "${RNL_VENDOR_LOCK}" tag)
set(RNL_CXX_MODULE_EXAMPLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/upstream-fixtures/NativeCxxModuleExample)
set(RNL_CXX_MODULE_EXAMPLE_FILES
    "NativeCxxModuleExample.js=fd2b26e6de736303db8c98011b3b8e1c3849b02848d3e7ccb27e2804f3b3c741"
    "NativeCxxModuleExample.h=77c99fa77785cf1b440934c2d9d0be49b73461a081e5b5f3ddfb2b3b628aecc8"
    "NativeCxxModuleExample.cpp=aeb2d61de3acba0712c256dce3bd606c3c5cd1e6d01ad55381d67bb9429adc29"
    "tests/NativeCxxModuleExampleTests.cpp=135f973b48865cd8a9d3b1ae041e484cc89531cd187fdb6d2ac572a6ed73e33b")

foreach(RNL_FILE_AND_HASH IN LISTS RNL_CXX_MODULE_EXAMPLE_FILES)
    string(REPLACE "=" ";" RNL_FILE_AND_HASH "${RNL_FILE_AND_HASH}")
    list(GET RNL_FILE_AND_HASH 0 RNL_FILE)
    list(GET RNL_FILE_AND_HASH 1 RNL_HASH)
    file(DOWNLOAD
        https://raw.githubusercontent.com/facebook/react-native/${RNL_REACT_NATIVE_TAG}/packages/rn-tester/NativeCxxModuleExample/${RNL_FILE}
        ${RNL_CXX_MODULE_EXAMPLE_DIR}/${RNL_FILE}
        EXPECTED_HASH SHA256=${RNL_HASH})
endforeach()

find_program(RNL_NODE node REQUIRED)
add_custom_command(
    OUTPUT ${RNL_CXX_MODULE_EXAMPLE_DIR}/AppSpecsJSI.h
    COMMAND ${RNL_NODE} ${CMAKE_SOURCE_DIR}/scripts/codegen-cxx-module.ts
        ${RNL_CXX_MODULE_EXAMPLE_DIR}/NativeCxxModuleExample.js AppSpecs ${RNL_CXX_MODULE_EXAMPLE_DIR}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/codegen-cxx-module.ts ${RNL_CXX_MODULE_EXAMPLE_DIR}/NativeCxxModuleExample.js
    COMMENT "Generating AppSpecsJSI.h from rn-tester's NativeCxxModuleExample spec"
    VERBATIM)

# TurboModuleTestFixture.h is included as <ReactCommon/TurboModuleTestFixture.h> but lives in
# nativemodule/core/tests/, so it gets the same configure-time symlink treatment as TestCallInvoker.h below.
set(RNL_TURBO_MODULE_FIXTURE_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/upstream-turbomodule-fixture)
file(MAKE_DIRECTORY ${RNL_TURBO_MODULE_FIXTURE_INCLUDE_DIR})

if(NOT IS_SYMLINK ${RNL_TURBO_MODULE_FIXTURE_INCLUDE_DIR}/ReactCommon)
    file(CREATE_LINK ${REACT_COMMON_DIR}/react/nativemodule/core/tests
        ${RNL_TURBO_MODULE_FIXTURE_INCLUDE_DIR}/ReactCommon SYMBOLIC)
endif()

# The upstream suites, each compiled unmodified from its upstream path — link, don't copy — so a version bump
# re-runs them at the new SHA. jsi/jsi/test/testlib.cpp is upstream's JSI conformance suite; the only source of
# ours in this binary is HermesRuntimeFactory.cpp, which supplies the runtimeGenerators() it declares but does
# not define, exactly as react-native-windows' JsiRuntimeGenerators.cpp does.
# #210: the Fantom-style headless runner and the mount-tree text its cases assert on. Both are compiled here
# rather than linked from rnl_react_core because the runner exists only for tests, and rnl_react_core is what
# rnl_window links.
add_executable(rnl_core_hermes_tests
    CrossThreadPromiseStressTest.cpp
    FantomTesterTest.cpp
    HermesRuntimeFactory.cpp
    CxxModuleTypeSurfaceTest.cpp
    InstanceTeardownLeakTest.cpp
    WorkletsVersionTest.cpp
    ${RNL_CXX_MODULE_EXAMPLE_DIR}/AppSpecsJSI.h
    ${RNL_CXX_MODULE_EXAMPLE_DIR}/NativeCxxModuleExample.cpp
    ${RNL_CXX_MODULE_EXAMPLE_DIR}/tests/NativeCxxModuleExampleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/FantomTester.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/MountTreeText.cpp
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
    PRIVATE ${REACT_COMMON_DIR}/jsi ${RNL_UPSTREAM_FIXTURE_INCLUDE_DIR} ${RNL_TURBO_MODULE_FIXTURE_INCLUDE_DIR})
# rnl_react_core rather than RNL_REACT_COMMON_TARGETS directly: the Linux TextLayoutManager and ImageManager
# swaps put our TextPipeline.cpp and ImagePipeline.cpp inside two of those upstream object libraries, and the
# rest of what those two call — ImageContent, JsErrorReporter — is in rnl_react_core.
target_link_libraries(rnl_core_hermes_tests
    PRIVATE
        rnl_hermes_googletest
        rnl_react_core
        rnl_worklets
        react_renderer_element
        ${RNL_REACT_COMMON_TARGETS}
        atomic)
set_target_properties(rnl_core_hermes_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(rnl_core_hermes_tests
    PRIVATE "RNL_WORKLETS_PACKAGE_JSON=\"${RNL_WORKLETS_DIR}/package.json\"")

# Two things exist only where rnl_react_core links Skia, and the sanitizer presets build without it: the decoder
# behind CrossThreadMountingStressTest's image-decode leg, and the text pipeline ResizeTextCostTest shapes through.
if(TARGET rnl_skia)
    target_compile_definitions(rnl_core_hermes_tests PRIVATE RNL_ENABLE_IMAGES)
    target_sources(rnl_core_hermes_tests PRIVATE ResizeTextCostTest.cpp)
endif()

# Two upstream cases are not discovered in the TSan configure, and only there; both run in dev, in asan and
# locally. Each is a filter on one named case rather than a sanitizer suppression file, of which this
# repository has none. Delete each when the React Native pin moves past a version that has fixed it.
#
#   #383 SetRuntimeData: a lock-order inversion, M0 => M1 => M0, between the process-global JSI runtime-data
#        mutex in ReactCommon/jsi/jsi/jsi.cpp and Hermes' HadesGC mutex.
#   #384 JSErrorStackOverflowHandling: the case recurses through a host function that re-enters JS, so the
#        native stack grows with the JS one; under TSan the frames are large enough that the real stack blows
#        before Hermes' native-stack guard fires, and the process wedges on ThreadSanitizer's own
#        stack-overflow DEADLYSIGNAL instead of exiting. It hangs until the timeout rather than failing.
#   #393 highResTimeStampTest: round-trips HighResTimeStamp::now() through a double of milliseconds and
#        back through static_cast<int64_t>(units * 1e6), which truncates; whenever the product lands a hair
#        below the integer it started from, the nanosecond is lost, which is about one timestamp in five at
#        any uptime.
#   #395 immediateTaskYieldsToSynchronousAccess and normalTaskYieldsToSynchronousAccess: each starts a real
#        thread that requests synchronous access and lines it up with the scheduler's yield check by sleeping,
#        so on a loaded runner whichever side wins decides what the stub queue holds at the assertion.
#   #511 VoidFunEmitsEvents: upstream's case static_pointer_casts the generated AsyncEventEmitter<jsi::Value> for
#        onClick to AsyncEventEmitter<std::string>, which UBSan reports as an invalid downcast. What it checks is
#        covered through JSI by CxxModuleTypeSurfaceTest.
#
# All are vendored and none is reachable from anything this platform calls.
set(RNL_HERMES_TESTS_FILTER
    "-BridgingTest.highResTimeStampTest:UseModernRuntimeScheduler/RuntimeSchedulerTest.*TaskYieldsToSynchronousAccess/*:NativeCxxModuleExampleTests.VoidFunEmitsEvents")

if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=thread")
    string(APPEND RNL_HERMES_TESTS_FILTER
        ":Runtimes/JSITest.SetRuntimeData/*:Runtimes/JSITest.JSErrorStackOverflowHandling/*")
endif()

gtest_discover_tests(rnl_core_hermes_tests TEST_FILTER "${RNL_HERMES_TESTS_FILTER}")
