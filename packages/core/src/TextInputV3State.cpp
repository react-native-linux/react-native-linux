#include "TextInputV3State.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

void TextInputV3State::enter() {
    focused_ = true;
    enabled_ = false;
    needsStateResend_ = false;
    resetComposition();
}

std::vector<InputEvent> TextInputV3State::leave() {
    std::vector<InputEvent> events;

    if (!preeditText_.empty()) {
        events.push_back(InputEvent{.kind = InputEventKind::ImePreedit});
    }

    focused_ = false;
    enabled_ = false;
    needsStateResend_ = false;
    resetComposition();

    return events;
}

void TextInputV3State::enable() {
    enabled_ = true;
    resetComposition();
}

void TextInputV3State::disable() {
    enabled_ = false;
    resetComposition();
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

void TextInputV3State::recordCommitRequest() noexcept { ++commitRequestCount_; }

std::vector<InputEvent> TextInputV3State::applyDone(uint32_t serial) {
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
    needsStateResend_ = serial != commitRequestCount_;
    resetPending();

    return events;
}

bool TextInputV3State::isFocused() const noexcept { return focused_; }

bool TextInputV3State::isEnabled() const noexcept { return enabled_; }

bool TextInputV3State::needsStateResend() const noexcept { return needsStateResend_; }

void TextInputV3State::resetPending() {
    pendingPreeditText_.clear();
    pendingCommitText_.clear();
    pendingPreeditCursorBegin_ = 0;
    pendingPreeditCursorEnd_ = 0;
    pendingDeleteBeforeLength_ = 0;
    pendingDeleteAfterLength_ = 0;
}

void TextInputV3State::resetComposition() {
    preeditText_.clear();
    preeditCursorBegin_ = 0;
    preeditCursorEnd_ = 0;
    resetPending();
}

} // namespace react_native_linux
