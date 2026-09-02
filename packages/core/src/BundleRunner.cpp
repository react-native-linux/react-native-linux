#include "BundleRunner.h"

#include "FabricHost.h"
#include "InputPipeline.h"
#include "ReactHost.h"

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

void loadAndSettle(ReactHost& reactHost, const std::optional<std::string>& bundlePath) {
    std::unique_ptr<const facebook::react::JSBigString> script = readScript(bundlePath);

    reactHost.loadScript(std::move(script), bundlePath.value_or(kSmokeSourceUrl));

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }
}

/**
 * Waits for the first commit to reach the scene and returns it, leaving the damage take that came with it behind.
 *
 * There is no callback for "a transaction was mounted", so this polls: drain the JavaScript thread, look, repeat.
 * The bundle's second commit is timer-driven and therefore far away in wall-clock terms, which is what makes a
 * poll good enough to land between the two. A run that misses that window ends with no damage left to take, and
 * the caller reports that rather than producing a golden that proves nothing.
 */
SceneSnapshot waitForFirstCommit(ReactHost& reactHost, FabricHost& fabricHost) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kFirstCommitBudget;

    while (std::chrono::steady_clock::now() < deadline) {
        reactHost.drainJavaScriptThread();

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

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }
}

std::unique_ptr<FabricHost> startFabricRun(ReactHost& reactHost, const std::string& bundlePath,
                                           facebook::react::Size surfaceSize) {
    std::unique_ptr<FabricHost> fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), surfaceSize);

    reactHost.loadScript(facebook::react::JSBigFileString::fromPath(bundlePath), bundlePath);

    return fabricHost;
}

/**
 * Reads the scene out and tears the host down, in the one order that is safe: both readings happen before the
 * surface is stopped, because stopping it commits an empty tree, and the JavaScript thread is drained before the
 * host is destroyed so the queued unmount runs while the scheduler delegate is still alive.
 */
FabricRunResult finishFabricRun(ReactHost& reactHost, std::unique_ptr<FabricHost>& fabricHost) {
    SceneSnapshot scene = fabricHost->snapshotScene();
    std::string sceneDump = fabricHost->dumpScene();

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    return FabricRunResult{.scene = std::move(scene),
                           .sceneDump = std::move(sceneDump),
                           .hasReportedFatalError = reactHost.hasReportedFatalError()};
}

} // namespace

int runInjectedClick(const std::string& bundlePath, facebook::react::Point surfacePoint) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, kHeadlessSurfaceSize);

    const bool hasCommitted = !waitForFirstCommit(reactHost, *fabricHost).empty();

    if (!hasCommitted) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to click" << std::endl;
    }

    deliverInputFrame(reactHost, *fabricHost, makeMotionFrame(surfacePoint));
    deliverInputFrame(reactHost, *fabricHost,
                      {InputEvent{.kind = InputEventKind::PointerButtonPress, .surfacePoint = surfacePoint}});
    deliverInputFrame(reactHost, *fabricHost,
                      {InputEvent{.kind = InputEventKind::PointerButtonRelease, .surfacePoint = surfacePoint}});

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    return hasCommitted && !reactHost.hasReportedFatalError() ? 0 : 1;
}

FabricDamageRunResult runFabricBundleAcrossCommits(const std::string& bundlePath, facebook::react::Size surfaceSize) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    FabricDamageRunResult result{.firstScene = waitForFirstCommit(reactHost, *fabricHost)};

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

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
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    if (waitForFirstCommit(reactHost, *fabricHost).empty()) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to scroll" << std::endl;
    }

    fabricHost->dispatchInput(makeWheelFrame(surfacePoint, wheelNotches));

    // The beat is induced after the glide rather than inside it: a state update replaces the previous one for the
    // same node, so only the last position could survive the flush anyway, and inducing per frame would buy this
    // run a JavaScript round trip per frame and no extra assertion.
    for (size_t frame = 0;
         frame < kMaximumInjectedScrollFrames && fabricHost->advanceScroll(kInjectedFrameMilliseconds); ++frame) {
    }

    fabricHost->induceEventBeat();

    if (!reactHost.runUntilQuiescent(kQuiescenceBudget)) {
        std::cerr << "[bundle-runner] gave up waiting for pending timers" << std::endl;
    }

    return finishFabricRun(reactHost, fabricHost);
}

FabricRunResult runFocusTabbedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                           int tabPresses) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    if (waitForFirstCommit(reactHost, *fabricHost).empty()) {
        std::cerr << "[bundle-runner] the bundle committed no scene, so there is nothing to focus" << std::endl;
    }

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

FabricRunResult runTypedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                     const std::string& keySequence) {
    ReactHost reactHost;
    std::unique_ptr<FabricHost> fabricHost = startFabricRun(reactHost, bundlePath, surfaceSize);

    if (waitForFirstCommit(reactHost, *fabricHost).empty()) {
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

    loadAndSettle(reactHost, bundlePath);

    // One empty frame before the scene is read, because a frame is not only what the compositor sent: it is also
    // where every mounted `<TextInput>` publishes its buffer into the shadow tree. Without it a `secureTextEntry`
    // field would rasterise from the string React mounted rather than from the masked one, which is the whole of
    // react-native-macos#423. A window does this every frame; a headless render has to do it once.
    deliverInputFrame(reactHost, *fabricHost, {});

    return finishFabricRun(reactHost, fabricHost);
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
