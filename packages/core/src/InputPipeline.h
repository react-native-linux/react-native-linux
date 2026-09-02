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
    ImePreedit,
    ImeCommit,
    ImeDeleteSurrounding,
    /**
     * `wl_pointer.axis`: a smooth scroll delta in surface points, which is what a touchpad produces while two
     * fingers are down.
     */
    PointerScrollContinuous,
    /**
     * `wl_pointer.axis_discrete`: whole wheel notches. A compositor sends an `axis` event carrying the same notch
     * in units of its own choosing alongside it, and the queue drops that one in favour of this one.
     */
    PointerScrollDiscrete,
    /**
     * `wl_pointer.axis_stop`: the fingers left the touchpad, so whatever velocity the last deltas implied becomes
     * a fling.
     */
    PointerScrollStop,
};

/**
 * Which axis a scroll event moved. `wl_pointer` numbers these 0 and 1; this is the same pair, named.
 */
enum class ScrollAxisKind : uint8_t {
    Vertical,
    Horizontal,
};

/**
 * One thing the compositor told us, in surface coordinates and with no Wayland types in it.
 *
 * `button` is the DOM button number — 0 primary, 1 auxiliary, 2 secondary — not the `BTN_*` code Wayland sends;
 * the seat maps it. `key` and `code` are the DOM key values `domKeyName` and `domKeyCode` produce, and are empty
 * for pointer events. `scrollAmount` is points for `PointerScrollContinuous`, whole notches for
 * `PointerScrollDiscrete`, and unused otherwise; both grow in the direction `contentOffset` grows in, which is
 * content moving up or left.
 */
struct InputEvent {
    InputEventKind kind{InputEventKind::PointerMotion};
    facebook::react::Point surfacePoint{};
    int button{0};
    std::string key{};
    std::string code{};
    InputModifiers modifiers{};
    std::string text{};
    int32_t preeditCursorBegin{0};
    int32_t preeditCursorEnd{0};
    uint32_t deleteBeforeLength{0};
    uint32_t deleteAfterLength{0};
    ScrollAxisKind scrollAxis{ScrollAxisKind::Vertical};
    double scrollAmount{0.0};
};

/**
 * Whether this event belongs to the scroll pipeline rather than the pointer one. The two are routed separately
 * because a wheel moves the deepest `<ScrollView>` under the pointer, which is not the node a click would land on.
 */
bool isScrollEvent(const InputEvent& event);

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
 * Scroll deltas coalesce by the same contiguity rule and a different arithmetic: consecutive deltas on one axis are
 * **summed**, because every one of them is displacement rather than a position. A `wl_pointer` frame carrying a
 * wheel notch sends `axis_discrete` and then an `axis` measuring the same notch in compositor-defined units, so a
 * continuous delta that lands directly behind a discrete one on the same axis is dropped as the duplicate it is.
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

/**
 * The DOM `key` value for a key press: the text the key produced, or the name of the key when it produced none.
 *
 * Both halves come from the same xkbcommon call sequence the seat already makes — `keysymName` is
 * `xkb_keysym_get_name` and `keyText` is `xkb_state_key_get_utf8` — and the order they are consulted in is what
 * makes the result a DOM value rather than an X11 one. The named table wins first, so Tab, Enter, Escape and
 * Backspace do not arrive as the control characters their keysyms also produce. A single-character keysym name
 * wins next, so `Ctrl`+`a` is still `a` rather than the U+0001 the modified text would be. The text is the
 * fallback, which is what turns keysyms named `slash`, `period` and `exclam` into `/`, `.` and `!` without a
 * table entry each.
 *
 * `ISO_Left_Tab` — what a keymap reports for `Shift`+`Tab` — maps to `Tab`, because the DOM says the key is `Tab`
 * and the shift is in the modifiers. Anything left over is `Unidentified`, which is the DOM's own name for it.
 */
std::string domKeyName(const std::string& keysymName, const std::string& keyText);

/**
 * The DOM `code` value for a key: the physical key, independent of whatever keymap is on top of it, taken from
 * the evdev keycode the kernel reported. Unknown keys are `Unidentified`, which is also the DOM's answer.
 */
std::string domKeyCode(uint32_t evdevKeycode);

/**
 * The click a key activation produces on the focused node.
 *
 * It is deliberately the same synthetic `click` a press and a release on one target produce, built by the same
 * code, so `Pressability` turns Enter and Space into `onPressIn`, `onPressOut` and `onPress` with no keyboard
 * path of its own — which is what react-native-macos#1622 was missing. The coordinates are the target's own
 * origin rather than wherever the pointer happens to be resting, so the offset inside the target is zero and no
 * handler can mistake the activation for a click somewhere else.
 */
PointerDispatch makeActivationDispatch(const InputEvent& event, facebook::react::Point targetOrigin);

/**
 * Whatever owns the text cursor, from the platform's side of `zwp_text_input_v3`.
 *
 * The three calls are the three things a v3 `done` can ask a text buffer to do, and they arrive already batched
 * and already ordered by `TextInputV3State`. `onImePreedit` carries the composition as it stands after that
 * batch — an empty string means the composition ended and the pre-edit run must disappear — and the cursor pair
 * is the byte range inside it that the input method wants marked, with `-1, -1` meaning no cursor at all.
 *
 * `onImeCommit` is the only source of text on this path. A key event that arrives while a composition is active
 * is the compositor's business and not text; see *IME* in docs/cpp-toolchain.md.
 *
 * `TextInputController` is the implementation the input dispatcher routes to, and it delivers the composition to
 * whichever field holds focus, dropping it when none does. `rnl_window --ime-debug` is the second one, and it
 * consumes the frame's events itself rather than going through the dispatcher.
 */
class ImeSink {
public:
    ImeSink() = default;
    ImeSink(const ImeSink&) = delete;
    ImeSink(ImeSink&&) = delete;
    ImeSink& operator=(const ImeSink&) = delete;
    ImeSink& operator=(ImeSink&&) = delete;
    virtual ~ImeSink() = default;

    virtual void onImePreedit(const std::string& text, int32_t cursorBegin, int32_t cursorEnd) = 0;
    virtual void onImeCommit(const std::string& text) = 0;
    virtual void onImeDeleteSurrounding(uint32_t beforeLength, uint32_t afterLength) = 0;
};

void deliverImeEvent(const InputEvent& event, ImeSink& sink);

/**
 * The compositor's text input, as the focus model and the focused field drive it: enabled while the focused node
 * owns a text cursor, disabled the moment focus leaves it, and told where that cursor is while it is there.
 *
 * This is the other half of the `ImeSink` seam. `ImeSink` is where a composition lands; this is what decides
 * whether a composition can start at all and where the input method may not put its candidate window, and
 * `zwp_text_input_v3` needs all of it because `enable`, `set_surrounding_text` and `set_cursor_rectangle` are
 * requests the client makes rather than state the compositor infers. `TextInputClient` is the only
 * implementation, and it already had all four methods.
 *
 * The sink is borrowed, never owned, and must outlive the dispatcher it was given to.
 */
class TextInputFocusSink {
public:
    TextInputFocusSink() = default;
    TextInputFocusSink(const TextInputFocusSink&) = delete;
    TextInputFocusSink(TextInputFocusSink&&) = delete;
    TextInputFocusSink& operator=(const TextInputFocusSink&) = delete;
    TextInputFocusSink& operator=(TextInputFocusSink&&) = delete;
    virtual ~TextInputFocusSink() = default;

    virtual void enable() = 0;
    virtual void disable() = 0;
    virtual void setSurroundingText(std::string text, int32_t cursor, int32_t anchor) = 0;
    virtual void setCursorRectangle(int32_t x, int32_t y, int32_t width, int32_t height) = 0;
};

/**
 * Whether a DOM `key` value is a character to insert rather than a key to act on.
 *
 * The DOM's own rule, and therefore no table: a key that produced a character has that character as its `key`,
 * so a value that is exactly one code point is text and everything longer — `Enter`, `Tab`, `ArrowLeft`, `F1`,
 * `Unidentified` — is a name. The one entry that looks like an exception is the space bar, whose `key` is `" "`;
 * it is a character, it types a space in a text field, and it activates a `<Pressable>` only because nothing
 * consumed it first.
 */
bool isTextKey(const std::string& key);

/**
 * Parses the key-sequence notation `hello_react --type` takes into the events a compositor would have sent.
 *
 * The grammar is one line long: every character is itself, and `{...}` is one named key, optionally with
 * modifiers and a payload. `{Left}`, `{Backspace}`, `{Enter}`, `{Home}` and the rest name a key; `{Shift+Left}`
 * and `{Ctrl+A}` add modifiers; `{Preedit:nih}` and `{Commit:你好}` inject the composition events a
 * `zwp_text_input_v3` input method would have sent, which is what lets a headless run prove the IME path with no
 * compositor and no input method. An unrecognised token produces no events rather than failing the run, because
 * a sequence is test input and a typo in one should read as a missing keystroke rather than a crash.
 */
std::vector<InputEvent> parseKeySequence(const std::string& sequence);

} // namespace react_native_linux
