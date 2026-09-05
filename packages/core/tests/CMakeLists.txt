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
# core and mounting need react/renderer/element (ComponentBuilder) and react/test_utils, both of which are
# compiled nowhere in our tree yet; they join the next tranche with their own target additions.

add_executable(rnl_core_tests
    AnimatedHitTestTest.cpp
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
    UpstreamAnimatedSuiteTest.cpp
    UpstreamFabricSuiteTest.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/AnimatedNodeTests.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/AnimationDriverTests.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/DecayAnimationDriverTest.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/EventAnimationDriverTests.cpp
    ${RNL_UPSTREAM_ANIMATED_TESTS_DIR}/ManagedPropsMountingOverrideTests.cpp
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
    ${RNL_UPSTREAM_CSS_TESTS}
    ${RNL_UPSTREAM_GRAPHICS_TESTS}
    ${RNL_UPSTREAM_TIMELINE_TESTS})

# core and mounting: the suites that need react/renderer/element's ComponentBuilder compile separately from the
# standalone ones so the element-linked half of the tranche is explicit.
# The Hermes-free target cannot take the core tests that include hermes/hermes.h for a runtime fixture
# (ConcreteShadowNode, EventQueueProcessor, EventTarget, FindNodeAtPoint, LayoutableShadowNode, RawProps,
# RawValue, ShadowNodeFamily); they join the jsi/jsinspector tranche later.
file(GLOB RNL_UPSTREAM_CORE_TESTS_STANDALONE
    ${REACT_COMMON_DIR}/react/renderer/core/tests/*Test.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/*Tests.cpp)
list(REMOVE_ITEM RNL_UPSTREAM_CORE_TESTS_STANDALONE
    ${REACT_COMMON_DIR}/react/renderer/core/tests/ConcreteShadowNodeTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/EventQueueProcessorTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/EventTargetTests.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/FindNodeAtPointTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/LayoutableShadowNodeTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/RawPropsTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/RawValueTest.cpp
    ${REACT_COMMON_DIR}/react/renderer/core/tests/ShadowNodeFamilyTest.cpp)
# mounting: StateReconciliationTest instantiates ModalHostView components (rrc_modal, not linked here), so it
# joins the next tranche with that module. The rest of the mounting suite is standalone.
file(GLOB RNL_UPSTREAM_MOUNTING_TESTS
    ${REACT_COMMON_DIR}/react/renderer/mounting/tests/*Test.cpp)
list(REMOVE_ITEM RNL_UPSTREAM_MOUNTING_TESTS
    ${REACT_COMMON_DIR}/react/renderer/mounting/tests/StateReconciliationTest.cpp)
file(GLOB RNL_UPSTREAM_VIEW_TESTS
    ${REACT_COMMON_DIR}/react/renderer/components/view/tests/*Test.cpp)
target_sources(rnl_core_tests PRIVATE
    ${RNL_UPSTREAM_CORE_TESTS_STANDALONE}
    ${RNL_UPSTREAM_MOUNTING_TESTS}
    ${RNL_UPSTREAM_VIEW_TESTS})
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
