#include "Clipboard.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

std::mutex clipboardMutex;
std::string clipboardStorage;
uint64_t clipboardRevision = 0;
bool isPublishPending = false;

} // namespace

std::string clipboardText() {
    const std::scoped_lock lock(clipboardMutex);

    return clipboardStorage;
}

void setClipboardText(std::string text) {
    const std::scoped_lock lock(clipboardMutex);

    clipboardStorage = std::move(text);
    ++clipboardRevision;
    isPublishPending = true;
}

bool takeClipboardPublish() {
    const std::scoped_lock lock(clipboardMutex);

    return std::exchange(isPublishPending, false);
}

uint64_t beginClipboardReceive() {
    const std::scoped_lock lock(clipboardMutex);

    clipboardStorage.clear();
    isPublishPending = false;

    return ++clipboardRevision;
}

bool receiveClipboardText(std::string text, uint64_t revision) {
    const std::scoped_lock lock(clipboardMutex);

    if (revision != clipboardRevision) {
        return false;
    }

    clipboardStorage = std::move(text);

    return true;
}

std::string createClipboardOwnerMimeType() {
    std::random_device randomDevice;

    return "application/x-react-native-linux-" + std::to_string(getpid()) + "-" + std::to_string(randomDevice()) +
           std::to_string(randomDevice());
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
