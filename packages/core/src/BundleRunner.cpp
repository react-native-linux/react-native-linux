#include "BundleRunner.h"

#include "DimensionsSource.h"
#include "FabricHost.h"
#include "InputPipeline.h"
#include "ReactHost.h"

#ifdef RNL_ENABLE_IMAGES
#include "ImageDecoder.h"
#endif

#include <cxxreact/JSBigString.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Size.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{30000};
constexpr std::chrono::milliseconds kFirstCommitBudget{10000};
constexpr std::chrono::milliseconds kCommitPollInterval{1};
constexpr char kSmokeSource[] = "console.log('react-native-linux: hermes alive');";
constexpr char kSmokeSourceUrl[] = "smoke.js";
constexpr facebook::react::Size kHeadlessSurfaceSize{.width = 800, .height = 600};
// Five rather than three since #109: a `scrollTo` issued from a frame's `onScroll` is applied by the frame after
// it, so a fixture that chains three commands off each other's events needs three frames beyond the first commit.
// Every additional frame is empty for a static fixture, so no golden moves.
constexpr size_t kHeadlessFrameCount = 5;
constexpr size_t kInjectedMotionCount = 17;
// One 60 Hz frame, in milliseconds. A headless run has no compositor to pace it, so the scroll physics is stepped
// at a fixed rate instead of a measured one and the settled position stops depending on the machine.
constexpr double kInjectedFrameMilliseconds = 1000.0 / 60.0;
// About thirty-three seconds of 60 Hz frames, which is longer than the slowest deceleration curve React Native's
// `decelerationRate` can ask for. It exists so a physics bug is a run that ends rather than a run that hangs.
constexpr size_t kMaximumInjectedScrollFrames = 2000;
// What a keymap reports for the Tab key, already in the DOM names the seat produces. `domKeyName` is what turns
// the keysym into these; injecting them directly is what lets a headless run press Tab with no keymap at all.
constexpr char kTabKeyName[] = "Tab";
constexpr char kTabKeyCode[] = "Tab";

std::unique_ptr<const facebook::react::JSBigString> readScript(const std::optional<std::string>& bundlePath) {
    if (bundlePath.has_value()) {
        return facebook::react::JSBigFileString::fromPath(bundlePath.value());
    }

    return std::make_unique<facebook::react::JSBigStdString>(kSmokeSource);
}

/**
 * The headless counterpart of an `xdg_toplevel.configure`: a run with no compositor still has a surface, and
 * `Dimensions.get` has to answer with that surface's size rather than with the pre-configure default.
 */
void configureDimensions(ReactHost& reactHost, facebook::react::Size surfaceSize) {
    reactHost.dimensions().configure(static_cast<double>(surfaceSize.width), static_cast<double>(surfaceSize.height),
                                     DimensionsSource::kDefaultScale);
}

void loadAndSettle(ReactHost& reactHost, const std::optional<std::string>& bundlePath) {
    std::unique_ptr<const facebook::react::JSBigString> script = readScript(bundlePath);

    reactHost.loadScript(std::move(script), bundlePath.value_or(kSmokeSourceUrl));

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }
}

/**
 * Waits for the decode queue to empty, because a scene is only read once the pixels are in it.
 *
 * A decode is asynchronous, and the completion is what hands the nodes drawing a source their pixels; a scene
 * snapshotted before that carries none, and there is no run loop here to notice the damage the completion
 * produced. Reading a settled scene is also what makes an image golden deterministic — without it the same
 * bundle would produce a picture that depends on how fast a codec ran. Without Skia there is no decoder and
 * nothing to wait for.
 */
void settlePendingImageDecodes() {
#ifdef RNL_ENABLE_IMAGES
    constexpr std::chrono::milliseconds kImageDecodeBudget{30000};

    if (!waitForPendingImageDecodes(kImageDecodeBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for image decoding" << std::endl;
    }
#endif
}

/**
 * Waits for the first commit to reach the scene and returns it, leaving the damage take that came with it behind.
 *
 * There is no callback for "a transaction was mounted", so this polls: drain the JavaScript thread, look, repeat.
 * The bundle's second commit is timer-driven and therefore far away in wall-clock terms, which is what makes a
 * poll good enough to land between the two. A run that misses that window ends with no damage left to take, and
 * the caller reports that rather than producing a golden that proves nothing.
 */
SceneSnapshot waitForFirstCommit(ReactHost& reactHost, FabricHost& fabricHost, bool shouldSettleImageDecodes) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kFirstCommitBudget;

    while (std::chrono::steady_clock::now() < deadline) {
        reactHost.drainJavaScriptThread();

        // The damage proof wants the first frame with its pixels attached; the first-frame proof wants it exactly
        // as it was committed, decodes still in flight, or the "first" scene is already the settled one and the
        // comparison tests nothing.
        if (shouldSettleImageDecodes) {
            settlePendingImageDecodes();
        }

        SceneFrame frame = fabricHost.takeFrame();

        if (!frame.scene.empty()) {
            return std::move(frame.scene);
        }

        std::this_thread::sleep_for(kCommitPollInterval);
    }

    return {};
}

/**
 * One frame's worth of a mouse sweeping to `surfacePoint`, pushed through the same `InputQueue` the Wayland seat
 * fills. The count is what a 1000 Hz mouse produces inside a 60 Hz frame, rounded up, and the queue is expected to
 * hand back exactly one motion event for it.
 */
std::vector<InputEvent> makeMotionFrame(facebook::react::Point surfacePoint) {
    InputQueue queue;

    for (size_t step = 1; step <= kInjectedMotionCount; ++step) {
        const facebook::react::Float progress =
            static_cast<facebook::react::Float>(step) / static_cast<facebook::react::Float>(kInjectedMotionCount);

        queue.push(InputEvent{
            .kind = InputEventKind::PointerMotion,
            .surfacePoint = facebook::react::Point{.x = surfacePoint.x * progress, .y = surfacePoint.y * progress}});
    }

    return queue.drain();
}

/**
 * One frame's worth of a mouse wheel turned `wheelNotches` times over `surfacePoint`, pushed through the same
 * `InputQueue` the Wayland seat fills — so the notches arrive coalesced into the one event a real frame would
 * hand over, rather than as a shape only this function produces.
 */
std::vector<InputEvent> makeWheelFrame(facebook::react::Point surfacePoint, int wheelNotches) {
    InputQueue queue;

    for (int notch = 0; notch < wheelNotches; ++notch) {
        queue.push(InputEvent{.kind = InputEventKind::PointerScrollDiscrete,
                              .surfacePoint = surfacePoint,
                              .scrollAmount = 1.0});
    }

    return queue.drain();
}

void deliverInputFrame(ReactHost& reactHost, FabricHost& fabricHost, const std::vector<InputEvent>& events) {
    fabricHost.dispatchInput(events);
    fabricHost.induceEventBeat();

    // The headless counterpart of the window loop's tick, in the same place: after the frame's input has been
    // released onto the JavaScript thread and before the run settles, so whatever an animation frame hands back to
    // JavaScript is delivered by the drain that follows. See *Animation choreographer* in docs/cpp-toolchain.md.
    fabricHost.tickAnimations(std::chrono::steady_clock::now());

    // The scroll half of the same frame, for the same reason: the window advances scroll on every frame, so a
    // headless frame that did not would never apply a `scrollTo` command at all — the queue is drained here. With
    // no input and nothing moving this costs one pass over an empty target list and changes no existing run.
    fabricHost.advanceScroll(kInjectedFrameMilliseconds);

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }
}

std::unique_ptr<FabricHost> startFabricRun(ReactHost& reactHost, const std::string& bundlePath,
                                           facebook::react::Size surfaceSize) {
    std::unique_ptr<FabricHost> fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), surfaceSize);

    configureDimensions(reactHost, surfaceSize);
    reactHost.loadScript(facebook::react::JSBigFileString::fromPath(bundlePath), bundlePath);

    return fabricHost;
}

/**
 * A run that injects input, once its bundle has committed: the Fabric host, and whether there was a scene to
 * inject into at all.
 *
 * The `ReactHost` stays with the caller rather than moving in here, because it owns the JavaScript thread this
 * host's teardown has to drain and it outlives the host by construction.
 */
struct StartedFabricRun {
    std::unique_ptr<FabricHost> fabricHost;
    bool hasCommitted{};
};

/**
 * Boots a bundle at the headless surface size and waits for its first commit. `subject` completes the message a
 * bundle that committed nothing gets, which is the only thing that differs between the runs that call this.
 */
StartedFabricRun startFabricRunAtFirstCommit(ReactHost& reactHost, const std::string& bundlePath,
                                             std::string_view subject) {
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, kHeadlessSurfaceSize);
    const bool hasCommitted = !waitForFirstCommit(reactHost, *fabricHost, true).empty();

    if (!hasCommitted) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to " << subject
                  << std::endl;
    }

    return StartedFabricRun{.fabricHost = std::move(fabricHost), .hasCommitted = hasCommitted};
}

/**
 * Tears an injected run down in the same order `finishFabricRun` does — stop the surface, drain the JavaScript
 * thread so the queued unmount runs while the scheduler delegate is still alive, then destroy the host — and
 * reports the exit status the run earned.
 */
int finishFabricRunWithStatus(ReactHost& reactHost, StartedFabricRun& startedRun) {
    startedRun.fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    startedRun.fabricHost.reset();

    return startedRun.hasCommitted && !reactHost.hasReportedFatalError() ? 0 : 1;
}

/**
 * Reads the scene out and tears the host down, in the one order that is safe: both readings happen before the
 * surface is stopped, because stopping it commits an empty tree, and the JavaScript thread is drained before the
 * host is destroyed so the queued unmount runs while the scheduler delegate is still alive.
 */
FabricRunResult finishFabricRun(ReactHost& reactHost, std::unique_ptr<FabricHost>& fabricHost) {
    settlePendingImageDecodes();

    SceneSnapshot scene = fabricHost->snapshotScene();
    std::string sceneDump = fabricHost->dumpScene();

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    return FabricRunResult{.scene = std::move(scene),
                           .sceneDump = std::move(sceneDump),
                           .hasReportedFatalError = reactHost.hasReportedFatalError()};
}

/**
 * A started bundle whose wheel gesture has already been turned into a settled scroll position: the half both
 * wheel-driven runs share, up to and including the beat that reports it.
 *
 * The glide is integrated at a fixed 60 Hz step rather than at whatever rate this machine loops at, and the beat is
 * induced after it rather than inside it — a state update replaces the previous one for the same node, so only the
 * last position could survive the flush anyway, and inducing per frame would buy a JavaScript round trip per frame
 * and no extra assertion. Nothing here waits for pending timers: a fixture that arms a commit from its scroll event
 * must not have it land before the caller has looked at the scene it is going to change.
 */
struct WheelDrivenRun {
    std::unique_ptr<FabricHost> fabricHost;
    bool hasCommitted{false};
};

WheelDrivenRun startWheelDrivenRun(ReactHost& reactHost, const std::string& bundlePath,
                                   facebook::react::Size surfaceSize, facebook::react::Point surfacePoint,
                                   int wheelNotches) {
    WheelDrivenRun run{.fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize)};

    run.hasCommitted = !waitForFirstCommit(reactHost, *run.fabricHost, true).empty();

    run.fabricHost->dispatchInput(makeWheelFrame(surfacePoint, wheelNotches));

    for (size_t frame = 0;
         frame < kMaximumInjectedScrollFrames && run.fabricHost->advanceScroll(kInjectedFrameMilliseconds);
         ++frame) {
    }

    run.fabricHost->induceEventBeat();

    return run;
}

/**
 * A move onto `surfacePoint`, a press and a release there, one frame apiece — the click every injected-click run
 * delivers, whether it is proving a hit test or a focus change.
 */
void deliverClickFrames(ReactHost& reactHost, FabricHost& fabricHost, facebook::react::Point surfacePoint) {
    deliverInputFrame(reactHost, fabricHost, makeMotionFrame(surfacePoint));
    deliverInputFrame(reactHost, fabricHost,
                      {InputEvent{.kind = InputEventKind::PointerButtonPress, .surfacePoint = surfacePoint}});
    deliverInputFrame(reactHost, fabricHost,
                      {InputEvent{.kind = InputEventKind::PointerButtonRelease, .surfacePoint = surfacePoint}});
}

} // namespace

int runInjectedClick(const std::string& bundlePath, facebook::react::Point surfacePoint) {
    ReactHost reactHost;
    StartedFabricRun startedRun = startFabricRunAtFirstCommit(reactHost, bundlePath, "click");

    deliverClickFrames(reactHost, *startedRun.fabricHost, surfacePoint);

    return finishFabricRunWithStatus(reactHost, startedRun);
}

int runAnimatedScroll(const std::string& bundlePath, facebook::react::Point surfacePoint, int wheelNotches) {
    ReactHost reactHost;
    StartedFabricRun startedRun = startFabricRunAtFirstCommit(reactHost, bundlePath, "scroll");
    FabricHost& fabricHost = *startedRun.fabricHost;

    // One frame before the wheel, because `NativeAnimatedNodesManager` ignores an event that does not arrive on
    // the thread an animation frame has run on, and the bundle's operation batch is what that frame drains. A
    // window ticks from the first frame it draws; a headless run has to spend a frame on it.
    deliverInputFrame(reactHost, fabricHost, {});

    fabricHost.dispatchInput(makeWheelFrame(surfacePoint, wheelNotches));

    for (size_t frame = 0; frame < kMaximumInjectedScrollFrames; ++frame) {
        const bool isScrolling = fabricHost.advanceScroll(kInjectedFrameMilliseconds);

        fabricHost.induceEventBeat();
        fabricHost.tickAnimations(std::chrono::steady_clock::now());

        if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
            std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
        }

        if (!isScrolling) {
            break;
        }
    }

    return finishFabricRunWithStatus(reactHost, startedRun);
}

int runResizedFabricBundle(const std::string& bundlePath, facebook::react::Size resizedSurfaceSize) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, kHeadlessSurfaceSize);

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    // The pair a window applies on `xdg_toplevel.configure`, in the same order: new layout constraints for Fabric,
    // the same extent for `Dimensions`, then the one change event the frame is allowed to emit.
    fabricHost->setSurfaceSize(resizedSurfaceSize);
    configureDimensions(reactHost, resizedSurfaceSize);
    reactHost.publishPendingDimensions();

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    return reactHost.hasReportedFatalError() ? 1 : 0;
}

FabricFrameRunResult runFabricBundleAcrossFrames(const std::string& bundlePath, facebook::react::Size surfaceSize) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);
    FabricFrameRunResult result{.firstScene = waitForFirstCommit(reactHost, *fabricHost, false)};

    // The same settling every single-frame golden gets, so the second snapshot is the one the golden paints.
    for (size_t frame = 0; frame < kHeadlessFrameCount; ++frame) {
        deliverInputFrame(reactHost, *fabricHost, {});
    }

    settlePendingImageDecodes();

    result.settledScene = fabricHost->snapshotScene();

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    result.hasReportedFatalError = reactHost.hasReportedFatalError();

    return result;
}

FabricDamageRunResult runFabricBundleAcrossCommits(const std::string& bundlePath, facebook::react::Size surfaceSize) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    FabricDamageRunResult result{.firstScene = waitForFirstCommit(reactHost, *fabricHost, true)};

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    settlePendingImageDecodes();

    SceneFrame secondFrame = fabricHost->takeFrame();

    result.secondScene = std::move(secondFrame.scene);
    result.damage = std::move(secondFrame.damage);

    if (result.firstScene.empty()) {
        result.failure = "the bundle committed no scene before its second commit";
    } else if (result.damage.empty()) {
        result.failure = "the bundle produced no damage after its first commit";
    }

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    result.hasReportedFatalError = reactHost.hasReportedFatalError();

    return result;
}

FabricRunResult runScrolledFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                        facebook::react::Point surfacePoint, int wheelNotches) {
    ReactHost reactHost;
    WheelDrivenRun run = startWheelDrivenRun(reactHost, bundlePath, surfaceSize, surfacePoint, wheelNotches);

    if (!run.hasCommitted) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to scroll" << std::endl;
    }

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    return finishFabricRun(reactHost, run.fabricHost);
}

FabricPrependRunResult runFabricBundleAcrossPrepend(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                                    facebook::react::Point surfacePoint, int wheelNotches) {
    ReactHost reactHost;
    WheelDrivenRun run = startWheelDrivenRun(reactHost, bundlePath, surfaceSize, surfacePoint, wheelNotches);
    FabricPrependRunResult result;

    if (!run.hasCommitted) {
        result.failure = "the bundle committed no scene, so there is nothing to scroll";
    }

    reactHost.drainJavaScriptThread();

    result.beforeScene = run.fabricHost->snapshotScene();

    // The prepend is a timer the fixture arms from the scroll event the drain above delivered, so it cannot have
    // landed before this line and cannot fail to land after it.
    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    run.fabricHost->advanceScroll(kInjectedFrameMilliseconds);
    run.fabricHost->induceEventBeat();
    reactHost.drainJavaScriptThread();

    result.afterScene = run.fabricHost->snapshotScene();

    run.fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    run.fabricHost.reset();

    result.hasReportedFatalError = reactHost.hasReportedFatalError();

    return result;
}

/**
 * Boots a bundle at `surfaceSize` and waits for its first commit, warning if there was none — the setup every
 * focus proof shares, whether what follows is a Tab press or a click.
 */
std::unique_ptr<FabricHost> startFabricRunWarningIfEmptyForFocus(ReactHost& reactHost, const std::string& bundlePath,
                                                                 facebook::react::Size surfaceSize) {
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    if (waitForFirstCommit(reactHost, *fabricHost, true).empty()) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to focus" << std::endl;
    }

    return fabricHost;
}

FabricRunResult runFocusTabbedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                           int tabPresses) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRunWarningIfEmptyForFocus(reactHost, bundlePath, surfaceSize);

    // One press per frame, press and release together, because that is what a compositor delivers: a key held
    // across a frame boundary is a repeat, and repeats are not synthesised. See *Input* in docs/cpp-toolchain.md.
    for (int press = 0; press < tabPresses; ++press) {
        deliverInputFrame(
            reactHost, *fabricHost,
            {InputEvent{.kind = InputEventKind::KeyPress, .key = kTabKeyName, .code = kTabKeyCode},
             InputEvent{.kind = InputEventKind::KeyRelease, .key = kTabKeyName, .code = kTabKeyCode}});
    }

    return finishFabricRun(reactHost, fabricHost);
}

FabricRunResult runFocusClickedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                            facebook::react::Point surfacePoint) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRunWarningIfEmptyForFocus(reactHost, bundlePath, surfaceSize);

    deliverClickFrames(reactHost, *fabricHost, surfacePoint);

    return finishFabricRun(reactHost, fabricHost);
}

FabricRunResult runFocusCommandedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                              facebook::react::Tag focusedTag) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRunWarningIfEmptyForFocus(reactHost, bundlePath, surfaceSize);

    // Synthesised directly, the way `--focus-click` synthesises the pointer events a real click would have
    // produced — a bundle's own `dispatchCommand` call schedules a rendering update on the runtime scheduler
    // rather than reaching the queue in the same script turn that called it, and this run needs to count frames
    // from a known starting point rather than guess how many that scheduled update needs to land in.
    fabricHost->injectFocusCommand(focusedTag);

    // Two frames, and not one, because of where the event beat sits relative to `advanceScroll` rather than
    // anything issue #248 could shrink further: `deliverInputFrame` induces the beat *before* `advanceScroll`
    // runs, so the first frame's `advanceScroll` is what has to resolve the `focus` command and its own `scrollTo`
    // together — the one guarantee `FabricHost::advanceScroll`'s second drain exists for — but the state write
    // that `scrollTo` queues has no beat left to ride out on this frame, and rides the second frame's instead. A
    // headless run that skipped the second-drain fix would need a third frame here rather than a second one: the
    // first frame would recognise the `focus` command alone, the second would recognise the `scrollTo` it left
    // behind, and only the third would carry the resulting state write out.
    deliverInputFrame(reactHost, *fabricHost, {});
    deliverInputFrame(reactHost, *fabricHost, {});

    return finishFabricRun(reactHost, fabricHost);
}

FabricRunResult runAnimatedImageFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                             int frameCount) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    if (waitForFirstCommit(reactHost, *fabricHost, true).empty()) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to animate" << std::endl;
    }

    // The frames and the settle that every other golden gets, before a single animation frame is advanced: an
    // `<Image>` mounts with the `Invalid` source `ImageShadowNode::initialStateData` seeds and only reaches its
    // real one on the commit its first layout produces, so advancing before that has nothing to advance and the
    // picture would depend on which of the two the run happened to catch.
    for (size_t frame = 0; frame < kHeadlessFrameCount; ++frame) {
        deliverInputFrame(reactHost, *fabricHost, {});
    }

    settlePendingImageDecodes();

    for (int frame = 0; frame < frameCount; ++frame) {
        fabricHost->advanceImageAnimations(kInjectedFrameMilliseconds);
    }

    return finishFabricRun(reactHost, fabricHost);
}

FabricRunResult runTypedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                     const std::string& keySequence) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    if (waitForFirstCommit(reactHost, *fabricHost, true).empty()) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to type into"
                  << std::endl;
    }

    // The same Tab a user presses to reach the field, through the same traversal `--focus-tab` proves: the
    // fixture puts the input first, so one press focuses it and the compositor's text input is enabled.
    deliverInputFrame(reactHost, *fabricHost,
                      {InputEvent{.kind = InputEventKind::KeyPress, .key = kTabKeyName, .code = kTabKeyCode},
                       InputEvent{.kind = InputEventKind::KeyRelease, .key = kTabKeyName, .code = kTabKeyCode}});

    // One event per frame rather than one sequence per frame, because a frame is what the event beat batches and
    // a caret that moved twice inside one would only ever report where it ended up.
    for (const InputEvent& event : parseKeySequence(keySequence)) {
        deliverInputFrame(reactHost, *fabricHost, {event});
    }

    // One more empty frame, so the commit the last keystroke produced is reconciled before the scene is read.
    deliverInputFrame(reactHost, *fabricHost, {});

    return finishFabricRun(reactHost, fabricHost);
}

FabricRunResult runFabricBundle(const std::optional<std::string>& bundlePath, facebook::react::Size surfaceSize) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), surfaceSize);

    configureDimensions(reactHost, surfaceSize);
    loadAndSettle(reactHost, bundlePath);

    // Empty frames before the scene is read, because a frame is not only what the compositor sent: it is also
    // where every mounted `<TextInput>` publishes its buffer into the shadow tree. Without one a `secureTextEntry`
    // field would rasterise from the string React mounted rather than from the masked one, which is the whole of
    // react-native-macos#423.
    //
    // A window supplies frames for as long as it is open; a headless run has to say how many. It is more than one
    // because a frame is also the animation backend's only tick: the first runs the operations a batch queued and
    // steps its drivers, and a value the resulting callback makes JavaScript ask for again is answered by the
    // next. See *Animation choreographer* in docs/cpp-toolchain.md.
    for (size_t frame = 0; frame < kHeadlessFrameCount; ++frame) {
        deliverInputFrame(reactHost, *fabricHost, {});
    }

    return finishFabricRun(reactHost, fabricHost);
}

FabricHitPaintRunResult runHitSampledFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                                  int sampleStep) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), surfaceSize);

    configureDimensions(reactHost, surfaceSize);
    loadAndSettle(reactHost, bundlePath);

    for (size_t frame = 0; frame < kHeadlessFrameCount; ++frame) {
        deliverInputFrame(reactHost, *fabricHost, {});
    }

    settlePendingImageDecodes();

    std::vector<FabricHitSample> hits;

    // At the centre of each sampled pixel, not at its corner: a pixel's colour describes the area around its
    // centre, so that is the point whose hit answer the colour can be compared against. On a rotated edge the
    // difference is the whole disagreement — the corner of a fully painted pixel can sit outside the shape.
    constexpr facebook::react::Float kPixelCentre = 0.5F;

    for (int y = 0; y < static_cast<int>(surfaceSize.height); y += sampleStep) {
        for (int x = 0; x < static_cast<int>(surfaceSize.width); x += sampleStep) {
            const facebook::react::Point point{.x = static_cast<facebook::react::Float>(x) + kPixelCentre,
                                               .y = static_cast<facebook::react::Float>(y) + kPixelCentre};

            hits.push_back(FabricHitSample{.point = point, .tag = fabricHost->findNodeAtPoint(point).tag});
        }
    }

    const FabricRunResult run = finishFabricRun(reactHost, fabricHost);

    return FabricHitPaintRunResult{.scene = run.scene,
                                   .hits = std::move(hits),
                                   .hasReportedFatalError = run.hasReportedFatalError};
}

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode) {
    if (bundleMode == BundleMode::Fabric) {
        const FabricRunResult result = runFabricBundle(bundlePath, kHeadlessSurfaceSize);

        std::cout << result.sceneDump << std::flush;

        return result.hasReportedFatalError ? 1 : 0;
    }

    ReactHost reactHost;

    loadAndSettle(reactHost, bundlePath);

    return reactHost.hasReportedFatalError() ? 1 : 0;
}

} // namespace react_native_linux
