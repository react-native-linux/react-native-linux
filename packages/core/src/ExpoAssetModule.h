#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoAsset` (ADR-0004), the native module upstream's unmodified `expo-asset` requires as `expo` loads, and whose
 * one function, `downloadAsync(url, md5Hash, type)`, answers as upstream's iOS module does: a `file://` URL is
 * already local and resolves unchanged; any other URL is downloaded once into the XDG cache directory as
 * `ExponentAsset-<md5Hash>.<type>`, and resolves that file's URL.
 *
 * Threading contract: the JavaScript thread only, like every TurboModule method here; a download blocks it, as
 * `AsyncStorage`'s store does.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoAssetModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
