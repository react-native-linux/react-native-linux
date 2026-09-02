#pragma once

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Point.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * The modifier state as the keyboard last reported it, carried on every event because a pointer event needs it too.
 */
struct InputModifiers {
    bool control{false};
    bool shift{false};
    bool alt{false};
    bool meta{false};
};

enum class InputEventKind : uint8_t {
    PointerMotion,
    PointerButtonPress,
    PointerButtonRelease,
    PointerLeave,
    KeyPress,
    KeyRelease,
};

/**
 * One thing the compositor told us, in surface coordinates and with no Wayland types in it.
 *
 * `button` is the DOM button number — 0 primary, 1 auxiliary, 2 secondary — not the `BTN_*` code Wayland sends;
 * the seat maps it. `key` is the xkbcommon keysym name and is empty for pointer events.
 */
struct InputEvent {
    InputEventKind kind{InputEventKind::PointerMotion};
    facebook::react::Point surfacePoint{};
    int button{0};
    std::string key{};
    InputModifiers modifiers{};
};

/**
 * The number of events one frame may carry. Motion coalescing is what keeps a 1000 Hz mouse well under it, so
 * reaching the cap means a button or key stream no human produced; the overflow is counted rather than ignored.
 */
constexpr size_t kInputQueueCapacity = 256;

/**
 * The queue between the compositor and the frame: everything that arrived since the last frame, in order, with
 * consecutive motion collapsed to the last position.
 *
 * Coalescing is contiguous on purpose. A 1000 Hz mouse produces sixteen motion events per 60 Hz frame and
 * seventeen per 120 Hz frame, and React has no use for the intermediate positions — but it does need to know that
 * a button went down between two of them, so a press splits the run and the positions on either side survive
 * independently.
 *
 * Threading contract: this type is not synchronised, and does not need to be. Wayland listeners run inside
 * `wl_display_dispatch_pending` on the frame thread, which is also the thread that drains the queue.
 */
class InputQueue final {
public:
    void push(const InputEvent& event);
    std::vector<InputEvent> drain();
    size_t droppedEventCount() const noexcept;

private:
    std::vector<InputEvent> events_;
    size_t droppedEventCount_{0};
};

enum class PointerDispatchType : uint8_t {
    Move,
    Down,
    Up,
    Leave,
    Click,
};

/**
 * One event Fabric should be given, already in the shape `TouchEventEmitter` takes.
 *
 * There is no target in here: every dispatch a single `route` call returns belongs to the node the caller
 * hit-tested for that input event, so naming it twice would only create a way for the two to disagree.
 */
struct PointerDispatch {
    PointerDispatchType type{PointerDispatchType::Move};
    facebook::react::PointerEvent event{};
};

/**
 * The mouse state machine: which buttons are down, which node the press started on, and therefore whether a
 * release is also a click.
 *
 * Everything else a desktop pointer implies — `pointerEnter`, `pointerLeave` between siblings, `pointerOver`,
 * `pointerOut`, the hover chain, and bubbling — is computed by upstream's `PointerEventsProcessor` when the event
 * reaches `UIManagerBinding` on the JavaScript thread. This class deliberately produces only the raw events that
 * processor expects to receive from a platform, because a second hover implementation here would be a second
 * chance to disagree with React's.
 *
 * `click` is the exception: upstream treats it as synthetic and passes it straight through, so the platform is
 * what decides that a press and a release on the same target are a press gesture. That is the event
 * `Pressability` turns into `onPressIn`/`onPressOut`/`onPress`.
 */
class PointerRouter final {
public:
    std::vector<PointerDispatch> route(const InputEvent& event, facebook::react::Tag targetTag,
                                       facebook::react::Point targetOrigin);

private:
    std::vector<PointerDispatch> routeRelease(const InputEvent& event, facebook::react::Tag targetTag,
                                              facebook::react::Point targetOrigin);

    facebook::react::Tag pressedTag_{0};
    int pressedButtons_{0};
};

} // namespace react_native_linux
