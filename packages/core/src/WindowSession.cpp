#include "WindowSession.h"

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
    reactHost_.loadScript(facebook::react::JSBigFileString::fromPath(bundlePath), bundlePath);
}

WindowSession::~WindowSession() noexcept {
    fabricHost_->stopSurface();
    reactHost_.drainJavaScriptThread();
}

void WindowSession::resize(WindowSize size) { fabricHost_->setSurfaceSize(toSurfaceSize(size)); }

void WindowSession::deliverInput(const std::vector<InputEvent>& events) {
    fabricHost_->dispatchInput(events);
    fabricHost_->advanceScroll(takeFrameMilliseconds());
    fabricHost_->induceEventBeat();
}

double WindowSession::takeFrameMilliseconds() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(now - lastFrameTime_).count();

    lastFrameTime_ = now;

    return elapsed;
}

SceneFrame WindowSession::takeFrame() { return fabricHost_->takeFrame(); }

bool WindowSession::hasReportedFatalError() const { return reactHost_.hasReportedFatalError(); }

} // namespace react_native_linux
