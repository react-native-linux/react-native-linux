#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoKeepAwake` (ADR-0004), which upstream's unmodified `expo-keep-awake` requires as it is imported. Keeping the
 * screen awake is not offered on Linux yet, so this answers through upstream's own availability query:
 * `isAvailableAsync` resolves `false`, and `activate` and `deactivate` reject with upstream's `ERR_UNAVAILABLE` code
 * rather than resolving as if the screen were held awake.
 *
 * Threading contract: the JavaScript thread only, like every TurboModule method here.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoKeepAwakeModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
