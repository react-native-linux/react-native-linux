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
 * | `maybeFlushUIUpdatesQueueFunction` | a frame request; the module proxy host (#140) performs operations there |
 * | `registerSensor`, `unregisterSensor` | stub (#143) |
 * | `setGestureStateFunction` | stub (#143) |
 * | `subscribeForKeyboardEvents`, `unsubscribeFromKeyboardEvents` | stub (#143) |
 * | `attachPseudoSelector`, `detachPseudoSelector` | stub (#143) |
 * | `cssCanRouteProperty`, `cssApplyTransition` | empty (#141) |
 * | `cssRemoveTransition`, `cssGetPlatformValue` | empty (#141) |
 * | `platformAnimationFactory` | null (#141) |
 *
 * A stub throws a `std::runtime_error` that names its member, so reaching one is a JavaScript error saying what
 * Linux lacks rather than a `std::bad_function_call`. The CSS members stay empty because upstream null-checks
 * every one of them and then runs the animation on its own loop, which is the behaviour Linux wants.
 *
 * Threading contract: every member runs on worklets' UI thread, which is the frame thread (ADR-0003).
 */
reanimated::PlatformDepMethodsHolder
makePlatformDepMethodsHolder(AnimationFrameQueue& frames,
                             std::shared_ptr<facebook::react::IMountingManager> mountingManager);

} // namespace react_native_linux
