#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

class ActivationModel;

/**
 * `ExpoLinking` (ADR-0004), the native module upstream's unmodified `expo-linking` requires as it is imported.
 * `getLinkingURL` answers the URL that last reached this process, the same `ActivationModel` state React Native's
 * `Linking.getInitialURL` reads, and `onURLReceived` subscribes to the `url` device event that state already emits,
 * with upstream's `{ url }` payload. `clearInitialURL` is not answered: upstream's optional call is then the no-op it
 * is on web.
 *
 * Threading contract: the JavaScript thread only, like every TurboModule method here.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoLinkingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                      std::shared_ptr<ActivationModel> activationModel);

} // namespace react_native_linux
