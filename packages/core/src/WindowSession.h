#pragma once

#include "FabricHost.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "ReactHost.h"
#include "WaylandWindow.h"

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
 * hit-tests and enqueues the frame's events and then induces the event beat, which is what releases everything
 * Fabric has queued — input, layout events, JavaScript-driven events — onto the JavaScript thread at frame
 * cadence instead of per raw compositor event.
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
    void deliverInput(const std::vector<InputEvent>& events);
    SceneFrame takeFrame();
    bool hasReportedFatalError() const;

private:
    ReactHost reactHost_;
    std::unique_ptr<FabricHost> fabricHost_;
};

} // namespace react_native_linux
