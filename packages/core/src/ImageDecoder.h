#pragma once

#include "RetainedScene.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace react_native_linux {

/**
 * Called once, on the decode thread, with the frames a source finished decoding into.
 *
 * A decode is the one thing that changes the picture without a Fabric mutation behind it, so nothing damages the
 * frame for it unless somebody listens. The mounting manager is what does; see `damageImageSource`.
 *
 * The frames are handed over rather than looked up again, because the cache is allowed to refuse them: a source
 * whose frames exceed the whole capacity is never admitted, and a listener that answered by asking the cache
 * would hand the scene nothing and paint an animation as a blank box.
 */
using ImageDecodeListener =
    std::function<void(const std::string& uri, const std::shared_ptr<const DecodedImageFrames>& decoded)>;

/**
 * The Skia-free half of the image pipeline's API, so the Fabric host, the scene and the headless runner can drive
 * it without putting Skia on their include path. Requesting a decode is on the other side, in `ImagePipeline.h`,
 * because only `ImageManager` asks for one.
 */
void setImageDecodeListener(ImageDecodeListener listener);

/**
 * The decoded frames of `uri`, or null when the source has not been decoded, failed, or has been evicted. Marks
 * the entry as most recently used.
 *
 * Type-erased frame by frame for the reason `ImageCache` erases its values and upstream's `ImageResponse` erases
 * its own: this is what the scene attaches to the nodes drawing the source, and the scene links no Skia. The
 * painter casts one frame back to `SkImage`.
 */
std::shared_ptr<const DecodedImageFrames> decodedImage(const std::string& uri);

/**
 * Blocks until every requested decode has published its pixels, or until `budget` runs out; returns whether the
 * pipeline settled.
 *
 * This exists for the golden-image rig, which has to rasterise a settled picture: the run loop that would
 * eventually notice the damage a decode produces is a window, and there is no window in a headless render.
 *
 * Settled is stronger than "the codec is done": a decode is counted out only after the completions and the
 * listener above have run, so a snapshot taken the instant this returns cannot land in the window where the
 * pixels are cached but no node draws them yet. See `PendingImageDecodes` and issue #296.
 */
bool waitForPendingImageDecodes(std::chrono::milliseconds budget);

} // namespace react_native_linux
