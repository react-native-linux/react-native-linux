#include "InputDispatcher.h"

#include <folly/dynamic.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/LayoutPrimitives.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <react/renderer/mounting/ShadowView.h>

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
constexpr char kFocusCommandName[] = "focus";
constexpr char kScrollToCommandName[] = "scrollTo";
constexpr char kPreventScrollArgument[] = "preventScroll";
constexpr char kFocusVisibleArgument[] = "focusVisible";
constexpr facebook::react::Tag kNoTag = 0;

/**
 * Whether a `focus` command asked to skip the scroll-into-view every other focus change gets. Missing or
 * malformed arguments read as `false`, the same tolerance `ScrollController::routeCommand` applies to a
 * `scrollTo` command's own arguments: a command arrives from JavaScript, and the frame thread is not where a
 * third-party library's mistake should be fatal.
 */
bool readPreventScroll(const folly::dynamic& args) {
    if (!args.isObject()) {
        return false;
    }

    const folly::dynamic* preventScroll = args.get_ptr(kPreventScrollArgument);

    return preventScroll != nullptr && preventScroll->isBool() && preventScroll->asBool();
}

/**
 * The `focusVisible` a `focus` command named explicitly, or nothing when it did not — missing and malformed both
 * read as absent, the same tolerance `readPreventScroll` applies, so an absent value falls back to the ordinary
 * keyboard-visible default rather than to a guessed boolean.
 */
std::optional<bool> readFocusVisible(const folly::dynamic& args) {
    if (!args.isObject()) {
        return std::nullopt;
    }

    const folly::dynamic* focusVisible = args.get_ptr(kFocusVisibleArgument);

    if (focusVisible == nullptr || !focusVisible->isBool()) {
        return std::nullopt;
    }

    return focusVisible->asBool();
}

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

/**
 * The committed shadow node a scene tag names. Node identity is the Fabric tag on both sides, so this is the whole
 * translation from "what was painted here" to "what can receive an event"; a tag the scene holds and this tree
 * does not is a node from another surface or from a revision this thread has not seen, and the caller falls back
 * to the shadow-tree hit test for it.
 */
std::shared_ptr<const facebook::react::ShadowNode> shadowNodeWithTag(
    const std::shared_ptr<const facebook::react::ShadowNode>& shadowNode, facebook::react::Tag tag) {
    if (shadowNode->getTag() == tag) {
        return shadowNode;
    }

    for (const std::shared_ptr<const facebook::react::ShadowNode>& child : shadowNode->getChildren()) {
        const std::shared_ptr<const facebook::react::ShadowNode> found = shadowNodeWithTag(child, tag);

        if (found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

/**
 * The `accessibilityRole` a focused node declares, or the empty string a `Pressable` leaves when it gives none —
 * both of which `isActivationKey` treats as a button. Only `ViewProps` carries the prop, so a node whose props
 * are not a `ViewProps` — none of `isFocusableNode`'s candidates are anything else — activates on both keys too.
 */
std::string accessibilityRoleOf(const facebook::react::ShadowNode& shadowNode) {
    const std::shared_ptr<const facebook::react::ViewProps> viewProps =
        std::dynamic_pointer_cast<const facebook::react::ViewProps>(shadowNode.getProps());

    return viewProps == nullptr ? std::string{} : viewProps->accessibilityRole;
}

/**
 * The nearest ancestor of `shadowNode`, deepest first, whose child `predicate` accepts — the shape both
 * `InputDispatcher::focusableAncestorTag` and the scroll-into-view geometry below need, since a click's target
 * and a focused node's `<ScrollView>` are both "the nearest ancestor with some property" read off the same
 * `getAncestors` path, just with a different predicate over it.
 *
 * `getAncestors` hands the path back as (parent, child index) pairs ordered from the root down, so walking it
 * backwards visits the node closest to `shadowNode` first.
 */
template <typename Predicate>
std::shared_ptr<const facebook::react::ShadowNode> deepestAncestorMatching(
    const facebook::react::ShadowNode& shadowNode, const facebook::react::ShadowNode& rootNode,
    Predicate&& predicate) {
    const facebook::react::ShadowNodeFamily::AncestorList ancestors = shadowNode.getFamily().getAncestors(rootNode);

    for (size_t depth = ancestors.size(); depth > 0; --depth) {
        const facebook::react::ShadowNode& parent = ancestors[depth - 1].first.get();
        const std::shared_ptr<const facebook::react::ShadowNode>& child =
            parent.getChildren()[static_cast<size_t>(ancestors[depth - 1].second)];

        if (predicate(child)) {
            return child;
        }
    }

    return nullptr;
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
      textInputController_(uiManager_, mountingManager_, surfaceId) {}

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

        // A scroll ends a press the wheel moved out from under — see *A scroll cancels the press it started
        // under* — and, over a multiline `<TextInput>`, is the field's own rather than the ScrollController's;
        // see *Inner scrolling* in docs/cpp-toolchain.md.
        if (isScrollEvent(event)) {
            router_.cancelPressForScroll(event);
            textInputController_.handleScroll(event);

            continue;
        }

        dispatchPointerEvent(event);
    }

    // After the frame's events rather than per event: one reconciliation, one state write and one set of change
    // events per frame, whatever the compositor sent inside it.
    textInputController_.synchronize();
}

void InputDispatcher::dispatchCommands(const std::vector<SceneCommand>& commands) {
    for (const SceneCommand& command : commands) {
        if (command.name != kFocusCommandName) {
            continue;
        }

        const std::optional<bool> focusVisible = readFocusVisible(command.args);
        const FocusTransition transition = focusVisible.has_value()
                                               ? focusModel_.focusTagWithVisibility(command.tag, focusVisible.value())
                                               : focusModel_.focusTag(command.tag, FocusOrigin::Keyboard);

        applyFocusTransition(transition, readPreventScroll(command.args));
    }
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

PointerTarget InputDispatcher::resolveTarget(const InputEvent& event) const {
    const std::shared_ptr<const facebook::react::ShadowNode> root = rootShadowNode();

    if (root == nullptr) {
        return PointerTarget{};
    }

    if (event.kind != InputEventKind::PointerLeave) {
        const SceneHit paintedHit = mountingManager_->findNodeAtPoint(surfaceId_, event.surfacePoint);
        const std::shared_ptr<const facebook::react::ShadowNode> painted = shadowNodeWithTag(root, paintedHit.tag);

        if (painted != nullptr) {
            return PointerTarget{.shadowNode = painted, .origin = paintedHit.origin};
        }

        const std::shared_ptr<const facebook::react::ShadowNode> hit =
            uiManager_->findNodeAtPoint(root, event.surfacePoint);

        if (hit != nullptr) {
            return PointerTarget{.shadowNode = hit, .origin = absoluteOrigin(*hit)};
        }
    }

    return PointerTarget{.shadowNode = root, .origin = absoluteOrigin(*root)};
}

facebook::react::Point InputDispatcher::absoluteOrigin(const facebook::react::ShadowNode& shadowNode) const {
    return uiManager_->getRelativeLayoutMetrics(shadowNode, nullptr, {.includeTransform = true}).frame.origin;
}

void InputDispatcher::dispatchPointerEvent(const InputEvent& event) {
    const PointerTarget target = resolveTarget(event);

    if (target.shadowNode == nullptr) {
        return;
    }

    // A press is what moves focus, not a release: that is when a desktop control takes it, and it is what makes a
    // click on the empty background blur the focused node — react-native-macos#999.
    if (event.kind == InputEventKind::PointerButtonPress) {
        applyFocusTransition(
            focusModel_.focusTag(focusableAncestorTag(*target.shadowNode), FocusOrigin::Pointer));
    }

    // After the focus transition, so a press that focuses a field also places its caret, and a drag that started
    // inside it keeps extending the selection wherever the pointer goes.
    textInputController_.handlePointer(event);

    const std::shared_ptr<const facebook::react::TouchEventEmitter> emitter =
        std::dynamic_pointer_cast<const facebook::react::TouchEventEmitter>(target.shadowNode->getEventEmitter());

    if (emitter == nullptr) {
        return;
    }

    for (const PointerDispatch& pointerDispatch :
         router_.route(event, target.shadowNode->getTag(), target.origin)) {
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

    if (focusedNode_ != nullptr && isActivationKey(accessibilityRoleOf(*focusedNode_), event.key)) {
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

void InputDispatcher::applyFocusTransition(const FocusTransition& transition, bool preventScroll) {
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

    if (transition.focusedTag != kNoTag && !preventScroll) {
        scrollFocusedNodeIntoView();
    }
}

/**
 * Reveals the focused node inside its innermost `<ScrollView>`, if it is inside one and is not already visible.
 *
 * `computeScrollIntoViewOffset` is the whole of the arithmetic and is a pure function `FocusModel` carries so the
 * coverage gate can see every branch of it; this is the geometry it needs, read fresh rather than cached because
 * a commit may have resized either the target or the viewport since the last one. The command this dispatches is
 * the ordinary `scrollTo` `ScrollController::routeCommand` already applies — issue #248 does not touch scroll
 * physics, it only ever asks for the offset that already exists to move to.
 */
void InputDispatcher::scrollFocusedNodeIntoView() const {
    if (focusedNode_ == nullptr) {
        return;
    }

    const std::shared_ptr<const facebook::react::ShadowNode> root = rootShadowNode();

    if (root == nullptr) {
        return;
    }

    const std::shared_ptr<const facebook::react::ShadowNode> scrollViewAncestor = deepestAncestorMatching(
        *focusedNode_, *root,
        [](const std::shared_ptr<const facebook::react::ShadowNode>& child) {
            return std::dynamic_pointer_cast<const facebook::react::ScrollViewShadowNode>(child) != nullptr;
        });

    if (scrollViewAncestor == nullptr) {
        return;
    }

    const std::shared_ptr<const facebook::react::ScrollViewShadowNode> scrollView =
        std::static_pointer_cast<const facebook::react::ScrollViewShadowNode>(scrollViewAncestor);

    const facebook::react::Size viewportSize = scrollView->getLayoutMetrics().frame.size;
    const facebook::react::Point contentOffset = scrollView->getStateData().contentOffset;
    const facebook::react::Rect targetFrame =
        uiManager_->getRelativeLayoutMetrics(*focusedNode_, scrollView.get(), {.includeTransform = false}).frame;

    const std::optional<double> nextX =
        computeScrollIntoViewOffset(contentOffset.x, viewportSize.width, targetFrame.origin.x, targetFrame.size.width);
    const std::optional<double> nextY = computeScrollIntoViewOffset(
        contentOffset.y, viewportSize.height, targetFrame.origin.y, targetFrame.size.height);

    if (!nextX.has_value() && !nextY.has_value()) {
        return;
    }

    const folly::dynamic scrollToArguments =
        folly::dynamic::array(nextX.value_or(contentOffset.x), nextY.value_or(contentOffset.y), false);

    mountingManager_->dispatchCommand(facebook::react::ShadowView(*scrollView), kScrollToCommandName,
                                      scrollToArguments);
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
 * `deepestAncestorMatching` is the walk, shared with the scroll-into-view geometry's own nearest-`<ScrollView>`
 * search; this call's predicate is "is a focusable tag".
 */
facebook::react::Tag InputDispatcher::focusableAncestorTag(const facebook::react::ShadowNode& shadowNode) const {
    const std::shared_ptr<const facebook::react::ShadowNode> root = rootShadowNode();

    if (root == nullptr) {
        return kNoTag;
    }

    const std::shared_ptr<const facebook::react::ShadowNode> ancestor = deepestAncestorMatching(
        shadowNode, *root,
        [this](const std::shared_ptr<const facebook::react::ShadowNode>& child) {
            return focusableNode(child->getTag()) != nullptr;
        });

    return ancestor == nullptr ? kNoTag : ancestor->getTag();
}

} // namespace react_native_linux
