#include "ShadowTreeTestSupport.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class UpstreamSuiteLinkage { Linked, Excluded };

/**
 * One vendored `tests/` directory: its expected file list at v0.87.1, whether any file from it compiles into
 * `rnl_core_tests` today, and — when it does not, or does only in part — the reviewed reason, mirroring the
 * comment that states the same reason in packages/core/tests/CMakeLists.txt. `exclusionReason` is documentation
 * for a partially linked directory and a reviewed gate for a fully excluded one; the test below requires it
 * non-empty exactly when `linkage` is `Excluded`.
 */
struct UpstreamTestsDirectory {
    std::string relativePath; // relative to the vendored package root, e.g. "ReactCommon/react/renderer/css/tests"
    std::vector<std::string> expectedFiles;
    UpstreamSuiteLinkage linkage;
    std::string_view exclusionReason;
};

} // namespace

namespace {

/**
 * The drift oracle for every vendored `tests/` directory (#230, generalizing #132 and #211): `ReactCommon` holds
 * 36 and `ReactCxxPlatform` holds 3, and every one of the 39 is named here — the 26 whose files already compile
 * into `rnl_core_tests` (#132, #211, #227) and the 13 this table is what makes reviewed rather than invisible.
 * An upstream add or remove of a directory or a file in one fails this test, and #58's bump ritual reads this
 * file, not a memory of which directories exist. Upstream v0.87.1.
 */
TEST(UpstreamTestDriftOracleTest, VendoredTestsDirectoriesMatchTheV0871FileLists) {
    const std::filesystem::path reactCommonRoot{RNL_UPSTREAM_REACT_COMMON_DIR};
    const std::filesystem::path vendorRoot = reactCommonRoot.parent_path();

    // clang-format off
    static const std::vector<UpstreamTestsDirectory> directories{
        {"ReactCommon/callinvoker/ReactCommon/tests",
         {"TestCallInvoker.h"},
         UpstreamSuiteLinkage::Excluded,
         "a fixture header, not a test suite; consumed by the bridging and io tests E2 would host"},
        {"ReactCommon/cxxreact/tests",
         {"RecoverableErrorTest.cpp", "jsarg_helpers.cpp", "jsbigstring.cpp", "methodcall.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/jserrorhandler/tests",
         {"StackTraceParserTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/jsinspector-modern/tests",
         {"ConsoleApiTest.cpp", "ConsoleCreateTaskTest.cpp", "ConsoleTimeStampTest.cpp",
          "DebuggerSessionObserverTest.cpp", "FollyDynamicMatchers.cpp", "FollyDynamicMatchers.h", "GmockHelpers.h",
          "HostTargetTest.cpp", "InspectorMocks.h", "InspectorPackagerConnectionMultiSessionTest.cpp",
          "InspectorPackagerConnectionTest.cpp", "InspectorPackagerConnectionTest.h", "JsiIntegrationTest.cpp",
          "JsiIntegrationTest.h", "NetworkReporterTest.cpp", "ReactInstanceIntegrationTest.cpp",
          "ReactInstanceIntegrationTest.h", "ReactNativeMocks.cpp", "ReactNativeMocks.h", "TracingTest.cpp",
          "TracingTest.h", "UniquePtrFactory.h", "UniquePtrFactoryTest.cpp", "Utf8.cpp", "WeakListTest.cpp",
          "prelude.js.h"},
         UpstreamSuiteLinkage::Excluded,
         "needs gmock (BUILD_GMOCK is OFF) and, for JsiIntegrationTest, Hermes; the CDP and Fusebox drift guard E3 "
         "is the binary that would host it, with #79"},
        {"ReactCommon/jsinspector-modern/tracing/tests",
         {"ProfileTreeNodeTest.cpp", "RuntimeSamplingProfileTraceEventSerializerTest.cpp",
          "TimeWindowedBufferTest.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "all three files include gmock/gmock.h unconditionally, and BUILD_GMOCK is OFF"},
        {"ReactCommon/react/bridging/tests",
         {"BridgingTest.cpp", "BridgingTest.h", "ClassTest.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "BridgingTest.h includes hermes/hermes.h; joins the Hermes-linked binary E2 would add"},
        {"ReactCommon/react/debug/redbox/tests",
         {"AnsiParserTest.cpp", "JscSafeUrlTest.cpp", "RedBoxErrorParserTest.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "we have no RedBox; react/debug/redbox is not built. Revisit with the error surface (#56)"},
        {"ReactCommon/react/featureflags/tests",
         {"ReactNativeFeatureFlagsDynamicProviderTest.cpp", "ReactNativeFeatureFlagsTest.cpp",
          "test_rewrite_feature_flag_defaults.py"},
         UpstreamSuiteLinkage::Excluded,
         "ReactNativeFeatureFlagsDynamicProviderTest asserts a fresh global flag-access state, and our platform's "
         "ReactNativeFeatureFlagsOverridesLinux plus our own FeatureFlagsTest legitimately mutate that state "
         "earlier in the binary; upstream has no isolation hook between suites"},
        {"ReactCommon/react/nativemodule/core/tests",
         {"TurboModuleTestFixture.h"},
         UpstreamSuiteLinkage::Excluded,
         "a fixture header, not a test suite; consumed by the TurboModule tests E2 would host"},
        {"ReactCommon/react/performance/timeline/tests",
         {"CircularBufferTest.cpp", "PerformanceEntryReporterTest.cpp", "PerformanceEntryTest.cpp",
          "PerformanceObserverTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/animated/tests",
         {"AnimatedNodeTests.cpp", "AnimationDriverTests.cpp", "AnimationTestsBase.h",
          "DecayAnimationDriverTest.cpp", "EventAnimationDriverTests.cpp", "ManagedPropsMountingOverrideTests.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/animations/tests",
         {"LayoutAnimationTest.cpp", "MutationComparatorTest.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "LayoutAnimation is undecided (#123, needs:decision) and react/renderer/animations is not built; link "
         "only if #123 decides to implement it"},
        {"ReactCommon/react/renderer/attributedstring/tests",
         {"AttributedStringBoxTest.cpp", "ParagraphAttributesTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/components/image/tests",
         {"ImageTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/components/root/tests",
         {"RootShadowNodeTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/components/scrollview/tests",
         {"ScrollViewTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/components/text/tests",
         {"BaseTextShadowNodeTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/components/view/tests",
         {"ConversionsTest.cpp", "LayoutTest.cpp", "ResolveTransformTest.cpp", "ViewTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/core/tests",
         {"ComponentDescriptorTest.cpp", "ConcreteShadowNodeTest.cpp", "DynamicPropsUtilitiesTest.cpp",
          "EventQueueProcessorTest.cpp", "EventTargetTests.cpp", "FindNodeAtPointTest.cpp",
          "LayoutableShadowNodeTest.cpp", "PrimitivesTest.cpp", "PropsConceptsTest.cpp", "RawPropsTest.cpp",
          "RawValueTest.cpp", "ShadowNodeFamilyTest.cpp", "ShadowNodeTest.cpp", "TestComponent.h"},
         UpstreamSuiteLinkage::Linked,
         "10 of 14 files compile; EventQueueProcessorTest.cpp, EventTargetTests.cpp, RawPropsTest.cpp and "
         "RawValueTest.cpp construct a real Runtime and include hermes/hermes.h, joining E2. TestComponent.h is a "
         "shared header, not a compiled suite"},
        {"ReactCommon/react/renderer/css/tests",
         {"CSSAngleTest.cpp", "CSSBackgroundImageTest.cpp", "CSSColorTest.cpp", "CSSFilterTest.cpp",
          "CSSFontVariantTest.cpp", "CSSKeywordTest.cpp", "CSSLengthPercentageTest.cpp", "CSSLengthTest.cpp",
          "CSSListTest.cpp", "CSSNumberLengthTest.cpp", "CSSNumberTest.cpp", "CSSRatioTest.cpp", "CSSShadowTest.cpp",
          "CSSSyntaxParserTest.cpp", "CSSTokenizerTest.cpp", "CSSTransformOriginTest.cpp", "CSSTransformTest.cpp",
          "CSSValueParserTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/debug/tests",
         {"DebugStringConvertibleTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/element/tests",
         {"ElementTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/graphics/tests",
         {"ColorTest.cpp", "GraphicsTest.cpp", "PointTest.cpp", "TransformTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/imagemanager/tests",
         {"ImageManagerTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/mapbuffer/tests",
         {"MapBufferTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/mounting/tests",
         {"DifferentiatorUnflattenTest.cpp", "OrderIndexTest.cpp", "ShadowTreeLifeCycleTest.cpp",
          "ShadowTreeReactBranchingTest.cpp", "StackingContextTest.cpp", "StateReconciliationTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/runtimescheduler/tests",
         {"RuntimeSchedulerTest.cpp", "SchedulerPriorityTest.cpp", "StubClock.h", "StubErrorUtils.h",
          "StubQueue.h"},
         UpstreamSuiteLinkage::Linked,
         "only SchedulerPriorityTest.cpp compiles; RuntimeSchedulerTest.cpp needs Hermes for its real "
         "std::thread cross-thread scheduling and joins E2. StubClock.h, StubErrorUtils.h and StubQueue.h are its "
         "fixtures"},
        {"ReactCommon/react/renderer/scheduler/tests",
         {"SchedulerDelegateInvalidationTest.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "needs Hermes for its real std::thread cross-thread scheduling against a StubClock; joins E2, the #212 "
         "pattern"},
        {"ReactCommon/react/renderer/telemetry/tests",
         {"TransactionTelemetryTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/textlayoutmanager/tests",
         {"TextLayoutManagerTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/uimanager/consistency/tests",
         {"LazyShadowTreeRevisionConsistencyManagerTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/renderer/uimanager/tests",
         {"FabricUIManagerTest.cpp", "FindShadowNodeByTagTest.cpp", "PointerEventsProcessorTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         "PointerEventsProcessorTest.cpp compiles and links, but every case aborts at runtime on "
         "std::__shared_ptr_deref<ContextContainer>'s null-pointer assertion; its fixture's UIManager is built "
         "without the ContextContainer a Runtime-backed host would supply. FabricUIManagerTest and "
         "FindShadowNodeByTagTest pass and stay"},
        {"ReactCommon/react/runtime/tests",
         {},
         UpstreamSuiteLinkage::Excluded,
         "no direct files; the two Hermes-linked ReactInstanceTest and RuntimeExecutorShutdownTest suites live "
         "under runtime/tests/cxx/, which needs a Hermes-linked binary (E2)"},
        {"ReactCommon/react/timing/tests",
         {"PrimitivesTest.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
        {"ReactCommon/react/utils/tests",
         {"Base64Tests.cpp", "SimpleThreadSafeCacheTest.cpp", "UuidTest.cpp", "fnv1aTests.cpp",
          "hash_combineTests.cpp"},
         UpstreamSuiteLinkage::Linked,
         "UuidTest.cpp includes gmock/gmock.h and BUILD_GMOCK is OFF; the system gmock predates the pinned "
         "googletest 1.18.0 and fails outright"},
        {"ReactCommon/reactperflogger/fusebox/tests",
         {"FuseboxTracerTest.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "Fusebox is the DevTools side of #79; joins E3"},
        {"ReactCxxPlatform/react/io/tests",
         {"NetworkingModuleTests.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "needs Hermes; react/io is not built. Belongs with the Metro dev server work (#79)"},
        {"ReactCxxPlatform/react/profiling/tests",
         {"TimeSeriesTests.cpp"},
         UpstreamSuiteLinkage::Excluded,
         "react/profiling is not added as a CMake subdirectory yet; no technical blocker, an E1 follow-up"},
        {"ReactCxxPlatform/react/threading/tests",
         {".clang-tidy", "TaskDispatchThreadTests.cpp"},
         UpstreamSuiteLinkage::Linked,
         ""},
    };
    // clang-format on

    std::set<std::filesystem::path> expectedDirectories;

    for (const auto& directory : directories) {
        SCOPED_TRACE(directory.relativePath);
        const std::filesystem::path fullPath = vendorRoot / directory.relativePath;

        ASSERT_TRUE(std::filesystem::is_directory(fullPath));
        EXPECT_EQ(react_native_linux::sortedUpstreamFileNames(fullPath), directory.expectedFiles);

        if (directory.linkage == UpstreamSuiteLinkage::Excluded) {
            EXPECT_FALSE(directory.exclusionReason.empty())
                << "an excluded directory's exclusion must be a reviewed reason, not an unseen one";
        }

        expectedDirectories.insert(std::filesystem::path{directory.relativePath});
    }

    // The 39 directories above are exhaustive: an upstream add or remove of a whole tests/ directory fails here
    // even before any file-list assertion above would notice it.
    std::set<std::filesystem::path> actualDirectories;
    for (const std::string& subtree : {"ReactCommon", "ReactCxxPlatform"}) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 vendorRoot / subtree, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_directory() && entry.path().filename() == "tests") {
                actualDirectories.insert(std::filesystem::relative(entry.path(), vendorRoot));
            }
        }
    }

    EXPECT_EQ(actualDirectories, expectedDirectories);
}

} // namespace
