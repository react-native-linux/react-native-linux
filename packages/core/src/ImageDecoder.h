#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace react_native_linux {

/**
 * Called once, on the decode thread, after a source finished decoding into the cache.
 *
 * A decode is the one thing that changes the picture without a Fabric mutation behind it, so nothing damages the
 * frame for it unless somebody listens. The mounting manager is what does; see `damageImageSource`.
 */
using ImageDecodeListener = std::function<void(const std::string& uri)>;

/**
 * The Skia-free half of the image pipeline's API, so the Fabric host, the scene and the headless runner can drive
 * it without putting Skia on their include path. Requesting a decode is on the other side, in `ImagePipeline.h`,
 * because only `ImageManager` asks for one.
 */
void setImageDecodeListener(ImageDecodeListener listener);

/**
 * The decoded pixels for `uri`, or null when the source has not been decoded, failed, or has been evicted. Marks
 * the entry as most recently used.
 *
 * Type-erased for the reason `ImageCache` erases its values and upstream's `ImageResponse` erases its own: this
 * is what the scene attaches to the nodes drawing the source, and the scene links no Skia. The painter casts it
 * back to `SkImage`.
 */
std::shared_ptr<void> decodedImagePixels(const std::string& uri);

/**
 * Blocks until nothing is queued or decoding, or until `budget` runs out; returns whether the pipeline went idle.
 *
 * This exists for the golden-image rig, which has to rasterise a settled picture: the run loop that would
 * eventually notice the damage a decode produces is a window, and there is no window in a headless render.
 */
bool waitForPendingImageDecodes(std::chrono::milliseconds budget);

} // namespace react_native_linux
