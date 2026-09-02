#include "ImageContent.h"

#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Point.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr std::string_view kDataScheme = "data:";
constexpr std::string_view kFileScheme = "file://";
constexpr std::string_view kSchemeSeparator = "://";
constexpr std::string_view kBase64Marker = ";base64";
constexpr uint32_t kBase64BitsPerCharacter = 6;
constexpr uint32_t kBitsPerByte = 8;
constexpr uint32_t kByteMask = 0xFFU;
constexpr int kLowercaseBase64Offset = 26;
constexpr int kDigitBase64Offset = 52;
constexpr int kPlusBase64Value = 62;
constexpr int kSlashBase64Value = 63;
constexpr float kHalf = 0.5F;

int base64Value(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }

    if (character >= 'a' && character <= 'z') {
        return (character - 'a') + kLowercaseBase64Offset;
    }

    if (character >= '0' && character <= '9') {
        return (character - '0') + kDigitBase64Offset;
    }

    if (character == '+') {
        return kPlusBase64Value;
    }

    if (character == '/') {
        return kSlashBase64Value;
    }

    return -1;
}

/**
 * Standard base64, stopping at the first padding character. Anything outside the alphabet fails the whole payload
 * rather than being skipped: a `data:` URI is machine-generated, so a stray character means the URI is wrong, not
 * that it needs repairing.
 */
std::vector<uint8_t> decodeBase64(std::string_view payload) {
    std::vector<uint8_t> bytes;
    uint32_t accumulator = 0;
    uint32_t bitCount = 0;

    for (char character : payload) {
        if (character == '=') {
            break;
        }

        const int value = base64Value(character);

        if (value < 0) {
            return {};
        }

        accumulator = (accumulator << kBase64BitsPerCharacter) | static_cast<uint32_t>(value);
        bitCount += kBase64BitsPerCharacter;

        if (bitCount >= kBitsPerByte) {
            bitCount -= kBitsPerByte;
            bytes.push_back(static_cast<uint8_t>((accumulator >> bitCount) & kByteMask));
        }
    }

    return bytes;
}

bool startsWith(const std::string& text, std::string_view prefix) {
    return std::string_view{text}.substr(0, prefix.size()) == prefix;
}

ResolvedImageSource resolveDataUri(const std::string& uri) {
    const size_t payloadStart = uri.find(',');

    if (payloadStart == std::string::npos) {
        return {};
    }

    const std::string_view mediaType = std::string_view{uri}.substr(0, payloadStart);

    if (mediaType.find(kBase64Marker) == std::string_view::npos) {
        return {};
    }

    std::vector<uint8_t> bytes = decodeBase64(std::string_view{uri}.substr(payloadStart + 1));

    if (bytes.empty()) {
        return {};
    }

    return ResolvedImageSource{.kind = ImageSourceKind::Data, .filePath = {}, .bytes = std::move(bytes)};
}

facebook::react::Float placementScale(SceneImageResizeMode resizeMode, const facebook::react::Rect& frame,
                                      facebook::react::Size imageSize) {
    const facebook::react::Float widthRatio = frame.size.width / imageSize.width;
    const facebook::react::Float heightRatio = frame.size.height / imageSize.height;

    if (resizeMode == SceneImageResizeMode::Contain) {
        return std::min(widthRatio, heightRatio);
    }

    if (resizeMode == SceneImageResizeMode::Cover) {
        return std::max(widthRatio, heightRatio);
    }

    return 1;
}

} // namespace

ResolvedImageSource resolveImageSource(const std::string& uri, const std::string& assetDirectory) {
    if (uri.empty()) {
        return {};
    }

    if (startsWith(uri, kDataScheme)) {
        return resolveDataUri(uri);
    }

    if (startsWith(uri, kFileScheme)) {
        return ResolvedImageSource{.kind = ImageSourceKind::File,
                                   .filePath = uri.substr(kFileScheme.size()),
                                   .bytes = {}};
    }

    // Every other scheme is remote as far as this platform is concerned, and there is no networking stack behind
    // it. See the deferrals in *Image* in docs/cpp-toolchain.md.
    if (uri.find(kSchemeSeparator) != std::string::npos) {
        return {};
    }

    if (uri.front() == '/') {
        return ResolvedImageSource{.kind = ImageSourceKind::File, .filePath = uri, .bytes = {}};
    }

    return ResolvedImageSource{.kind = ImageSourceKind::File,
                               .filePath = assetDirectory + "/" + uri,
                               .bytes = {}};
}

facebook::react::Rect imagePlacement(SceneImageResizeMode resizeMode, const facebook::react::Rect& frame,
                                     facebook::react::Size imageSize) {
    if (imageSize.width <= 0 || imageSize.height <= 0) {
        return facebook::react::Rect{.origin = frame.origin, .size = facebook::react::Size{}};
    }

    if (resizeMode == SceneImageResizeMode::Stretch) {
        return frame;
    }

    const facebook::react::Float scale = placementScale(resizeMode, frame, imageSize);
    const facebook::react::Size scaledSize{.width = imageSize.width * scale, .height = imageSize.height * scale};

    if (resizeMode == SceneImageResizeMode::Repeat) {
        return facebook::react::Rect{.origin = frame.origin, .size = scaledSize};
    }

    return facebook::react::Rect{
        .origin = facebook::react::Point{.x = frame.origin.x + ((frame.size.width - scaledSize.width) * kHalf),
                                         .y = frame.origin.y + ((frame.size.height - scaledSize.height) * kHalf)},
        .size = scaledSize};
}

ImageCache::ImageCache(size_t byteCapacity) : byteCapacity_(byteCapacity) {}

std::shared_ptr<void> ImageCache::find(const std::string& uri) {
    const auto entry = entries_.find(uri);

    if (entry == entries_.end()) {
        return nullptr;
    }

    order_.splice(order_.begin(), order_, entry->second.order);

    return entry->second.image;
}

void ImageCache::insert(const std::string& uri, std::shared_ptr<void> image, size_t byteCount) {
    eraseEntry(uri);

    if (byteCount > byteCapacity_) {
        return;
    }

    order_.push_front(uri);
    entries_.emplace(uri, Entry{.image = std::move(image), .byteCount = byteCount, .order = order_.begin()});
    byteCount_ += byteCount;

    while (byteCount_ > byteCapacity_) {
        const std::string leastRecentlyUsed = order_.back();

        eraseEntry(leastRecentlyUsed);
    }
}

size_t ImageCache::byteCount() const {
    return byteCount_;
}

size_t ImageCache::entryCount() const {
    return entries_.size();
}

void ImageCache::eraseEntry(const std::string& uri) {
    const auto entry = entries_.find(uri);

    if (entry == entries_.end()) {
        return;
    }

    byteCount_ -= entry->second.byteCount;
    order_.erase(entry->second.order);
    entries_.erase(entry);
}

} // namespace react_native_linux
