#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoApplication` (ADR-0004), which upstream's unmodified `expo-application` requires as it is imported. The
 * application's name, id, version and build come from the bundle's manifest, which a `linux` build does not embed
 * yet (#648), so all four answer `null`, as iOS does for an `Info.plist` without them. The installation-time,
 * install-referrer, vendor-id and store queries are not answered, so upstream rejects each with its own
 * `UnavailabilityError`.
 *
 * Threading contract: the JavaScript thread only, like every TurboModule method here.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoApplicationModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
