#include "TextInputClient.h"

#include "text-input-unstable-v3-client-protocol.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

std::string toText(const char* text) { return text == nullptr ? std::string{} : std::string(text); }

uint32_t protocolContentPurpose(TextInputContentPurpose purpose) {
    switch (purpose) {
    case TextInputContentPurpose::Digits:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DIGITS;
    case TextInputContentPurpose::Number:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER;
    case TextInputContentPurpose::Phone:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PHONE;
    case TextInputContentPurpose::Url:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_URL;
    case TextInputContentPurpose::Email:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL;
    case TextInputContentPurpose::Password:
        return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
    case TextInputContentPurpose::Normal:
        break;
    }

    return ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
}

/**
 * A masked field is the one content hint that changes what an input method may do rather than how it guesses:
 * `hidden_text` stops it echoing the characters into its own candidate window and `sensitive_data` stops it
 * learning them, which together are what keeps a password out of an input method's history.
 */
uint32_t protocolContentHint(TextInputContentPurpose purpose) {
    if (purpose == TextInputContentPurpose::Password) {
        return ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
    }

    return ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE;
}

} // namespace

zwp_text_input_v3_listener TextInputClient::makeTextInputListener() {
    // Value-initialised and then filled member by member, for the reason WaylandSeat::makePointerListener gives:
    // wayland-protocols 1.49 raised zwp_text_input_v3 to version 2 and the struct grew `action`, `language` and
    // `preedit_hint` with it. The manager is bound at version 1, so those three are unreachable, and naming them
    // would pin this file to one wayland-protocols release.
    zwp_text_input_v3_listener listener{};

    listener.enter = TextInputClient::handleEnter;
    listener.leave = TextInputClient::handleLeave;
    listener.preedit_string = TextInputClient::handlePreeditString;
    listener.commit_string = TextInputClient::handleCommitString;
    listener.delete_surrounding_text = TextInputClient::handleDeleteSurroundingText;
    listener.done = TextInputClient::handleDone;

    return listener;
}

const zwp_text_input_v3_listener TextInputClient::kTextInputListener = TextInputClient::makeTextInputListener();

TextInputClient::TextInputClient(zwp_text_input_manager_v3* manager, wl_seat* seat, InputQueue& queue)
    : textInput_(zwp_text_input_manager_v3_get_text_input(manager, seat)), queue_(queue) {
    zwp_text_input_v3_add_listener(textInput_, &kTextInputListener, this);
}

TextInputClient::~TextInputClient() noexcept {
    if (textInput_ != nullptr) {
        zwp_text_input_v3_destroy(textInput_);
    }
}

void TextInputClient::focusField(int32_t fieldIdentifier, TextInputContentPurpose contentPurpose) {
    session_.focusField(fieldIdentifier, contentPurpose);
}

void TextInputClient::blurField() { session_.blurField(); }

void TextInputClient::setSurroundingText(std::string text, int32_t cursor, int32_t anchor) {
    session_.setSurroundingText(std::move(text), cursor, anchor);
}

void TextInputClient::setCursorRectangle(int32_t x, int32_t y, int32_t width, int32_t height) {
    session_.setCursorRectangle(TextInputCursorRectangle{.x = x, .y = y, .width = width, .height = height});
}

void TextInputClient::flushTextInput() {
    const TextInputSessionBatch batch = session_.takeBatch();

    // The lifecycle traces beside `InputDispatcher`'s field-focus lines: enable and disable are the session's own
    // transitions, and the two that no field change explains — a re-enable after a keyboard leave and enter — are
    // the ones #340's e2e reads.
    if (batch.enable) {
        std::cout << "[rnl-ime] session enabled" << std::endl;
    }

    if (batch.disable) {
        std::cout << "[rnl-ime] session disabled" << std::endl;
    }

    // The protocol has no reset request, so a teardown is a `disable` with a `commit` of its own — see
    // TextInputSession for why a field change goes through one.
    if (batch.disable) {
        zwp_text_input_v3_disable(textInput_);
        zwp_text_input_v3_commit(textInput_);
    }

    if (batch.enable && batch.contentPurpose.has_value()) {
        zwp_text_input_v3_enable(textInput_);
        zwp_text_input_v3_set_content_type(textInput_, protocolContentHint(batch.contentPurpose.value()),
                                           protocolContentPurpose(batch.contentPurpose.value()));
    }

    if (batch.surroundingText.has_value()) {
        zwp_text_input_v3_set_surrounding_text(textInput_, batch.surroundingText->text.c_str(),
                                               batch.surroundingText->cursor, batch.surroundingText->anchor);
    }

    if (batch.cursorRectangle.has_value()) {
        zwp_text_input_v3_set_cursor_rectangle(textInput_, batch.cursorRectangle->x, batch.cursorRectangle->y,
                                               batch.cursorRectangle->width, batch.cursorRectangle->height);
    }

    if (batch.commit) {
        zwp_text_input_v3_commit(textInput_);
    }

    pushEvents(batch.events);
}

bool TextInputClient::isFocused() const noexcept { return session_.hasKeyboardFocus(); }

bool TextInputClient::isEnabled() const noexcept { return session_.isEnabled(); }

void TextInputClient::pushEvents(const std::vector<InputEvent>& events) {
    for (const InputEvent& event : events) {
        queue_.push(event);
    }
}

void TextInputClient::onEnter() { session_.enter(); }

void TextInputClient::onLeave() { pushEvents(session_.leave()); }

void TextInputClient::compose(const std::string& text) {
    session_.recordPreeditString(text, static_cast<int32_t>(text.size()), static_cast<int32_t>(text.size()));
    pushEvents(session_.applyDone());
}

void TextInputClient::commitComposition(const std::string& text) {
    session_.recordCommitString(text);
    pushEvents(session_.applyDone());
}

void TextInputClient::onDone(uint32_t serial) {
    // No flush here: `applyDone` can release pending surrounding-text or cursor-rectangle state that
    // `InputDispatcher::dispatch()` has not reconciled yet this frame, and flushing on the spot would commit that
    // release before the frame's own end-of-frame flush commits everything else it produced — two `commit`
    // requests for one frame's worth of state. `dispatch()` calls `flushTextInput()` exactly once, after
    // `TextInputController::synchronize()` and `updateTextInput()` have both run, so the released state reaches
    // the compositor in that one batch instead.
    pushEvents(session_.applyDone(serial));
}

void TextInputClient::handleEnter(void* data, zwp_text_input_v3* /*textInput*/, wl_surface* /*surface*/) {
    static_cast<TextInputClient*>(data)->onEnter();
}

void TextInputClient::handleLeave(void* data, zwp_text_input_v3* /*textInput*/, wl_surface* /*surface*/) {
    static_cast<TextInputClient*>(data)->onLeave();
}

void TextInputClient::handlePreeditString(void* data, zwp_text_input_v3* /*textInput*/, const char* text,
                                          int32_t cursorBegin, int32_t cursorEnd) {
    static_cast<TextInputClient*>(data)->session_.recordPreeditString(toText(text), cursorBegin, cursorEnd);
}

void TextInputClient::handleCommitString(void* data, zwp_text_input_v3* /*textInput*/, const char* text) {
    static_cast<TextInputClient*>(data)->session_.recordCommitString(toText(text));
}

void TextInputClient::handleDeleteSurroundingText(void* data, zwp_text_input_v3* /*textInput*/, uint32_t beforeLength,
                                                  uint32_t afterLength) {
    static_cast<TextInputClient*>(data)->session_.recordDeleteSurroundingText(beforeLength, afterLength);
}

void TextInputClient::handleDone(void* data, zwp_text_input_v3* /*textInput*/, uint32_t serial) {
    static_cast<TextInputClient*>(data)->onDone(serial);
}

} // namespace react_native_linux
