#include "Clipboard.h"

#include <mutex>
#include <string>
#include <utility>

namespace react_native_linux {

namespace {

std::mutex clipboardMutex;
std::string clipboardStorage;

} // namespace

std::string clipboardText() {
    const std::scoped_lock lock(clipboardMutex);

    return clipboardStorage;
}

void setClipboardText(std::string text) {
    const std::scoped_lock lock(clipboardMutex);

    clipboardStorage = std::move(text);
}

} // namespace react_native_linux
