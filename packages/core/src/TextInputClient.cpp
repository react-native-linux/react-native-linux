#include "TextInputClient.h"

#include "text-input-unstable-v3-client-protocol.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

std::string toText(const char* text) { return text == nullptr ? std::string{} : std::string(text); }

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

void TextInputClient::enable() {
    if (!state_.isFocused()) {
        return;
    }

    zwp_text_input_v3_enable(textInput_);
    state_.enable();
    stateDirty_ = true;
    sendState();
}

void TextInputClient::disable() {
    zwp_text_input_v3_disable(textInput_);
    zwp_text_input_v3_commit(textInput_);
    state_.disable();
    state_.recordCommitRequest();
    stateDirty_ = false;
}

void TextInputClient::setSurroundingText(std::string text, int32_t cursor, int32_t anchor) {
    surroundingText_ = std::move(text);
    surroundingCursor_ = cursor;
    surroundingAnchor_ = anchor;
    stateDirty_ = true;
    sendState();
}

void TextInputClient::setCursorRectangle(int32_t x, int32_t y, int32_t width, int32_t height) {
    cursorRectangle_ = ImeCursorRectangle{.x = x, .y = y, .width = width, .height = height};
    stateDirty_ = true;
    sendState();
}

bool TextInputClient::isFocused() const noexcept { return state_.isFocused(); }

bool TextInputClient::isEnabled() const noexcept { return state_.isEnabled(); }

void TextInputClient::sendState() {
    if (!state_.isEnabled() || state_.needsStateResend()) {
        return;
    }

    if (!surroundingText_.empty()) {
        zwp_text_input_v3_set_surrounding_text(textInput_, surroundingText_.c_str(), surroundingCursor_,
                                               surroundingAnchor_);
    }

    // An all-zero rectangle is how the protocol spells "this client does not know where its cursor is", and it
    // says later attempts to change it may then have no effect, so an unset rectangle is not sent.
    if (cursorRectangle_.width != 0 && cursorRectangle_.height != 0) {
        zwp_text_input_v3_set_cursor_rectangle(textInput_, cursorRectangle_.x, cursorRectangle_.y,
                                               cursorRectangle_.width, cursorRectangle_.height);
    }

    zwp_text_input_v3_commit(textInput_);
    state_.recordCommitRequest();
    stateDirty_ = false;
}

void TextInputClient::pushEvents(const std::vector<InputEvent>& events) {
    for (const InputEvent& event : events) {
        queue_.push(event);
    }
}

void TextInputClient::onLeave() { pushEvents(state_.leave()); }

void TextInputClient::onDone(uint32_t serial) {
    pushEvents(state_.applyDone(serial));

    if (stateDirty_) {
        sendState();
    }
}

void TextInputClient::handleEnter(void* data, zwp_text_input_v3* /*textInput*/, wl_surface* /*surface*/) {
    static_cast<TextInputClient*>(data)->state_.enter();
}

void TextInputClient::handleLeave(void* data, zwp_text_input_v3* /*textInput*/, wl_surface* /*surface*/) {
    static_cast<TextInputClient*>(data)->onLeave();
}

void TextInputClient::handlePreeditString(void* data, zwp_text_input_v3* /*textInput*/, const char* text,
                                          int32_t cursorBegin, int32_t cursorEnd) {
    static_cast<TextInputClient*>(data)->state_.recordPreeditString(toText(text), cursorBegin, cursorEnd);
}

void TextInputClient::handleCommitString(void* data, zwp_text_input_v3* /*textInput*/, const char* text) {
    static_cast<TextInputClient*>(data)->state_.recordCommitString(toText(text));
}

void TextInputClient::handleDeleteSurroundingText(void* data, zwp_text_input_v3* /*textInput*/, uint32_t beforeLength,
                                                  uint32_t afterLength) {
    static_cast<TextInputClient*>(data)->state_.recordDeleteSurroundingText(beforeLength, afterLength);
}

void TextInputClient::handleDone(void* data, zwp_text_input_v3* /*textInput*/, uint32_t serial) {
    static_cast<TextInputClient*>(data)->onDone(serial);
}

} // namespace react_native_linux
