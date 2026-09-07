#include "WindowDecorations.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using react_native_linux::ContentExtent;
using react_native_linux::contentExtentOf;
using react_native_linux::decideDecorationMode;
using react_native_linux::DecorationHit;
using react_native_linux::DecorationMetrics;
using react_native_linux::DecorationMode;
using react_native_linux::DecorationRect;
using react_native_linux::DoubleClickDetector;
using react_native_linux::hitTestDecorations;
using react_native_linux::kDecorationModeClientSide;
using react_native_linux::kDecorationModeServerSide;
using react_native_linux::kDoubleClickIntervalMilliseconds;
using react_native_linux::layoutTitleBar;
using react_native_linux::PointerCapture;
using react_native_linux::resizeEdgeOfHit;
using react_native_linux::TitleBarLayout;

constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
constexpr uint32_t kUnknownDecorationMode = 7;
constexpr DecorationMetrics kMetrics{};

struct ModeCase {
    std::string name;
    bool hasDecorationManager;
    bool forceClientDecorations;
    std::optional<uint32_t> configuredMode;
    DecorationMode expected;
};

TEST(WindowDecorationsTest, TheModeTableIsTheWholeNegotiation) {
    const std::vector<ModeCase> cases{
        {"no manager, nobody to ask", false, false, std::nullopt, DecorationMode::Client},
        {"no manager, and the flag agrees", false, true, std::nullopt, DecorationMode::Client},
        {"the flag overrides a server-side answer", true, true, kDecorationModeServerSide, DecorationMode::Client},
        {"the compositor answered client-side", true, false, kDecorationModeClientSide, DecorationMode::Client},
        {"the compositor answered server-side", true, false, kDecorationModeServerSide, DecorationMode::Server},
        {"the manager exists and has not answered", true, false, std::nullopt, DecorationMode::Server},
        {"an unrecognised answer stays server-side", true, false, kUnknownDecorationMode, DecorationMode::Server},
    };

    for (const ModeCase& modeCase : cases) {
        EXPECT_EQ(decideDecorationMode(modeCase.hasDecorationManager, modeCase.forceClientDecorations,
                                       modeCase.configuredMode),
                  modeCase.expected)
            << modeCase.name;
    }
}

TEST(WindowDecorationsTest, TheBarSpansTheWindowAndTheButtonsSitAgainstItsRightEdge) {
    const TitleBarLayout layout = layoutTitleBar(kWindowWidth, kMetrics);

    EXPECT_EQ(layout.bar, (DecorationRect{0.0F, 0.0F, 800.0F, 32.0F}));
    EXPECT_EQ(layout.drag, (DecorationRect{0.0F, 0.0F, 680.0F, 32.0F}));
    EXPECT_EQ(layout.minimize, (DecorationRect{680.0F, 0.0F, 720.0F, 32.0F}));
    EXPECT_EQ(layout.maximize, (DecorationRect{720.0F, 0.0F, 760.0F, 32.0F}));
    EXPECT_EQ(layout.close, (DecorationRect{760.0F, 0.0F, 800.0F, 32.0F}));
}

TEST(WindowDecorationsTest, ANarrowWindowHasNoDragRegionRatherThanOneUnderTheButtons) {
    const TitleBarLayout layout = layoutTitleBar(100, kMetrics);

    EXPECT_LT(layout.drag.right, layout.drag.left);
    EXPECT_EQ(hitTestDecorations(kMetrics, 100, kWindowHeight, 50.0F, 16.0F), DecorationHit::Maximize);
    EXPECT_EQ(hitTestDecorations(kMetrics, 100, kWindowHeight, 10.0F, 16.0F), DecorationHit::Minimize);
}

struct HitCase {
    std::string name;
    float x;
    float y;
    DecorationHit expected;
};

TEST(WindowDecorationsTest, TheHitTableCoversEveryEdgeCornerAndButton) {
    const std::vector<HitCase> cases{
        {"top left corner", 2.0F, 2.0F, DecorationHit::ResizeTopLeft},
        {"top right corner", 797.0F, 2.0F, DecorationHit::ResizeTopRight},
        {"bottom left corner", 2.0F, 597.0F, DecorationHit::ResizeBottomLeft},
        {"bottom right corner", 797.0F, 597.0F, DecorationHit::ResizeBottomRight},
        {"top edge", 400.0F, 2.0F, DecorationHit::ResizeTop},
        {"bottom edge", 400.0F, 597.0F, DecorationHit::ResizeBottom},
        {"left edge", 2.0F, 300.0F, DecorationHit::ResizeLeft},
        {"right edge", 797.0F, 300.0F, DecorationHit::ResizeRight},
        {"close", 780.0F, 20.0F, DecorationHit::Close},
        {"maximize", 740.0F, 20.0F, DecorationHit::Maximize},
        {"minimize", 700.0F, 20.0F, DecorationHit::Minimize},
        {"the drag region", 400.0F, 20.0F, DecorationHit::Drag},
        {"the first content row", 400.0F, 32.0F, DecorationHit::Content},
        {"the middle of the content", 400.0F, 300.0F, DecorationHit::Content},
    };

    for (const HitCase& hitCase : cases) {
        EXPECT_EQ(hitTestDecorations(kMetrics, kWindowWidth, kWindowHeight, hitCase.x, hitCase.y), hitCase.expected)
            << hitCase.name;
    }
}

TEST(WindowDecorationsTest, AResizeCornerWinsOverTheCloseButtonBeneathIt) {
    EXPECT_EQ(hitTestDecorations(kMetrics, kWindowWidth, kWindowHeight, 799.0F, 1.0F), DecorationHit::ResizeTopRight);
    EXPECT_EQ(hitTestDecorations(kMetrics, kWindowWidth, kWindowHeight, 780.0F, 9.0F), DecorationHit::Close);
}

TEST(WindowDecorationsTest, EveryHitMapsToItsXdgShellResizeEdge) {
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeTop), 1U);
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeBottom), 2U);
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeLeft), 4U);
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeTopLeft), 5U);
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeBottomLeft), 6U);
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeRight), 8U);
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeTopRight), 9U);
    EXPECT_EQ(resizeEdgeOfHit(DecorationHit::ResizeBottomRight), 10U);

    for (const DecorationHit hit : {DecorationHit::Content, DecorationHit::Drag, DecorationHit::Minimize,
                                    DecorationHit::Maximize, DecorationHit::Close}) {
        EXPECT_EQ(resizeEdgeOfHit(hit), 0U);
    }
}

TEST(WindowDecorationsTest, ServerSideDecorationsLeaveTheContentAtTheSurfaceOrigin) {
    const ContentExtent extent = contentExtentOf(DecorationMode::Server, kMetrics, kWindowWidth, kWindowHeight);

    EXPECT_EQ(extent.width, kWindowWidth);
    EXPECT_EQ(extent.height, kWindowHeight);
    EXPECT_EQ(extent.topOffset, 0.0F);
}

TEST(WindowDecorationsTest, ClientSideDecorationsPushTheContentBelowTheBar) {
    const ContentExtent extent = contentExtentOf(DecorationMode::Client, kMetrics, kWindowWidth, kWindowHeight);

    EXPECT_EQ(extent.width, kWindowWidth);
    EXPECT_EQ(extent.height, kWindowHeight - 32U);
    EXPECT_EQ(extent.topOffset, 32.0F);
}

TEST(WindowDecorationsTest, AWindowShorterThanItsOwnBarKeepsOneRowOfContent) {
    for (const uint32_t windowHeight : {1U, 32U}) {
        EXPECT_EQ(contentExtentOf(DecorationMode::Client, kMetrics, kWindowWidth, windowHeight).height, 1U);
    }
}

TEST(WindowDecorationsTest, TwoPressesInsideTheIntervalAreOneDoubleClick) {
    DoubleClickDetector detector;

    EXPECT_FALSE(detector.recordPress(1000));
    EXPECT_TRUE(detector.recordPress(1000 + kDoubleClickIntervalMilliseconds));
}

TEST(WindowDecorationsTest, APressBeyondTheIntervalStartsOver) {
    DoubleClickDetector detector;

    EXPECT_FALSE(detector.recordPress(1000));
    EXPECT_FALSE(detector.recordPress(1001 + kDoubleClickIntervalMilliseconds));
    EXPECT_TRUE(detector.recordPress(1001 + kDoubleClickIntervalMilliseconds));
}

TEST(WindowDecorationsTest, AThirdPressIsNotASecondDoubleClick) {
    DoubleClickDetector detector;

    EXPECT_FALSE(detector.recordPress(1000));
    EXPECT_TRUE(detector.recordPress(1100));
    EXPECT_FALSE(detector.recordPress(1200));
    EXPECT_TRUE(detector.recordPress(1300));
}

TEST(WindowDecorationsTest, APressInContentKeepsRoutingToContentThroughAReleaseOnTheBar) {
    PointerCapture capture;

    EXPECT_TRUE(capture.routeToContent(DecorationHit::Content, /*isPrimaryPress=*/true, /*isPrimaryRelease=*/false));
    EXPECT_TRUE(capture.routeToContent(DecorationHit::Drag, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/false));
    EXPECT_TRUE(capture.routeToContent(DecorationHit::Drag, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/true));

    // The release ended the capture, so the next press is free to land on the bar again.
    EXPECT_FALSE(capture.routeToContent(DecorationHit::Drag, /*isPrimaryPress=*/true, /*isPrimaryRelease=*/false));
}

TEST(WindowDecorationsTest, APressOnTheBarKeepsRoutingToChromeThroughAReleaseInContent) {
    PointerCapture capture;

    EXPECT_FALSE(capture.routeToContent(DecorationHit::Drag, /*isPrimaryPress=*/true, /*isPrimaryRelease=*/false));
    EXPECT_FALSE(capture.routeToContent(DecorationHit::Content, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/false));
    EXPECT_FALSE(capture.routeToContent(DecorationHit::Content, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/true));

    // The release ended the capture, so the next event is routed by its own hit test again.
    EXPECT_TRUE(capture.routeToContent(DecorationHit::Content, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/false));
}

TEST(WindowDecorationsTest, WithNoCaptureInProgressEveryEventRoutesByItsOwnHitTest) {
    PointerCapture capture;

    EXPECT_TRUE(capture.routeToContent(DecorationHit::Content, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/false));
    EXPECT_FALSE(capture.routeToContent(DecorationHit::Close, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/false));
}

} // namespace
