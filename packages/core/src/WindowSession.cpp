#include "WindowSession.h"

#include "AsyncStorage.h"
#include "DimensionsSource.h"
#include "ExpoAsset.h"
#include "ExpoFileSystem.h"
#include "ExpoSQLite.h"
#include "ExpoSecureStore.h"
#include "FrameProfiling.h"
#include "I18n.h"

#ifdef RNL_ENABLE_SECRET_SERVICE
#include "SecretService.h"
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Size.h>

namespace react_native_linux {

namespace {

facebook::react::Size toSurfaceSize(WindowSize size) {
    return facebook::react::Size{.width = static_cast<facebook::react::Float>(size.width),
                                 .height = static_cast<facebook::react::Float>(size.height)};
}

// A fixed nominal refresh, matching the 16.7 ms CI regression budget documented for the animated-frames scenario
// in *Frame timing* (docs/cpp-toolchain.md): a real per-output refresh-rate-driven threshold needs the frame
// journal to see `wp_presentation`'s refresh hint, which `WindowSession` does not have — it knows nothing about
// Wayland. Reading that hint into the threshold is a follow-up, not this change.
constexpr uint64_t kNominalVsyncNanoseconds = 16'666'666;
constexpr uint64_t kHangThresholdVsyncCount = 2;

uint64_t toNanosecondsSinceEpoch(std::chrono::steady_clock::time_point timePoint) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint.time_since_epoch()).count());
}

} // namespace

WindowSession::WindowSession(const std::string& bundlePath, WindowSize size, double scale,
                             const std::string& asyncStorageDatabasePath,
                             std::optional<std::string> initialActivationUrl, std::string assetsDirectory)
    : fabricHost_(std::make_unique<FabricHost>(reactHost_.reactInstance(), toSurfaceSize(size))),
      frameJournal_(kNominalVsyncNanoseconds, kHangThresholdVsyncCount * kNominalVsyncNanoseconds) {
    // Before the script, so the first `Dimensions.get` a bundle makes at module scope already answers with the
    // window's requested size and scale rather than with the pre-configure default.
    resize(size, scale);
    seedColorScheme();
    const std::filesystem::path applicationDirectory = std::filesystem::path(asyncStorageDatabasePath).parent_path();
    const char* home = std::getenv("HOME");
    const std::optional<std::filesystem::path> cacheDirectory =
        fileSystemDirectories(applicationDirectory, std::getenv("XDG_CACHE_HOME"), home == nullptr ? "" : home).cache;

    reactHost_.keyValueStore().setDatabasePath(asyncStorageDatabasePath);
    reactHost_.sqliteDatabases().setDirectory((applicationDirectory / "SQLite").string());
    reactHost_.fileSystem().applicationDirectory = applicationDirectory;
#ifdef RNL_ENABLE_SECRET_SERVICE
    reactHost_.secureStore().configure(applicationDirectory, connectSessionKeyring);
#else
    reactHost_.secureStore().configure(applicationDirectory, [](const std::string&, const std::stop_token&) {
        return std::unique_ptr<SecretBackend>();
    });
#endif

    if (cacheDirectory.has_value()) {
        reactHost_.expoAssetDownloads().setDirectory(cacheDirectory.value() / "expo-asset");
    }

    // After the store has its file and before the script, so the first layout already has the persisted direction.
    reactHost_.i18n().restore();
    applyLayoutDirectionChange();

    if (initialActivationUrl.has_value()) {
        deliverActivationUrl(initialActivationUrl.value());
    }

    reactHost_.setAssetsDirectory(std::move(assetsDirectory));
    reactHost_.loadBundle(bundlePath);
    reactHost_.startHotModuleReplacement(bundlePath);
}

WindowSession::~WindowSession() noexcept {
    fabricHost_->stopSurface();
    reactHost_.drainJavaScriptThread();
}

void WindowSession::resize(WindowSize size, double scale) {
    fabricHost_->setSurfaceSize(toSurfaceSize(size), static_cast<facebook::react::Float>(scale));
    reactHost_.dimensions().configure(static_cast<double>(size.width), static_cast<double>(size.height), scale);
}

void WindowSession::setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) {
    fabricHost_->setTextInputFocusSink(textInputFocusSink);
}

void WindowSession::deliverInput(std::vector<InputEvent> events) {
    ZoneScopedN("input and event beat");
    // One sample of the client clock for the whole batch: the mapper uses it to establish the compositor offset on
    // the first timed event and every later event is shifted by that same offset (#455).
    const facebook::react::HighResTimeStamp receivedAt = facebook::react::HighResTimeStamp::now();

    for (InputEvent& event : events) {
        if (event.eventTimeMilliseconds != 0) {
            event.eventTime = eventTimeMapper_.map(event.eventTimeMilliseconds, receivedAt);
        }
    }

    // The frame journal's input tag (#345): the events this batch carries are charged to the next dirty edge, so
    // the presented frame that answers them names them in the frame log. Charged before anything else the batch
    // does, because everything below — dispatch, the event beat, the scroll integration — is work the tag answers.
    frameJournal_.recordInput(events.size(), earliestEventTimeNanoseconds(events));

    const double frameMilliseconds = takeFrameMilliseconds();

    // Once per frame, whatever the compositor sent: this is what turns any number of configures since the last
    // frame into at most one `didUpdateDimensions`.
    reactHost_.publishPendingDimensions();
    applyLayoutDirectionChange();

#ifdef RNL_ENABLE_APPEARANCE_PORTAL
    appearancePortal_.processPendingSignals(reactHost_.appearance());
#endif

    // The blink is advanced before the frame's input, because dispatching input is also what republishes the
    // caret into the scene: toggling afterwards would show every phase one frame late.
    fabricHost_->advanceCaretBlink(frameMilliseconds);
    fabricHost_->advanceImageAnimations(frameMilliseconds);
    fabricHost_->advanceControlAnimations(frameMilliseconds);
    fabricHost_->dispatchInput(events);
    fabricHost_->advanceScroll(frameMilliseconds);
    fabricHost_->induceEventBeat();
}

void WindowSession::tickAnimations(std::chrono::steady_clock::time_point now) {
    ZoneScopedN("animation tick");
    // Worklets' UI jobs and frame callbacks run first, so a `runOnUI` job that starts an animation, such as
    // Reanimated's `withTiming`, has its first step taken by the animation backend in this frame, not the next.
    // The JavaScript `requestAnimationFrame` dispatch this also posts runs on the JavaScript thread, concurrently
    // with the backend either way. Both are driven from here so a fallback-timeout frame — the only kind an occluded
    // window gets — drives them too.
    reactHost_.dispatchAnimationFrames(now);
    fabricHost_->tickAnimations(now);

    // The mutation an animation step produces lands here, after `recordFrameTick` has already read
    // `hasPendingWork` for this frame and before `takeFrame` consumes the damage it produced, so without this the
    // journal never sees the dirty edge of a natively driven animation: the step that lands the final value is
    // charged to no interval at all. `recordDamage` is edge-only, so a frame that was already dirty coalesces.
    if (hasPendingWork()) {
        frameJournal_.recordDamage(toNanosecondsSinceEpoch(now));
    }
}

void WindowSession::recordDrawOutcome(bool presented) noexcept { frameClock_.recordDrawOutcome(presented); }

FrameClock::Tick WindowSession::recordFrameTick(FrameClock::Source source, std::chrono::steady_clock::time_point now) {
    const bool hasPending = hasPendingWork();

    if (hasPending) {
        frameJournal_.recordDamage(toNanosecondsSinceEpoch(now));
    }

    if (source == FrameClock::Source::Callback) {
        return frameClock_.onFrameCallback(now, hasPending);
    }

    return frameClock_.onFallbackTimeout(now, hasPending);
}

const FrameClock& WindowSession::frameClock() const noexcept { return frameClock_; }

void WindowSession::deliverActivationUrl(const std::string& url) {
    reactHost_.activation().onActivationUrlReceived(url);
}

void WindowSession::seedColorScheme() {
#ifdef RNL_ENABLE_APPEARANCE_PORTAL
    const std::optional<ColorScheme> portalColorScheme = appearancePortal_.initialColorScheme();

    if (portalColorScheme.has_value()) {
        reactHost_.appearance().onPortalColorSchemeChanged(portalColorScheme.value());
    }
#endif
}

void WindowSession::recordPaintStart(std::chrono::steady_clock::time_point now) {
    frameJournal_.recordPaintStart(toNanosecondsSinceEpoch(now));
}

void WindowSession::recordPaintEnd(std::chrono::steady_clock::time_point now) {
    frameJournal_.recordPaintEnd(toNanosecondsSinceEpoch(now));
}

std::optional<FrameJournal::ClosedFrame> WindowSession::closeJournalFrame(uint64_t presentedNanoseconds) {
    return frameJournal_.recordPresented(presentedNanoseconds);
}

void WindowSession::reportJournalDiscontinuity() { frameJournal_.recordDiscontinuity(); }

FrameJournal::Summary WindowSession::frameJournalSummary() const { return frameJournal_.summarise(); }

bool WindowSession::hasPendingWork() const {
    return fabricHost_->hasPendingWork() || reactHost_.hasPendingFrameRequests();
}

double WindowSession::takeFrameMilliseconds() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(now - lastFrameTime_).count();

    lastFrameTime_ = now;

    return elapsed;
}

SceneFrame WindowSession::takeFrame() {
    ZoneScopedN("take frame");

    return fabricHost_->takeFrame();
}

void WindowSession::applyLayoutDirectionChange() {
    const std::optional<LayoutDirectionRequest> change = reactHost_.i18n().takeLayoutDirectionChange();

    if (change.has_value()) {
        fabricHost_->setLayoutDirection(change.value());
    }
}

bool WindowSession::hasReportedFatalError() const { return reactHost_.hasReportedFatalError(); }

bool WindowSession::isReloadRequested() const { return reactHost_.isReloadRequested(); }

SceneNodes WindowSession::visualTreeNodes() const { return fabricHost_->visualTreeNodes(); }

std::vector<AccessibilityChange> WindowSession::takeAccessibilityChanges() {
    return fabricHost_->takeAccessibilityChanges();
}

void WindowSession::blockJavaScriptThread(std::chrono::milliseconds duration) {
    reactHost_.blockJavaScriptThread(duration);
}

bool WindowSession::hasMarkedTestPassed() const { return reactHost_.hasMarkedTestPassed(); }

} // namespace react_native_linux
