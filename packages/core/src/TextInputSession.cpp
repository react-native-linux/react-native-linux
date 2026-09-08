#include "TextInputSession.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

void TextInputSession::enter() {
    hasKeyboardFocus_ = true;
    forgetCompositorState();
    composition_.reset();
}

std::vector<InputEvent> TextInputSession::leave() {
    hasKeyboardFocus_ = false;
    forgetCompositorState();

    return composition_.reset();
}

void TextInputSession::focusField(int32_t fieldIdentifier, TextInputContentPurpose contentPurpose) {
    const FocusedField field{.identifier = fieldIdentifier, .contentPurpose = contentPurpose};

    if (focusedField_ == field) {
        return;
    }

    focusedField_ = field;
    pendingSurroundingText_.reset();
    pendingCursorRectangle_.reset();
}

void TextInputSession::blurField() {
    if (!focusedField_.has_value()) {
        return;
    }

    focusedField_.reset();
    pendingSurroundingText_.reset();
    pendingCursorRectangle_.reset();
}

void TextInputSession::setSurroundingText(std::string text, int32_t cursor, int32_t anchor) {
    // The protocol reads an empty surrounding text as "this client does not support surrounding text", and warns
    // that later attempts to change it may then have no effect. A field with nothing around the caret yet says
    // nothing rather than something it cannot take back.
    if (text.empty()) {
        return;
    }

    pendingSurroundingText_ = TextInputSurroundingText{.text = std::move(text), .cursor = cursor, .anchor = anchor};
}

void TextInputSession::setCursorRectangle(TextInputCursorRectangle rectangle) {
    // An all-zero rectangle is how the protocol spells "this client does not know where its cursor is", and the
    // same warning applies to it as to an empty surrounding text.
    if (rectangle.width == 0 || rectangle.height == 0) {
        return;
    }

    pendingCursorRectangle_ = rectangle;
}

void TextInputSession::recordPreeditString(std::string text, int32_t cursorBegin, int32_t cursorEnd) {
    composition_.recordPreeditString(std::move(text), cursorBegin, cursorEnd);
}

void TextInputSession::recordCommitString(std::string text) { composition_.recordCommitString(std::move(text)); }

void TextInputSession::recordDeleteSurroundingText(uint32_t beforeLength, uint32_t afterLength) {
    composition_.recordDeleteSurroundingText(beforeLength, afterLength);
}

std::vector<InputEvent> TextInputSession::applyDone(uint32_t serial) {
    needsStateResend_ = serial != commitRequestCount_;

    if (needsStateResend_) {
        sentSurroundingText_.reset();
        sentCursorRectangle_.reset();
    }

    return composition_.applyDone();
}

std::vector<InputEvent> TextInputSession::applyDone() { return applyDone(commitRequestCount_); }

TextInputSessionBatch TextInputSession::takeBatch() {
    TextInputSessionBatch batch;
    const std::optional<FocusedField> target = hasKeyboardFocus_ ? focusedField_ : std::nullopt;

    if (enabledField_.has_value() && enabledField_ != target) {
        batch.disable = true;
        batch.events = composition_.reset();
        enabledField_.reset();
        forgetCompositorState();
        ++commitRequestCount_;
    }

    if (!enabledField_.has_value() && target.has_value()) {
        batch.enable = true;
        batch.contentPurpose = target->contentPurpose;
        enabledField_ = target;
    }

    if (enabledField_.has_value() && !needsStateResend_) {
        if (pendingSurroundingText_.has_value() && pendingSurroundingText_ != sentSurroundingText_) {
            batch.surroundingText = pendingSurroundingText_;
            sentSurroundingText_ = pendingSurroundingText_;
        }

        if (pendingCursorRectangle_.has_value() && pendingCursorRectangle_ != sentCursorRectangle_) {
            batch.cursorRectangle = pendingCursorRectangle_;
            sentCursorRectangle_ = pendingCursorRectangle_;
        }
    }

    batch.commit = batch.enable || batch.surroundingText.has_value() || batch.cursorRectangle.has_value();

    if (batch.commit) {
        ++commitRequestCount_;
    }

    return batch;
}

bool TextInputSession::hasKeyboardFocus() const noexcept { return hasKeyboardFocus_; }

bool TextInputSession::isEnabled() const noexcept { return enabledField_.has_value(); }

void TextInputSession::forgetCompositorState() {
    enabledField_.reset();
    sentSurroundingText_.reset();
    sentCursorRectangle_.reset();
    needsStateResend_ = false;
}

} // namespace react_native_linux
