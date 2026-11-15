import React from 'react';

import { Worklets } from '../../handlers/gestures/reanimatedWrapper';
import HostGestureDetector, {
  type RNGestureHandlerDetectorNativeComponentProps,
} from './HostGestureDetector';

// Linux recognizes on the React Native runtime, so no native view exists for Reanimated to register the worklet
// handler with. The detector schedules each event onto the UI runtime instead, flattened and named the way
// Reanimated's web jsListener hands a native view's event to the worklet: the handlers read handlerTag at the top
// level, and a composed handler routes by eventName. The flattening also copies the event, which the UI runtime
// reads later: the engine reuses an activation's state-change event for the update after it, clearing its oldState.
// useEvent hands the detector a handler object whose workletEventHandler.worklet is the worklet to run. It is passed
// on as it is, because the UI runtime runs a worklet by its identity.
function isWorklet(value: unknown): value is (event: object) => void {
  return typeof value === 'function';
}

function workletOf(handler: unknown): ((event: object) => void) | null {
  if (typeof handler !== 'object' || handler === null || !('workletEventHandler' in handler)) {
    return null;
  }
  const eventHandler: unknown = handler.workletEventHandler;
  if (typeof eventHandler !== 'object' || eventHandler === null || !('worklet' in eventHandler)) {
    return null;
  }
  return isWorklet(eventHandler.worklet) ? eventHandler.worklet : null;
}

export function ReanimatedNativeDetector({
  onGestureHandlerReanimatedEvent,
  ...props
}: RNGestureHandlerDetectorNativeComponentProps) {
  const worklet = workletOf(onGestureHandlerReanimatedEvent);
  const dispatch =
    (eventName: string) =>
    ({ nativeEvent }: { nativeEvent: object }) => {
      if (worklet) {
        Worklets?.scheduleOnUI(worklet, { ...nativeEvent, eventName });
      }
    };

  return (
    <HostGestureDetector
      {...props}
      onGestureHandlerReanimatedEvent={dispatch(
        'onGestureHandlerReanimatedEvent'
      )}
      onGestureHandlerReanimatedStateChange={dispatch(
        'onGestureHandlerReanimatedStateChange'
      )}
      onGestureHandlerReanimatedTouchEvent={dispatch(
        'onGestureHandlerReanimatedTouchEvent'
      )}
    />
  );
}
