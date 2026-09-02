#pragma once

#include "InputPipeline.h"
#include "TextInputV3State.h"

#include <cstdint>
#include <string>

struct wl_seat;
struct wl_surface;
struct zwp_text_input_manager_v3;
struct zwp_text_input_v3;
struct zwp_text_input_v3_listener;

namespace react_native_linux {

/**
 * The area around the text cursor, in surface-local coordinates, that the input method must not cover with its
 * candidate window. Nothing here draws that window: on Wayland the compositor's input method owns it, which is
 * the whole reason a client only has to say where the cursor is.
 */
struct ImeCursorRectangle {
    int32_t x{0};
    int32_t y{0};
    int32_t width{0};
    int32_t height{0};
};

/**
 * One `zwp_text_input_v3` object for one seat: the requests a text field issues, and the composition the
 * compositor's input method answers with.
 *
 * The protocol's state is double-buffered on both sides. Every request here is pending until a `commit`, so the
 * setters cache what they were told and `sendState` issues surrounding text, cursor rectangle and `commit`
 * together — a text field never has to know that a `commit` exists. Every event is pending until a `done`, which
 * is `TextInputV3State`'s job; this class only turns the batch that `done` releases into `InputEvent`s on the
 * seat's queue, where they ride the same per-frame beat as pointer and keyboard events.
 *
 * `enable` is refused while the text input has no focus, because the protocol says the compositor ignores every
 * request from a text input that has not been sent `enter`. Focus is the compositor's keyboard focus: the
 * `enter` event follows it, and it invalidates all state, so a field that wants input after a focus change must
 * enable again. `rnl_window --ime-debug` is what does that today; issue #17's `<TextInput>` is what will.
 *
 * An empty surrounding text is not sent at all. The protocol reads an empty value as "this client does not
 * support surrounding text", and it says later attempts to change it may then have no effect, so a field that
 * has not reported its text yet must say nothing rather than say nothing useful.
 *
 * Threading contract: constructed, called and destroyed on the thread that owns the Wayland connection. The
 * listeners run inside that thread's `wl_display_dispatch_pending`, which is also where the queue is filled.
 */
class TextInputClient final {
public:
    TextInputClient(zwp_text_input_manager_v3* manager, wl_seat* seat, InputQueue& queue);
    TextInputClient(const TextInputClient&) = delete;
    TextInputClient(TextInputClient&&) = delete;
    TextInputClient& operator=(const TextInputClient&) = delete;
    TextInputClient& operator=(TextInputClient&&) = delete;
    ~TextInputClient() noexcept;

    void enable();
    void disable();
    void setSurroundingText(std::string text, int32_t cursor, int32_t anchor);
    void setCursorRectangle(int32_t x, int32_t y, int32_t width, int32_t height);

    bool isFocused() const noexcept;
    bool isEnabled() const noexcept;

private:
    void sendState();
    void pushEvents(const std::vector<InputEvent>& events);
    void onLeave();
    void onDone(uint32_t serial);

    static zwp_text_input_v3_listener makeTextInputListener();

    static const zwp_text_input_v3_listener kTextInputListener;

    static void handleEnter(void* data, zwp_text_input_v3* textInput, wl_surface* surface);
    static void handleLeave(void* data, zwp_text_input_v3* textInput, wl_surface* surface);
    static void handlePreeditString(void* data, zwp_text_input_v3* textInput, const char* text, int32_t cursorBegin,
                                    int32_t cursorEnd);
    static void handleCommitString(void* data, zwp_text_input_v3* textInput, const char* text);
    static void handleDeleteSurroundingText(void* data, zwp_text_input_v3* textInput, uint32_t beforeLength,
                                            uint32_t afterLength);
    static void handleDone(void* data, zwp_text_input_v3* textInput, uint32_t serial);

    zwp_text_input_v3* textInput_{nullptr};
    InputQueue& queue_;
    TextInputV3State state_;
    std::string surroundingText_;
    ImeCursorRectangle cursorRectangle_;
    int32_t surroundingCursor_{0};
    int32_t surroundingAnchor_{0};
    bool stateDirty_{false};
};

} // namespace react_native_linux
