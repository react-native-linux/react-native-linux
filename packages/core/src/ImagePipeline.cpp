#include "ImagePipeline.h"

#include "ImageContent.h"
#include "ImageDecoder.h"

#include "include/codec/SkCodec.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
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

// Only the two formats the pinned Skia archive was built with; its release name carries `jpegd`, and libpng is
// compiled into the archive. Passing the decoder list explicitly rather than relying on `SkCodecs::Register` keeps
// the supported set visible here instead of depending on which objects the linker happened to pull in.
constexpr std::array<SkCodecs::Decoder, 2> kImageDecoders{SkPngDecoder::Decoder(), SkJpegDecoder::Decoder()};

sk_sp<SkData> readImageBytes(const ResolvedImageSource& source) {
    if (source.kind == ImageSourceKind::Data) {
        return SkData::MakeWithCopy(source.bytes.data(), source.bytes.size());
    }

    return SkData::MakeFromFileName(source.filePath.c_str());
}

sk_sp<SkImage> decodeImageSource(const std::string& uri) {
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
        std::cerr << "[image] no PNG or JPEG codec matched " << uri << std::endl;

        return nullptr;
    }

    auto [image, result] = codec->getImage();

    if (image == nullptr) {
        std::cerr << "[image] decoding " << uri << " failed with SkCodec result " << static_cast<int>(result)
                  << std::endl;
    }

    return image;
}

std::shared_ptr<void> toSharedImage(sk_sp<SkImage> image) {
    if (image == nullptr) {
        return nullptr;
    }

    return std::shared_ptr<void>(image.release(), [](SkImage* decoded) { decoded->unref(); });
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
                isDecoding = true;
            }

            const sk_sp<SkImage> decodedSkImage = decodeImageSource(uri);
            const size_t byteCount = decodedSkImage == nullptr ? 0
                                                               : decodedSkImage->imageInfo().computeMinByteSize();
            const std::shared_ptr<void> image = toSharedImage(decodedSkImage);
            std::vector<ImageDecodeCompletion> completions;
            ImageDecodeListener decoded;

            {
                const std::lock_guard<std::mutex> guard(mutex);

                requestedUris.erase(uri);
                isDecoding = false;
                completions = std::move(completionsByUri[uri]);
                completionsByUri.erase(uri);

                if (image != nullptr) {
                    cache.insert(uri, image, byteCount);
                    decoded = listener;
                }
            }

            for (const ImageDecodeCompletion& completion : completions) {
                completion(image);
            }

            if (decoded) {
                decoded(uri);
            }

            // Last, so that going idle means the pixels have been published, not merely cached: the golden rig
            // waits on this and then snapshots the scene, and the nodes drawing this source are handed their
            // pixels by the listener above. Notifying first is a race that draws a blank image (#108).
            idle.notify_all();
        }
    }

    std::mutex mutex;
    std::condition_variable queued;
    std::condition_variable idle;
    ImageCache cache{kImageCacheByteCapacity};
    std::deque<std::string> queuedUris;
    std::unordered_set<std::string> requestedUris;
    std::unordered_map<std::string, std::vector<ImageDecodeCompletion>> completionsByUri;
    ImageDecodeListener listener;
    bool isDecoding{false};
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
    std::shared_ptr<void> cached;

    {
        const std::lock_guard<std::mutex> guard(state.mutex);

        cached = state.cache.find(uri);

        if (cached == nullptr) {
            state.completionsByUri[uri].push_back(std::move(completion));

            if (!state.requestedUris.insert(uri).second) {
                return;
            }

            state.queuedUris.push_back(uri);
        }
    }

    if (cached != nullptr) {
        completion(cached);

        return;
    }

    state.queued.notify_one();
}

std::shared_ptr<void> decodedImagePixels(const std::string& uri) {
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
    std::unique_lock<std::mutex> guard(state.mutex);

    return state.idle.wait_for(guard, budget, [&state]() { return state.queuedUris.empty() && !state.isDecoding; });
}

} // namespace react_native_linux
