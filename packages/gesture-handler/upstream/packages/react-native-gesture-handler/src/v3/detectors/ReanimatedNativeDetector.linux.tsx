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
export function ReanimatedNativeDetector({
  onGestureHandlerReanimatedEvent,
  ...props
}: RNGestureHandlerDetectorNativeComponentProps) {
  const { workletEventHandler } = onGestureHandlerReanimatedEvent as unknown as {
    workletEventHandler: { worklet: (event: object) => void };
  };
  const dispatch =
    (eventName: string) =>
    ({ nativeEvent }: { nativeEvent: object }) =>
      Worklets?.scheduleOnUI(workletEventHandler.worklet, {
        ...nativeEvent,
        eventName,
      });

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
