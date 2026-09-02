#pragma once

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"

#include <functional>
#include <memory>
#include <string>

namespace react_native_linux {

/**
 * Called once with the decoded image, or with null when the source could not be decoded. Runs on the decode
 * thread, or inline on the caller's thread when the source is already cached.
 */
using ImageDecodeCompletion = std::function<void(const std::shared_ptr<void>& image)>;

/**
 * Turns an `<Image>` source into decoded pixels. This is the whole image pipeline: source resolution and the
 * bounded cache are `ImageContent.h`, decoding is Skia's own PNG and JPEG codecs inside `libskia.a`, and there is
 * no image abstraction between them.
 *
 * `ImageManager::requestImage` calls `requestImageDecode` during layout, on whichever thread commits; the decode
 * itself runs on one process-wide worker thread, so a frame is never blocked on a codec. Two requests for the same
 * source decode once: the second one joins the first's completion list. The painter calls `decodedImage` on the
 * frame thread and draws whatever is there — a source that has not finished decoding yet draws nothing, and the
 * completion damages the frame so the next one draws it.
 *
 * Threading contract: every function here is safe to call from any thread. The cache, the queue, the completion
 * lists and the listener all live under one mutex, and it is never held while a codec runs or while a completion
 * or the listener is called, so a decode never blocks a commit and the listener may take the mounting manager's
 * lock without inverting anything.
 */
void requestImageDecode(const std::string& uri, ImageDecodeCompletion completion);

/**
 * The decoded image for `uri`, or null when it has not been decoded, failed, or has been evicted. Marks the entry
 * as most recently used.
 */
sk_sp<SkImage> decodedImage(const std::string& uri);

} // namespace react_native_linux
