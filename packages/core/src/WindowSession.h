#pragma once

#include "FabricHost.h"
#include "FrameClock.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "ReactHost.h"
#include "WaylandWindow.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * A React Native session bound to one window: a bridgeless instance, a Fabric surface sized by the window, and
 * the bundle that drives them, kept alive for as long as the window is open.
 *
 * This is the persistent counterpart of `BundleRunner`, which runs a bundle to quiescence and exits. A window
 * outlives quiescence by definition, so the session never waits for the JavaScript thread to go quiet; it only
 * boots it, keeps it, and tears it down.
 *
 * Threading contract, per ADR-0001 decision 6: this object is constructed, used and destroyed on the platform
 * frame thread — the thread that owns the Wayland connection and the run loop. JavaScript runs on the instance's
 * own JavaScript thread, and a commit driven from JavaScript is mounted there too, when the `RuntimeScheduler`
 * drains its rendering update. The two members the frame thread calls while that is happening are `takeFrame`,
 * which copies the scene and its accumulated damage out under the mounting manager's mutex, and `resize`, which
 * commits new layout constraints through `SurfaceHandler`. That commit carries the default commit options, whose
 * `mountSynchronously` is true, so a resize relayouts and mounts on the frame thread itself. Thread affinity is
 * therefore not what keeps the scene consistent; the mounting manager's mutex is.
 *
 * `deliverInput` is the third, and is called once per frame whether or not the compositor sent anything: it
 * hit-tests and enqueues the frame's events, integrates a frame of scroll physics, and then induces the event
 * beat, which is what releases everything Fabric has queued — input, scroll-position state updates, layout
 * events, JavaScript-driven events — onto the JavaScript thread at frame cadence instead of per raw compositor
 * event. The session is where the frame clock lives, because the scroll physics is the first thing in this stack
 * that needs to know how long the last frame took.
 *
 * The same beat is what publishes `Dimensions`: `resize` records the configured extent in the host's
 * `DimensionsSource` and `deliverInput` emits at most one `didUpdateDimensions` per frame for whatever accumulated
 * there, so a compositor that sends a burst of configures during an interactive resize cannot re-render a
 * `useWindowDimensions` consumer once per event. See *Dimensions and TurboModules* in docs/cpp-toolchain.md.
 *
 * `recordFrameTick` is a second, separate clock: `FrameClock` decides whether the *paint* — `takeFrame` plus the
 * renderer's present — happens at all this iteration, which `deliverInput`'s per-input frame timing does not need
 * to know about. See *Frame clock* in docs/cpp-toolchain.md for why the two are independent.
 *
 * Shutdown contract: destruction stops the surface, drains the JavaScript thread so the queued unmount runs while
 * the scheduler delegate is still alive, and only then destroys the Fabric host and the instance, in that order.
 */
class WindowSession final {
public:
    WindowSession(const std::string& bundlePath, WindowSize size);
    WindowSession(const WindowSession&) = delete;
    WindowSession(WindowSession&&) = delete;
    WindowSession& operator=(const WindowSession&) = delete;
    WindowSession& operator=(WindowSession&&) = delete;
    ~WindowSession() noexcept;

    void resize(WindowSize size);

    /**
     * Registers the seat's `zwp_text_input_v3` with the focus model, so the compositor's text input is enabled
     * exactly while a text component holds focus. Null when the compositor advertises no text-input manager.
     */
    void setTextInputFocusSink(TextInputFocusSink* textInputFocusSink);
    void deliverInput(const std::vector<InputEvent>& events);

    /**
     * Feeds one `WaylandWindow::waitForRedraw` outcome to the frame clock and returns its draw decision. `source`
     * is `Callback` when `WaylandWindow::hasFrameCallbackFired` was true and `Timer` otherwise; the pending-work
     * flag `FrameClock` needs for a `Timer` tick is computed here from the Fabric host and the JS timer registry,
     * so the caller only has to say which frame source woke it.
     */
    FrameClock::Tick recordFrameTick(FrameClock::Source source, std::chrono::steady_clock::time_point now);
    SceneFrame takeFrame();
    bool hasReportedFatalError() const;

    /**
     * Liveness counters for the frame clock. There is no Tracy integration yet; this is a plain getter until one
     * exists.
     */
    const FrameClock& frameClock() const noexcept;

private:
    void configureDimensions(WindowSize size);
    double takeFrameMilliseconds();
    bool hasPendingWork() const;

    ReactHost reactHost_;
    std::unique_ptr<FabricHost> fabricHost_;
    std::chrono::steady_clock::time_point lastFrameTime_{std::chrono::steady_clock::now()};
    FrameClock frameClock_;
};

} // namespace react_native_linux
