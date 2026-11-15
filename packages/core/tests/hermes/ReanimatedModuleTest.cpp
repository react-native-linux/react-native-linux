#include "FabricHost.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"
#include "WorkletsModule.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <jsi/instrumentation.h>
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
constexpr char kStartWorklets[] = R"JAVASCRIPT(
const worklets = globalThis.nativeModuleProxy.WorkletsModule;
const stub = ['(function () {})', 'stub.js', ''];
const registry = ['(function () { globalThis.__customSerializationRegistry = []; })', 'registry.js', ''];
worklets.installTurboModule(false);
globalThis.__workletsModuleProxy.loadUnpackersWithCode(...registry, ...stub, ...stub, ...stub, ...stub, ...stub);
worklets.start();
)JAVASCRIPT";

constexpr char kInstallReanimated[] = R"JAVASCRIPT(
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
globalThis.commitChildren = (nodes) => {
  const childSet = fabric.createChildSet();
  nodes.forEach((node) => fabric.appendChildToSet(childSet, node));
  fabric.completeRoot(1, childSet);
};
const commit = (node) => globalThis.commitChildren([node]);
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

// What worklets' own frame loop does on the UI runtime once its bundle is loaded: run JavaScript every frame. Each
// frame also checks that Reanimated's UI bindings are all there or all absent, `_updateProps` being the first that
// `UIRuntimeDecorator::decorate` installs and `_maybeFlushUIUpdatesQueue` the last.
constexpr char kUILoop[] = R"JAVASCRIPT(
globalThis.uiFrames = 0;
globalThis.halfDecoratedFrames = 0;
const loop = () => {
  globalThis.uiFrames += 1;
  if (typeof _updateProps !== typeof _maybeFlushUIUpdatesQueue) {
    globalThis.halfDecoratedFrames += 1;
  }
  __nativeRequestAnimationFrame(loop);
};
__nativeRequestAnimationFrame(loop);
)JAVASCRIPT";

// What Reanimated's JavaScript does while a view is mounted: write its registry entries under the manager's lock.
constexpr char kTouchRegistries[] = R"JAVASCRIPT(
globalThis.touchRegistries = () => {
  globalThis.__reanimatedModuleProxy.markNodeAsRemovable(globalThis.animatedNode);
  globalThis.__reanimatedModuleProxy.unmarkNodeAsRemovable(2);
};
)JAVASCRIPT";

// A second view, mounted beside the animated one, that Reanimated's JavaScript has static props for.
constexpr char kMountRemovable[] = R"JAVASCRIPT(
globalThis.removable = globalThis.nativeFabricUIManager.createNode(3, 'View', 1,
  { collapsable: false, position: 'absolute', left: 100, top: 10, width: 20, height: 20 }, {});
globalThis.commitChildren([globalThis.animatedNode, globalThis.removable]);
globalThis.__reanimatedModuleProxy.setViewStyle(3, { opacity: 0.5 });
)JAVASCRIPT";

// What an `Animated` component does as it unmounts: mark its node removable, and React commits the tree without it.
constexpr char kUnmountRemovable[] = R"JAVASCRIPT(
globalThis.__reanimatedModuleProxy.markNodeAsRemovable(globalThis.removable);
globalThis.removable = undefined;
globalThis.commitChildren([globalThis.animatedNode]);
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

void install(ReactHost& reactHost) {
    evaluate(reactHost, kStartWorklets, "worklets.js");
    evaluate(reactHost, kInstallReanimated, "reanimated.js");
}

std::shared_ptr<const facebook::react::ShadowNode> shadowNodeOf(ReactHost& reactHost, const char* global) {
    return onJavaScriptThread<std::shared_ptr<const facebook::react::ShadowNode>>(
        reactHost, [global](facebook::jsi::Runtime& runtime) -> std::shared_ptr<const facebook::react::ShadowNode> {
            return facebook::react::Bridging<std::shared_ptr<const facebook::react::ShadowNode>>::fromJs(
                runtime, runtime.global().getProperty(runtime, global));
        });
}

void evaluateOnTheUIRuntime(worklets::WorkletRuntime& uiRuntime, const char* source) {
    uiRuntime.runSync([source](facebook::jsi::Runtime& runtime) {
        runtime.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "ui.js");
    });
}

/** Both hosts tick as `WindowSession::tickAnimations` ticks them: worklets' frame work, then the animation backend. */
void tick(ReactHost& reactHost, FabricHost& fabricHost, std::chrono::steady_clock::time_point now) {
    reactHost.dispatchAnimationFrames(now);
    fabricHost.tickAnimations(now);
}

std::string reanimatedTrace(ReactHost& reactHost) {
    return onJavaScriptThread<std::string>(reactHost, [](facebook::jsi::Runtime& runtime) {
        return runtime.global().getProperty(runtime, "reanimatedTrace").getString(runtime).utf8(runtime);
    });
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
        install(reactHost_);
        onJavaScriptThread<bool>(reactHost_, [this](facebook::jsi::Runtime& runtime) -> bool {
            facebook::react::UIManagerBinding::getBinding(runtime)->getUIManager().registerCommitHook(commitCounter_);
            return true;
        });
        const std::shared_ptr<const facebook::react::ShadowNode> animatedNode =
            shadowNodeOf(reactHost_, "animatedNode");

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

    void tick() {
        now_ += kFrameInterval;
        react_native_linux::tick(reactHost_, *fabricHost_, now_);
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

/**
 * #140: with nothing left to animate, Reanimated's `stopBackendIfIdle` leaves the backend. The UI runtime here runs
 * no frame loop of its own, so this proves the backend side only, not that a stock UI runtime ever idles: worklets'
 * loaded bundle re-requests a frame from inside every frame, which #671 owns.
 */
TEST_F(ReanimatedModuleTest, AnIdleAnimationLeavesTheBackend) {
    animateTranslateXOnTheNextFrame(20);
    tick();
    ASSERT_TRUE(fabricHost_->hasPendingWork());
    tick();
    fabricHost_->takeFrame();

    EXPECT_FALSE(fabricHost_->hasPendingWork());
}

/**
 * #140, run under TSan: on the JavaScript thread React commits, and Reanimated's JavaScript writes the view's
 * registry entries through `__reanimatedModuleProxy` under the registry manager's lock, while the frame thread runs
 * an animation that writes the same view every frame through the backend callback, which takes that lock too. The
 * last frame's transform lands, and the instance is torn down with the animation still running.
 */
TEST_F(ReanimatedModuleTest, ReactCommitsAndRegistryWritesDuringFramesAreRaceFree) {
    const float restingTranslateX = animatedPrimitive(*fabricHost_).matrix.translateX;
    evaluate(reactHost_, kTouchRegistries, "registries.js");
    std::thread javaScriptWork([executor = reactHost_.reactInstance().getBufferedRuntimeExecutor()]() {
        for (size_t commit = 0; commit < kConcurrentFrames; ++commit) {
            reRender(executor);
            executor([](facebook::jsi::Runtime& runtime) {
                runtime.global().getPropertyAsFunction(runtime, "touchRegistries").call(runtime);
            });
        }
    });

    for (size_t frame = 1; frame <= kConcurrentFrames; ++frame) {
        animateTranslateXOnTheNextFrame(static_cast<double>(frame));
        tick();
    }

    javaScriptWork.join();
    reactHost_.drainJavaScriptThread();
    EXPECT_FLOAT_EQ(animatedPrimitive(*fabricHost_).matrix.translateX,
                    restingTranslateX + static_cast<float>(kConcurrentFrames));
    animateTranslateXOnTheNextFrame(1);
    EXPECT_FALSE(reactHost_.hasReportedFatalError());
}

/**
 * #140: an unmounted view leaves nothing behind in Reanimated. On the animation backend path upstream's mount hook
 * returns before `handleNodeRemovals`, so every node Reanimated's JavaScript marks removable stayed in its registries
 * for the proxy's lifetime, with the node's `ShadowNodeFamily`; patch 0004 drains them. The family is the observable
 * part: `handleNodeRemovals` drops the view's entry from every registry in the step that drops the family.
 */
TEST_F(ReanimatedModuleTest, AnUnmountedViewLeavesNothingInTheRegistries) {
    evaluate(reactHost_, kMountRemovable, "mount.js");
    const std::weak_ptr<const facebook::react::ShadowNodeFamily> family =
        shadowNodeOf(reactHost_, "removable")->getFamilyShared();

    evaluate(reactHost_, kUnmountRemovable, "unmount.js");
    onJavaScriptThread<bool>(reactHost_, [](facebook::jsi::Runtime& runtime) -> bool {
        runtime.instrumentation().collectGarbage("unmounted");
        return true;
    });
    tick();
    fabricHost_->takeFrame();

    EXPECT_TRUE(family.expired());
}

double uiGlobal(worklets::WorkletRuntime& uiRuntime, const char* name) {
    return uiRuntime.runSync(
        [name](facebook::jsi::Runtime& runtime) { return runtime.global().getProperty(runtime, name).asNumber(); });
}

/**
 * #140: JavaScript installs Reanimated while the frame thread runs worklets on the UI runtime every frame, as it does
 * once worklets' bundle is loaded. Every JSI call on that runtime takes its lock, but a frame could still run between
 * two of the install's calls and find half of Reanimated's bindings; the install holds the lock for all of them, so
 * no frame does. Run under TSan as well.
 */
TEST(ReanimatedModuleInstallTest, NoUIRuntimeFrameSeesAHalfInstalledReanimated) {
    ReactHost reactHost;
    auto fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), kSurfaceSize);
    evaluate(reactHost, kTree, "tree.js");
    evaluate(reactHost, kStartWorklets, "worklets.js");
    std::shared_ptr<worklets::WorkletRuntime> uiRuntime = reactHost.worklets().uiWorkletRuntime();
    evaluateOnTheUIRuntime(*uiRuntime, kUILoop);
    std::atomic<bool> installed{false};
    std::chrono::steady_clock::time_point now{std::chrono::seconds(1)};

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kInstallReanimated), "reanimated.js");
    reactHost.reactInstance().getBufferedRuntimeExecutor()(
        [&installed](facebook::jsi::Runtime& /*runtime*/) { installed = true; });
    while (!installed) {
        now += kFrameInterval;
        tick(reactHost, *fabricHost, now);
    }

    EXPECT_EQ(reanimatedTrace(reactHost), R"({"installed":true,"proxy":"object","animationBackend":true})");
    EXPECT_GT(uiGlobal(*uiRuntime, "uiFrames"), 0);
    EXPECT_EQ(uiGlobal(*uiRuntime, "halfDecoratedFrames"), 0);
    uiRuntime.reset();
    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();
    reactHost.drainJavaScriptThread();
}

/** #140: without a Fabric surface there is no UIManager to attach to, and Reanimated installs its dummy proxy. */
TEST(ReanimatedModuleWithoutFabricTest, InstallAnswersFalse) {
    ReactHost reactHost;

    install(reactHost);

    EXPECT_EQ(reanimatedTrace(reactHost), R"({"installed":false,"proxy":"undefined","animationBackend":false})");
}

} // namespace
} // namespace react_native_linux
