#pragma once

#include "FocusModel.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/uimanager/UIManager.h>

#include <memory>
#include <vector>

namespace react_native_linux {

/**
 * Turns a frame's worth of platform input into Fabric events: hit-test, translate, enqueue — and owns the one
 * focused node the keyboard talks to.
 *
 * Hit testing is upstream's, not ours. `UIManager::findNodeAtPoint` walks the committed shadow tree from the root,
 * honours `pointerEvents`, transforms and overflow, and returns the deepest node that can be a touch target — the
 * same function `NativeDOM` uses for `elementFromPoint`. The retained scene also carries absolute frames and could
 * answer the same question, but it has no shadow nodes and therefore no event emitters behind them, and a second
 * hit-test implementation would be a second chance to disagree with React about what was clicked.
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
 * focus, so the target of a pre-edit or a commit is whatever holds the text cursor. Those events go to the
 * registered `ImeSink` — a borrowed, platform-level focus owner that must outlive this dispatcher — and are
 * dropped when there is none, while the `TextInputFocusSink` is enabled and disabled as focus enters and leaves a
 * text component. See *IME* and *Focus and keyboard* in docs/cpp-toolchain.md.
 *
 * Threading contract: `dispatch` runs on the platform frame thread. Everything it touches is documented as
 * callable from any thread — `ShadowTreeRegistry::visit` and `ShadowTree::getCurrentRevision` take a shared lock,
 * shadow nodes are immutable once committed, `EventQueue::enqueueEvent` takes its own mutex, and the mounting
 * manager's focus mark is written under the same mutex that guards the scene.
 */
class InputDispatcher final {
public:
    InputDispatcher(std::shared_ptr<facebook::react::UIManager> uiManager,
                    std::shared_ptr<LinuxMountingManager> mountingManager, facebook::react::SurfaceId surfaceId);

    void dispatch(const std::vector<InputEvent>& events);
    void setImeSink(ImeSink* imeSink) noexcept;
    void setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) noexcept;

private:
    std::shared_ptr<const facebook::react::ShadowNode> rootShadowNode() const;
    std::shared_ptr<const facebook::react::ShadowNode> resolveTarget(const InputEvent& event) const;
    facebook::react::Point absoluteOrigin(const facebook::react::ShadowNode& shadowNode) const;
    void dispatchPointerEvent(const InputEvent& event);
    void dispatchKeyEvent(const InputEvent& event);
    void emitKeyEvent(const InputEvent& event) const;
    void emitActivation(const InputEvent& event) const;
    void emitFocusEvent(const facebook::react::ShadowNode& shadowNode, bool isFocused) const;

    void syncFocusables();
    void collectFocusables(const facebook::react::ShadowNode& shadowNode);
    void applyFocusTransition(const FocusTransition& transition);
    void updateTextInput();
    std::shared_ptr<const facebook::react::ShadowNode> focusableNode(facebook::react::Tag tag) const;
    facebook::react::Tag focusableAncestorTag(const facebook::react::ShadowNode& shadowNode) const;

    std::shared_ptr<facebook::react::UIManager> uiManager_;
    std::shared_ptr<LinuxMountingManager> mountingManager_;
    facebook::react::SurfaceId surfaceId_;
    PointerRouter router_;
    FocusModel focusModel_;
    std::vector<std::shared_ptr<const facebook::react::ShadowNode>> focusableNodes_;
    std::shared_ptr<const facebook::react::ShadowNode> focusedNode_;
    std::shared_ptr<const facebook::react::ShadowNode> syncedRoot_;
    bool isTextInputEnabled_{false};
    ImeSink* imeSink_{nullptr};
    TextInputFocusSink* textInputFocusSink_{nullptr};
};

} // namespace react_native_linux
