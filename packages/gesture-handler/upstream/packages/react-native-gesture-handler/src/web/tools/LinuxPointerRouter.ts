// Linux has no DOM, so a view cannot listen for pointer events itself. GestureHandlerRootView.linux captures every
// pointer event under it and this router hands each one to the views that would have received it on web: a down
// to every attached view containing its target, and every later event for that pointer to the views that took
// the down, which is what setPointerCapture gives a web view.

type PointerListener = (event: LinuxPointerEvent) => void;

export interface LinuxElement {
  contains(other: LinuxElement): boolean;
  getBoundingClientRect(): {
    left: number;
    top: number;
    right: number;
    bottom: number;
  };
}

export interface LinuxPointerEvent {
  clientX: number;
  clientY: number;
  x: number;
  y: number;
  pointerId: number;
  pointerType: string;
  button: number;
  timeStamp: number;
  tiltX: number;
  tiltY: number;
  target: {
    tagName: string;
    setPointerCapture(pointerId: number): void;
    releasePointerCapture(pointerId: number): void;
    hasPointerCapture(pointerId: number): boolean;
  };
}

export class LinuxView {
  private readonly listeners = new Map<string, Set<PointerListener>>();

  constructor(public readonly element: LinuxElement) {}

  addEventListener(type: string, listener: PointerListener): void {
    if (!this.listeners.has(type)) {
      this.listeners.set(type, new Set());
    }
    this.listeners.get(type)!.add(listener);
    views.add(this);
  }

  removeEventListener(type: string, listener: PointerListener): void {
    this.listeners.get(type)?.delete(listener);
  }

  getBoundingClientRect() {
    return this.element.getBoundingClientRect();
  }

  dispatch(type: string, event: LinuxPointerEvent): void {
    this.listeners.get(type)?.forEach((listener) => listener(event));
  }
}

const views = new Set<LinuxView>();
const captures = new Map<number, LinuxView[]>();
const capturedTarget = {
  tagName: 'VIEW',
  setPointerCapture() {},
  releasePointerCapture() {},
  hasPointerCapture: () => true,
};

export function forgetLinuxView(view: LinuxView): void {
  views.delete(view);
}

export interface RootPointerEvent {
  target: unknown;
  nativeEvent: {
    clientX: number;
    clientY: number;
    pointerId: number;
    pointerType?: string;
    button?: number;
    tiltX?: number;
    tiltY?: number;
  };
  timeStamp: number;
}

export function routeLinuxPointerEvent(
  type: 'pointerdown' | 'pointermove' | 'pointerup' | 'pointercancel',
  rootEvent: RootPointerEvent
): void {
  const { nativeEvent } = rootEvent;
  const event: LinuxPointerEvent = {
    clientX: nativeEvent.clientX,
    clientY: nativeEvent.clientY,
    x: nativeEvent.clientX,
    y: nativeEvent.clientY,
    pointerId: nativeEvent.pointerId,
    pointerType: nativeEvent.pointerType ?? 'mouse',
    button: nativeEvent.button ?? 0,
    timeStamp: rootEvent.timeStamp,
    tiltX: nativeEvent.tiltX ?? 0,
    tiltY: nativeEvent.tiltY ?? 0,
    target: capturedTarget,
  };

  if (type === 'pointerdown') {
    const target = rootEvent.target as LinuxElement;
    const receivers = [...views].filter((view) =>
      view.element.contains(target)
    );
    captures.set(event.pointerId, receivers);
    receivers.forEach((view) => view.dispatch(type, event));
    return;
  }

  const receivers = captures.get(event.pointerId) ?? [];
  if (type !== 'pointermove') {
    captures.delete(event.pointerId);
  }
  receivers.forEach((view) => view.dispatch(type, event));
}
