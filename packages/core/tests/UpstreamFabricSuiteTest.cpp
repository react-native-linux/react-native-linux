#include "ShadowTreeTestSupport.h"

#include <gtest/gtest.h>

namespace {

/**
 * The drift oracle for the upstream Fabric suites (#211), generalizing the #132 pattern: the file list each
 * vendored module's tests directory contains is asserted here, so an upstream add or remove on a version bump
 * fails this test and forces the inclusion list in packages/core/tests/CMakeLists.txt to be reviewed rather
 * than silently changing what the suites cover. Upstream v0.87.1.
 */
TEST(UpstreamFabricSuiteTest, VendoredModuleFileListsMatchTheV0871FileList) {
    const std::filesystem::path reactCommonRoot{RNL_UPSTREAM_REACT_COMMON_DIR};

    // File lists of every regular file the directories hold (TestComponent.h is a header the tests
    // include), so an upstream add OR remove of any file trips the oracle.
    const std::vector<std::pair<std::filesystem::path, std::vector<std::string>>> expectedLists{
        {reactCommonRoot / "react/renderer/css/tests",
         {"CSSAngleTest.cpp", "CSSBackgroundImageTest.cpp", "CSSColorTest.cpp", "CSSFilterTest.cpp",
          "CSSFontVariantTest.cpp", "CSSKeywordTest.cpp", "CSSLengthPercentageTest.cpp", "CSSLengthTest.cpp",
          "CSSListTest.cpp", "CSSNumberLengthTest.cpp", "CSSNumberTest.cpp", "CSSRatioTest.cpp", "CSSShadowTest.cpp",
          "CSSSyntaxParserTest.cpp", "CSSTokenizerTest.cpp", "CSSTransformOriginTest.cpp", "CSSTransformTest.cpp",
          "CSSValueParserTest.cpp"}},
        {reactCommonRoot / "react/renderer/core/tests",
         {"ComponentDescriptorTest.cpp", "ConcreteShadowNodeTest.cpp", "DynamicPropsUtilitiesTest.cpp",
          "EventQueueProcessorTest.cpp", "EventTargetTests.cpp", "FindNodeAtPointTest.cpp",
          "LayoutableShadowNodeTest.cpp", "PrimitivesTest.cpp", "PropsConceptsTest.cpp", "RawPropsTest.cpp",
          "RawValueTest.cpp", "ShadowNodeFamilyTest.cpp", "ShadowNodeTest.cpp", "TestComponent.h"}},
        {reactCommonRoot / "react/renderer/mounting/tests",
         {"DifferentiatorUnflattenTest.cpp", "OrderIndexTest.cpp", "ShadowTreeLifeCycleTest.cpp",
          "ShadowTreeReactBranchingTest.cpp", "StackingContextTest.cpp", "StateReconciliationTest.cpp"}},
        {reactCommonRoot / "react/renderer/graphics/tests",
         {"ColorTest.cpp", "GraphicsTest.cpp", "PointTest.cpp", "TransformTest.cpp"}},
        {reactCommonRoot / "react/renderer/components/view/tests",
         {"ConversionsTest.cpp", "LayoutTest.cpp", "ResolveTransformTest.cpp", "ViewTest.cpp"}},
        {reactCommonRoot / "react/performance/timeline/tests",
         {"CircularBufferTest.cpp", "PerformanceEntryReporterTest.cpp", "PerformanceEntryTest.cpp",
          "PerformanceObserverTest.cpp"}},
    };

    for (const auto& [directory, expected] : expectedLists) {
        SCOPED_TRACE(directory.string());

        ASSERT_TRUE(std::filesystem::is_directory(directory));
        EXPECT_EQ(react_native_linux::sortedUpstreamFileNames(directory), expected);
    }
}

} // namespace
