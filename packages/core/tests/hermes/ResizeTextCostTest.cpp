#include "FabricHost.h"
#include "ReactHost.h"
#include "TextGeometry.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>

namespace react_native_linux {
namespace {

constexpr int kConfigureCount = 20;
constexpr float kWidthStep = 7.0F;
constexpr facebook::react::Size kInitialSize{.width = 400, .height = 300};
constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

// Three paragraphs whose width never depends on the window, and three that take half of it. All are set at 6
// points in boxes narrower than their text, wrapped or ellipsised: the sizes that broke macOS's measure cache
// (#42). Only the three that take half the window are resized.
constexpr int kResizedParagraphCount = 3;

constexpr char kResizeFixture[] = R"JAVASCRIPT(
const fabric = globalThis.nativeFabricUIManager;
const handles = [];
let tag = 1;
const node = (componentName, props, children = []) => {
  const handle = {};
  handles.push(handle);
  tag += 1;
  const created = fabric.createNode(tag, componentName, 1, props, handle);
  for (const child of children) {
    fabric.appendChild(created, child);
  }
  return created;
};
const prose = 'One line of prose long enough that no box on this surface can hold all of it at once, or twice.';
const paragraph = (props) => node('Paragraph', { fontSize: 6, ...props }, [node('RawText', { text: prose })]);
const container = node('View', { flex: 1 }, [
  paragraph({ width: 120, numberOfLines: 1, ellipsizeMode: 'tail' }),
  paragraph({ width: 60, numberOfLines: 3 }),
  paragraph({ width: 40, numberOfLines: 1, ellipsizeMode: 'middle' }),
  paragraph({ width: '50%', numberOfLines: 1, ellipsizeMode: 'tail' }),
  paragraph({ width: '50%', numberOfLines: 2 }),
  paragraph({ width: '50%' }),
]);
const children = fabric.createChildSet();
fabric.appendChildToSet(children, container);
fabric.completeRoot(1, children);
)JAVASCRIPT";

/**
 * Issue #42: a window dragged through `kConfigureCount` sizes, each a `setSurfaceSize` — the configure path,
 * which lays out and mounts on the calling thread — followed by the frame that paints it. A configure shapes at
 * most the paragraphs whose width it changed, once each; the ones whose width it did not change are never shaped
 * again, because Yoga does not re-measure a node whose constraints are unchanged. Every configure leaves exactly
 * one frame of work, and once the drag stops nothing asks for another.
 */
TEST(ResizeTextCostTest, AConfigureSequenceShapesOnlyResizedTextAndSettlesInOneFrame) {
    ReactHost reactHost;
    auto fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), kInitialSize);

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kResizeFixture), "ResizeTextCostTest.js");
    reactHost.drainJavaScriptThread();
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    static_cast<void>(fabricHost->takeFrame());

    const uint64_t shapesBefore = paragraphLayoutCount();
    uint64_t mostShapesInOneConfigure = 0;
    int configuresLeavingWork = 0;

    for (int configure = 1; configure <= kConfigureCount; ++configure) {
        const uint64_t configureShapesBefore = paragraphLayoutCount();

        fabricHost->setSurfaceSize(facebook::react::Size{
            .width = kInitialSize.width + (kWidthStep * static_cast<float>(configure)), .height = kInitialSize.height});
        configuresLeavingWork += fabricHost->hasPendingWork() ? 1 : 0;
        static_cast<void>(fabricHost->takeFrame());
        mostShapesInOneConfigure = std::max(mostShapesInOneConfigure, paragraphLayoutCount() - configureShapesBefore);
    }

    const bool pendingAfterSettle = fabricHost->hasPendingWork();
    const uint64_t shapes = paragraphLayoutCount() - shapesBefore;

    std::fprintf(stderr, "[cost] resize: %d configures, %llu paragraph shapes, at most %llu in one\n", kConfigureCount,
                 static_cast<unsigned long long>(shapes), static_cast<unsigned long long>(mostShapesInOneConfigure));

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    EXPECT_EQ(configuresLeavingWork, kConfigureCount);
    EXPECT_FALSE(pendingAfterSettle);
    EXPECT_LE(mostShapesInOneConfigure, static_cast<uint64_t>(kResizedParagraphCount));
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
