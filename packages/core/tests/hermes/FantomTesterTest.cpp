#include "FantomTester.h"

#include <gtest/gtest.h>

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kSurfaceSize{.width = 400, .height = 300};

// C++ holds instance handles weakly, so React retains them on its fibers. A task that commits has to do the
// same, and across tasks, which is why they hang off `globalThis` rather than off a local.
constexpr char kFabricPrelude[] = R"JAVASCRIPT(
globalThis.fabric = globalThis.nativeFabricUIManager;
globalThis.instanceHandles = [];
globalThis.createNode = (tag, componentName, props) => {
  const instanceHandle = {};

  globalThis.instanceHandles.push(instanceHandle);

  return globalThis.fabric.createNode(tag, componentName, 1, props, instanceHandle);
};
globalThis.commit = (children) => {
  const childSet = globalThis.fabric.createChildSet();

  for (const child of children) {
    globalThis.fabric.appendChildToSet(childSet, child);
  }

  globalThis.fabric.completeRoot(1, childSet);
};
globalThis.box = (left, top, width, height) => ({ height, left, position: 'absolute', top, width });
)JAVASCRIPT";

// One node per M1 component: `View`, `Text` — which mounts as the `Paragraph` its `RawText` flattens into —
// `Image`, `ScrollView`, `TextInput`, and the `View` a `Pressable` is at the mounting layer, which is the same
// component with a responder prop on it. Every frame is authored absolutely, so what the tree asserts is the
// mount, not the measurement of a font this container may not have.
//
// `collapsable: false` on the panel is what keeps it a parent: a `View` carrying only a `testID` forms a view but
// not a stacking context, and Fabric hoists the children of such a node to the nearest ancestor that is one — the
// root — so without it the tree mounts flat. That is upstream's view flattening, and the prop is how React Native
// itself opts a node out of it.
constexpr char kSixComponentsTask[] = R"JAVASCRIPT(
const panel = globalThis.createNode(2, 'View', { collapsable: false, testID: 'panel', ...globalThis.box(20, 20, 360, 260) });
const pressable = globalThis.createNode(3, 'View', {
  onStartShouldSetResponder: true,
  testID: 'pressable',
  ...globalThis.box(10, 10, 100, 40),
});
const paragraph = globalThis.createNode(4, 'Paragraph', {
  color: 0xff000000 | 0,
  fontSize: 16,
  testID: 'label',
  ...globalThis.box(10, 60, 200, 20),
});

globalThis.fabric.appendChild(paragraph, globalThis.createNode(104, 'RawText', { text: 'Hello' }));

const image = globalThis.createNode(5, 'Image', { testID: 'image', ...globalThis.box(10, 90, 40, 40) });
const scrollView = globalThis.createNode(6, 'ScrollView', {
  testID: 'scroll',
  ...globalThis.box(10, 140, 120, 100),
});

globalThis.fabric.appendChild(
  scrollView,
  globalThis.createNode(7, 'View', { testID: 'content', ...globalThis.box(0, 0, 120, 400) }),
);

const textInput = globalThis.createNode(8, 'TextInput', { testID: 'input', ...globalThis.box(150, 10, 120, 30) });

for (const child of [pressable, paragraph, image, scrollView, textInput]) {
  globalThis.fabric.appendChild(panel, child);
}

globalThis.commit([panel]);
)JAVASCRIPT";

// The second task the first one's tree survives into: one prop change, committed on its own, which is what
// makes this a task runner rather than a bundle runner.
constexpr char kResizeTask[] = R"JAVASCRIPT(
const panel = globalThis.createNode(2, 'View', { collapsable: false, testID: 'panel', ...globalThis.box(20, 20, 360, 260) });

globalThis.fabric.appendChild(
  panel,
  globalThis.createNode(3, 'View', { testID: 'pressable', ...globalThis.box(10, 10, 220, 40) }),
);
globalThis.commit([panel]);
)JAVASCRIPT";

TEST(FantomTesterTest, MountsTheM1ComponentsAndRendersTheTreeWithoutACompositor) {
    FantomTester tester{kSurfaceSize};

    tester.runTask(kFabricPrelude);
    tester.runTask(kSixComponentsTask);

    EXPECT_EQ(tester.mountTreeText(),
              "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:400,height:300}\">\n"
              "  <rn-view layoutMetrics-frame=\"{x:20,y:20,width:360,height:260}\" testID=\"panel\">\n"
              "    <rn-view layoutMetrics-frame=\"{x:10,y:10,width:100,height:40}\" testID=\"pressable\" />\n"
              "    <rn-paragraph layoutMetrics-frame=\"{x:10,y:60,width:200,height:20}\" testID=\"label\" />\n"
              "    <rn-image layoutMetrics-frame=\"{x:10,y:90,width:40,height:40}\" testID=\"image\" />\n"
              "    <rn-scrollview layoutMetrics-frame=\"{x:10,y:140,width:120,height:100}\" testID=\"scroll\">\n"
              "      <rn-view layoutMetrics-frame=\"{x:0,y:0,width:120,height:400}\" testID=\"content\" />\n"
              "    </rn-scrollview>\n"
              "    <rn-textinput layoutMetrics-frame=\"{x:150,y:10,width:120,height:30}\" testID=\"input\" />\n"
              "  </rn-view>\n"
              "</rn-rootview>\n");
    EXPECT_FALSE(tester.hasReportedFatalError());
}

TEST(FantomTesterTest, LetsASecondTaskMutateTheTreeTheFirstOneCommitted) {
    FantomTester tester{kSurfaceSize};

    tester.runTask(kFabricPrelude);
    tester.runTask(kSixComponentsTask);
    tester.runTask(kResizeTask);

    EXPECT_EQ(tester.mountTreeText(),
              "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:400,height:300}\">\n"
              "  <rn-view layoutMetrics-frame=\"{x:20,y:20,width:360,height:260}\" testID=\"panel\">\n"
              "    <rn-view layoutMetrics-frame=\"{x:10,y:10,width:220,height:40}\" testID=\"pressable\" />\n"
              "  </rn-view>\n"
              "</rn-rootview>\n");
    EXPECT_FALSE(tester.hasReportedFatalError());
}

TEST(FantomTesterTest, ReportsAJavaScriptErrorRatherThanFailingSilently) {
    FantomTester tester{kSurfaceSize};

    tester.runTask("throw new Error('fantom task failed');");

    EXPECT_TRUE(tester.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
