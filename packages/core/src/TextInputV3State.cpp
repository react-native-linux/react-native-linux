#include "TextInputV3State.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

std::vector<InputEvent> TextInputV3State::reset() {
    std::vector<InputEvent> events;

    if (!preeditText_.empty()) {
        events.push_back(InputEvent{.kind = InputEventKind::ImePreedit});
    }

    preeditText_.clear();
    preeditCursorBegin_ = 0;
    preeditCursorEnd_ = 0;
    resetPending();

    return events;
}

void TextInputV3State::recordPreeditString(std::string text, int32_t cursorBegin, int32_t cursorEnd) {
    pendingPreeditText_ = std::move(text);
    pendingPreeditCursorBegin_ = cursorBegin;
    pendingPreeditCursorEnd_ = cursorEnd;
}

void TextInputV3State::recordCommitString(std::string text) { pendingCommitText_ = std::move(text); }

void TextInputV3State::recordDeleteSurroundingText(uint32_t beforeLength, uint32_t afterLength) {
    pendingDeleteBeforeLength_ = beforeLength;
    pendingDeleteAfterLength_ = afterLength;
}

std::vector<InputEvent> TextInputV3State::applyDone() {
    std::vector<InputEvent> events;

    if (pendingDeleteBeforeLength_ != 0 || pendingDeleteAfterLength_ != 0) {
        events.push_back(InputEvent{.kind = InputEventKind::ImeDeleteSurrounding,
                                    .deleteBeforeLength = pendingDeleteBeforeLength_,
                                    .deleteAfterLength = pendingDeleteAfterLength_});
    }

    if (!pendingCommitText_.empty()) {
        events.push_back(InputEvent{.kind = InputEventKind::ImeCommit, .text = pendingCommitText_});
    }

    const bool preeditChanged = pendingPreeditText_ != preeditText_ ||
                                pendingPreeditCursorBegin_ != preeditCursorBegin_ ||
                                pendingPreeditCursorEnd_ != preeditCursorEnd_;

    if (preeditChanged) {
        events.push_back(InputEvent{.kind = InputEventKind::ImePreedit,
                                    .text = pendingPreeditText_,
                                    .preeditCursorBegin = pendingPreeditCursorBegin_,
                                    .preeditCursorEnd = pendingPreeditCursorEnd_});
    }

    preeditText_ = pendingPreeditText_;
    preeditCursorBegin_ = pendingPreeditCursorBegin_;
    preeditCursorEnd_ = pendingPreeditCursorEnd_;
    resetPending();

    return events;
}

void TextInputV3State::resetPending() {
    pendingPreeditText_.clear();
    pendingCommitText_.clear();
    pendingPreeditCursorBegin_ = 0;
    pendingPreeditCursorEnd_ = 0;
    pendingDeleteBeforeLength_ = 0;
    pendingDeleteAfterLength_ = 0;
}

} // namespace react_native_linux
