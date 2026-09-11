#pragma once

#ifdef RNL_ENABLE_APPEARANCE_PORTAL
#include "AppearancePortal.h"
#endif

#include "FabricHost.h"
#include "FrameClock.h"
#include "FrameJournal.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "ReactHost.h"
#include "WaylandWindow.h"

#include <chrono>
#include <memory>
#include <optional>
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
 * `deliverInput` pumps the appearance portal on the same beat and for the same reason (#52): the D-Bus
 * connection is polled where the frame already is, so `org.freedesktop.portal.Settings`'s `SettingChanged`
 * callback runs on the frame thread and the unsynchronised `AppearanceModel` behind `Appearance.getColorScheme()`
 * has exactly one writer. See *Appearance and PlatformColor* in docs/cpp-toolchain.md.
 *
 * `recordFrameTick` is a second, separate clock: `FrameClock` decides whether the *paint* — `takeFrame` plus the
 * renderer's present — happens at all this iteration, which `deliverInput`'s per-input frame timing does not need
 * to know about. See *Frame clock* in docs/cpp-toolchain.md for why the two are independent.
 *
 * `recordFrameTick` is where the frame journal of #345 sees its dirty edge: it marks `FrameJournal` dirty from
 * the same `hasPendingWork` signal the fallback timeout already reads, on every call regardless of source,
 * because an invalidation exists independently of whichever frame source wakes the loop that answers it.
 * `tickAnimations` marks it again on the same terms, because an animation step's mutation lands after
 * `recordFrameTick` has read that signal and before `takeFrame` consumes it.
 * `recordPaintStart`/`recordPaintEnd` bracket the paint span `WindowMain` runs between `takeFrame` and the
 * renderer's present, and `closeJournalFrame` closes the interval once a `wp_presentation` result exists for it.
 * `deliverInput` feeds the batch's size to `FrameJournal::recordInput` before dispatching, which is what makes
 * an injected input event traceable to the presented frame that answered it: the closed frame's log line names
 * how many input events the window had received but no presented frame had yet answered when its dirty edge
 * fired. See *Frame journal* in docs/cpp-toolchain.md.
 *
 * Shutdown contract: destruction stops the surface, drains the JavaScript thread so the queued unmount runs while
 * the scheduler delegate is still alive, and only then destroys the Fabric host and the instance, in that order.
 */
class WindowSession final {
public:
    WindowSession(const std::string& bundlePath, WindowSize size,
                  std::optional<std::string> initialActivationUrl = std::nullopt);
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
    void deliverInput(std::vector<InputEvent> events);

    /**
     * Feeds an activation URL to `Linking` (#363): this process's own launch `argv`, seeded once before the
     * bundle loads, or a later instance's forwarded `argv`, delivered as it arrives. See `ActivationModel`.
     */
    void deliverActivationUrl(const std::string& url);

    /** Delegates to `FabricHost::tickAnimations` once per drawn frame. */
    void tickAnimations(std::chrono::steady_clock::time_point now);

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
     * Brackets the paint span the caller runs between `takeFrame` and the renderer's present. Both are no-ops
     * when the frame journal has no open interval — a callback-driven draw of an unchanged picture still paints
     * without ever having been dirty, and the journal must not fabricate a dirty edge to explain it.
     */
    void recordPaintStart(std::chrono::steady_clock::time_point now);
    void recordPaintEnd(std::chrono::steady_clock::time_point now);

    /**
     * Closes the frame journal's open interval, if any, against a `wp_presentation` result. `std::nullopt` is the
     * idle-boundary outcome — nothing was dirty since the last close — not an error.
     */
    std::optional<FrameJournal::ClosedFrame> closeJournalFrame(uint64_t presentedNanoseconds);
    /** A presentation the compositor discarded: abandons the open interval so no latency reads across it. */
    void reportJournalDiscontinuity();
    FrameJournal::Summary frameJournalSummary() const;

    /**
     * The three questions the automation channel (#214) asks of a running session: the committed tree, copied
     * out under the mounting manager's mutex exactly as `takeFrame` is; a real block of the JavaScript thread;
     * and whether the bundle called `globalThis.__rnlMarkTestPassed()`. Called from the frame thread, between
     * frames. See *The automation channel* in docs/cpp-toolchain.md.
     */
    SceneNodes visualTreeNodes() const;

    /** `FabricHost::takeAccessibilityChanges`, for `ListAccessibilityChanges` (#264). */
    std::vector<AccessibilityChange> takeAccessibilityChanges();
    void blockJavaScriptThread(std::chrono::milliseconds duration);
    bool hasMarkedTestPassed() const;

    /**
     * Liveness counters for the frame clock. There is no Tracy integration yet; this is a plain getter until one
     * exists.
     */
    const FrameClock& frameClock() const noexcept;

private:
    void configureDimensions(WindowSize size);

    /**
     * Applies the portal's answer before the bundle runs, so a module-scope `Appearance.getColorScheme()` already
     * sees the desktop's scheme rather than `kFallbackColorScheme` followed by a change event one frame later.
     */
    void seedColorScheme();
    double takeFrameMilliseconds();
    bool hasPendingWork() const;

    ReactHost reactHost_;
#ifdef RNL_ENABLE_APPEARANCE_PORTAL
    AppearancePortal appearancePortal_;
#endif
    std::unique_ptr<FabricHost> fabricHost_;
    std::chrono::steady_clock::time_point lastFrameTime_{std::chrono::steady_clock::now()};
    FrameClock frameClock_;
    FrameJournal frameJournal_;
    EventTimeMapper eventTimeMapper_;
};

} // namespace react_native_linux
