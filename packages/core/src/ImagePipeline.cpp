#include "ImagePipeline.h"

#include "AutomationProtocol.h"
#include "CurlHttpClient.h"
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
#include <cstdint>
#include <deque>
#include <folly/io/IOBuf.h>
#include <memory>
#include <mutex>
#include <optional>
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
constexpr uint16_t kStatusClassDivisor = 100;
constexpr uint16_t kSuccessStatusClass = 2;
// NSURLRequest's default. Without one, a server that stops answering on an open connection would keep the source
// pending, and its `onError` unsent, for the life of the process.
constexpr uint32_t kDownloadTimeoutMilliseconds = 60'000;

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

/**
 * One source the worker owes a decode. A local one is just its URI, resolved on the worker; a remote one is queued
 * only once its download has finished, as the bytes it downloaded or as the failure that ended it.
 */
struct QueuedDecode {
    std::string uri;
    std::optional<std::vector<uint8_t>> downloaded;
    std::optional<std::string> failure;
};

std::shared_ptr<const DecodedImageFrames> decodeImageSource(QueuedDecode& queued) {
    const std::string& uri = queued.uri;

    if (queued.failure.has_value()) {
        reportNativeError("image", "could not download " + uri + ": " + queued.failure.value());

        return nullptr;
    }

    const ResolvedImageSource source = queued.downloaded.has_value()
                                           ? ResolvedImageSource{.kind = ImageSourceKind::Data,
                                                                 .filePath = {},
                                                                 .bytes = std::move(queued.downloaded.value())}
                                           : resolveImageSource(uri, RNL_BUNDLED_ASSET_DIR);

    if (source.kind == ImageSourceKind::Unsupported) {
        reportNativeError("image", "unsupported source " + uri);

        return nullptr;
    }

    const sk_sp<SkData> bytes = readImageBytes(source);

    if (bytes == nullptr) {
        reportNativeError("image", "could not read " + uri);

        return nullptr;
    }

    const std::unique_ptr<SkCodec> codec = SkCodec::MakeFromData(bytes, kImageDecoders);

    if (codec == nullptr) {
        reportNativeError("image", "no PNG, JPEG or GIF codec matched " + uri);

        return nullptr;
    }

    if (codec->getFrameCount() > 1) {
        return decodeAnimatedImage(*codec);
    }

    auto [image, result] = codec->getImage();

    if (image == nullptr) {
        reportNativeError("image", "decoding " + uri + " failed with SkCodec result " +
                                       std::to_string(static_cast<int>(result)));

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
        // First, because its destructor joins the download thread, which is the only other thread that queues.
        httpClient.reset();

        {
            const std::lock_guard<std::mutex> guard(mutex);

            isStopping = true;
        }

        queued.notify_all();
        worker.join();
    }

    void enqueue(QueuedDecode decode) {
        {
            const std::lock_guard<std::mutex> guard(mutex);

            queuedDecodes.push_back(std::move(decode));
        }

        queued.notify_one();
    }

    /**
     * Downloads a remote source on `httpClient`'s own thread and queues the result for the worker, so a slow
     * server never holds up the local decodes queued behind it. The body is buffered whole, because a codec needs
     * all of it; a status other than 2xx is a failure even when a body came with it, since that body is the
     * server's error page and not the image.
     */
    void download(const std::string& uri) {
        struct Response {
            uint16_t status{};
            std::unique_ptr<folly::IOBuf> body;
        };

        const std::shared_ptr<Response> response = std::make_shared<Response>();
        facebook::react::http::NetworkCallbacks callbacks;

        callbacks.onResponse = [response](uint16_t status, const facebook::react::http::Headers& /*headers*/) {
            response->status = status;
        };
        callbacks.onBody = [response](std::unique_ptr<folly::IOBuf> body) { response->body = std::move(body); };
        callbacks.onResponseComplete = [this, uri, response](const std::string& error, bool /*timeoutError*/) {
            QueuedDecode decode{.uri = uri, .downloaded = std::nullopt, .failure = std::nullopt};

            if (!error.empty()) {
                decode.failure = error;
            } else if (response->status / kStatusClassDivisor != kSuccessStatusClass) {
                decode.failure = "HTTP " + std::to_string(response->status);
            } else {
                const folly::ByteRange bytes = response->body->coalesce();

                decode.downloaded = std::vector<uint8_t>(bytes.begin(), bytes.end());
            }

            enqueue(std::move(decode));
        };

        httpClient->sendRequest(std::move(callbacks), "GET", uri, {}, {}, kDownloadTimeoutMilliseconds, std::nullopt);
    }

    void runWorker() {
        while (true) {
            QueuedDecode decode;

            {
                std::unique_lock<std::mutex> guard(mutex);

                queued.wait(guard, [this]() { return isStopping || !queuedDecodes.empty(); });

                if (isStopping) {
                    return;
                }

                decode = std::move(queuedDecodes.front());
                queuedDecodes.pop_front();
            }

            const std::string& uri = decode.uri;
            const std::shared_ptr<const DecodedImageFrames> decoded = decodeImageSource(decode);
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
    std::deque<QueuedDecode> queuedDecodes;
    std::unordered_set<std::string> requestedUris;
    std::unordered_map<std::string, std::vector<ImageDecodeCompletion>> completionsByUri;
    ImageDecodeListener listener;
    bool isStopping{false};
    // Created by the first remote source, so an application that never names one never starts its thread.
    std::unique_ptr<CurlHttpClient> httpClient;
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
    bool isRemote = false;

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

            isRemote = isRemoteImageSource(uri);

            if (isRemote && state.httpClient == nullptr) {
                state.httpClient = std::make_unique<CurlHttpClient>();
            } else if (!isRemote) {
                state.queuedDecodes.push_back(
                    QueuedDecode{.uri = uri, .downloaded = std::nullopt, .failure = std::nullopt});
            }
        }
    }

    if (cached != nullptr) {
        completion(cached);
    } else if (isRemote) {
        state.download(uri);
    } else {
        state.queued.notify_one();
    }
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
