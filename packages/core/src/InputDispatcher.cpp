#include "InputDispatcher.h"

#include <folly/dynamic.h>
#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/mounting/ShadowTree.h>

#include <memory>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kKeyDownEventType[] = "keyDown";
constexpr char kKeyUpEventType[] = "keyUp";

folly::dynamic makeKeyPayload(const InputEvent& event) {
    return folly::dynamic::object("key", event.key)("ctrlKey", event.modifiers.control)(
        "shiftKey", event.modifiers.shift)("altKey", event.modifiers.alt)("metaKey", event.modifiers.meta);
}

bool isKeyEvent(const InputEvent& event) {
    return event.kind == InputEventKind::KeyPress || event.kind == InputEventKind::KeyRelease;
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
                                 facebook::react::SurfaceId surfaceId)
    : uiManager_(std::move(uiManager)), surfaceId_(surfaceId) {}

void InputDispatcher::dispatch(const std::vector<InputEvent>& events) {
    for (const InputEvent& event : events) {
        if (isKeyEvent(event)) {
            emitKeyEvent(event);

            continue;
        }

        const std::shared_ptr<const facebook::react::ShadowNode> target = resolveTarget(event);

        if (target == nullptr) {
            continue;
        }

        keyboardTarget_ = event.kind == InputEventKind::PointerLeave ? nullptr : target;

        const std::shared_ptr<const facebook::react::TouchEventEmitter> emitter =
            std::dynamic_pointer_cast<const facebook::react::TouchEventEmitter>(target->getEventEmitter());

        if (emitter == nullptr) {
            continue;
        }

        for (const PointerDispatch& pointerDispatch :
             router_.route(event, target->getTag(), absoluteOrigin(*target))) {
            emitPointerDispatch(*emitter, pointerDispatch);
        }
    }
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

void InputDispatcher::emitKeyEvent(const InputEvent& event) const {
    if (keyboardTarget_ == nullptr) {
        return;
    }

    const facebook::react::SharedEventEmitter& emitter = keyboardTarget_->getEventEmitter();

    if (emitter == nullptr) {
        return;
    }

    emitter->dispatchEvent(event.kind == InputEventKind::KeyPress ? kKeyDownEventType : kKeyUpEventType,
                           makeKeyPayload(event), facebook::react::RawEvent::Category::Discrete);
}

} // namespace react_native_linux
