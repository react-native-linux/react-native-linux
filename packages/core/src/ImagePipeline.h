#pragma once

#include "RetainedScene.h"

#include <functional>
#include <memory>
#include <string>

namespace react_native_linux {

/**
 * Called once with the decoded frames, or with null when the source could not be decoded. Runs on the decode
 * thread, or inline on the caller's thread when the source is already cached.
 */
using ImageDecodeCompletion = std::function<void(const std::shared_ptr<const DecodedImageFrames>& decoded)>;

/**
 * Turns an `<Image>` source into decoded pixels. This is the whole image pipeline: source resolution and the
 * bounded cache are `ImageContent.h`, decoding is Skia's own PNG, JPEG and GIF codecs inside `libskia.a`, and
 * there is no image abstraction between them.
 *
 * `ImageManager::requestImage` calls `requestImageDecode` during layout, on whichever thread commits; the decode
 * itself runs on one process-wide worker thread, so a frame is never blocked on a codec. Two requests for the same
 * source decode once: the second one joins the first's completion list. The scene asks `decodedImage` for
 * a source when it mounts a node and when a decode reports, and the painter draws the pixels the node is holding
 * — a source that has not finished decoding yet draws nothing, and the completion damages the frame so the next
 * one draws it.
 *
 * Threading contract: every function here is safe to call from any thread. The cache, the queue, the completion
 * lists and the listener all live under one mutex, and nothing but bookkeeping runs under it — hash and list
 * operations, and a cache eviction loop bounded by the entries the cache holds. The file read, the codec, the
 * completions and the listener are all outside it, so a decode never blocks a commit, a thread blocking on this
 * mutex waits microseconds, and the listener may take the mounting manager's lock without inverting anything.
 *
 * `PendingImageDecodes` holds the only other mutex, and whenever both are held this one comes first:
 * `requestImageDecode` nests the counter's inside it to count a decode in the same step that registers the URI,
 * and `waitForPendingImageDecodes` hands this mutex to `waitUntilSettled`, which takes and releases it before it
 * takes the counter's. Nested on one path and sequential on the other, in the same order on both.
 */
void requestImageDecode(const std::string& uri, ImageDecodeCompletion completion);

} // namespace react_native_linux
