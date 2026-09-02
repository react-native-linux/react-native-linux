#include "Clipboard.h"

#include <string>
#include <utility>

namespace react_native_linux {

namespace {

std::string& clipboardStorage() {
    static std::string storage;

    return storage;
}

} // namespace

const std::string& clipboardText() { return clipboardStorage(); }

void setClipboardText(std::string text) { clipboardStorage() = std::move(text); }

} // namespace react_native_linux
