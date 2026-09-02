#pragma once

#include "LinuxMountingManager.h"
#include "RetainedScene.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/scheduler/Scheduler.h>
#include <react/renderer/scheduler/SchedulerDelegateImpl.h>
#include <react/renderer/scheduler/SurfaceHandler.h>
#include <react/runtime/ReactInstance.h>
#include <react/utils/ContextContainer.h>

#include <memory>
#include <string>

namespace react_native_linux {

/**
 * Bootstraps Fabric on top of an already initialised bridgeless `ReactInstance`: one `Scheduler`, one
 * `SurfaceHandler` started as an empty surface, and the retained scene the resulting mounting transactions are
 * applied to. The surface is sized by the caller: headlessly that is a fixed size, and under a window it is the
 * window's size, republished through `setSurfaceSize` on every configure so Yoga relayouts against it.
 *
 * The surface is started with an empty module name, which takes `SurfaceHandler` through
 * `UIManager::startEmptySurface` instead of `AppRegistryBinding`. That is what makes a JavaScript bundle without
 * React Native's own JavaScript runtime able to drive the renderer.
 *
 * Threading contract: construction and destruction happen on the thread that owns the process run loop, before
 * the JavaScript bundle is loaded and after the JavaScript thread has been drained. Everything Fabric does in
 * between happens on the JavaScript thread. `setSurfaceSize`, `takeFrame` and `snapshotScene` are the members that
 * may be called while the surface is running: `constraintLayout` commits on the calling thread and, because the
 * default commit options mount synchronously, mounts there as well, and the other two copy the scene — and, for
 * `takeFrame`, its accumulated damage — out under the mounting manager's mutex.
 *
 * Shutdown contract: stopSurface commits an empty tree and queues the resulting unmount onto the JavaScript
 * thread. The owner drains that thread before destroying the host, because the queued rendering update holds a
 * raw pointer to the scheduler delegate and upstream only guards it behind a feature flag that is off by default.
 */
class FabricHost final {
public:
    FabricHost(facebook::react::ReactInstance& reactInstance, facebook::react::Size surfaceSize);
    FabricHost(const FabricHost&) = delete;
    FabricHost(FabricHost&&) = delete;
    FabricHost& operator=(const FabricHost&) = delete;
    FabricHost& operator=(FabricHost&&) = delete;
    ~FabricHost() noexcept;

    void setSurfaceSize(facebook::react::Size surfaceSize);
    void stopSurface();
    SceneFrame takeFrame();
    SceneSnapshot snapshotScene() const;
    std::string dumpScene() const;

private:
    std::shared_ptr<const facebook::react::ContextContainer> contextContainer_;
    std::shared_ptr<facebook::react::ComponentDescriptorProviderRegistry> componentDescriptorProviderRegistry_;
    std::shared_ptr<LinuxMountingManager> mountingManager_;
    std::unique_ptr<facebook::react::SchedulerDelegateImpl> schedulerDelegate_;
    std::unique_ptr<facebook::react::Scheduler> scheduler_;
    std::unique_ptr<facebook::react::SurfaceHandler> surfaceHandler_;
};

} // namespace react_native_linux
