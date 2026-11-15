#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoNetwork` (ADR-0004), which upstream's unmodified `expo-network` requires as it is imported. Connectivity comes
 * from the NetworkMonitor portal, which this platform does not read yet (#648), so `getNetworkStateAsync` resolves
 * upstream's `UNKNOWN` type with `isConnected` and `isInternetReachable` left unset, never a guessed connection.
 * `onNetworkStateChanged` is React Native's C++ event emitter, so `addNetworkStateListener` returns a subscription;
 * nothing emits on it while the state stays unknown. Upstream rejects `getIpAddressAsync` and
 * `isAirplaneModeEnabledAsync` itself, with `UnavailabilityError`, because neither is a method here.
 *
 * Threading contract: every method runs on the JavaScript thread.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoNetworkModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
