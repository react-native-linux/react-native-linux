#pragma once

#include "InputPipeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * The `zwp_text_input_v3` double buffer, as a class with no Wayland types in it.
 *
 * Composition arrives in pieces — `preedit_string`, `commit_string`, `delete_surrounding_text` — and none of them
 * mean anything on their own: the protocol says each modifies pending state, that `done` replaces the current
 * state with all of it at once, and that the pending values reset to initial afterwards. Applying a piece as it
 * arrives is not a shortcut, it is a different protocol, and it is what produces the duplicated and reordered
 * characters every half-finished IME implementation is reported for.
 *
 * `applyDone` returns the events in the order the protocol's `done` description evaluates them: the old pre-edit
 * is replaced by the cursor, the surrounding text is deleted, the commit string is inserted, and the new pre-edit
 * is placed last. A text buffer that applies them in that order is correct by construction.
 *
 * The serial on `done` is the compositor's count of our `commit` requests. One that is not our own count means
 * the compositor answered a state we have already replaced: those changes still apply, but our own state
 * requests must wait for a `done` that matches before they are sent again — `needsStateResend`.
 *
 * `enter` and `enable` both invalidate every piece of state the protocol carries, which is why they clear the
 * current pre-edit rather than only the pending one: after either, the compositor knows nothing about this text
 * input until the client says it again. They clear `needsStateResend` for the same reason, and because the
 * `commit` that carries an `enable` cannot wait for a serial that only another `commit` would produce.
 *
 * Threading contract: constructed, called and destroyed on the frame thread, from inside the Wayland dispatch
 * `TextInputClient` listens on. Nothing here is synchronised and nothing here needs to be.
 */
class TextInputV3State final {
public:
    void enter();
    std::vector<InputEvent> leave();
    void enable();
    void disable();
    void recordPreeditString(std::string text, int32_t cursorBegin, int32_t cursorEnd);
    void recordCommitString(std::string text);
    void recordDeleteSurroundingText(uint32_t beforeLength, uint32_t afterLength);
    void recordCommitRequest() noexcept;
    std::vector<InputEvent> applyDone(uint32_t serial);

    bool isFocused() const noexcept;
    bool isEnabled() const noexcept;
    bool needsStateResend() const noexcept;

private:
    void resetPending();
    void resetComposition();

    std::string preeditText_;
    std::string pendingPreeditText_;
    std::string pendingCommitText_;
    int32_t preeditCursorBegin_{0};
    int32_t preeditCursorEnd_{0};
    int32_t pendingPreeditCursorBegin_{0};
    int32_t pendingPreeditCursorEnd_{0};
    uint32_t pendingDeleteBeforeLength_{0};
    uint32_t pendingDeleteAfterLength_{0};
    uint32_t commitRequestCount_{0};
    bool focused_{false};
    bool enabled_{false};
    bool needsStateResend_{false};
};

} // namespace react_native_linux
