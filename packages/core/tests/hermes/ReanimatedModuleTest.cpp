#include "FabricHost.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"
#include "ReanimatedPlatform.h"
#include "WorkletsModule.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cxxreact/JSBigString.h>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <worklets/WorkletRuntime/WorkletRuntime.h>

#include <react/renderer/bridging/bridging.h>
#include <react/renderer/uimanager/UIManagerBinding.h>
#include <react/renderer/uimanager/UIManagerCommitHook.h>
#include <react/renderer/uimanager/primitives.h>

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kSurfaceSize{.width = 400, .height = 300};
constexpr facebook::react::Tag kAnimatedTag = 2;
constexpr std::chrono::milliseconds kFrameInterval{16};
constexpr size_t kConcurrentFrames = 200;

// Worklets' six unpackers are stubs, as in WorkletsModuleTest: this suite proves the hosts, not worklets' bundle.
constexpr char kInstall[] = R"JAVASCRIPT(
const worklets = globalThis.nativeModuleProxy.WorkletsModule;
const stub = ['(function () {})', 'stub.js', ''];
const registry = ['(function () { globalThis.__customSerializationRegistry = []; })', 'registry.js', ''];
worklets.installTurboModule(false);
globalThis.__workletsModuleProxy.loadUnpackersWithCode(...registry, ...stub, ...stub, ...stub, ...stub, ...stub);
worklets.start();
globalThis.installed = globalThis.nativeModuleProxy.ReanimatedModule.installTurboModule();
globalThis.reanimatedTrace = JSON.stringify({
  installed: globalThis.installed,
  proxy: typeof globalThis.__reanimatedModuleProxy,
  animationBackend: globalThis.installed &&
    globalThis.__reanimatedModuleProxy.getStaticFeatureFlag('USE_ANIMATION_BACKEND'),
});
)JAVASCRIPT";

// One absolutely positioned view, and the React re-render that changes nothing an animation touches.
constexpr char kTree[] = R"JAVASCRIPT(
const fabric = globalThis.nativeFabricUIManager;
const box = { collapsable: false, position: 'absolute', left: 10, top: 10, width: 50, height: 50,
  backgroundColor: 0xff38bdf8 };
const commit = (node) => {
  const childSet = fabric.createChildSet();
  fabric.appendChildToSet(childSet, node);
  fabric.completeRoot(1, childSet);
};
globalThis.instanceHandle = {};
globalThis.animatedNode = fabric.createNode(2, 'View', 1, box, globalThis.instanceHandle);
commit(globalThis.animatedNode);
globalThis.width = 50;
globalThis.reRender = () => {
  globalThis.width = globalThis.width === 50 ? 60 : 50;
  globalThis.animatedNode = fabric.cloneNodeWithNewProps(globalThis.animatedNode, { width: globalThis.width });
  commit(globalThis.animatedNode);
};
)JAVASCRIPT";

// What a mapper does with a shared value on the UI runtime: on the next frame, write the view's animated style.
constexpr char kAnimateOnTheUIRuntime[] = R"JAVASCRIPT(
globalThis.frameTimestamps = [];
globalThis.animateTranslateX = (translateX) => __nativeRequestAnimationFrame((timestamp) => {
  globalThis.frameTimestamps.push(timestamp);
  _updateProps([{ shadowNodeWrapper: globalThis.animatedNode, updates: { transform: [{ translateX }] } }]);
});
)JAVASCRIPT";

class CommitCounter final : public facebook::react::UIManagerCommitHook {
public:
    void commitHookWasRegistered(const facebook::react::UIManager& /*uiManager*/) noexcept override {}

    void commitHookWasUnregistered(const facebook::react::UIManager& /*uiManager*/) noexcept override {}

    facebook::react::RootShadowNode::Unshared
    shadowTreeWillCommit(const facebook::react::ShadowTree& /*shadowTree*/,
                         const facebook::react::RootShadowNode::Shared& /*oldRootShadowNode*/,
                         const facebook::react::RootShadowNode::Unshared& newRootShadowNode,
                         const facebook::react::ShadowTreeCommitOptions& /*commitOptions*/) noexcept override {
        commits_++;
        return newRootShadowNode;
    }

    size_t commits() const { return commits_.load(); }

private:
    std::atomic<size_t> commits_{0};
};

void evaluate(ReactHost& reactHost, const char* source, const char* sourceUrl) {
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(source), sourceUrl);
    reactHost.drainJavaScriptThread();
}

std::string reanimatedTrace(ReactHost& reactHost) {
    return onJavaScriptThread<std::string>(reactHost, [](facebook::jsi::Runtime& runtime) {
        return runtime.global().getProperty(runtime, "reanimatedTrace").getString(runtime).utf8(runtime);
    });
}

/** `executor` rather than the host, because work from a thread other than the host's is the point of one case. */
void onRuntime(const facebook::react::RuntimeExecutor& executor, const std::function<void()>& work) {
    std::promise<void> done;

    executor([&done, &work](facebook::jsi::Runtime& /*runtime*/) {
        work();
        done.set_value();
    });
    done.get_future().get();
}

void reRender(const facebook::react::RuntimeExecutor& executor) {
    std::promise<void> committed;

    executor([&committed](facebook::jsi::Runtime& runtime) {
        runtime.global().getPropertyAsFunction(runtime, "reRender").call(runtime);
        committed.set_value();
    });
    committed.get_future().get();
}

ScenePrimitive animatedPrimitive(const FabricHost& fabricHost) {
    for (const ScenePrimitive& primitive : fabricHost.snapshotScene()) {
        if (primitive.tag == kAnimatedTag) {
            return primitive;
        }
    }

    ADD_FAILURE() << "the scene paints no view " << kAnimatedTag;
    return {};
}

/**
 * A surface with the one view mounted, both hosts installed from JavaScript, and the view's shadow node handed to the
 * UI runtime the way Reanimated's JavaScript hands a ref's shadow node to a mapper.
 */
class ReanimatedModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        fabricHost_ = std::make_unique<FabricHost>(reactHost_.reactInstance(), kSurfaceSize);
        evaluate(reactHost_, kTree, "tree.js");
        evaluate(reactHost_, kInstall, "install.js");

        const auto animatedNode = onJavaScriptThread<std::shared_ptr<const facebook::react::ShadowNode>>(
            reactHost_, [this](facebook::jsi::Runtime& runtime) -> std::shared_ptr<const facebook::react::ShadowNode> {
                facebook::react::UIManagerBinding::getBinding(runtime)->getUIManager().registerCommitHook(
                    commitCounter_);
                return facebook::react::Bridging<std::shared_ptr<const facebook::react::ShadowNode>>::fromJs(
                    runtime, runtime.global().getProperty(runtime, "animatedNode"));
            });

        uiRuntime_ = reactHost_.worklets().uiWorkletRuntime();
        uiRuntime_->runSync([&animatedNode](facebook::jsi::Runtime& runtime) {
            runtime.global().setProperty(runtime, "animatedNode",
                                         facebook::react::valueFromShadowNode(runtime, animatedNode));
            runtime.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(kAnimateOnTheUIRuntime), "ui.js");
        });
    }

    void TearDown() override {
        onJavaScriptThread<bool>(reactHost_, [this](facebook::jsi::Runtime& runtime) -> bool {
            facebook::react::UIManagerBinding::getBinding(runtime)->getUIManager().unregisterCommitHook(commitCounter_);
            return true;
        });
        uiRuntime_.reset();
        fabricHost_->stopSurface();
        reactHost_.drainJavaScriptThread();
        fabricHost_.reset();
        reactHost_.drainJavaScriptThread();
    }

    void animateTranslateXOnTheNextFrame(double translateX) {
        uiRuntime_->runSync([translateX](facebook::jsi::Runtime& runtime) {
            runtime.global().getPropertyAsFunction(runtime, "animateTranslateX").call(runtime, translateX);
        });
    }

    /** One frame as `WindowSession::tickAnimations` runs it: the animation backend, then worklets' frame work. */
    void tick() {
        now_ += kFrameInterval;
        fabricHost_->tickAnimations(now_);
        reactHost_.dispatchAnimationFrames(now_);
    }

    std::string frameTimestamps() {
        return uiRuntime_->runSync([](facebook::jsi::Runtime& runtime) {
            return runtime.global()
                .getPropertyAsObject(runtime, "JSON")
                .getPropertyAsFunction(runtime, "stringify")
                .call(runtime, runtime.global().getProperty(runtime, "frameTimestamps"))
                .getString(runtime)
                .utf8(runtime);
        });
    }

    ReactHost reactHost_;
    std::unique_ptr<FabricHost> fabricHost_;
    std::shared_ptr<worklets::WorkletRuntime> uiRuntime_;
    CommitCounter commitCounter_;
    std::chrono::steady_clock::time_point now_{std::chrono::seconds(1)};
};

/**
 * #140: Reanimated's JavaScript installs the host as it does on Apple, and the proxy it gets is compiled for the
 * shared animation backend, which is what makes this host's flush the backend's frame callback.
 */
TEST_F(ReanimatedModuleTest, JavaScriptInstallsTheProxyOnTheAnimationBackend) {
    EXPECT_EQ(reanimatedTrace(reactHost_), R"({"installed":true,"proxy":"object","animationBackend":true})");
    EXPECT_FALSE(reactHost_.hasReportedFatalError());
}

/**
 * #140: a mapper's style write lands on the frame after it asked, stamped with that frame's time, and moves the
 * mounted view through the animation backend without a Fabric commit, because `transform` is not a layout prop.
 */
TEST_F(ReanimatedModuleTest, AnAnimatedTransformReachesTheSceneWithoutACommit) {
    const float restingTranslateX = animatedPrimitive(*fabricHost_).matrix.translateX;
    const size_t commitsBefore = commitCounter_.commits();

    animateTranslateXOnTheNextFrame(20);
    tick();

    EXPECT_EQ(frameTimestamps(), "[1016]");
    EXPECT_FLOAT_EQ(animatedPrimitive(*fabricHost_).matrix.translateX, restingTranslateX + 20.0F);
    EXPECT_EQ(commitCounter_.commits(), commitsBefore);
    EXPECT_TRUE(fabricHost_->hasPendingWork());
}

/**
 * #140: React re-renders the animated view while its animation runs, and the commit keeps the animated transform.
 * Reanimated's commit hook passes React's commit through on the backend path and React Native's
 * `AnimationBackendCommitHook` re-applies the animated props, so the result is the same whichever runs first.
 */
TEST_F(ReanimatedModuleTest, AReactReRenderDuringAnAnimationKeepsTheAnimatedTransform) {
    const float restingTranslateX = animatedPrimitive(*fabricHost_).matrix.translateX;

    animateTranslateXOnTheNextFrame(20);
    tick();
    reRender(reactHost_.reactInstance().getBufferedRuntimeExecutor());
    reactHost_.drainJavaScriptThread();

    const ScenePrimitive reRendered = animatedPrimitive(*fabricHost_);
    EXPECT_FLOAT_EQ(reRendered.frame.size.width, 60.0F);
    EXPECT_FLOAT_EQ(reRendered.matrix.translateX, restingTranslateX + 20.0F);

    animateTranslateXOnTheNextFrame(30);
    tick();
    reRender(reactHost_.reactInstance().getBufferedRuntimeExecutor());
    reactHost_.drainJavaScriptThread();

    EXPECT_FLOAT_EQ(animatedPrimitive(*fabricHost_).frame.size.width, 50.0F);
    EXPECT_FLOAT_EQ(animatedPrimitive(*fabricHost_).matrix.translateX, restingTranslateX + 30.0F);
}

/** #140: with nothing left to animate, Reanimated leaves the backend, and the frame clock may go idle. */
TEST_F(ReanimatedModuleTest, AnIdleAnimationLeavesTheBackend) {
    animateTranslateXOnTheNextFrame(20);
    tick();
    ASSERT_TRUE(fabricHost_->hasPendingWork());
    tick();
    fabricHost_->takeFrame();

    EXPECT_FALSE(fabricHost_->hasPendingWork());
}

/**
 * #140, run under TSan: on the JavaScript thread React commits and the platform's `requestRender` is called, as
 * Reanimated's operations loop calls it when JavaScript schedules a CSS animation, while the frame thread runs an
 * animation that writes the same view every frame and dispatches those renders. Every render lands, and the
 * instance is torn down with the animation still running.
 */
TEST_F(ReanimatedModuleTest, ReactCommitsAndRenderRequestsDuringFramesAreRaceFree) {
    const reanimated::PlatformDepMethodsHolder platform =
        makePlatformDepMethodsHolder(reactHost_.worklets().animationFrames(), nullptr);
    std::atomic<size_t> renders{0};
    std::thread javaScriptWork(
        [executor = reactHost_.reactInstance().getBufferedRuntimeExecutor(), &platform, &renders]() {
            for (size_t commit = 0; commit < kConcurrentFrames; ++commit) {
                reRender(executor);
                onRuntime(executor, [&platform, &renders]() {
                    platform.requestRender([&renders](double /*timestamp*/) { renders++; });
                });
            }
        });

    for (size_t frame = 0; frame < kConcurrentFrames; ++frame) {
        animateTranslateXOnTheNextFrame(static_cast<double>(frame));
        tick();
    }

    javaScriptWork.join();
    tick();
    animateTranslateXOnTheNextFrame(1);
    EXPECT_EQ(renders.load(), kConcurrentFrames);
    EXPECT_FALSE(reactHost_.hasReportedFatalError());
}

/** #140: without a Fabric surface there is no UIManager to attach to, and Reanimated installs its dummy proxy. */
TEST(ReanimatedModuleWithoutFabricTest, InstallAnswersFalse) {
    ReactHost reactHost;

    evaluate(reactHost, kInstall, "install.js");

    EXPECT_EQ(reanimatedTrace(reactHost), R"({"installed":false,"proxy":"undefined","animationBackend":false})");
}

} // namespace
} // namespace react_native_linux
