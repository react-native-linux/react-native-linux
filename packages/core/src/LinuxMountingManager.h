#pragma once

#include "RetainedScene.h"

#include <folly/dynamic.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/uimanager/IMountingManager.h>

#include <mutex>
#include <string>

namespace react_native_linux {

/**
 * One frame's worth of scene state: what to paint, and the region of the surface that stopped being correct since
 * the last frame was taken.
 */
struct SceneFrame {
    SceneSnapshot scene;
    SceneDamage damage;
};

/**
 * Applies Fabric mounting transactions to the retained scene.
 *
 * This is the interface `ReactCxxPlatform`'s `SchedulerDelegateImpl` drives, so the Fabric plumbing between the
 * `Scheduler` and this class is upstream code rather than ours.
 *
 * Threading contract: executeMount runs on the JS thread, inside the rendering update the `RuntimeScheduler`
 * drains at the end of an event loop tick. takeFrame runs on the platform frame thread, once per frame, and
 * snapshotScene and dumpScene on the thread that owns the process run loop. The mutex is what makes those pairs
 * safe, and copying the scene out under it is why the frame thread never reads a half-applied transaction.
 *
 * takeFrame exists because the pair has to be atomic: a transaction landing between a snapshot and a damage take
 * would leave the frame thread with damage it cannot satisfy from the scene it holds, and the region would be
 * repainted from stale state and never repainted again. Taking both under one lock is the whole guarantee.
 */
class LinuxMountingManager final : public facebook::react::IMountingManager {
public:
    void startSurface(facebook::react::SurfaceId surfaceId, facebook::react::Size size);
    SceneFrame takeFrame();
    SceneSnapshot snapshotScene() const;
    std::string dumpScene() const;

    void executeMount(facebook::react::SurfaceId surfaceId,
                      facebook::react::MountingTransaction&& mountingTransaction) override;
    void dispatchCommand(const facebook::react::ShadowView& shadowView, const std::string& commandName,
                         const folly::dynamic& args) override;

private:
    mutable std::mutex sceneMutex_;
    RetainedScene scene_;
};

} // namespace react_native_linux
