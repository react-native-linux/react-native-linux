#pragma once

#include "RetainedScene.h"

#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace react_native_linux {

/**
 * What a source URI turned out to be. `Unsupported` covers everything this platform declines to fetch: `http`,
 * `https`, any other scheme, and a malformed `data:` URI. There is no networking stack here yet, so a remote
 * source is not a decode that failed — it is a decode that was never attempted.
 */
enum class ImageSourceKind : uint8_t { File, Data, Unsupported };

/**
 * A source URI reduced to the one thing a decoder can open: a filesystem path, or the bytes a `data:` URI carried.
 * Exactly one of the two members is filled, chosen by `kind`.
 */
struct ResolvedImageSource {
    ImageSourceKind kind{ImageSourceKind::Unsupported};
    std::string filePath;
    std::vector<uint8_t> bytes;
};

/**
 * Resolves one `<Image>` source URI without touching the filesystem, so the whole of source resolution is testable
 * as arithmetic on strings.
 *
 * `file://` and an absolute path are the same thing with and without the scheme. A relative path is resolved
 * against `assetDirectory`, because there is no asset packaging yet and a bundle cannot know where it was
 * installed; see *Image* in docs/cpp-toolchain.md. A `data:` URI must be base64, which is the only form React
 * Native's own tooling emits.
 */
ResolvedImageSource resolveImageSource(const std::string& uri, const std::string& assetDirectory);

/**
 * Where a decoded image of `imageSize` is drawn inside `frame` under one `resizeMode`.
 *
 * `stretch` fills the frame, `contain` and `cover` scale uniformly to the smaller and the larger of the two
 * ratios, and `center` and `repeat` keep the natural size. Everything but `repeat` is centred in the frame;
 * `repeat` anchors at the frame's top-left corner because the returned rectangle is its first tile. An image with
 * no area produces an empty rectangle rather than a division by zero.
 *
 * Nothing here clips: `cover` deliberately returns a rectangle larger than the frame, and the painter's rounded
 * clip is what cuts it.
 */
facebook::react::Rect imagePlacement(SceneImageResizeMode resizeMode, const facebook::react::Rect& frame,
                                     facebook::react::Size imageSize);

/**
 * A bounded least-recently-used cache of decoded images, keyed by source URI.
 *
 * The bound is bytes rather than entries, because the cost of a decoded image is its pixels and two sources can
 * differ by three orders of magnitude in area. Inserting past the capacity evicts from the least recently used end
 * until the total fits; an entry that alone exceeds the capacity is not cached at all, so one oversized image
 * cannot flush everything else out on its way to being evicted itself.
 *
 * The value is `std::shared_ptr<void>` so this translation unit links no Skia and stays inside the coverage gate.
 * The decoder puts an `SkImage` in and casts it back out, which is the same type erasure upstream's own
 * `ImageResponse` uses for exactly this reason.
 *
 * Threading contract: this type is not synchronised. Its owner serialises access.
 */
class ImageCache final {
public:
    explicit ImageCache(size_t byteCapacity);

    std::shared_ptr<void> find(const std::string& uri);
    void insert(const std::string& uri, std::shared_ptr<void> image, size_t byteCount);
    size_t byteCount() const;
    size_t entryCount() const;

private:
    struct Entry {
        std::shared_ptr<void> image;
        size_t byteCount{};
        std::list<std::string>::iterator order;
    };

    void eraseEntry(const std::string& uri);

    size_t byteCapacity_;
    size_t byteCount_{};
    std::list<std::string> order_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace react_native_linux
