#include "WindowDecorations.h"

#include <cstdint>
#include <cstdlib>
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
using react_native_linux::logicalToSurface;
using react_native_linux::PointerCapture;
using react_native_linux::resizeEdgeOfHit;
using react_native_linux::surfaceToLogical;
using react_native_linux::TitleBarLayout;
using react_native_linux::WindowExtent;

constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
constexpr uint32_t kUnknownDecorationMode = 7;
constexpr DecorationMetrics kMetrics{};

struct ModeCase {
    std::string name;
    bool hasDecorationManager;
    bool forceClientDecorations;
    bool noDecorations;
    std::optional<uint32_t> configuredMode;
    DecorationMode expected;
};

TEST(WindowDecorationsTest, TheModeTableIsTheWholeNegotiation) {
    const std::vector<ModeCase> cases{
        {"no manager, nobody to ask", false, false, false, std::nullopt, DecorationMode::Client},
        {"no manager, and the flag agrees", false, true, false, std::nullopt, DecorationMode::Client},
        {"the flag overrides a server-side answer", true, true, false, kDecorationModeServerSide,
         DecorationMode::Client},
        {"the compositor answered client-side", true, false, false, kDecorationModeClientSide, DecorationMode::Client},
        {"the compositor answered server-side", true, false, false, kDecorationModeServerSide, DecorationMode::Server},
        {"the manager exists and has not answered", true, false, false, std::nullopt, DecorationMode::Server},
        {"an unrecognised answer stays server-side", true, false, false, kUnknownDecorationMode,
         DecorationMode::Server},
        {"no manager, and the rig asks for bare", false, false, true, std::nullopt, DecorationMode::Bare},
        {"a manager exists, but the rig asks for bare", true, false, true, std::nullopt, DecorationMode::Bare},
        {"the compositor answered server-side, but the rig asks for bare", true, false, true, kDecorationModeServerSide,
         DecorationMode::Bare},
        {"forced client wins over bare", true, true, true, std::nullopt, DecorationMode::Client},
    };

    for (const ModeCase& modeCase : cases) {
        EXPECT_EQ(decideDecorationMode(modeCase.hasDecorationManager, modeCase.forceClientDecorations,
                                       modeCase.noDecorations, modeCase.configuredMode),
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
    EXPECT_EQ(hitTestDecorations(kMetrics, 100, kWindowHeight, false, 50.0F, 16.0F), DecorationHit::Maximize);
    EXPECT_EQ(hitTestDecorations(kMetrics, 100, kWindowHeight, false, 10.0F, 16.0F), DecorationHit::Minimize);
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
        EXPECT_EQ(hitTestDecorations(kMetrics, kWindowWidth, kWindowHeight, false, hitCase.x, hitCase.y),
                  hitCase.expected)
            << hitCase.name;
    }
}

TEST(WindowDecorationsTest, AResizeCornerWinsOverTheCloseButtonBeneathIt) {
    EXPECT_EQ(hitTestDecorations(kMetrics, kWindowWidth, kWindowHeight, false, 799.0F, 1.0F),
              DecorationHit::ResizeTopRight);
    EXPECT_EQ(hitTestDecorations(kMetrics, kWindowWidth, kWindowHeight, false, 780.0F, 9.0F), DecorationHit::Close);
}

TEST(WindowDecorationsTest, ATiledWindowHasNoResizeEdgesButKeepsItsButtonsAndDragRegion) {
    for (const HitCase& hitCase : std::vector<HitCase>{
             // With the resize edges out of the way, a point that used to win as a corner handle falls through to
             // whatever is actually there: the bar's own drag region or button at the top, and plain content at
             // the bottom, where there is no bar to fall into.
             {"a one-time top left corner is drag, not a handle", 2.0F, 2.0F, DecorationHit::Drag},
             {"a one-time top right corner is close, not a handle", 797.0F, 2.0F, DecorationHit::Close},
             {"a one-time bottom right corner falls back to content", 797.0F, 597.0F, DecorationHit::Content},
             {"a one-time bottom left corner falls back to content", 2.0F, 597.0F, DecorationHit::Content},
             {"close still hits", 780.0F, 20.0F, DecorationHit::Close},
             {"the drag region still hits", 400.0F, 20.0F, DecorationHit::Drag},
         }) {
        EXPECT_EQ(hitTestDecorations(kMetrics, kWindowWidth, kWindowHeight, true, hitCase.x, hitCase.y),
                  hitCase.expected)
            << hitCase.name;
    }
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

struct ContentExtentCase {
    std::string name;
    DecorationMode mode;
    bool isFullscreen;
    uint32_t expectedHeight;
    float expectedTopOffset;
};

// Every row shares the same shape — a bar term that is either zero or the metrics' own `titleBarHeight` — so one
// table covers "which modes draw a bar" and "fullscreen zeroes it under every mode" together, rather than
// letting the near-identical `ContentExtent` assertions drift into jscpd-flagged copies of each other.
TEST(WindowDecorationsTest, ContentExtentDropsTheBarOutsideFloatingClientDecorations) {
    const std::vector<ContentExtentCase> cases{
        {"server-side decorations", DecorationMode::Server, false, kWindowHeight, 0.0F},
        {"client-side decorations", DecorationMode::Client, false, kWindowHeight - 32U, 32.0F},
        {"bare decorations", DecorationMode::Bare, false, kWindowHeight, 0.0F},
        {"client-side decorations, fullscreen", DecorationMode::Client, true, kWindowHeight, 0.0F},
        {"bare decorations, fullscreen", DecorationMode::Bare, true, kWindowHeight, 0.0F},
    };

    for (const ContentExtentCase& extentCase : cases) {
        const ContentExtent extent =
            contentExtentOf(extentCase.mode, extentCase.isFullscreen, kMetrics, kWindowWidth, kWindowHeight);

        EXPECT_EQ(extent.width, kWindowWidth) << extentCase.name;
        EXPECT_EQ(extent.height, extentCase.expectedHeight) << extentCase.name;
        EXPECT_EQ(extent.topOffset, extentCase.expectedTopOffset) << extentCase.name;
    }
}

TEST(WindowDecorationsTest, AWindowShorterThanItsOwnBarKeepsOneRowOfContent) {
    for (const uint32_t windowHeight : {1U, 32U}) {
        EXPECT_EQ(contentExtentOf(DecorationMode::Client, /*isFullscreen=*/false, kMetrics, kWindowWidth, windowHeight)
                      .height,
                  1U);
    }
}

struct LogicalConversionCase {
    std::string name;
    DecorationMode mode;
    bool isFullscreen;
    double scale;
    WindowExtent surface;
    WindowExtent logical;
};

// Every row round-trips: `surfaceToLogical(surface) == logical` and `logicalToSurface(logical) == surface`, over
// every scenario the acceptance names — no decorations and server-side decorations both collapse to the same
// zero-inset arithmetic path, exactly as the Electron comment this fixes says they should; maximised keeps the
// bar (this platform does not remove it, unlike fullscreen); tiled leaves the conversion untouched, because
// nothing about the inset changes when the compositor tiles a window, only what `hitTestDecorations` grants; and
// fullscreen drops the bar under either decoration mode.
TEST(WindowDecorationsTest, TheConversionPairRoundTripsOverEveryDecorationScenario) {
    const std::vector<LogicalConversionCase> cases{
        {"no decorations", DecorationMode::Server, false, 1.0, {800, 600}, {800, 600}},
        {"server-side decorations", DecorationMode::Server, false, 1.0, {800, 600}, {800, 600}},
        {"client-side decorations, floating", DecorationMode::Client, false, 1.0, {800, 632}, {800, 600}},
        {"client-side decorations, maximised (the bar stays)",
         DecorationMode::Client,
         false,
         1.0,
         {800, 632},
         {800, 600}},
        {"client-side decorations, tiled (the inset is unaffected)",
         DecorationMode::Client,
         false,
         1.0,
         {800, 632},
         {800, 600}},
        {"client-side decorations, fullscreen (the bar is gone)",
         DecorationMode::Client,
         true,
         1.0,
         {800, 600},
         {800, 600}},
        {"server-side decorations, fullscreen", DecorationMode::Server, true, 1.0, {800, 600}, {800, 600}},
        {"bare decorations (no bar to begin with)", DecorationMode::Bare, false, 1.0, {800, 600}, {800, 600}},
        {"bare decorations, fullscreen", DecorationMode::Bare, true, 1.0, {800, 600}, {800, 600}},
        {"client-side decorations at scale 1.25", DecorationMode::Client, false, 1.25, {800, 632}, {640, 480}},
        {"client-side decorations at scale 1.5", DecorationMode::Client, false, 1.5, {600, 482}, {400, 300}},
        {"server-side decorations at scale 1.25", DecorationMode::Server, false, 1.25, {1000, 750}, {800, 600}},
    };

    for (const LogicalConversionCase& conversionCase : cases) {
        EXPECT_EQ(surfaceToLogical(conversionCase.mode, conversionCase.isFullscreen, kMetrics, conversionCase.scale,
                                   conversionCase.surface),
                  conversionCase.logical)
            << conversionCase.name;
        EXPECT_EQ(logicalToSurface(conversionCase.mode, conversionCase.isFullscreen, kMetrics, conversionCase.scale,
                                   conversionCase.logical),
                  conversionCase.surface)
            << conversionCase.name;
    }
}

struct ScaleGridCase {
    std::string name;
    double scale;
    uint32_t surfaceExtent;
    bool isOnGrid;
};

// `surfaceToLogical`/`logicalToSurface` round-trip exactly only when the surface extent divides evenly by the
// scale's numerator in lowest terms (3 at 1.5, 5 at 1.25, 2 at 2 — see the docblock), and are bounded to at most
// one surface pixel of error everywhere else. Server-side decorations keep `barRows` at zero so this isolates the
// scale arithmetic from the bar term the other round-trip test already covers.
TEST(WindowDecorationsTest, TheConversionPairIsExactOnTheScaleGridAndBoundedByOnePixelOffIt) {
    const std::vector<ScaleGridCase> cases{
        {"scale 1, always on its own grid", 1.0, 801, true},
        {"scale 1.25, on grid (multiple of 5)", 1.25, 800, true},
        {"scale 1.25, off grid", 1.25, 802, false},
        {"scale 1.5, on grid (multiple of 3)", 1.5, 999, true},
        {"scale 1.5, off grid (the finding's own example)", 1.5, 1000, false},
        {"scale 2, on grid (multiple of 2)", 2.0, 800, true},
        {"scale 2, off grid", 2.0, 801, false},
    };

    for (const ScaleGridCase& gridCase : cases) {
        const WindowExtent surface{gridCase.surfaceExtent, gridCase.surfaceExtent};
        const WindowExtent logical = surfaceToLogical(DecorationMode::Server, /*isFullscreen=*/false, kMetrics,
                                                       gridCase.scale, surface);
        const WindowExtent roundTripped =
            logicalToSurface(DecorationMode::Server, /*isFullscreen=*/false, kMetrics, gridCase.scale, logical);

        if (gridCase.isOnGrid) {
            EXPECT_EQ(roundTripped, surface) << gridCase.name;
        } else {
            EXPECT_NE(roundTripped, surface) << gridCase.name << ": expected this case to demonstrate rounding drift";
        }

        EXPECT_LE(std::abs(static_cast<int64_t>(roundTripped.width) - static_cast<int64_t>(surface.width)), 1)
            << gridCase.name;
        EXPECT_LE(std::abs(static_cast<int64_t>(roundTripped.height) - static_cast<int64_t>(surface.height)), 1)
            << gridCase.name;
    }
}

TEST(WindowDecorationsTest, ASurfaceShorterThanItsOwnBarKeepsOneLogicalRowOfContentToo) {
    for (const uint32_t surfaceHeight : {1U, 32U}) {
        EXPECT_EQ(surfaceToLogical(DecorationMode::Client, /*isFullscreen=*/false, kMetrics, 1.0,
                                   WindowExtent{kWindowWidth, surfaceHeight})
                      .height,
                  1U);
    }
}

TEST(WindowDecorationsTest, AZeroLogicalMaximumIsNotInflatedByTheBarOrTheScale) {
    for (const DecorationMode mode : {DecorationMode::Server, DecorationMode::Client, DecorationMode::Bare}) {
        EXPECT_EQ(logicalToSurface(mode, /*isFullscreen=*/false, kMetrics, 1.25, WindowExtent{0, 0}),
                  (WindowExtent{0, 0}))
            << "both components zero";
        EXPECT_EQ(logicalToSurface(mode, /*isFullscreen=*/false, kMetrics, 1.0, WindowExtent{800, 0}),
                  (WindowExtent{800, 0}))
            << "zero height leaves width inflated alone and adds no bar";
    }

    // A zero width with a real height still inflates the height by whatever the bar is under that mode: the
    // "leave it at zero" rule is per-component, not "the whole size is a sentinel", because a real
    // `set_max_size` can legitimately constrain only one axis.
    EXPECT_EQ(logicalToSurface(DecorationMode::Server, /*isFullscreen=*/false, kMetrics, 1.0, WindowExtent{0, 600}),
              (WindowExtent{0, 600}));
    EXPECT_EQ(logicalToSurface(DecorationMode::Client, /*isFullscreen=*/false, kMetrics, 1.0, WindowExtent{0, 600}),
              (WindowExtent{0, 632}));
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

struct ReleaseAcrossModeChangeCase {
    std::string name;
    DecorationHit capturingHit;
};

/**
 * `refreshChrome` calls `release()` exactly when `decorationMode()` disagrees with the chrome's own last-seen
 * mode — Client, then Server, per #399's follow-up — because a capture `routeToContent` took under `Client` is
 * otherwise still there when the compositor hands decorating back, and the first press after the switch would be
 * routed by a hit test the pointer never made rather than its own.
 */
TEST(WindowDecorationsTest, ReleaseDropsTheCaptureABareOrServerModeWouldOtherwiseInherit) {
    const std::vector<ReleaseAcrossModeChangeCase> cases{
        {"a capture that had landed in content", DecorationHit::Content},
        {"a capture that had landed on the chrome", DecorationHit::Drag},
    };

    for (const ReleaseAcrossModeChangeCase& releaseCase : cases) {
        PointerCapture capture;

        // Client: a primary press starts a capture.
        capture.routeToContent(releaseCase.capturingHit, /*isPrimaryPress=*/true, /*isPrimaryRelease=*/false);

        // Server: refreshChrome sees decorationMode() disagree with the chrome's mode and releases the capture.
        capture.release();

        // Client: the dropped capture no longer overrides the next event's own hit test, in either direction.
        EXPECT_TRUE(capture.routeToContent(DecorationHit::Content, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/false))
            << releaseCase.name;
        EXPECT_FALSE(capture.routeToContent(DecorationHit::Close, /*isPrimaryPress=*/false, /*isPrimaryRelease=*/false))
            << releaseCase.name;
    }
}

} // namespace
