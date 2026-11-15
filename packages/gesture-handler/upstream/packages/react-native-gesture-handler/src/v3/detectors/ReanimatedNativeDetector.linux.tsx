import React from 'react';

import { Worklets } from '../../handlers/gestures/reanimatedWrapper';
import HostGestureDetector, {
  type RNGestureHandlerDetectorNativeComponentProps,
} from './HostGestureDetector';

// Linux recognizes on the React Native runtime, so no native view exists for Reanimated to register the worklet
// handler with. The detector schedules each event onto the UI runtime instead, where a native view's event would
// have reached it. The UI runtime reads the event later, and the engine reuses an activation's state-change event
// for the update that follows it, clearing its oldState, so each event is copied when it is sent.
export function ReanimatedNativeDetector({
  onGestureHandlerReanimatedEvent,
  ...props
}: RNGestureHandlerDetectorNativeComponentProps) {
  const { workletEventHandler } = onGestureHandlerReanimatedEvent as unknown as {
    workletEventHandler: { worklet: (event: object) => void };
  };
  const dispatch = ({ nativeEvent }: { nativeEvent: object }) =>
    Worklets?.scheduleOnUI(workletEventHandler.worklet, {
      nativeEvent: { ...nativeEvent },
    });

  return (
    <HostGestureDetector
      {...props}
      onGestureHandlerReanimatedEvent={dispatch}
      onGestureHandlerReanimatedStateChange={dispatch}
      onGestureHandlerReanimatedTouchEvent={dispatch}
    />
  );
}
