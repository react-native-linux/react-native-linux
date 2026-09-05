#The GoogleTest binary for @react - native - linux / core.
#
#This directory is reached only from RNL_BUILD_TESTS, which is also what keeps Hermes out of
#the configure : Hermes' bundled llvh defines the `gtest` and `gtest_main` target names
#unconditionally, so upstream googletest has nowhere to go in the default build.See
#docs / cpp - toolchain.md.
#
#The sources under test are compiled into the test executable rather than linked from
#rnl_react_core, because rnl_react_core is a Hermes - linked target and because the coverage
#instrumentation below then applies to exactly the files the coverage gate scores.

#Pinned by commit rather than by tag : a tag is a mutable ref, and this is the only dependency
#in the build that is fetched purely to run tests.
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

# #132 : upstream's own react/renderer/animated tests, run unmodified as a drift oracle. The tree
#is already inside the packages / react - native / ReactCommon sparse path scripts / vendor.lock.json
#lists, so no sparse - path change was needed to reach it.See docs / cpp - toolchain.md, "Upstream
#animated tests(#132) ".
set(RNL_UPSTREAM_ANIMATED_TESTS_DIR ${REACT_COMMON_DIR}/react/renderer/animated/tests)

# #211 : upstream 's own Fabric conformance suites, compiled unmodified from their upstream paths — link, don' t
#copy — so a version bump re - runs them at the new SHA and a test we can no longer satisfy fails the bump.
#Per - module inclusion list, reviewed : css first(18 files, no platform deps), then the rest of the six - module
#tranche as each compiles.Excluded modules get a stated reason.
file(GLOB RNL_UPSTREAM_CSS_TESTS
    ${REACT_COMMON_DIR}/react/renderer/css/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_GRAPHICS_TESTS
    ${REACT_COMMON_DIR}/react/renderer/graphics/tests/*Test.cpp)
# featureflags EXCLUDED: its DynamicProviderTest asserts a fresh global flag-access state, and our platform's
# ReactNativeFeatureFlagsOverridesLinux plus our own FeatureFlagsTest legitimately mutate that state earlier in
# the binary. Upstream has no isolation hook between suites; revisit if upstream adds one.
file(GLOB RNL_UPSTREAM_TIMELINE_TESTS
    ${REACT_COMMON_DIR}/react/performance/timeline/tests/*Test.cpp)
# core and mounting need react/renderer/element (ComponentBuilder); see the target_sources block below,
# which links react_renderer_element for the whole upstream tranche.

add_executable(rnl_core_tests
    AnimatedHitTestTest.cpp
    AnimationFrameQueueTest.cpp
    AnimatedPropAllowlistTest.cpp
    AnimatedConformanceTest.cpp
    AnimatedPropsTest.cpp
    AnimationChoreographerTest.cpp
    AllocationCostTest.cpp
    BorderGeometryTest.cpp
    DimensionsTest.cpp
    SceneReuseTest.cpp
    ShadowTest.cpp
    EditorTest.cpp
    FeatureFlagsTest.cpp
    FocusTest.cpp
    FrameClockTest.cpp
    FrameTimingTest.cpp
    HitTestDifferentialTest.cpp
    ImageCacheTest.cpp
    ImageTest.cpp
    ImeTest.cpp
    InputTest.cpp
    LayoutConformanceTest.cpp
    LineBoxMetricsTest.cpp
    LinuxMountingManagerDiagnosticsTest.cpp
    LinuxMountingManagerPendingDamageTest.cpp
    SceneTest.cpp
    ScrollTest.cpp
    ShadowTreeCommitTest.cpp
    TextRasterizationPolicyTest.cpp
    ThreadingConformanceTest.cpp
    TextInputControllerTest.cpp
    ToplevelStateTest.cpp
    UpstreamTestDriftOracleTest.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/AnimatedNodeTests.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/AnimationDriverTests.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/DecayAnimationDriverTest.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/EventAnimationDriverTests.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/ManagedPropsMountingOverrideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/AnimationFrameQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Clipboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/DimensionsSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/EditorModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FocusModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FrameClock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FrameTiming.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/HostTimerRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ImageContent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/InputPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/LineBoxMetrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/LinuxAnimationChoreographer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/LinuxMountingManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/RetainedScene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ScrollController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ScrollEventCadence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ScrollPhysics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/TextInputComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/TextInputController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/TextInputV3State.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ToplevelState.cpp
    ${RNL_UPSTREAM_CSS_TESTS}
    ${RNL_UPSTREAM_GRAPHICS_TESTS}
    ${RNL_UPSTREAM_TIMELINE_TESTS})

# core and mounting: the suites that need react/renderer/element's ComponentBuilder compile separately from the
# standalone ones so the element-linked half of the tranche is explicit.
# The Hermes-free target cannot take the core tests that construct a real Runtime for a fixture and so include
# hermes/hermes.h (EventQueueProcessor, EventTarget, RawProps, RawValue); they join the jsi/jsinspector tranche
# later (#228). ConcreteShadowNode, FindNodeAtPoint, LayoutableShadowNode and ShadowNodeFamily include only
# react/renderer/element/testUtils.h and compile here (#227).
file(GLOB RNL_UPSTREAM_CORE_TESTS_STANDALONE
    ${REACT_COMMON_DIR}/react/renderer/core/tests/*Test.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/*Tests.cpp)
list(REMOVE_ITEM RNL_UPSTREAM_CORE_TESTS_STANDALONE
    ${REACT_COMMON_DIR}/react/renderer/core/tests/EventQueueProcessorTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/EventTargetTests.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/RawPropsTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/RawValueTest.cpp)
# mounting: StateReconciliationTest instantiates ModalHostView components, and rrc_modal is linked
# (RNL_REACT_COMMON_TARGETS), so the whole mounting suite is standalone (#227).
file(GLOB RNL_UPSTREAM_MOUNTING_TESTS
    ${REACT_COMMON_DIR}/react/renderer/mounting/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_VIEW_TESTS
    ${REACT_COMMON_DIR}/react/renderer/components/view/tests/*Test.cpp)

# #227: the rest of the "no new code" tranche section B's first table names — every dependency is already in
# RNL_REACT_COMMON_TARGETS or RNL_REACT_CXX_PLATFORM_SUBDIRS, so each is a source-list addition and nothing
# else. runtimescheduler takes only SchedulerPriorityTest.cpp: RuntimeSchedulerTest.cpp in that same directory
# includes hermes/hermes.h and stays with the jsi tranche (#228).
file(GLOB RNL_UPSTREAM_SCROLLVIEW_TESTS
    ${REACT_COMMON_DIR}/react/renderer/components/scrollview/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_ROOT_TESTS
    ${REACT_COMMON_DIR}/react/renderer/components/root/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_TEXT_TESTS
    ${REACT_COMMON_DIR}/react/renderer/components/text/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_IMAGE_COMPONENT_TESTS
    ${REACT_COMMON_DIR}/react/renderer/components/image/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_ATTRIBUTEDSTRING_TESTS
    ${REACT_COMMON_DIR}/react/renderer/attributedstring/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_TEXTLAYOUTMANAGER_TESTS
    ${REACT_COMMON_DIR}/react/renderer/textlayoutmanager/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_IMAGEMANAGER_TESTS
    ${REACT_COMMON_DIR}/react/renderer/imagemanager/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_UIMANAGER_TESTS
    ${REACT_COMMON_DIR}/react/renderer/uimanager/tests/*Test.cpp)
# PointerEventsProcessorTest.cpp EXCLUDED: it compiles and links, but every case aborts at runtime on
# std::__shared_ptr_deref<ContextContainer>'s null-pointer assertion — its fixture's UIManager is built
# without the ContextContainer a Runtime-backed host would supply. FabricUIManagerTest and
# FindShadowNodeByTagTest in the same directory pass and stay.
list(REMOVE_ITEM RNL_UPSTREAM_UIMANAGER_TESTS
    ${REACT_COMMON_DIR}/react/renderer/uimanager/tests/PointerEventsProcessorTest.cpp)
file(GLOB RNL_UPSTREAM_UIMANAGER_CONSISTENCY_TESTS
    ${REACT_COMMON_DIR}/react/renderer/uimanager/consistency/tests/*Test.cpp)
set(RNL_UPSTREAM_SCHEDULER_PRIORITY_TEST
    ${REACT_COMMON_DIR}/react/renderer/runtimescheduler/tests/SchedulerPriorityTest.cpp)
file(GLOB RNL_UPSTREAM_TELEMETRY_TESTS
    ${REACT_COMMON_DIR}/react/renderer/telemetry/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_ELEMENT_TESTS
    ${REACT_COMMON_DIR}/react/renderer/element/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_MAPBUFFER_TESTS
    ${REACT_COMMON_DIR}/react/renderer/mapbuffer/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_RENDERER_DEBUG_TESTS
    ${REACT_COMMON_DIR}/react/renderer/debug/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_UTILS_TESTS
    ${REACT_COMMON_DIR}/react/utils/tests/*Test.cpp
    ${REACT_COMMON_DIR}/react/utils/tests/*Tests.cpp)
# UuidTest.cpp EXCLUDED: it includes gmock/gmock.h, and BUILD_GMOCK is OFF above; the system gmock at
# /usr/include predates the pinned googletest 1.18.0 and fails outright (StringLike renamed, MutexBase
# retrenched). It has no other file in the directory forcing the same fate.
list(REMOVE_ITEM RNL_UPSTREAM_UTILS_TESTS
    ${REACT_COMMON_DIR}/react/utils/tests/UuidTest.cpp)
file(GLOB RNL_UPSTREAM_TIMING_TESTS
    ${REACT_COMMON_DIR}/react/timing/tests/*Test.cpp)
file(GLOB RNL_UPSTREAM_CXXREACT_TESTS
    ${REACT_COMMON_DIR}/cxxreact/tests/*.cpp)
file(GLOB RNL_UPSTREAM_JSERRORHANDLER_TESTS
    ${REACT_COMMON_DIR}/jserrorhandler/tests/*Test.cpp)
# jsinspector-modern/tracing/tests EXCLUDED: all three files (ProfileTreeNode, RuntimeSamplingProfileTrace-
# EventSerializer, TimeWindowedBuffer) include gmock/gmock.h unconditionally, and BUILD_GMOCK is OFF above.
set(RNL_UPSTREAM_TASK_DISPATCH_THREAD_TESTS
    ${REACT_CXX_PLATFORM_DIR}/react/threading/tests/TaskDispatchThreadTests.cpp)

target_sources(rnl_core_tests PRIVATE
    ${RNL_UPSTREAM_CORE_TESTS_STANDALONE}
    ${RNL_UPSTREAM_MOUNTING_TESTS}
    ${RNL_UPSTREAM_VIEW_TESTS}
    ${RNL_UPSTREAM_SCROLLVIEW_TESTS}
    ${RNL_UPSTREAM_ROOT_TESTS}
    ${RNL_UPSTREAM_TEXT_TESTS}
    ${RNL_UPSTREAM_IMAGE_COMPONENT_TESTS}
    ${RNL_UPSTREAM_ATTRIBUTEDSTRING_TESTS}
    ${RNL_UPSTREAM_TEXTLAYOUTMANAGER_TESTS}
    ${RNL_UPSTREAM_IMAGEMANAGER_TESTS}
    ${RNL_UPSTREAM_UIMANAGER_TESTS}
    ${RNL_UPSTREAM_UIMANAGER_CONSISTENCY_TESTS}
    ${RNL_UPSTREAM_SCHEDULER_PRIORITY_TEST}
    ${RNL_UPSTREAM_TELEMETRY_TESTS}
    ${RNL_UPSTREAM_ELEMENT_TESTS}
    ${RNL_UPSTREAM_MAPBUFFER_TESTS}
    ${RNL_UPSTREAM_RENDERER_DEBUG_TESTS}
    ${RNL_UPSTREAM_UTILS_TESTS}
    ${RNL_UPSTREAM_TIMING_TESTS}
    ${RNL_UPSTREAM_CXXREACT_TESTS}
    ${RNL_UPSTREAM_JSERRORHANDLER_TESTS}
    ${RNL_UPSTREAM_TASK_DISPATCH_THREAD_TESTS})
target_link_libraries(rnl_core_tests PRIVATE react_renderer_element)
target_compile_options(rnl_core_tests PRIVATE -Wall -Wextra -fexceptions -frtti)
target_include_directories(rnl_core_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${RNL_UPSTREAM_ANIMATED_TESTS_DIR})
target_compile_definitions(rnl_core_tests
    PRIVATE "RNL_ANIMATED_TESTS_DIR=\"${RNL_UPSTREAM_ANIMATED_TESTS_DIR}\""
    PRIVATE "RNL_UPSTREAM_REACT_COMMON_DIR=\"${REACT_COMMON_DIR}\"")
target_link_libraries(rnl_core_tests
    PRIVATE
        GTest::gtest_main
        jsi
        rnl_jsidynamic
        folly_runtime
        rnl_textinput
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
