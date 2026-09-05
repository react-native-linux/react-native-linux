#include <react/renderer/imagemanager/ImageManager.h>

#include "ImagePipeline.h"

#include <react/renderer/imagemanager/ImageRequest.h>
#include <react/renderer/imagemanager/ImageRequestParams.h>
#include <react/renderer/imagemanager/ImageResponse.h>
#include <react/renderer/imagemanager/ImageResponseObserverCoordinator.h>
#include <react/renderer/imagemanager/ImageTelemetry.h>
#include <react/renderer/imagemanager/primitives.h>

#include <memory>
#include <utility>

/**
 * The Linux `ImageManager`: the one request `ImageShadowNode` makes, answered by Skia's codecs.
 *
 * This file replaces `react/renderer/imagemanager/platform/cxx/.../ImageManager.cpp`, whose `requestImage` is
 * marked `// Not implemented` and returns a request nothing ever completes. It is compiled in that file's place by
 * a source swap at our own CMake call site rather than by editing the vendored tree, and only when Skia is
 * available; a build configured with `-DRNL_ENABLE_SKIA=OFF` keeps the upstream stub, so the sanitizer presets and
 * the Skia-free unit build stay exactly as they were and an `<Image>` there simply draws nothing.
 *
 * Replacing the definition rather than subclassing is deliberate. `ImageComponentDescriptor` resolves its manager
 * with `getManagerByName<ImageManager>(contextContainer, "ImageManager")`, which falls back to
 * `std::make_shared<ImageManager>(contextContainer)` when the key is absent — so replacing the definition means
 * every descriptor gets this implementation with no registration step and no place for the two to disagree. That
 * is also why nothing inserts an `"ImageManager"` entry into the context container.
 *
 * The decode is asynchronous and shared: `requestImageDecode` queues the source on the pipeline's worker thread
 * and calls back with the decoded image, so a commit never waits on a codec and two `<Image>` nodes with the same
 * source decode once. The observer coordinator upstream builds for every request is completed from that callback,
 * which is what makes `onLoad` and `onError` a matter of emitting events rather than of plumbing.
 */
namespace facebook::react {

ImageManager::ImageManager(const std::shared_ptr<const ContextContainer>& /*contextContainer*/) {
    // Same silencing the upstream cxx implementation does: `self_` is the platform handle iOS and Android store
    // their loader in, and this platform keeps its state in the process-wide pipeline instead.
    (void)self_;
}

ImageManager::~ImageManager() = default;

ImageRequest ImageManager::requestImage(const ImageSource& imageSource, SurfaceId surfaceId,
                                        const ImageRequestParams& /*imageRequestParams*/, Tag /*tag*/) const {
    ImageRequest imageRequest{imageSource, std::make_shared<const ImageTelemetry>(surfaceId)};

    if (imageSource.uri.empty()) {
        return imageRequest;
    }

    // Weak, because the request belongs to the shadow node's state: a source that changes before its decode
    // finishes leaves a coordinator nothing is waiting on, and completing it would be reviving a dead request.
    const std::weak_ptr<const ImageResponseObserverCoordinator> observers =
        imageRequest.getSharedObserverCoordinator();

    react_native_linux::requestImageDecode(
        imageSource.uri,
        [observers](const std::shared_ptr<const react_native_linux::DecodedImageFrames>& decoded) {
            const std::shared_ptr<const ImageResponseObserverCoordinator> coordinator = observers.lock();

            if (coordinator == nullptr) {
                return;
            }

            if (decoded == nullptr) {
                coordinator->nativeImageResponseFailed(ImageLoadError{nullptr});

                return;
            }

            // The first frame, because that is what "the image loaded" means to an observer: an animated source
            // fires `onLoad` once, when it can first be drawn, not once per frame.
            coordinator->nativeImageResponseComplete(ImageResponse{decoded->frames.front(), nullptr});
        });

    return imageRequest;
}

} // namespace facebook::react
