#pragma once

#include "RetainedScene.h"

#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
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
 * Which frame of `decoded` is on screen `elapsedMilliseconds` after its first frame appeared.
 *
 * This is the whole animation schedule, and it is arithmetic on the durations the codec read out of the file — not
 * a counter of display refreshes. A 120 Hz display asks twice as often as a 60 Hz one and gets the same answer at
 * the same instant, which is react-native#33039: a GIF whose frames were paced by vsync instead of by its own
 * durations plays at twice the speed on a 120 Hz device.
 *
 * A source with fewer than two frames, with no durations, or whose durations sum to nothing, is a still image and
 * is always frame zero. `repetitionCount` is `SkCodec::getRepetitionCount`: the number of plays *after* the first,
 * so a count of one plays the whole animation twice and then holds the last frame forever, and
 * `kAnimatedImageRepeatsForever` never holds.
 */
size_t animatedImageFrameIndex(const DecodedImageFrames& decoded, double elapsedMilliseconds);

/**
 * Whether `decoded` is something `animatedImageFrameIndex` can move: more than one frame, a duration for each of
 * them, and a total duration greater than zero.
 */
bool isAnimatedImage(const DecodedImageFrames& decoded);

/**
 * A bounded least-recently-used cache of decoded images, keyed by source URI.
 *
 * The bound is bytes rather than entries, because the cost of a decoded image is its pixels and two sources can
 * differ by three orders of magnitude in area. Inserting past the capacity evicts from the least recently used end
 * until the total fits; an entry that alone exceeds the capacity is not cached at all, so one oversized image
 * cannot flush everything else out on its way to being evicted itself.
 *
 * The value is a `DecodedImageFrames`, whose own frames are `std::shared_ptr<void>`, so this translation unit
 * links no Skia and stays inside the coverage gate. The decoder puts `SkImage`s in and the painter casts one back
 * out, which is the same type erasure upstream's own `ImageResponse` uses for exactly this reason. One entry is
 * one source, still or animated: every frame of a GIF is evicted together, because they are drawn together.
 *
 * Threading contract: this type is not synchronised. Its owner serialises access.
 */
class ImageCache final {
public:
    explicit ImageCache(size_t byteCapacity);

    std::shared_ptr<const DecodedImageFrames> find(const std::string& uri);
    void insert(const std::string& uri, std::shared_ptr<const DecodedImageFrames> image, size_t byteCount);
    size_t byteCount() const;
    size_t entryCount() const;

private:
    struct Entry {
        std::shared_ptr<const DecodedImageFrames> image;
        size_t byteCount{};
        std::list<std::string>::iterator order;
    };

    void eraseEntry(const std::string& uri);

    size_t byteCapacity_;
    size_t byteCount_{};
    std::list<std::string> order_;
    std::unordered_map<std::string, Entry> entries_;
};

/**
 * How many decodes the pipeline still owes, and the wait a headless run takes on that reaching zero.
 *
 * A decode stops being pending only once its pixels have been handed to everything that draws them — the
 * per-request completions and the decode listener that damages the scene — and not when the codec returned. The
 * distinction is the whole of issue #296: a golden rasterised in the window between "the codec is done" and "the
 * scene has the pixels" paints a blank tile, and how wide that window is depends on machine load, which is why
 * the same fixture regenerated to two different PNGs.
 *
 * A decode counts from the moment it is requested rather than from the moment the worker picks it up, so one
 * requested by a later commit than the run waited for still holds the settle open.
 *
 * Threading contract: every function is safe to call from any thread. `noteRequested` runs on whichever thread
 * commits, `notePublished` on the decode worker, and `waitUntilSettled` on the thread driving a headless run.
 * The mutex is never held while a codec, a completion or the listener runs, because those happen between the
 * two notes rather than inside either.
 */
class PendingImageDecodes final {
public:
    void noteRequested();
    void notePublished();
    bool waitUntilSettled(std::chrono::milliseconds budget);

private:
    std::mutex mutex_;
    std::condition_variable settled_;
    size_t pendingCount_{};
};

} // namespace react_native_linux
