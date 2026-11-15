#pragma once

#include "AnimationFrameQueue.h"

#include <memory>
#include <reanimated/Tools/PlatformDepMethodsHolder.h>

#include <react/renderer/uimanager/IMountingManager.h>

namespace react_native_linux {

/**
 * Reanimated's platform contract on Linux (#139), the counterpart of Apple's `PlatformDepMethodsHolderImpl.mm`.
 *
 * | Member | On Linux |
 * | --- | --- |
 * | `requestRender` | `frames`, dispatched once per frame on worklets' UI thread with the frame's timestamp |
 * | `getAnimationTimestamp` | `std::chrono::steady_clock`, the clock frame timestamps are read from |
 * | `synchronouslyUpdateUIPropsFunction` | `IMountingManager::synchronouslyUpdateViewOnUIThread` |
 * | `maybeFlushUIUpdatesQueueFunction` | a frame request; unused on the animation backend path (#140) |
 * | `registerSensor`, `subscribeForKeyboardEvents` | unavailable: `-1`, as upstream's `DummyReanimatedModuleProxy` |
 * | `unregisterSensor`, `unsubscribeFromKeyboardEvents` | nothing to undo |
 * | `attachPseudoSelector`, `detachPseudoSelector` | nothing until pointer state reaches them (#143) |
 * | `setGestureStateFunction` | throws (#143) |
 * | `cssCanRouteProperty`, `cssApplyTransition` | empty (#141) |
 * | `cssRemoveTransition`, `cssGetPlatformValue` | empty (#141) |
 * | `platformAnimationFactory` | null (#141) |
 *
 * `-1` makes `useAnimatedSensor` report `isAvailable = false` and keyboard tracking stay at its initial state,
 * the desktop answer. Pseudo-selectors must not throw: upstream's `PseudoStylesRegistry` records the tag before
 * attaching, so a throw would leave it half-registered; until #143, `:hover` styles stay as if never hovered.
 * `setGestureState` is reached only by an explicit gesture-handler manual activation, so it throws a
 * `std::runtime_error` naming itself rather than pretending the gesture changed. The CSS members stay empty
 * because upstream null-checks every one of them and then runs the animation on its own loop.
 *
 * Threading contract: `requestRender` (the frame queue locks) and `getAnimationTimestamp` (`steady_clock`) may be
 * called from any thread: the JavaScript thread reads the timestamp and the mount hook requests renders from the
 * mounting thread. Every other member runs on worklets' UI thread, which is the frame thread (ADR-0003).
 */
reanimated::PlatformDepMethodsHolder
makePlatformDepMethodsHolder(AnimationFrameQueue& frames,
                             std::shared_ptr<facebook::react::IMountingManager> mountingManager);

} // namespace react_native_linux
