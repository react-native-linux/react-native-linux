#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

class KeyValueStore;

/**
 * `GtReactNative` (#647), which `gt-react-native` requires with `TurboModuleRegistry.getEnforcing` as it is imported
 * on every platform but web, written against `TurboModule::methodMap_` because its spec belongs to that package.
 * `getNativeLocales` answers `preferredLanguageTags()`; `nativeStoreGet` and `nativeStoreSet` keep the locale the
 * user picked in a `KeyValueStore` database of their own, so it outlives a restart as Android's `SharedPreferences`
 * does and no AsyncStorage key collides with it. A set that changes nothing is not written, because `GTProvider`
 * sets its three keys again every time it mounts. `multiply` is the package's template method, answered as its Android
 * and iOS modules answer it.
 *
 * Threading contract: every method runs synchronously on the JavaScript thread, `KeyValueStore`'s own thread.
 */
std::shared_ptr<facebook::react::TurboModule>
makeGtReactNativeModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, std::shared_ptr<KeyValueStore> store);

} // namespace react_native_linux
