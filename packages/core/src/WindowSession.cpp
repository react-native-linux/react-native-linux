#include "WindowSession.h"

#include "DimensionsSource.h"

#include <chrono>
#include <cxxreact/JSBigString.h>
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

WindowSession::WindowSession(const std::string& bundlePath, WindowSize size,
                             std::optional<std::string> initialActivationUrl)
    : fabricHost_(std::make_unique<FabricHost>(reactHost_.reactInstance(), toSurfaceSize(size))),
      frameJournal_(kNominalVsyncNanoseconds, kHangThresholdVsyncCount * kNominalVsyncNanoseconds) {
    // Before the script, so the first `Dimensions.get` a bundle makes at module scope already answers with the
    // window's requested size rather than with the pre-configure default.
    configureDimensions(size);
    seedColorScheme();

    if (initialActivationUrl.has_value()) {
        deliverActivationUrl(initialActivationUrl.value());
    }

    reactHost_.loadScript(facebook::react::JSBigFileString::fromPath(bundlePath), bundlePath);
}

WindowSession::~WindowSession() noexcept {
    fabricHost_->stopSurface();
    reactHost_.drainJavaScriptThread();
}

void WindowSession::resize(WindowSize size) {
    fabricHost_->setSurfaceSize(toSurfaceSize(size));
    configureDimensions(size);
}

void WindowSession::setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) {
    fabricHost_->setTextInputFocusSink(textInputFocusSink);
}

void WindowSession::deliverInput(const std::vector<InputEvent>& events) {
    // The frame journal's input tag (#345): the events this batch carries are charged to the next dirty edge, so
    // the presented frame that answers them names them in the frame log. Charged before anything else the batch
    // does, because everything below — dispatch, the event beat, the scroll integration — is work the tag answers.
    frameJournal_.recordInput(events.size(), earliestEventTimeNanoseconds(events));

    const double frameMilliseconds = takeFrameMilliseconds();

    // Once per frame, whatever the compositor sent: this is what turns any number of configures since the last
    // frame into at most one `didUpdateDimensions`.
    reactHost_.publishPendingDimensions();

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
    fabricHost_->tickAnimations(now);

    // The JavaScript half of the same frame: the native animation backend gets `now` directly, and the frame's
    // `requestAnimationFrame` callbacks get it through the JavaScript thread. Both are driven from here so a
    // fallback-timeout frame — the only kind an occluded window gets — drives them too.
    reactHost_.dispatchAnimationFrames(now);

    // The mutation an animation step produces lands here, after `recordFrameTick` has already read
    // `hasPendingWork` for this frame and before `takeFrame` consumes the damage it produced, so without this the
    // journal never sees the dirty edge of a natively driven animation: the step that lands the final value is
    // charged to no interval at all. `recordDamage` is edge-only, so a frame that was already dirty coalesces.
    if (hasPendingWork()) {
        frameJournal_.recordDamage(toNanosecondsSinceEpoch(now));
    }
}

FrameClock::Tick WindowSession::recordFrameTick(FrameClock::Source source, std::chrono::steady_clock::time_point now) {
    const bool hasPending = hasPendingWork();

    // The dirty edge exists independently of whichever frame source wakes this call: a callback tick draws
    // whether or not there is pending work, but the invalidation it might be answering was already there.
    if (hasPending) {
        frameJournal_.recordDamage(toNanosecondsSinceEpoch(now));
    }

    if (source == FrameClock::Source::Callback) {
        return frameClock_.onFrameCallback(now);
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

void WindowSession::configureDimensions(WindowSize size) {
    reactHost_.dimensions().configure(static_cast<double>(size.width), static_cast<double>(size.height),
                                      DimensionsSource::kDefaultScale);
}

bool WindowSession::hasPendingWork() const { return fabricHost_->hasPendingWork() || reactHost_.hasPendingTimers(); }

double WindowSession::takeFrameMilliseconds() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(now - lastFrameTime_).count();

    lastFrameTime_ = now;

    return elapsed;
}

SceneFrame WindowSession::takeFrame() { return fabricHost_->takeFrame(); }

bool WindowSession::hasReportedFatalError() const { return reactHost_.hasReportedFatalError(); }

SceneNodes WindowSession::visualTreeNodes() const { return fabricHost_->visualTreeNodes(); }

std::vector<AccessibilityChange> WindowSession::takeAccessibilityChanges() {
    return fabricHost_->takeAccessibilityChanges();
}

void WindowSession::blockJavaScriptThread(std::chrono::milliseconds duration) {
    reactHost_.blockJavaScriptThread(duration);
}

bool WindowSession::hasMarkedTestPassed() const { return reactHost_.hasMarkedTestPassed(); }

} // namespace react_native_linux
