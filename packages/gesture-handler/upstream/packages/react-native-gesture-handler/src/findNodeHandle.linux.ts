// @ts-ignore React Native ships no types for its renderer proxy.
import { findHostInstance_DEPRECATED } from 'react-native/Libraries/ReactNative/RendererProxy';

// The React Native element itself, as findNodeHandle.web answers the DOM element: Linux's gesture delegate measures
// and hit-tests the element rather than a numeric tag.
export default function findNodeHandle(viewRef: any): any {
  if (viewRef?.viewTag !== undefined) {
    return findNodeHandle(viewRef.viewTag);
  }

  return viewRef ? findHostInstance_DEPRECATED(viewRef) : null;
}
