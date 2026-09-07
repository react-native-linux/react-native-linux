#include "WindowDecorations.h"

namespace react_native_linux {

namespace {

constexpr uint32_t kResizeEdgeNone = 0;
constexpr uint32_t kResizeEdgeTop = 1;
constexpr uint32_t kResizeEdgeBottom = 2;
constexpr uint32_t kResizeEdgeLeft = 4;
constexpr uint32_t kResizeEdgeTopLeft = 5;
constexpr uint32_t kResizeEdgeBottomLeft = 6;
constexpr uint32_t kResizeEdgeRight = 8;
constexpr uint32_t kResizeEdgeTopRight = 9;
constexpr uint32_t kResizeEdgeBottomRight = 10;

constexpr uint32_t kMinimumContentHeight = 1;

DecorationHit resizeHitOfEdges(bool nearLeft, bool nearRight, bool nearTop, bool nearBottom) noexcept {
    if (nearTop && nearLeft) {
        return DecorationHit::ResizeTopLeft;
    }

    if (nearTop && nearRight) {
        return DecorationHit::ResizeTopRight;
    }

    if (nearBottom && nearLeft) {
        return DecorationHit::ResizeBottomLeft;
    }

    if (nearBottom && nearRight) {
        return DecorationHit::ResizeBottomRight;
    }

    if (nearTop) {
        return DecorationHit::ResizeTop;
    }

    if (nearBottom) {
        return DecorationHit::ResizeBottom;
    }

    if (nearLeft) {
        return DecorationHit::ResizeLeft;
    }

    if (nearRight) {
        return DecorationHit::ResizeRight;
    }

    return DecorationHit::Content;
}

} // namespace

DecorationMode decideDecorationMode(bool hasDecorationManager, bool forceClientDecorations,
                                    std::optional<uint32_t> configuredMode) noexcept {
    if (forceClientDecorations) {
        return DecorationMode::Client;
    }

    if (!hasDecorationManager) {
        return DecorationMode::Client;
    }

    if (configuredMode == kDecorationModeClientSide) {
        return DecorationMode::Client;
    }

    return DecorationMode::Server;
}

TitleBarLayout layoutTitleBar(uint32_t windowWidth, const DecorationMetrics& metrics) noexcept {
    const float width = static_cast<float>(windowWidth);
    const float height = metrics.titleBarHeight;
    const float closeLeft = width - metrics.buttonWidth;
    const float maximizeLeft = closeLeft - metrics.buttonWidth;
    const float minimizeLeft = maximizeLeft - metrics.buttonWidth;

    return TitleBarLayout{
        .bar = DecorationRect{.left = 0.0F, .top = 0.0F, .right = width, .bottom = height},
        .drag = DecorationRect{.left = 0.0F, .top = 0.0F, .right = minimizeLeft, .bottom = height},
        .minimize = DecorationRect{.left = minimizeLeft, .top = 0.0F, .right = maximizeLeft, .bottom = height},
        .maximize = DecorationRect{.left = maximizeLeft, .top = 0.0F, .right = closeLeft, .bottom = height},
        .close = DecorationRect{.left = closeLeft, .top = 0.0F, .right = width, .bottom = height},
    };
}

DecorationHit hitTestDecorations(const DecorationMetrics& metrics, uint32_t windowWidth, uint32_t windowHeight, float x,
                                 float y) noexcept {
    const float width = static_cast<float>(windowWidth);
    const float height = static_cast<float>(windowHeight);
    const DecorationHit edgeHit = resizeHitOfEdges(x < metrics.resizeEdgeWidth, x >= width - metrics.resizeEdgeWidth,
                                                   y < metrics.resizeEdgeWidth, y >= height - metrics.resizeEdgeWidth);

    if (edgeHit != DecorationHit::Content) {
        return edgeHit;
    }

    const TitleBarLayout layout = layoutTitleBar(windowWidth, metrics);

    if (y >= layout.bar.bottom) {
        return DecorationHit::Content;
    }

    // The three buttons are contiguous and flush against the right edge, so their left boundaries alone order
    // the bar: anything left of the leftmost of them is the drag region, however narrow the window has made it.
    if (x >= layout.close.left) {
        return DecorationHit::Close;
    }

    if (x >= layout.maximize.left) {
        return DecorationHit::Maximize;
    }

    if (x >= layout.minimize.left) {
        return DecorationHit::Minimize;
    }

    return DecorationHit::Drag;
}

uint32_t resizeEdgeOfHit(DecorationHit hit) noexcept {
    switch (hit) {
    case DecorationHit::ResizeTop:
        return kResizeEdgeTop;
    case DecorationHit::ResizeBottom:
        return kResizeEdgeBottom;
    case DecorationHit::ResizeLeft:
        return kResizeEdgeLeft;
    case DecorationHit::ResizeRight:
        return kResizeEdgeRight;
    case DecorationHit::ResizeTopLeft:
        return kResizeEdgeTopLeft;
    case DecorationHit::ResizeTopRight:
        return kResizeEdgeTopRight;
    case DecorationHit::ResizeBottomLeft:
        return kResizeEdgeBottomLeft;
    case DecorationHit::ResizeBottomRight:
        return kResizeEdgeBottomRight;
    default:
        return kResizeEdgeNone;
    }
}

ContentExtent contentExtentOf(DecorationMode mode, const DecorationMetrics& metrics, uint32_t windowWidth,
                              uint32_t windowHeight) noexcept {
    const float topOffset = mode == DecorationMode::Client ? metrics.titleBarHeight : 0.0F;
    const uint32_t barRows = static_cast<uint32_t>(topOffset);

    return ContentExtent{
        .width = windowWidth,
        .height = windowHeight > barRows ? windowHeight - barRows : kMinimumContentHeight,
        .topOffset = topOffset,
    };
}

bool DoubleClickDetector::recordPress(uint64_t milliseconds) noexcept {
    const bool isDoubleClick =
        previousPress_.has_value() && milliseconds - previousPress_.value() <= kDoubleClickIntervalMilliseconds;

    previousPress_ = isDoubleClick ? std::nullopt : std::optional<uint64_t>(milliseconds);

    return isDoubleClick;
}

} // namespace react_native_linux
