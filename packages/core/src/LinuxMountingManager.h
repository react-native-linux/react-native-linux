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
 * snapshotScene and dumpScene on the thread that owns the process run loop. damageImageSource runs on the image
 * decode thread. The mutex is what makes those pairs safe, and copying the scene out under it is why the frame
 * thread never reads a half-applied transaction.
 *
 * takeFrame exists because the pair has to be atomic: a transaction landing between a snapshot and a damage take
 * would leave the frame thread with damage it cannot satisfy from the scene it holds, and the region would be
 * repainted from stale state and never repainted again. Taking both under one lock is the whole guarantee.
 */
class LinuxMountingManager final : public facebook::react::IMountingManager {
public:
    void startSurface(facebook::react::SurfaceId surfaceId, facebook::react::Size size);

    /**
     * Damages every node drawing `uri`. Called from a decode thread when an image finishes decoding, which is the
     * one thing that changes the picture without a Fabric mutation behind it.
     */
    void damageImageSource(const std::string& uri);

    /**
     * Marks the focused node and whether it draws the focus ring. Called from the frame thread by the input
     * dispatcher, which is the other thing besides a mutation that changes what the next frame paints.
     */
    void setFocus(facebook::react::Tag tag, bool isFocusVisible);

    /**
     * Publishes a `<TextInput>`'s caret, selection and composing run. Called from the frame thread by the text
     * input controller, and the third thing besides a mutation and a focus change that decides what the next
     * frame paints.
     */
    void setEditorState(facebook::react::Tag tag, const SceneEditorState& editorState);
    SceneFrame takeFrame();
    SceneSnapshot snapshotScene() const;
    std::string dumpScene() const;

    /**
     * Whether the scene has changed since the last `takeFrame`, for the frame clock's fallback-timeout decision
     * (see *Frame clock* in docs/cpp-toolchain.md): a caller pacing redraw off a withheld `wl_surface.frame` needs
     * to know there is a mounted change to paint before it spends a fallback tick drawing one. A mutation batch,
     * an image decode, a focus change and an editor-state publish all set it; only `takeFrame` clears it, so it
     * stays true across calls that do not consume the frame.
     */
    bool hasPendingDamage() const;

    void executeMount(facebook::react::SurfaceId surfaceId,
                      facebook::react::MountingTransaction&& mountingTransaction) override;
    void dispatchCommand(const facebook::react::ShadowView& shadowView, const std::string& commandName,
                         const folly::dynamic& args) override;

private:
    mutable std::mutex sceneMutex_;
    RetainedScene scene_;
    bool hasPendingDamage_{false};
};

} // namespace react_native_linux
