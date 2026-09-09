#pragma once

#include "InputPipeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * The `zwp_text_input_v3` composition double buffer, as a class with no Wayland types in it.
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
 * `reset` is every event and every request that invalidates a composition — `enter`, `leave`, `enable` and
 * `disable` all do — and it returns the empty pre-edit that clears a half-composed word off the screen. Which of
 * them happened, and whether one is legal, is `TextInputSession`'s question rather than this class's; nothing
 * here knows what focus is.
 *
 * Threading contract: constructed, called and destroyed on the frame thread, from inside the Wayland dispatch
 * `TextInputClient` listens on. Nothing here is synchronised and nothing here needs to be.
 */
class TextInputV3State final {
public:
    std::vector<InputEvent> reset();
    void recordPreeditString(std::string text, int32_t cursorBegin, int32_t cursorEnd);
    void recordCommitString(std::string text);
    void recordDeleteSurroundingText(uint32_t beforeLength, uint32_t afterLength);
    std::vector<InputEvent> applyDone();
    /**
     * Throws the pending batch away without emitting anything — what a `done` answering an already-replaced
     * commit does (#371): the composition it carries belongs to a state that is gone, possibly a field that no
     * longer holds the caret, and applying it would type the abandonment into whatever field is focused now.
     */
    void discardPending();

private:
    void resetPending();

    std::string preeditText_;
    std::string pendingPreeditText_;
    std::string pendingCommitText_;
    int32_t preeditCursorBegin_{0};
    int32_t preeditCursorEnd_{0};
    int32_t pendingPreeditCursorBegin_{0};
    int32_t pendingPreeditCursorEnd_{0};
    uint32_t pendingDeleteBeforeLength_{0};
    uint32_t pendingDeleteAfterLength_{0};
};

} // namespace react_native_linux
