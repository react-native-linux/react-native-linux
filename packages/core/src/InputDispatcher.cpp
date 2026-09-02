#include "InputDispatcher.h"

#include <folly/dynamic.h>
#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/LayoutPrimitives.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/mounting/ShadowTree.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kKeyDownEventType[] = "keyDown";
constexpr char kKeyUpEventType[] = "keyUp";
constexpr facebook::react::Tag kNoTag = 0;

/**
 * The payload shape both react-native-macos and react-native-windows already agree on, plus `code`, which only
 * Windows carries and which a Wayland client gets for free from the evdev keycode. `repeat` is always false
 * because `wl_keyboard.repeat_info` is accepted and ignored — a held key produces one `keyDown` — and it is in
 * the payload rather than absent from it so that a component reading it never sees `undefined`. See
 * *Focus and keyboard* in docs/cpp-toolchain.md for the divergences.
 */
folly::dynamic makeKeyPayload(const InputEvent& event) {
    folly::dynamic payload = folly::dynamic::object("key", event.key)("code", event.code);

    payload["ctrlKey"] = event.modifiers.control;
    payload["shiftKey"] = event.modifiers.shift;
    payload["altKey"] = event.modifiers.alt;
    payload["metaKey"] = event.modifiers.meta;
    payload["repeat"] = false;

    return payload;
}

bool isKeyEvent(const InputEvent& event) {
    return event.kind == InputEventKind::KeyPress || event.kind == InputEventKind::KeyRelease;
}

bool isImeEvent(const InputEvent& event) {
    return event.kind == InputEventKind::ImePreedit || event.kind == InputEventKind::ImeCommit ||
           event.kind == InputEventKind::ImeDeleteSurrounding;
}

/**
 * Whether a node can hold focus.
 *
 * The `cxx` platform has no `focusable` prop — Android and tvOS add one on their own `HostPlatformViewProps` and
 * the shared `BaseViewProps` has none — so the signal is `accessible`, which is the prop `Pressable` already sets
 * on every control it renders and the one an out-of-tree platform can read without forking a vendored header.
 * `accessibilityState.disabled` removes it again, which is react-native-macos#1655, and a `display: none` node is
 * skipped because Yoga has already decided it is not on screen.
 */
bool isFocusableNode(const facebook::react::ShadowNode& shadowNode) {
    const facebook::react::LayoutableShadowNode* layoutable =
        dynamic_cast<const facebook::react::LayoutableShadowNode*>(&shadowNode);

    if (layoutable != nullptr &&
        layoutable->getLayoutMetrics().displayType == facebook::react::DisplayType::None) {
        return false;
    }

    const std::shared_ptr<const facebook::react::ViewProps> viewProps =
        std::dynamic_pointer_cast<const facebook::react::ViewProps>(shadowNode.getProps());

    if (viewProps == nullptr || !viewProps->accessible) {
        return false;
    }

    return !viewProps->accessibilityState.has_value() || !viewProps->accessibilityState.value().disabled;
}

void emitPointerDispatch(const facebook::react::TouchEventEmitter& emitter, const PointerDispatch& dispatch) {
    switch (dispatch.type) {
        case PointerDispatchType::Move:
            emitter.onPointerMove(dispatch.event);
            break;
        case PointerDispatchType::Down:
            emitter.onPointerDown(dispatch.event);
            break;
        case PointerDispatchType::Up:
            emitter.onPointerUp(dispatch.event);
            break;
        case PointerDispatchType::Leave:
            emitter.onPointerLeave(dispatch.event);
            break;
        case PointerDispatchType::Click:
            emitter.onClick(dispatch.event);
            break;
    }
}

} // namespace

InputDispatcher::InputDispatcher(std::shared_ptr<facebook::react::UIManager> uiManager,
                                 std::shared_ptr<LinuxMountingManager> mountingManager,
                                 facebook::react::SurfaceId surfaceId)
    : uiManager_(std::move(uiManager)), mountingManager_(std::move(mountingManager)), surfaceId_(surfaceId),
      textInputController_(uiManager_, mountingManager_) {}

void InputDispatcher::dispatch(const std::vector<InputEvent>& events) {
    // Before the events rather than with them: a commit that unmounted the focused node has to blur it even on a
    // frame the compositor sent nothing at all, and the frame loop calls this every frame for that reason.
    syncFocusables();

    for (const InputEvent& event : events) {
        if (isKeyEvent(event)) {
            dispatchKeyEvent(event);

            continue;
        }

        if (isImeEvent(event)) {
            deliverImeEvent(event, textInputController_);

            continue;
        }

        dispatchPointerEvent(event);
    }

    // After the frame's events rather than per event: one reconciliation, one state write and one set of change
    // events per frame, whatever the compositor sent inside it.
    textInputController_.synchronize();
}

void InputDispatcher::setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) noexcept {
    textInputFocusSink_ = textInputFocusSink;
    textInputController_.setTextInputFocusSink(textInputFocusSink);
}

bool InputDispatcher::advanceCaretBlink(double frameMilliseconds) {
    return textInputController_.advanceCaretBlink(frameMilliseconds);
}

std::shared_ptr<const facebook::react::ShadowNode> InputDispatcher::rootShadowNode() const {
    std::shared_ptr<const facebook::react::ShadowNode> root;

    uiManager_->getShadowTreeRegistry().visit(surfaceId_, [&root](const facebook::react::ShadowTree& shadowTree) {
        root = shadowTree.getCurrentRevision().rootShadowNode;
    });

    return root;
}

std::shared_ptr<const facebook::react::ShadowNode> InputDispatcher::resolveTarget(const InputEvent& event) const {
    const std::shared_ptr<const facebook::react::ShadowNode> root = rootShadowNode();

    if (root == nullptr || event.kind == InputEventKind::PointerLeave) {
        return root;
    }

    const std::shared_ptr<const facebook::react::ShadowNode> hit =
        uiManager_->findNodeAtPoint(root, event.surfacePoint);

    return hit == nullptr ? root : hit;
}

facebook::react::Point InputDispatcher::absoluteOrigin(const facebook::react::ShadowNode& shadowNode) const {
    return uiManager_->getRelativeLayoutMetrics(shadowNode, nullptr, {.includeTransform = true}).frame.origin;
}

void InputDispatcher::dispatchPointerEvent(const InputEvent& event) {
    const std::shared_ptr<const facebook::react::ShadowNode> target = resolveTarget(event);

    if (target == nullptr) {
        return;
    }

    // A press is what moves focus, not a release: that is when a desktop control takes it, and it is what makes a
    // click on the empty background blur the focused node — react-native-macos#999.
    if (event.kind == InputEventKind::PointerButtonPress) {
        applyFocusTransition(focusModel_.focusTag(focusableAncestorTag(*target), FocusOrigin::Pointer));
    }

    // After the focus transition, so a press that focuses a field also places its caret, and a drag that started
    // inside it keeps extending the selection wherever the pointer goes.
    textInputController_.handlePointer(event);

    const std::shared_ptr<const facebook::react::TouchEventEmitter> emitter =
        std::dynamic_pointer_cast<const facebook::react::TouchEventEmitter>(target->getEventEmitter());

    if (emitter == nullptr) {
        return;
    }

    for (const PointerDispatch& pointerDispatch : router_.route(event, target->getTag(), absoluteOrigin(*target))) {
        emitPointerDispatch(*emitter, pointerDispatch);
    }
}

void InputDispatcher::dispatchKeyEvent(const InputEvent& event) {
    // While an input method is composing, a key is neither text nor a command: the commit is the text, and a
    // field that also saw the key would insert the character twice. Nothing reaches React either, which is the
    // react-native-macos#683 and #2312 ordering rule from *IME* in docs/cpp-toolchain.md.
    if (textInputController_.isComposing()) {
        return;
    }

    // The key reaches React before the traversal it may also trigger, so a Tab is visible to the node that had
    // focus rather than swallowed by the platform. There is no return channel from JavaScript on this path, so
    // nothing can cancel the traversal; that is the `preventDefault` deferral in docs/cpp-toolchain.md.
    emitKeyEvent(event);

    const TextInputKeyResult editorResult = textInputController_.handleKey(event);

    if (editorResult == TextInputKeyResult::ConsumedAndBlurred) {
        applyFocusTransition(focusModel_.focusTag(kNoTag, FocusOrigin::Keyboard));

        return;
    }

    if (editorResult == TextInputKeyResult::Consumed) {
        return;
    }

    if (event.kind != InputEventKind::KeyPress) {
        return;
    }

    const std::optional<FocusDirection> direction = focusDirectionForKey(event.key, event.modifiers.shift);

    if (direction.has_value()) {
        applyFocusTransition(focusModel_.move(direction.value()));

        return;
    }

    if (isActivationKey(event.key) && focusedNode_ != nullptr) {
        emitActivation(event);
    }
}

void InputDispatcher::emitKeyEvent(const InputEvent& event) const {
    if (focusedNode_ == nullptr) {
        return;
    }

    const facebook::react::SharedEventEmitter& emitter = focusedNode_->getEventEmitter();

    if (emitter == nullptr) {
        return;
    }

    emitter->dispatchEvent(event.kind == InputEventKind::KeyPress ? kKeyDownEventType : kKeyUpEventType,
                           makeKeyPayload(event), facebook::react::RawEvent::Category::Discrete);
}

void InputDispatcher::emitActivation(const InputEvent& event) const {
    const std::shared_ptr<const facebook::react::TouchEventEmitter> emitter =
        std::dynamic_pointer_cast<const facebook::react::TouchEventEmitter>(focusedNode_->getEventEmitter());

    if (emitter == nullptr) {
        return;
    }

    emitPointerDispatch(*emitter, makeActivationDispatch(event, absoluteOrigin(*focusedNode_)));
}

void InputDispatcher::emitFocusEvent(const facebook::react::ShadowNode& shadowNode, bool isFocused) const {
    const std::shared_ptr<const facebook::react::ViewEventEmitter> emitter =
        std::dynamic_pointer_cast<const facebook::react::ViewEventEmitter>(shadowNode.getEventEmitter());

    if (emitter == nullptr) {
        return;
    }

    if (isFocused) {
        emitter->onFocus();

        return;
    }

    emitter->onBlur();
}

void InputDispatcher::syncFocusables() {
    const std::shared_ptr<const facebook::react::ShadowNode> root = rootShadowNode();

    if (root == syncedRoot_) {
        return;
    }

    syncedRoot_ = root;
    focusableNodes_.clear();
    textInputNodes_.clear();

    if (root != nullptr) {
        collectFocusables(*root);
    }

    // Before the focus transition below, so a commit that dropped the focused field finds the new field set
    // already in place, and so every mounted field has a buffer before anything can be typed into one.
    textInputController_.setMountedFields(textInputNodes_);

    std::vector<facebook::react::Tag> focusableTags;

    focusableTags.reserve(focusableNodes_.size());

    for (const std::shared_ptr<const facebook::react::ShadowNode>& focusable : focusableNodes_) {
        focusableTags.push_back(focusable->getTag());
    }

    const FocusTransition transition = focusModel_.setFocusableTags(std::move(focusableTags));

    if (transition.hasChanged) {
        applyFocusTransition(transition);

        return;
    }

    // The focused node survived the commit as a new clone. Holding the old one would measure the activation click
    // against a frame that is no longer mounted.
    if (focusModel_.focusedTag() != kNoTag) {
        focusedNode_ = focusableNode(focusModel_.focusedTag());
    }
}

void InputDispatcher::collectFocusables(const facebook::react::ShadowNode& shadowNode) {
    for (const std::shared_ptr<const facebook::react::ShadowNode>& child : shadowNode.getChildren()) {
        if (isFocusableNode(*child)) {
            focusableNodes_.push_back(child);
        }

        const std::shared_ptr<const TextInputShadowNode> textInput =
            std::dynamic_pointer_cast<const TextInputShadowNode>(child);

        if (textInput != nullptr) {
            textInputNodes_.push_back(textInput);
        }

        collectFocusables(*child);
    }
}

void InputDispatcher::applyFocusTransition(const FocusTransition& transition) {
    if (!transition.hasChanged) {
        return;
    }

    if (transition.blurredTag != kNoTag && focusedNode_ != nullptr) {
        emitFocusEvent(*focusedNode_, false);
    }

    if (transition.focusedTag != kNoTag || transition.blurredTag != kNoTag) {
        focusedNode_ = focusableNode(transition.focusedTag);
    }

    if (transition.focusedTag != kNoTag && focusedNode_ != nullptr) {
        emitFocusEvent(*focusedNode_, true);
    }

    mountingManager_->setFocus(focusModel_.focusedTag(), focusModel_.isFocusVisible());
    textInputController_.setFocusedNode(focusedNode_);
    updateTextInput();
}

void InputDispatcher::updateTextInput() {
    if (textInputFocusSink_ == nullptr) {
        return;
    }

    const bool wantsTextInput =
        focusedNode_ != nullptr && isTextInputComponent(focusedNode_->getComponentName());

    if (wantsTextInput == isTextInputEnabled_) {
        return;
    }

    isTextInputEnabled_ = wantsTextInput;

    if (wantsTextInput) {
        textInputFocusSink_->enable();

        return;
    }

    textInputFocusSink_->disable();
}

std::shared_ptr<const facebook::react::ShadowNode> InputDispatcher::focusableNode(facebook::react::Tag tag) const {
    for (const std::shared_ptr<const facebook::react::ShadowNode>& focusable : focusableNodes_) {
        if (focusable->getTag() == tag) {
            return focusable;
        }
    }

    return nullptr;
}

/**
 * The focusable node a click lands on: the deepest focusable on the path from the root down to the hit node,
 * which is the hit node itself when it is focusable and its nearest focusable ancestor otherwise — so clicking
 * the label inside a `<Pressable>` focuses the `<Pressable>`.
 *
 * `getAncestors` hands that path back as (parent, child index) pairs ordered from the root down, exactly as the
 * scroll router uses it, so walking it backwards visits the hit node first.
 */
facebook::react::Tag InputDispatcher::focusableAncestorTag(const facebook::react::ShadowNode& shadowNode) const {
    const std::shared_ptr<const facebook::react::ShadowNode> root = rootShadowNode();

    if (root == nullptr) {
        return kNoTag;
    }

    const facebook::react::ShadowNodeFamily::AncestorList ancestors = shadowNode.getFamily().getAncestors(*root);

    for (size_t depth = ancestors.size(); depth > 0; --depth) {
        const facebook::react::ShadowNode& parent = ancestors[depth - 1].first.get();
        const facebook::react::Tag tag =
            parent.getChildren()[static_cast<size_t>(ancestors[depth - 1].second)]->getTag();

        if (focusableNode(tag) != nullptr) {
            return tag;
        }
    }

    return kNoTag;
}

} // namespace react_native_linux
