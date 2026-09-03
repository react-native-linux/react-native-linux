#include "WindowSession.h"

#include "DimensionsSource.h"

#include <cxxreact/JSBigString.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Size.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace react_native_linux {

namespace {

facebook::react::Size toSurfaceSize(WindowSize size) {
    return facebook::react::Size{.width = static_cast<facebook::react::Float>(size.width),
                                 .height = static_cast<facebook::react::Float>(size.height)};
}

} // namespace

WindowSession::WindowSession(const std::string& bundlePath, WindowSize size)
    : fabricHost_(std::make_unique<FabricHost>(reactHost_.reactInstance(), toSurfaceSize(size))) {
    // Before the script, so the first `Dimensions.get` a bundle makes at module scope already answers with the
    // window's requested size rather than with the pre-configure default.
    configureDimensions(size);
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
    const double frameMilliseconds = takeFrameMilliseconds();

    // Once per frame, whatever the compositor sent: this is what turns any number of configures since the last
    // frame into at most one `didUpdateDimensions`.
    reactHost_.publishPendingDimensions();

    // The blink is advanced before the frame's input, because dispatching input is also what republishes the
    // caret into the scene: toggling afterwards would show every phase one frame late.
    fabricHost_->advanceCaretBlink(frameMilliseconds);
    fabricHost_->dispatchInput(events);
    fabricHost_->advanceScroll(frameMilliseconds);
    fabricHost_->induceEventBeat();
}

FrameClock::Tick WindowSession::recordFrameTick(FrameClock::Source source, std::chrono::steady_clock::time_point now) {
    if (source == FrameClock::Source::Callback) {
        return frameClock_.onFrameCallback(now);
    }

    return frameClock_.onFallbackTimeout(now, hasPendingWork());
}

const FrameClock& WindowSession::frameClock() const noexcept { return frameClock_; }

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

} // namespace react_native_linux
