#include "FabricHost.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"

#include <algorithm>
#include <chrono>
#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kInitialSize{.width = 400, .height = 300};
constexpr facebook::react::Size kResizedSize{.width = 520, .height = 340};
constexpr facebook::react::Tag kModalTag = 3;
constexpr facebook::react::Tag kBackdropTag = 5;
constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

// ADR-0005's resize contract through a real `FabricHost`: a modal created and committed in two steps the test
// drives on the JavaScript thread, so a resize can be put between them.
constexpr char kModalFixture[] = R"JAVASCRIPT(
const fabric = globalThis.nativeFabricUIManager;
const handles = [];
const node = (tag, componentName, props, children = []) => {
  const handle = { stateNode: { node: null } };
  handles.push(handle);
  handle.stateNode.node = fabric.createNode(tag, componentName, 1, props, handle);
  children.forEach((child) => fabric.appendChild(handle.stateNode.node, child));
  return handle.stateNode.node;
};
globalThis.createModal = () => {
  globalThis.modal = node(3, 'ModalHostView', { position: 'absolute' }, [
    node(5, 'View', { flex: 1, backgroundColor: 0xff2b3a55 | 0 }),
  ]);
};
globalThis.commitModal = () => {
  const children = fabric.createChildSet();
  fabric.appendChildToSet(children, node(7, 'View', { flex: 1 }, [globalThis.modal]));
  fabric.completeRoot(1, children);
};
)JAVASCRIPT";

void callGlobal(ReactHost& reactHost, const char* name) {
    onJavaScriptThread<bool>(reactHost, [name](facebook::jsi::Runtime& runtime) {
        runtime.global().getPropertyAsFunction(runtime, name).call(runtime);

        return true;
    });
}

facebook::react::Size sceneSizeOf(const FabricHost& fabricHost, facebook::react::Tag tag) {
    const SceneNodes nodes = fabricHost.visualTreeNodes();
    const auto node = nodes.find(tag);

    return node == nodes.end() ? facebook::react::Size{} : node->second.layoutMetrics.frame.size;
}

void resize(ReactHost& reactHost, FabricHost& fabricHost, facebook::react::Size size) {
    reactHost.dimensions()->configure(size.width, size.height, 1.0);
    fabricHost.setSurfaceSize(size, 1.0F);
}

class ModalResizeTest : public ::testing::Test {
protected:
    ModalResizeTest() {
        reactHost_.dimensions()->configure(kInitialSize.width, kInitialSize.height, 1.0);
        reactHost_.loadScript(std::make_unique<facebook::react::JSBigStdString>(kModalFixture), "ModalResizeTest.js");
    }

    ~ModalResizeTest() override {
        fabricHost_->stopSurface();
        reactHost_.drainJavaScriptThread();
        fabricHost_.reset();
    }

    ModalResizeTest(const ModalResizeTest&) = delete;
    ModalResizeTest(ModalResizeTest&&) = delete;
    ModalResizeTest& operator=(const ModalResizeTest&) = delete;
    ModalResizeTest& operator=(ModalResizeTest&&) = delete;

    ReactHost reactHost_;
    std::unique_ptr<FabricHost> fabricHost_{std::make_unique<FabricHost>(reactHost_, kInitialSize)};
};

TEST_F(ModalResizeTest, AnOpenModalFollowsAResizeInTheFrameOfTheResize) {
    callGlobal(reactHost_, "createModal");
    callGlobal(reactHost_, "commitModal");
    ASSERT_TRUE(reactHost_.runUntilQuiescent(kQuiescenceBudget));
    ASSERT_EQ(sceneSizeOf(*fabricHost_, kModalTag), kInitialSize);

    resize(reactHost_, *fabricHost_, kResizedSize);

    // No beat and no JavaScript in between: the resize itself committed and mounted the new size.
    EXPECT_EQ(sceneSizeOf(*fabricHost_, kModalTag), kResizedSize);
    EXPECT_EQ(sceneSizeOf(*fabricHost_, kBackdropTag), kResizedSize);

    const SceneFrame frame = fabricHost_->takeFrame();
    const bool paintsTheResizedBackdrop =
        std::any_of(frame.scene.begin(), frame.scene.end(), [](const ScenePrimitive& primitive) {
            return primitive.tag == kBackdropTag && primitive.frame.size == kResizedSize;
        });

    EXPECT_TRUE(paintsTheResizedBackdrop);
    EXPECT_FALSE(reactHost_.hasReportedFatalError());
}

TEST_F(ModalResizeTest, AModalCreatedBeforeAResizeButCommittedAfterItFollowsAtItsMount) {
    callGlobal(reactHost_, "createModal");

    resize(reactHost_, *fabricHost_, kResizedSize);
    callGlobal(reactHost_, "commitModal");
    // The commit mounts in the rendering update after the task, which is what asks for the beat induced here.
    ASSERT_TRUE(reactHost_.runUntilQuiescent(kQuiescenceBudget));
    ASSERT_EQ(sceneSizeOf(*fabricHost_, kModalTag), kInitialSize);
    fabricHost_->induceEventBeat();
    ASSERT_TRUE(reactHost_.runUntilQuiescent(kQuiescenceBudget));

    EXPECT_EQ(sceneSizeOf(*fabricHost_, kModalTag), kResizedSize);
    EXPECT_EQ(sceneSizeOf(*fabricHost_, kBackdropTag), kResizedSize);
    EXPECT_FALSE(reactHost_.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
