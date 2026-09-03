#pragma once

#include <react/renderer/animationbackend/AnimationChoreographer.h>
#include <react/renderer/uimanager/UIManagerAnimationBackend.h>

#include <atomic>
#include <chrono>

namespace react_native_linux {

/**
 * The platform half of the shared animation backend: the frame source `AnimationBackend` drives its registered
 * callbacks from. Upstream names this the one part a new platform has to implement — on iOS it is `RCTDisplayLink`
 * and on Android the `Choreographer`; here it is the window loop's frame clock, so this class is only the flag and
 * the forwarding call between the two.
 *
 * `resume` is the backend saying "a callback is registered and I need frames"; `pause` is "the last one went
 * away". Nothing here schedules anything: the frame loop already ticks, and `isActive` is how it learns that an
 * animation is a reason to keep ticking even when the compositor withholds `wl_surface.frame`. That makes a
 * running animation the fourth pending-work signal beside mounting damage, active scroll physics and pending JS
 * timers. See *Frame clock* and *Animation choreographer* in docs/cpp-toolchain.md.
 *
 * `tick` delivers exactly one frame to the backend and carries the tick's own timestamp rather than a fresh clock
 * read, so every driver on that frame integrates against the same instant the frame clock measured. The timestamp
 * is `std::chrono::steady_clock` time since epoch as `AnimationTimestamp`
 * (`std::chrono::duration<double, std::milli>`), which is the basis of `AnimationChoreographer::now`'s default, so
 * a mutation pushed outside a frame and one produced inside one share a time base.
 *
 * Threading contract: `resume` and `pause` are called by `AnimationBackend::start` and `::stop`, which run on
 * whichever thread registered the callback — the JavaScript thread for an `Animated` batch, the frame thread when
 * a frame drains the last animation. `tick` and `isActive` are called by the frame loop on the frame thread. The
 * flag is therefore atomic; nothing else here is shared state, and the backend handle is upstream's own
 * `weak_ptr`, so a backend destroyed under a running loop makes `tick` a no-op instead of a crash.
 */
class LinuxAnimationChoreographer final : public facebook::react::AnimationChoreographer {
public:
    void resume() override;
    void pause() override;

    /**
     * Whether the backend currently has a frame callback registered, for the frame clock's pending-work decision.
     */
    bool isActive() const noexcept;

    /**
     * Delivers one animation frame to the backend, or nothing if no animation is running. Called once per drawn
     * frame, after the frame's input has been delivered and before the scene is taken, so the mutations the frame
     * produces are in the snapshot that frame paints.
     */
    void tick(std::chrono::steady_clock::time_point now);

private:
    std::atomic<bool> isActive_{false};
};

} // namespace react_native_linux
