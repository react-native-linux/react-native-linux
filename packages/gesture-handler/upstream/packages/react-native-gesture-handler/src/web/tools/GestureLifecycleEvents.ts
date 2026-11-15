import { isStylableElement } from '../utils';

export const GestureLifecycleEvent = {
  Began: 'gh:gestureBegan',
  Canceled: 'gh:gestureCanceled',
} as const;

export type GestureLifecycleEventName =
  (typeof GestureLifecycleEvent)[keyof typeof GestureLifecycleEvent];

export function dispatchGestureLifecycleEvent(
  view: HTMLElement | null | undefined,
  name: GestureLifecycleEventName
): void {
  if (isStylableElement(view)) {
    view.dispatchEvent(new CustomEvent(name));
  }
}
