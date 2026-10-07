#pragma once

#include "AnimationFrameQueue.h"
#include "LinuxUIScheduler.h"

#include <ReactCommon/TurboModule.h>
#include <chrono>
#include <jsi/jsi.h>
#include <memory>
#include <string_view>
#include <worklets/Tools/RNRuntimeStatus.h>

namespace worklets {

class WorkletRuntime;
class WorkletsModuleProxy;

} // namespace worklets

namespace react_native_linux {

/**
 * react-native-worklets' `WorkletsModule` (#136): the host `apple/worklets/apple/WorkletsModule.mm` and
 * `android/src/main/cpp/worklets/android/WorkletsModule.cpp` are on their platforms, as a C++ TurboModule.
 * `installTurboModule` constructs the one `WorkletsModuleProxy`, whose constructor installs
 * `globalThis.__workletsModuleProxy`, and `start` initializes the UI worklet runtime. Bundle Mode is off; see
 * *react-native-worklets* in docs/cpp-toolchain.md.
 *
 * The spec has three synchronous methods, so they are registered here by hand rather than through a generated
 * `NativeWorkletsModuleCxxSpec`: generating library specs is the autolinking driver's job (#137).
 *
 * Worklets' UI thread is the frame thread (ADR-0003), so `TurboModuleRegistry` constructs this module there,
 * eagerly. `tick` is the frame's one point where the UI jobs other threads queued run and worklets'
 * `requestAnimationFrame` callbacks fire, all with the frame's own timestamp. A callback registered while they
 * fire waits for the next `tick`.
 *
 * Threading contract: construction, `tick`, `hasPendingWork` and `uiWorkletRuntime` run on the frame thread, and
 * the three JavaScript methods and `invalidate` on the JavaScript thread. `ReactHost` calls `invalidate` on the
 * JavaScript thread before it quits it, while the frame thread waits, so the proxy and the UI worklet runtime are
 * destroyed while the React Native runtime is still alive and no frame is running.
 */
class LinuxWorkletsModule final : public facebook::react::TurboModule {
public:
    static constexpr std::string_view kModuleName = "WorkletsModule";

    explicit LinuxWorkletsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

    void tick(std::chrono::steady_clock::time_point now);
    bool hasPendingWork() const;

    /** The UI worklet runtime once `installTurboModule` has run, or null. */
    std::shared_ptr<worklets::WorkletRuntime> uiWorkletRuntime() const;

    void invalidate();

private:
    void installProxy(facebook::jsi::Runtime& runtime, bool bundleModeEnabled);
    void start();

    std::shared_ptr<LinuxUIScheduler> uiScheduler_{std::make_shared<LinuxUIScheduler>()};
    AnimationFrameQueue animationFrames_;
    std::shared_ptr<worklets::RNRuntimeStatus> rnRuntimeStatus_{std::make_shared<worklets::RNRuntimeStatus>()};
    std::shared_ptr<worklets::WorkletsModuleProxy> proxy_;
};

} // namespace react_native_linux
