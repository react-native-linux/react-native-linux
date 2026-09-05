#pragma once

#include "RetainedScene.h"

#include <cstdint>
#include <folly/dynamic.h>
#include <mutex>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/uimanager/IMountingManager.h>
#include <string>
#include <string_view>
#include <vector>

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
 * One `dispatchCommand` call, as the frame consumer receives it: which node it names, which command it is, and
 * the arguments React passed.
 *
 * Commands are queued rather than executed where they arrive because they arrive on the JavaScript thread and
 * every one of them acts on a node the frame thread owns. Queueing them under the same mutex the mutations take
 * is what makes them ordered against mounting rather than racing it: a command queued after a transaction is
 * drained after that transaction was applied, which is upstream's `dispatchCommand` timing bug read as a
 * contract. See *Commit termination and mounting atomicity* in docs/cpp-toolchain.md.
 */
struct SceneCommand {
    facebook::react::Tag tag{};
    std::string name;
    folly::dynamic args;
};

/**
 * The line logged for a prop the synchronous fast path dropped, and the whole of what a user has to go on.
 *
 * It names the prop, the set the Linux native driver can animate — read from `kAnimatableProps`, so the message
 * and the code cannot disagree — and the way out. It is a pure function of its arguments so the assertion on the
 * exact text is a unit test rather than a log-capture rig. See *Native-driver allowlist* in docs/cpp-toolchain.md.
 */
std::string rejectedAnimatedPropMessage(std::string_view propName, AnimatedPropRejection rejection);

/**
 * What the mounting layer saw that the scene could not explain.
 *
 * A mutation that updates, removes or deletes a tag the scene does not hold, and a command aimed at one, are the
 * Linux face of upstream's "Unable to find viewState for tag N". The policy is loud but not fatal: count every
 * one, keep the first with enough context to name the commit that produced it, log that first one, and apply the
 * transaction anyway. The frame thread never throws and never aborts, because a dropped frame is worse than a
 * wrong one and a crash is worse than both.
 *
 * A synchronous animated prop the fast path does not apply is counted the same way and for the same reason: the
 * driver's allowlist is wider than what this renderer can paint without a commit, and a prop dropped in silence
 * is an animation that runs at the wrong values with nothing to point at. See *Sync props fast path* and
 * *Native-driver allowlist* in docs/cpp-toolchain.md.
 */
struct MountDiagnostics {
    uint64_t unknownTagOperations{0};
    std::string firstUnknownOperation;
    facebook::react::Tag firstUnknownTag{0};
    facebook::react::MountingTransaction::Number firstUnknownTransactionNumber{0};
    uint64_t rejectedAnimatedProps{0};
    std::string firstRejectedAnimatedProp;
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
 *
 * dispatchCommand also runs on the JS thread and takes the same mutex, which is what orders a command against the
 * transactions around it; the frame thread drains the queue with takeCommands right after takeFrame. See *Commit
 * termination and mounting atomicity* in docs/cpp-toolchain.md.
 *
 * synchronouslyUpdateViewOnUIThread runs on the frame thread, inside the animation tick that precedes takeFrame,
 * while the JS thread may be committing — so the mutex is the only thing keeping the scene consistent, exactly as
 * it is for resize. It is also the one write to the scene that no Fabric commit follows. See *Sync props fast
 * path* in docs/cpp-toolchain.md.
 */
class LinuxMountingManager final : public facebook::react::IMountingManager {
public:
    void startSurface(facebook::react::SurfaceId surfaceId, facebook::react::Size size);

    /**
     * Damages every node drawing `uri` and hands them its decoded pixels. Called from a decode thread when an
     * image finishes decoding, which is the one thing that changes the picture without a Fabric mutation behind
     * it.
     */
    void damageImageSource(const std::string& uri, const std::shared_ptr<const DecodedImageFrames>& decoded);

    /**
     * Where the scene reads decoded pixels from. Set once by the host with the image pipeline's cache, before any
     * surface starts; see `RetainedScene::setDecodedImageProvider`.
     */
    void setDecodedImageProvider(RetainedScene::DecodedImageProvider decodedImages);

    /**
     * `RetainedScene::advanceImageAnimations` under the scene mutex, flagging pending damage when a frame
     * changed so the frame clock draws it. Called once per frame from the frame thread.
     */
    bool advanceImageAnimations(double frameMilliseconds);

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

    /**
     * Hands over every command queued since the last call, in arrival order, and empties the queue. The frame
     * consumer calls it right after `takeFrame`, so a command is delivered against the scene the same frame
     * paints and never against an older one.
     */
    std::vector<SceneCommand> takeCommands();

    /**
     * What the mounting layer could not explain, for whoever is debugging a commit. Reading it never clears it:
     * the counter is cumulative for the life of the surface.
     */
    MountDiagnostics mountDiagnostics() const;

    /**
     * The node painted under `surfacePoint` in `surfaceId`'s tree, and the origin it was painted at. Called from
     * the frame thread by the input dispatcher, under the same mutex a synchronous animated update takes — so a
     * press is answered against the scene state one frame paints and never against half of two.
     */
    SceneHit findNodeAtPoint(facebook::react::SurfaceId surfaceId, facebook::react::Point surfacePoint) const;
    SceneSnapshot snapshotScene() const;
    std::string dumpScene() const;

    /**
     * Every mounted node, copied out under the scene mutex, for the automation channel's `DumpVisualTree`.
     * Called from the frame thread while the JavaScript thread may be committing, exactly as `snapshotScene` is,
     * so a dump describes one transaction's tree and never half of two. See *The automation channel* in
     * docs/cpp-toolchain.md.
     */
    SceneNodes visualTreeNodes() const;

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

    /**
     * The non-layout fast path: the props an animation frame changed, applied straight to the retained scene with
     * no Fabric commit and no Yoga relayout behind them. `AnimationBackend` calls it through
     * `UIManager::synchronouslyUpdateViewOnUIThread` for every frame whose mutations touch no layout, which at
     * 120 Hz is the difference between a matrix multiply and a relayout per frame.
     *
     * The props of `kAnimatableProps` are applied; every other key the driver's allowlist permits, and any value
     * that is not finite, is counted in the diagnostics and logged once. An unknown tag is counted like any other
     * missing tag and damages nothing.
     */
    void synchronouslyUpdateViewOnUIThread(facebook::react::Tag tag, const folly::dynamic& props) override;

private:
    bool verifyTagIsKnown(std::string_view operation, facebook::react::Tag tag);
    void reportRejectedAnimatedProp(const RejectedAnimatedProp& rejectedProp);

    mutable std::mutex sceneMutex_;
    RetainedScene scene_;
    std::vector<SceneCommand> commands_;
    MountDiagnostics diagnostics_;
    facebook::react::MountingTransaction::Number lastTransactionNumber_{0};
    bool hasPendingDamage_{false};
};

} // namespace react_native_linux
