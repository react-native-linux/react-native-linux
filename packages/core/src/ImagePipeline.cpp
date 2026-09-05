#include "ImagePipeline.h"

#include "ImageContent.h"
#include "ImageDecoder.h"

#include "include/codec/SkCodec.h"
#include "include/codec/SkCodecAnimation.h"
#include "include/codec/SkGifDecoder.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkAlphaType.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorType.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSpan.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

// Bytes rather than entries, because the cost of a decoded image is its pixels and two sources can differ by three
// orders of magnitude in area. See *Image* in docs/cpp-toolchain.md.
constexpr size_t kImageCacheByteCapacity = 64UL * 1024UL * 1024UL;

// Only the three formats the pinned Skia archive was built with; its release name carries `jpegd`, libpng is
// compiled into the archive, and the GIF decoder is the one Skia vendors. Passing the decoder list
// explicitly rather than relying on `SkCodecs::Register` keeps the supported set visible here instead of
// depending on which objects the linker happened to pull in. WebP is deliberately absent: the archive carries no
// WebP symbols at all, so there is nothing to register. See *Image* in docs/cpp-toolchain.md.
constexpr std::array<SkCodecs::Decoder, 3> kImageDecoders{SkPngDecoder::Decoder(), SkJpegDecoder::Decoder(),
                                                          SkGifDecoder::Decoder()};

sk_sp<SkData> readImageBytes(const ResolvedImageSource& source) {
    if (source.kind == ImageSourceKind::Data) {
        return SkData::MakeWithCopy(source.bytes.data(), source.bytes.size());
    }

    return SkData::MakeFromFileName(source.filePath.c_str());
}

std::shared_ptr<void> toSharedImage(sk_sp<SkImage> image) {
    if (image == nullptr) {
        return nullptr;
    }

    return std::shared_ptr<void>(image.release(), [](SkImage* decoded) { decoded->unref(); });
}

/**
 * Every frame of an animated source, composed in order into `frames`.
 *
 * `SkCodec` decodes a GIF frame into the pixels of the frame it depends on, so the buffer is carried forward and
 * `fPriorFrame` names it whenever the dependency is the frame immediately before — which is the common case and
 * the one that keeps this linear rather than quadratic. When it is not, the option is left unset and the codec
 * decodes whatever prior frames it needs itself.
 *
 * A frame that fails to decode ends the animation there rather than failing the source: a truncated GIF still
 * shows the frames that were whole, which is what every browser does with one.
 */
std::vector<std::shared_ptr<void>> decodeAnimationFrames(SkCodec& codec,
                                                         const std::vector<SkCodec::FrameInfo>& frameInfos) {
    const SkImageInfo imageInfo = codec.getInfo().makeColorType(kN32_SkColorType).makeAlphaType(kPremul_SkAlphaType);
    std::vector<std::shared_ptr<void>> frames;
    SkBitmap bitmap;

    if (!bitmap.tryAllocPixels(imageInfo)) {
        return frames;
    }

    for (size_t index = 0; index < frameInfos.size(); ++index) {
        SkCodec::Options options;

        options.fFrameIndex = static_cast<int>(index);

        if (frameInfos[index].fRequiredFrame == static_cast<int>(index) - 1 && index > 0) {
            options.fPriorFrame = static_cast<int>(index) - 1;
        } else {
            bitmap.eraseColor(SK_ColorTRANSPARENT);
        }

        if (codec.getPixels(imageInfo, bitmap.getPixels(), bitmap.rowBytes(), &options) != SkCodec::kSuccess) {
            break;
        }

        frames.push_back(toSharedImage(SkImages::RasterFromPixmapCopy(bitmap.pixmap())));
    }

    return frames;
}

std::vector<int32_t> frameDurations(const std::vector<SkCodec::FrameInfo>& frameInfos, size_t frameCount) {
    std::vector<int32_t> durations;

    durations.reserve(frameCount);

    for (size_t index = 0; index < frameCount; ++index) {
        durations.push_back(frameInfos[index].fDuration);
    }

    return durations;
}

std::shared_ptr<const DecodedImageFrames> decodeAnimatedImage(SkCodec& codec) {
    const std::vector<SkCodec::FrameInfo> frameInfos = codec.getFrameInfo();
    std::vector<std::shared_ptr<void>> frames = decodeAnimationFrames(codec, frameInfos);

    if (frames.empty()) {
        return nullptr;
    }

    const size_t frameCount = frames.size();

    return std::make_shared<const DecodedImageFrames>(
        DecodedImageFrames{.frames = std::move(frames),
                           .frameDurationsMilliseconds = frameDurations(frameInfos, frameCount),
                           .repetitionCount = codec.getRepetitionCount()});
}

std::shared_ptr<const DecodedImageFrames> decodeImageSource(const std::string& uri) {
    const ResolvedImageSource source = resolveImageSource(uri, RNL_BUNDLED_ASSET_DIR);

    if (source.kind == ImageSourceKind::Unsupported) {
        std::cerr << "[image] unsupported source " << uri << std::endl;

        return nullptr;
    }

    const sk_sp<SkData> bytes = readImageBytes(source);

    if (bytes == nullptr) {
        std::cerr << "[image] could not read " << uri << std::endl;

        return nullptr;
    }

    const std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(bytes, kImageDecoders);

    if (codec == nullptr) {
        std::cerr << "[image] no PNG, JPEG or GIF codec matched " << uri << std::endl;

        return nullptr;
    }

    if (codec->getFrameCount() > 1) {
        return decodeAnimatedImage(*codec);
    }

    auto [image, result] = codec->getImage();

    if (image == nullptr) {
        std::cerr << "[image] decoding " << uri << " failed with SkCodec result " << static_cast<int>(result)
                  << std::endl;

        return nullptr;
    }

    return std::make_shared<const DecodedImageFrames>(DecodedImageFrames{.frames = {toSharedImage(image)}});
}

/**
 * What the cache accounts a source as: the pixels of every frame it decoded into.
 */
size_t decodedByteCount(const DecodedImageFrames& decoded) {
    size_t byteCount = 0;

    for (const std::shared_ptr<void>& frame : decoded.frames) {
        byteCount += static_cast<const SkImage*>(frame.get())->imageInfo().computeMinByteSize();
    }

    return byteCount;
}

/**
 * The process-wide owner of the decode queue, the worker thread and the cache, reached through one function-local
 * static exactly like the text pipeline's `FontCollection`.
 *
 * The worker is joined by the destructor before any member it touches is destroyed, which is what makes a
 * function-local static safe to hand a thread.
 */
struct ImagePipelineState {
    ImagePipelineState() : worker([this]() { runWorker(); }) {}

    ImagePipelineState(const ImagePipelineState&) = delete;
    ImagePipelineState(ImagePipelineState&&) = delete;
    ImagePipelineState& operator=(const ImagePipelineState&) = delete;
    ImagePipelineState& operator=(ImagePipelineState&&) = delete;

    ~ImagePipelineState() {
        {
            const std::lock_guard<std::mutex> guard(mutex);

            isStopping = true;
        }

        queued.notify_all();
        worker.join();
    }

    void runWorker() {
        while (true) {
            std::string uri;

            {
                std::unique_lock<std::mutex> guard(mutex);

                queued.wait(guard, [this]() { return isStopping || !queuedUris.empty(); });

                if (isStopping) {
                    return;
                }

                uri = std::move(queuedUris.front());
                queuedUris.pop_front();
            }

            const std::shared_ptr<const DecodedImageFrames> decoded = decodeImageSource(uri);
            const size_t byteCount = decoded == nullptr ? 0 : decodedByteCount(*decoded);
            std::vector<ImageDecodeCompletion> completions;
            ImageDecodeListener decodeListener;

            {
                const std::lock_guard<std::mutex> guard(mutex);

                requestedUris.erase(uri);
                completions = std::move(completionsByUri[uri]);
                completionsByUri.erase(uri);

                if (decoded != nullptr) {
                    // The insert may refuse these frames — one animation can be larger than the whole capacity —
                    // and the listener runs either way. Admission decides what a *later* mount finds without
                    // decoding again; it does not decide whether this decode reaches the screen.
                    cache.insert(uri, decoded, byteCount);
                    decodeListener = listener;
                }
            }

            for (const ImageDecodeCompletion& completion : completions) {
                completion(decoded);
            }

            if (decodeListener) {
                decodeListener(uri, decoded);
            }

            // Last, so that settling means the pixels have been published, not merely cached: the golden rig
            // waits on this and then snapshots the scene, and the nodes drawing this source are handed their
            // pixels by the listener above. Counting the decode out any earlier is a race that draws a blank
            // image (#108, #296).
            pendingDecodes.notePublished();
        }
    }

    std::mutex mutex;
    std::condition_variable queued;
    PendingImageDecodes pendingDecodes;
    ImageCache cache{kImageCacheByteCapacity};
    std::deque<std::string> queuedUris;
    std::unordered_set<std::string> requestedUris;
    std::unordered_map<std::string, std::vector<ImageDecodeCompletion>> completionsByUri;
    ImageDecodeListener listener;
    bool isStopping{false};
    std::thread worker;
};

ImagePipelineState& imagePipelineState() {
    static ImagePipelineState state;

    return state;
}

} // namespace

void requestImageDecode(const std::string& uri, ImageDecodeCompletion completion) {
    ImagePipelineState& state = imagePipelineState();
    std::shared_ptr<const DecodedImageFrames> cached;

    {
        const std::lock_guard<std::mutex> guard(state.mutex);

        cached = state.cache.find(uri);

        if (cached == nullptr) {
            state.completionsByUri[uri].push_back(std::move(completion));

            if (!state.requestedUris.insert(uri).second) {
                return;
            }

            // Inside the lock and before the queue push, so a decode is pending from the instant it is asked
            // for: a settle that runs between the request and the worker picking it up must still wait for it,
            // and `waitUntilSettled` passes through this same mutex so it cannot read the count between the
            // insert above and this line (#296).
            state.pendingDecodes.noteRequested();
            state.queuedUris.push_back(uri);
        }
    }

    if (cached != nullptr) {
        completion(cached);

        return;
    }

    state.queued.notify_one();
}

std::shared_ptr<const DecodedImageFrames> decodedImage(const std::string& uri) {
    ImagePipelineState& state = imagePipelineState();
    const std::lock_guard<std::mutex> guard(state.mutex);

    return state.cache.find(uri);
}

void setImageDecodeListener(ImageDecodeListener listener) {
    ImagePipelineState& state = imagePipelineState();
    const std::lock_guard<std::mutex> guard(state.mutex);

    state.listener = std::move(listener);
}

bool waitForPendingImageDecodes(std::chrono::milliseconds budget) {
    ImagePipelineState& state = imagePipelineState();

    return state.pendingDecodes.waitUntilSettled(state.mutex, budget);
}

} // namespace react_native_linux
