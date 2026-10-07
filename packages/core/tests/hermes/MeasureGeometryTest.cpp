#include "FabricHost.h"
#include "InputPipeline.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"

#include <cstddef>
#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kSurfaceSize{.width = 400, .height = 300};
constexpr facebook::react::Point kInsideTheScrollView{.x = 250, .y = 200};
constexpr double kFrameMilliseconds = 16.0;
constexpr size_t kMaximumScrollFrames = 600;

// Every case #115 names, on one surface: a plain node; a child under a `scale(2)` parent and one under a
// non-integer `scale(1.25)` parent, because a fractional output scale is that transform in practice; a child
// overflowing an `overflow: hidden` parent; a node with no paint props, which Fabric flattens away; and a node
// inside a ScrollView's content. `measureAll` answers every API for every case as one line each, and
// `measureScrolling` answers the ScrollView and the two nodes its scroll moves.
constexpr char kGeometryTree[] = R"JAVASCRIPT(
const fabric = globalThis.nativeFabricUIManager;
globalThis.instanceHandles = [];
const node = (tag, componentName, props, children) => {
  const instanceHandle = {};
  globalThis.instanceHandles.push(instanceHandle);
  const created = fabric.createNode(tag, componentName, 1, props, instanceHandle);
  for (const child of children) {
    fabric.appendChild(created, child);
  }
  return created;
};
const box = (left, top, width, height) => ({ position: 'absolute', left, top, width, height });
const solid = { collapsable: false };
const nodes = {
  plain: node(2, 'View', { ...solid, ...box(10, 20, 100, 50) }, []),
  scaledChild: node(6, 'View', { ...solid, ...box(10, 10, 20, 20) }, []),
  fractionalChild: node(10, 'View', { ...solid, ...box(10, 10, 20, 20) }, []),
  clippedChild: node(14, 'View', { ...solid, ...box(40, 40, 30, 30) }, []),
  flattened: node(16, 'View', box(100, 150, 60, 40), []),
  scrolledChild: node(24, 'View', { ...solid, ...box(0, 80, 50, 20) }, []),
};
nodes.scaled = node(4, 'View', { ...solid, transform: [{ scale: 2 }], ...box(150, 20, 100, 100) }, [nodes.scaledChild]);
nodes.fractional = node(8, 'View', { ...solid, transform: [{ scale: 1.25 }], ...box(280, 20, 100, 100) },
  [nodes.fractionalChild]);
nodes.clipped = node(12, 'View', { ...solid, overflow: 'hidden', ...box(10, 150, 50, 50) }, [nodes.clippedChild]);
nodes.content = node(22, 'View', { ...solid, ...box(0, 0, 150, 400) }, [nodes.scrolledChild]);
nodes.scrollView = node(20, 'ScrollView', box(200, 150, 150, 100), [nodes.content]);

const childSet = fabric.createChildSet();
for (const root of [nodes.plain, nodes.scaled, nodes.fractional, nodes.clipped, nodes.flattened, nodes.scrollView]) {
  fabric.appendChildToSet(childSet, root);
}
fabric.completeRoot(1, childSet);

const round = (value) => Math.round(value * 100) / 100;
const listed = (values) => values.map(round).join(' ');
const layoutLine = (label, child, ancestor) => {
  let line = `${label} failed`;
  fabric.measureLayout(child, ancestor, () => {}, (left, top, width, height) => {
    line = `${label}(${listed([left, top, width, height])})`;
  });
  return line;
};
const measureLines = (names) => {
  const lines = [];
  for (const name of names) {
    let line = name;
    fabric.measure(nodes[name], (x, y, width, height, pageX, pageY) => {
      line += ` measure(${listed([x, y, width, height, pageX, pageY])})`;
    });
    fabric.measureInWindow(nodes[name], (x, y, width, height) => {
      line += ` window(${listed([x, y, width, height])})`;
    });
    lines.push(line);
  }
  lines.push(layoutLine('scrolledChild layoutInScrollView', nodes.scrolledChild, nodes.scrollView));
  return lines;
};
globalThis.measureAll = () =>
  [
    ...measureLines(Object.keys(nodes)),
    layoutLine('scaledChild layoutInScaled', nodes.scaledChild, nodes.scaled),
    layoutLine('fractionalChild layoutInFractional', nodes.fractionalChild, nodes.fractional),
  ].join('\n') + '\n';
globalThis.measureScrolling = () => measureLines(['scrollView', 'content', 'scrolledChild']).join('\n') + '\n';
)JAVASCRIPT";

std::string measure(ReactHost& reactHost, const char* measurer) {
    return onJavaScriptThread<std::string>(reactHost, [measurer](facebook::jsi::Runtime& runtime) {
        return runtime.global().getPropertyAsFunction(runtime, measurer).call(runtime).getString(runtime).utf8(runtime);
    });
}

constexpr char kUnscrolledMeasures[] = R"(plain measure(10 20 100 50 10 20) window(10 20 100 50)
scaledChild measure(10 10 40 40 120 -10) window(120 -10 40 40)
fractionalChild measure(10 10 25 25 280 20) window(280 20 25 25)
clippedChild measure(40 40 30 30 50 190) window(50 190 30 30)
flattened measure(100 150 60 40 100 150) window(100 150 60 40)
scrolledChild measure(0 80 50 20 200 230) window(200 230 50 20)
scaled measure(150 20 200 200 100 -30) window(100 -30 200 200)
fractional measure(280 20 125 125 267.5 7.5) window(267.5 7.5 125 125)
clipped measure(10 150 50 50 10 150) window(10 150 50 50)
content measure(0 0 150 400 200 150) window(200 150 150 400)
scrollView measure(200 150 150 100 200 150) window(200 150 150 100)
scrolledChild layoutInScrollView(0 80 50 20)
scaledChild layoutInScaled(10 10 20 20)
fractionalChild layoutInFractional(10 10 20 20)
)";
// Three wheel notches scroll the content 120 points, so the content and its child move up 120 in the window.
// `measureLayout` against the ScrollView is upstream's, which leaves the content offset out: it reads 80 scrolled or
// not, and so equals the difference of the two window rectangles only while the ScrollView is unscrolled.
constexpr char kScrolledMeasures[] = R"(scrollView measure(200 150 150 100 200 150) window(200 150 150 100)
content measure(0 0 150 400 200 30) window(200 30 150 400)
scrolledChild measure(0 80 50 20 200 110) window(200 110 50 20)
scrolledChild layoutInScrollView(0 80 50 20)
)";

/**
 * Issue #115, with the owner's decision of 2026-10-06 that this platform matches React Native: `measure` and
 * `measureInWindow` fold ancestor transforms in, as upstream's `dom::measure` does, and `measureLayout` against an
 * ancestor does not. A clip does not shrink a frame. A flattened node is still measurable, because measuring reads
 * the shadow tree rather than the mounted views. A ScrollView's offset moves its content's window position, and
 * the offset is the one this platform's own scrolling wrote back.
 */
TEST(MeasureGeometryTest, MeasuresFollowUpstreamThroughTransformsClipsFlatteningAndScrolling) {
    ReactHost reactHost;
    auto fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), kSurfaceSize);

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kGeometryTree), "MeasureGeometryTest.js");
    reactHost.drainJavaScriptThread();

    EXPECT_EQ(measure(reactHost, "measureAll"), kUnscrolledMeasures);

    fabricHost->dispatchInput({InputEvent{
        .kind = InputEventKind::PointerScrollDiscrete, .surfacePoint = kInsideTheScrollView, .scrollAmount = 3.0}});

    for (size_t frame = 0; frame < kMaximumScrollFrames && fabricHost->advanceScroll(kFrameMilliseconds); ++frame) {
    }

    fabricHost->induceEventBeat();
    reactHost.drainJavaScriptThread();

    EXPECT_EQ(measure(reactHost, "measureScrolling"), kScrolledMeasures);

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
