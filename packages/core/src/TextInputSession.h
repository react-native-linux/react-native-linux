#pragma once

#include "InputPipeline.h"
#include "TextInputV3State.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace react_native_linux {

/** The text around the caret and where the caret is inside it, in the display string's UTF-16 offsets. */
struct TextInputSurroundingText {
    std::string text;
    int32_t cursor{0};
    int32_t anchor{0};

    bool operator==(const TextInputSurroundingText&) const = default;
};

/**
 * The area around the caret the input method must not cover, in integer surface-local coordinates.
 *
 * Integer, and compared as integers, because that is what the protocol carries: a caret that moved by a
 * fraction of a pixel is the same rectangle, and re-sending it would be protocol traffic every frame a
 * text field is animated near.
 */
struct TextInputCursorRectangle {
    int32_t x{0};
    int32_t y{0};
    int32_t width{0};
    int32_t height{0};

    bool operator==(const TextInputCursorRectangle&) const = default;
};

/**
 * One frame's worth of `zwp_text_input_v3` requests: at most one teardown, at most one setup, and the state that
 * follows the setup.
 *
 * `disable` carries its own `commit`, because the protocol has no reset request — tearing a session down and
 * building it back up is how a field change is expressed, and the two halves are two transactions. Everything
 * else in one batch rides the single `commit` that `commit` marks, which is what keeps a frame that moved the
 * caret and changed the surrounding text from committing twice before the compositor has answered once.
 */
struct TextInputSessionBatch {
    /** The composition the teardown took off the screen, for the field that was showing it. */
    std::vector<InputEvent> events;
    std::optional<TextInputContentPurpose> contentPurpose;
    std::optional<TextInputSurroundingText> surroundingText;
    std::optional<TextInputCursorRectangle> cursorRectangle;
    bool disable{false};
    bool enable{false};
    bool commit{false};
};

/**
 * The `zwp_text_input_v3` **session**, as one state machine with no Wayland types in it.
 *
 * Two focuses drive it and neither one is enough on its own. The compositor's keyboard focus arrives as
 * `enter` and `leave` and decides whether a request is legal at all — the protocol says every request from a
 * text input that has not been sent `enter` is ignored. The platform's own focus arrives as `focusField` and
 * `blurField` and decides whether a request should be made: a focusable node that is not a text field must not
 * leave an input method enabled over it, which is the candidate window that appears over ordinary interface
 * elements in zed-industries/zed#63774. So the session is enabled when, and only when, both hold, and it is
 * re-evaluated on every change to either rather than once when a field is focused.
 *
 * A `leave` therefore does not blur the field: the buffer, the caret and the surrounding text stay exactly
 * where they were, and the first `enter` after it enables again and re-sends all of it. That round trip is
 * zed-industries/zed#36834 and #32298 — an input method that stops working after a window switch, because the
 * object was never enabled a second time.
 *
 * Swapping between two fields is a `disable` followed by an `enable`, and it is not an optimisation to skip:
 * "text_input_v3 don't have something like a reset function", so a teardown is the only way to tell the
 * compositor that everything it was told belongs to a field that no longer has the caret. Without it the state
 * of one field leaks into the next — zed-industries/zed#52952 and #59882.
 *
 * The serial on `done` is the compositor's count of our `commit` requests. One that is not our own count means
 * the compositor answered a state we have already replaced, so the state we sent is treated as never having
 * landed and is sent again on the first `done` whose serial matches. The composition itself is never gated on
 * it: the user's keystrokes are not negotiable.
 *
 * Threading contract: constructed, called and destroyed on the frame thread, from inside the Wayland dispatch
 * `TextInputClient` listens on. Nothing here is synchronised and nothing here needs to be.
 */
class TextInputSession final {
public:
    void enter();
    std::vector<InputEvent> leave();

    void focusField(int32_t fieldIdentifier, TextInputContentPurpose contentPurpose);
    void blurField();
    void setSurroundingText(std::string text, int32_t cursor, int32_t anchor);
    void setCursorRectangle(TextInputCursorRectangle rectangle);

    void recordPreeditString(std::string text, int32_t cursorBegin, int32_t cursorEnd);
    void recordCommitString(std::string text);
    void recordDeleteSurroundingText(uint32_t beforeLength, uint32_t afterLength);
    std::vector<InputEvent> applyDone(uint32_t serial);

    /** The requests this frame owes the compositor. Taking them is what records that they were sent. */
    TextInputSessionBatch takeBatch();

    bool hasKeyboardFocus() const noexcept;
    bool isEnabled() const noexcept;

private:
    struct FocusedField {
        int32_t identifier{0};
        TextInputContentPurpose contentPurpose{TextInputContentPurpose::Normal};

        bool operator==(const FocusedField&) const = default;
    };

    void forgetCompositorState();

    TextInputV3State composition_;
    std::optional<FocusedField> focusedField_;
    std::optional<FocusedField> enabledField_;
    std::optional<TextInputSurroundingText> pendingSurroundingText_;
    std::optional<TextInputSurroundingText> sentSurroundingText_;
    std::optional<TextInputCursorRectangle> pendingCursorRectangle_;
    std::optional<TextInputCursorRectangle> sentCursorRectangle_;
    uint32_t commitRequestCount_{0};
    bool hasKeyboardFocus_{false};
    bool needsStateResend_{false};
};

} // namespace react_native_linux
