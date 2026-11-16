#pragma once

#include "SerialWorker.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>

namespace facebook::react {
class CallInvoker;
class TurboModule;
} // namespace facebook::react

namespace react_native_linux {

/**
 * What `ExpoSharing` hands a file to (#744), and the one thread it does that on. `WindowSession` points both functions
 * at the OpenURI portal (`OpenUriPortal.h`); a host that leaves them alone, one built without libsystemd or the
 * headless runner, has nothing to share with, and says so.
 *
 * Threading contract: the functions are assigned before the bundle runs and called only from `worker`, so a D-Bus
 * round trip never blocks the JavaScript thread. `TurboModuleRegistry` owns this object and destroys it only after
 * the JavaScript thread has quit, so the worker discards the calls still queued, whose promises nobody can read any
 * more, and waits only for the one running, which the portal's own timeouts bound. The worker is declared last, so it
 * joins before the functions, and the portal connection they hold, go.
 */
struct FileSharing {
    std::function<bool()> isAvailable = [] { return false; };
    std::function<void(const std::filesystem::path& file)> share = [](const std::filesystem::path&) {
        throw std::runtime_error("this host has no desktop portal to share through");
    };
    SerialWorker worker{SerialWorker::OnDestroy::discardQueued};
};

/**
 * `ExpoSharing`, the native module upstream expo-sharing's JavaScript requires as it is imported. `isAvailableAsync`
 * and `shareAsync` run `sharing`'s functions on its worker; `shareAsync` takes a `file://` URL or an absolute path and
 * ignores `mimeType`, `dialogTitle` and `UTI`, which the portal has no parameter for. Nothing shares into a Linux
 * application this way, so `getSharedPayloads` and `getResolvedSharedPayloadsAsync` answer an empty list and
 * `clearSharedPayloads` has nothing to clear.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoSharingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, FileSharing& sharing);

} // namespace react_native_linux
