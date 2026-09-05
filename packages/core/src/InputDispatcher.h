#pragma once

#include "FocusModel.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "TextInputController.h"

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/uimanager/UIManager.h>

#include <memory>
#include <vector>

namespace react_native_linux {

/**
 * The node a pointer event is delivered to, and where inside its own local box the event lands.
 *
 * `offset` is already `pointerOffsetWithinTarget`'s answer — this platform's `locationX/Y` — rather than an
 * absolute origin a caller would have to invert a transform against later. The two travel together because one
 * hit test produces both: an origin looked up separately would come from the shadow tree's `LayoutMetrics`, which
 * is exactly the stale geometry the scene hit test exists to avoid, and it would carry no matrix to invert a
 * rotated or scaled target's offset against either.
 */
struct PointerTarget {
    std::shared_ptr<const facebook::react::ShadowNode> shadowNode;
    facebook::react::Point offset{};
};

/**
 * The transform `pointerOffsetWithinTarget` inverts for a scene hit, read straight off `SceneHit` rather than a
 * second scene lookup: `hitTestNode`'s own walk is the only place that has the matrix for the node it just
 * matched, under the same lock and the same tree revision the tag itself came from, whether or not that node
 * painted anything at all (issue #299). A free, header-defined function rather than a private method of
 * `InputDispatcher` because `AnimatedHitTestTest.cpp` needs the identical conversion to assert `SceneHit` carries
 * the matrix it should, and that test target does not link `InputDispatcher.cpp` — one function two callers
 * share, rather than a linker dependency the test binary does not otherwise need.
 */
inline PointerTargetTransform transformOfHit(const SceneHit& hit) {
    return PointerTargetTransform{.scaleX = hit.matrix.scaleX,
                                  .skewX = hit.matrix.skewX,
                                  .translateX = hit.matrix.translateX,
                                  .skewY = hit.matrix.skewY,
                                  .scaleY = hit.matrix.scaleY,
                                  .translateY = hit.matrix.translateY,
                                  .frameOrigin = hit.frameOrigin};
}

/**
 * Turns a frame's worth of platform input into Fabric events: hit-test, translate, enqueue — and owns the one
 * focused node the keyboard talks to.
 *
 * Hit testing reads the retained scene, not the shadow tree. `RetainedScene::findNodeAtPoint` walks the same nodes
 * the painter walks, with the same composed matrices and the same `overflow: hidden` clips, and honours
 * `pointerEvents` by the same rule `ConcreteViewShadowNode::canBeTouchTarget` applies. It has to: a native-driven
 * animation writes the scene every frame and commits nothing, so `UIManager::findNodeAtPoint` would answer with
 * where a moving node started rather than where it is — upstream's open react-native#51621, which is a bug there
 * because the shadow tree is their only geometry and would be a bug here for the same reason. See *Hit-testing
 * under animation* in docs/cpp-toolchain.md. The scene answers with a tag; the shadow tree is then searched for
 * the node that tag names, because the event emitter lives there and only there. `UIManager::findNodeAtPoint`
 * remains the fallback for a point whose painted node has no shadow node in this surface's committed tree.
 *
 * Focus is read from the same tree, for the same reason: the focusable set is the pre-order walk of the committed
 * shadow tree filtered by props, and that walk is also where the event emitters that `onFocus` and `onBlur` go
 * through live. The retained scene has the tree and the mount order but no emitters, so reading focus off it
 * would need a second lookup to emit anything. The walk is not per frame: the committed root is one object per
 * commit, so an unchanged root pointer means an unchanged focusable set. `FocusModel` owns everything that can be
 * arithmetically wrong about the result; this class only feeds it tags and turns its answers into events, a scene
 * mark and a text-input enable.
 *
 * Keys go to the focused node and to nothing else. A key pressed with nothing focused is dropped here rather than
 * sent to the surface root, because a root with no instance handle cannot be an event target — see the deferral
 * in *Input* — and a key nobody consumed has nowhere else to go on Wayland, where the compositor already decided
 * this client should receive it.
 *
 * Composition is the one input that is not routed at all. `zwp_text_input_v3` follows the compositor's keyboard
 * focus, so the target of a pre-edit or a commit is whatever holds the text cursor: the `TextInputController`
 * this class owns is the `ImeSink`, and it drops a composition that arrives with no field focused. The
 * `TextInputFocusSink` is enabled and disabled as focus enters and leaves a text component, and is handed to the
 * controller so the caret rectangle reaches the input method. See *IME*, *Focus and keyboard* and *TextInput* in
 * docs/cpp-toolchain.md.
 *
 * A key the focused field consumes stops here: it reaches React as a `keyDown` and then goes no further, so
 * Space types a space instead of activating the field and Enter submits instead of clicking it. Tab is never
 * consumed, which is what keeps it moving focus out. While a composition is active no key is dispatched at all,
 * because text arrives as a commit and a field that also inserted the keys would double every composed
 * character.
 *
 * Threading contract: `dispatch` runs on the platform frame thread. Everything it touches is documented as
 * callable from any thread — `ShadowTreeRegistry::visit` and `ShadowTree::getCurrentRevision` take a shared lock,
 * shadow nodes are immutable once committed, `EventQueue::enqueueEvent` takes its own mutex, and the mounting
 * manager's focus mark is written under the same mutex that guards the scene. The scene hit test takes that mutex
 * too, which is the mutex an animation frame's synchronous prop update already writes under, so a press is
 * answered against one frame's scene state rather than against half of two.
 */
class InputDispatcher final {
public:
    InputDispatcher(std::shared_ptr<facebook::react::UIManager> uiManager,
                    std::shared_ptr<LinuxMountingManager> mountingManager, facebook::react::SurfaceId surfaceId);

    void dispatch(const std::vector<InputEvent>& events);

    /**
     * Applies the frame's `dispatchCommand` queue. The only command this reads is `focus`, a `<View>` ref's
     * `focus({ preventScroll, focusVisible })` — `preventScroll` true skips the scroll-into-view every other focus
     * change gets, and `focusVisible`, when named explicitly, decides the ring instead of the ordinary
     * keyboard-visible default a bare `focus()` gets; every other command, and one naming a tag that is not
     * focusable, is ignored, so the caller may hand over the whole queue exactly as
     * `ScrollController::dispatchCommands` does with the same one.
     */
    void dispatchCommands(const std::vector<SceneCommand>& commands);

    void setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) noexcept;

    /**
     * Advances the focused field's caret blink by one frame. Called from the frame loop rather than from a
     * timer, because there is no timer in the frame path.
     */
    bool advanceCaretBlink(double frameMilliseconds);

private:
    std::shared_ptr<const facebook::react::ShadowNode> rootShadowNode() const;
    PointerTarget resolveTarget(const InputEvent& event) const;
    facebook::react::Point absoluteOrigin(const facebook::react::ShadowNode& shadowNode) const;

    void dispatchPointerEvent(const InputEvent& event);
    void dispatchKeyEvent(const InputEvent& event);
    void emitKeyEvent(const InputEvent& event) const;
    void emitActivation(const InputEvent& event) const;
    void emitFocusEvent(const facebook::react::ShadowNode& shadowNode, bool isFocused) const;

    void syncFocusables();
    void collectFocusables(const facebook::react::ShadowNode& shadowNode);
    void applyFocusTransition(const FocusTransition& transition, bool preventScroll = false);
    void updateTextInput();
    void scrollFocusedNodeIntoView() const;
    std::shared_ptr<const facebook::react::ShadowNode> focusableNode(facebook::react::Tag tag) const;
    facebook::react::Tag focusableAncestorTag(const facebook::react::ShadowNode& shadowNode) const;

    std::shared_ptr<facebook::react::UIManager> uiManager_;
    std::shared_ptr<LinuxMountingManager> mountingManager_;
    facebook::react::SurfaceId surfaceId_;
    PointerRouter router_;
    FocusModel focusModel_;
    std::vector<std::shared_ptr<const facebook::react::ShadowNode>> focusableNodes_;
    std::vector<std::shared_ptr<const TextInputShadowNode>> textInputNodes_;
    std::shared_ptr<const facebook::react::ShadowNode> focusedNode_;
    std::shared_ptr<const facebook::react::ShadowNode> syncedRoot_;
    TextInputController textInputController_;
    bool isTextInputEnabled_{false};
    TextInputFocusSink* textInputFocusSink_{nullptr};
};

} // namespace react_native_linux
