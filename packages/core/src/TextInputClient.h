#pragma once

#include "InputPipeline.h"
#include "TextInputSession.h"

#include <cstdint>
#include <string>
#include <vector>

struct wl_seat;
struct wl_surface;
struct zwp_text_input_manager_v3;
struct zwp_text_input_v3;
struct zwp_text_input_v3_listener;

namespace react_native_linux {

/**
 * One `zwp_text_input_v3` object for one seat: the requests a text field issues, and the composition the
 * compositor's input method answers with.
 *
 * Everything that decides *what* to send is `TextInputSession`, which is a pure state machine over the two
 * focuses — the compositor's keyboard focus, which arrives here as `enter` and `leave`, and the platform's own
 * field focus, which arrives through `TextInputFocusSink`. What is left in this class is the wire: turn the
 * batch the session hands back into requests, and turn the events the compositor sends into `InputEvent`s on the
 * seat's queue, where they ride the same per-frame beat as pointer and keyboard events.
 *
 * The requests are double-buffered: every one of them is pending until a `commit`, so nothing is sent until
 * `flushTextInput` takes a batch, and a frame that changed the surrounding text and moved the caret commits
 * once. `InputDispatcher` calls it once per frame, after the frame's input has settled.
 *
 * Threading contract: constructed, called and destroyed on the thread that owns the Wayland connection. The
 * listeners run inside that thread's `wl_display_dispatch_pending`, which is also where the queue is filled.
 */
class TextInputClient final : public TextInputFocusSink {
public:
    TextInputClient(zwp_text_input_manager_v3* manager, wl_seat* seat, InputQueue& queue);
    TextInputClient(const TextInputClient&) = delete;
    TextInputClient(TextInputClient&&) = delete;
    TextInputClient& operator=(const TextInputClient&) = delete;
    TextInputClient& operator=(TextInputClient&&) = delete;
    ~TextInputClient() noexcept override;

    void focusField(int32_t fieldIdentifier, TextInputContentPurpose contentPurpose) override;
    void blurField() override;
    void setSurroundingText(std::string text, int32_t cursor, int32_t anchor) override;
    void setCursorRectangle(int32_t x, int32_t y, int32_t width, int32_t height) override;
    void flushTextInput() override;

    bool isFocused() const noexcept;
    bool isEnabled() const noexcept;

private:
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
    TextInputSession session_;
};

} // namespace react_native_linux
