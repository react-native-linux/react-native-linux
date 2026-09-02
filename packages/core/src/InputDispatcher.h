#pragma once

#include "InputPipeline.h"

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/uimanager/UIManager.h>

#include <memory>
#include <vector>

namespace react_native_linux {

/**
 * Turns a frame's worth of platform input into Fabric events: hit-test, translate, enqueue.
 *
 * Hit testing is upstream's, not ours. `UIManager::findNodeAtPoint` walks the committed shadow tree from the root,
 * honours `pointerEvents`, transforms and overflow, and returns the deepest node that can be a touch target — the
 * same function `NativeDOM` uses for `elementFromPoint`. The retained scene also carries absolute frames and could
 * answer the same question, but it has no shadow nodes and therefore no event emitters behind them, and a second
 * hit-test implementation would be a second chance to disagree with React about what was clicked.
 *
 * Nothing here dispatches to JavaScript. `TouchEventEmitter` enqueues into Fabric's `EventQueue`, which asks its
 * `EventBeat` for a beat; the frame thread induces that beat once per frame and the queue flushes on the
 * JavaScript thread. That is where `PointerEventsProcessor` runs, and therefore where hover chains, `pointerEnter`
 * and `pointerLeave` between siblings, capture and bubbling are computed. See *Input* in docs/cpp-toolchain.md.
 *
 * Threading contract: `dispatch` runs on the platform frame thread. Everything it touches is documented as
 * callable from any thread — `ShadowTreeRegistry::visit` and `ShadowTree::getCurrentRevision` take a shared lock,
 * shadow nodes are immutable once committed, and `EventQueue::enqueueEvent` takes its own mutex.
 */
class InputDispatcher final {
public:
    InputDispatcher(std::shared_ptr<facebook::react::UIManager> uiManager, facebook::react::SurfaceId surfaceId);

    void dispatch(const std::vector<InputEvent>& events);

private:
    std::shared_ptr<const facebook::react::ShadowNode> rootShadowNode() const;
    std::shared_ptr<const facebook::react::ShadowNode> resolveTarget(const InputEvent& event) const;
    facebook::react::Point absoluteOrigin(const facebook::react::ShadowNode& shadowNode) const;
    void emitKeyEvent(const InputEvent& event) const;

    std::shared_ptr<facebook::react::UIManager> uiManager_;
    facebook::react::SurfaceId surfaceId_;
    PointerRouter router_;
    std::shared_ptr<const facebook::react::ShadowNode> keyboardTarget_;
};

} // namespace react_native_linux
