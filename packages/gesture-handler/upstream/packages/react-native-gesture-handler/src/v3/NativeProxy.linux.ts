import { Worklets } from '../handlers/gestures/reanimatedWrapper';
import RNGestureHandlerModule from '../RNGestureHandlerModule';
import { NativeProxy as WebNativeProxy } from './NativeProxy.web';
import type { BaseGestureConfig } from './types';

// A SharedValue listener (hooks/utils/reanimatedUtils) calls updateGestureHandlerConfig on the UI runtime, where
// Android and iOS answer with a JSI host function. Linux's module is a JavaScript object on the React Native
// runtime, so the update goes back to it; everything else is the web proxy, called on that runtime.
const { updateGestureHandlerConfig } = RNGestureHandlerModule;
const scheduleOnRN = Worklets?.scheduleOnRN;

export const NativeProxy = {
  ...WebNativeProxy,
  updateGestureHandlerConfig: <
    TConfig,
    THandlerData,
    TExtendedHandlerData extends THandlerData,
  >(
    handlerTag: number,
    newConfig: BaseGestureConfig<TConfig, THandlerData, TExtendedHandlerData>
  ) => {
    'worklet';
    scheduleOnRN?.(updateGestureHandlerConfig, handlerTag, newConfig);
  },
} as const;
