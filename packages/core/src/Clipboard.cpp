#include "Clipboard.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

std::mutex clipboardMutex;
std::string clipboardStorage;
bool isPublishPending = false;

} // namespace

std::string clipboardText() {
    const std::scoped_lock lock(clipboardMutex);

    return clipboardStorage;
}

void setClipboardText(std::string text) {
    const std::scoped_lock lock(clipboardMutex);

    clipboardStorage = std::move(text);
    isPublishPending = true;
}

std::optional<std::string> takeClipboardPublish() {
    const std::scoped_lock lock(clipboardMutex);

    if (!std::exchange(isPublishPending, false)) {
        return std::nullopt;
    }

    return clipboardStorage;
}

void receiveClipboardText(std::string text) {
    const std::scoped_lock lock(clipboardMutex);

    clipboardStorage = std::move(text);
    isPublishPending = false;
}

std::optional<std::string_view> preferredClipboardMimeType(const std::vector<std::string>& offeredMimeTypes) {
    for (const std::string_view mimeType : kClipboardTextMimeTypes) {
        if (std::ranges::find(offeredMimeTypes, mimeType) != offeredMimeTypes.end()) {
            return mimeType;
        }
    }

    return std::nullopt;
}

} // namespace react_native_linux
