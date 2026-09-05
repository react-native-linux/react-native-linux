#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace react_native_linux {

/**
 * The `requestAnimationFrame` queue: what callbacks JavaScript has asked to run on the next frame, in the order it
 * asked. Upstream's `TimerManager` implements `requestAnimationFrame` as `setTimeout(callback, 0)` — its own
 * comment says so — and our `HostTimerRegistry` hands those to a `TaskDispatchThread` whose `std::priority_queue`
 * orders by deadline alone, so callbacks registered in one turn with one deadline come back in heap order rather
 * than registration order (react-native#48005) and a callback that registers another runs it as soon as the
 * dispatch thread pops it rather than on the next frame. This queue replaces that path for the two globals and
 * gives the three rules the specification states:
 *
 * - `dispatchFrame` invokes the callbacks registered before it in registration order, each exactly once, all with
 *   the same timestamp — which is what "once per frame" is observable as from JavaScript.
 * - A `request` made while a frame is dispatching lands in the *next* frame, never the running one, so a
 *   self-perpetuating animation loop runs at frame rate instead of spinning the dispatch thread.
 * - A `cancel` of a callback that has not run yet is honoured even when it comes from inside a callback of the
 *   frame that callback belongs to.
 *
 * `hasPendingRequests` is the liveness half, for the frame clock's pending-work signal: a window whose compositor
 * withholds `wl_surface.frame` — occluded, or on an inactive workspace, which ADR-0001 decision 3 names — still
 * has to draw a frame on the fallback timeout while an animation loop is registered, or that loop stops until a
 * pointer moves (react-native#57592). It stays true across a dispatch as well as before one, so a loop that
 * re-registers from inside its own callback never reads as idle between the two.
 *
 * Threading contract: `request`, `cancel` and `dispatchFrame` run on the JavaScript thread — the first two from
 * the JSI host functions, the third from the task `ReactHost::dispatchAnimationFrames` posts there. Callbacks are
 * therefore invoked with no lock held and may re-enter `request` and `cancel`. `hasPendingRequests` is read by the
 * frame thread, which is why the state is behind a mutex at all.
 */
class AnimationFrameQueue final {
public:
    using Callback = std::function<void(double)>;

    /**
     * Registers `callback` for the next frame and returns the handle `cancel` takes. Handles are never reused and
     * never zero, so a stale handle cancels nothing.
     */
    uint64_t request(Callback callback);

    /**
     * Drops the callback `requestHandle` names, whether it is waiting for the next frame or waiting its turn in
     * the frame being dispatched right now. Anything else — a handle that already ran, a handle from another
     * queue, zero — is a no-op, as `cancelAnimationFrame` is on the web.
     */
    void cancel(uint64_t requestHandle);

    /**
     * Whether a callback is registered, or a frame's callbacks are still being invoked.
     */
    bool hasPendingRequests() const;

    /**
     * Drops every registered callback without running any of them.
     *
     * This is teardown, not a JavaScript-visible operation: the callbacks are `jsi::Function`s, and a JSI pointer
     * that outlives its runtime aborts a debug Hermes, so `ReactHost` empties the queue before it destroys the
     * instance — the same ordering rule its TurboModules already follow.
     */
    void clear();

    /**
     * Invokes everything registered before this call, in registration order, with `frameTimestampMilliseconds`,
     * and returns how many callbacks ran. Requests those callbacks make are left for the next call.
     */
    size_t dispatchFrame(double frameTimestampMilliseconds);

private:
    struct Request {
        uint64_t handle;
        Callback callback;
    };

    mutable std::mutex mutex_;
    std::vector<Request> pendingRequests_;
    std::vector<Request> dispatchingRequests_;
    bool isDispatching_{false};
    uint64_t nextHandle_{1};
};

} // namespace react_native_linux
