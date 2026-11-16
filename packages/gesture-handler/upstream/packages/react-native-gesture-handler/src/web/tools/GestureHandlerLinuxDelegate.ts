import type IGestureHandler from '../handlers/IGestureHandler';
import type EventManager from './EventManager';
import GestureHandlerOrchestrator from './GestureHandlerOrchestrator';
import type {
  GestureHandlerDelegate,
  MeasureResult,
} from './GestureHandlerDelegate';
import LinuxPointerEventManager from './LinuxPointerEventManager';
import {
  forgetLinuxView,
  type LinuxElement,
  LinuxView,
  setLinuxGestureActive,
} from './LinuxPointerRouter';

// The web delegate's job on a React Native element: the bounds come from the element's own
// getBoundingClientRect, and the pointer events from LinuxPointerRouter rather than from DOM listeners.
export class GestureHandlerLinuxDelegate
  implements GestureHandlerDelegate<LinuxView, IGestureHandler>
{
  public view: LinuxView | null = null;
  private gestureHandler!: IGestureHandler;
  private eventManagers: EventManager<unknown>[] = [];

  init(viewRef: number, handler: IGestureHandler): void {
    if (!viewRef) {
      throw new Error(
        `Cannot find the view for handler ${handler.handlerTag}`
      );
    }

    this.gestureHandler = handler;
    const element = viewRef as unknown as LinuxElement;
    this.view = new LinuxView(
      element,
      element === handler.hostDetectorView?.current
    );
    this.eventManagers = [
      new LinuxPointerEventManager(this.view as unknown as HTMLElement, false),
    ];
    this.eventManagers.forEach((manager) => handler.attachEventManager(manager));
  }

  detach(): void {
    // The router keeps dispatching a captured pointer to this view until it lifts, and the orchestrator never cleans
    // a handler that detach already returned to UNDETERMINED: without both, the old pointer reaches a handler with no
    // props, and the next attach starts with that pointer still tracked.
    this.eventManagers.forEach((manager) => manager.setEnabled(false));
    GestureHandlerOrchestrator.instance.dropHandler(this.gestureHandler);
    this.eventManagers = [];
    this.forgetView();
  }

  updateDOM(): void {}

  isPointerInBounds({ x, y }: { x: number; y: number }): boolean {
    if (!this.view) {
      return false;
    }

    const rect = this.view.getBoundingClientRect();

    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
  }

  measureView(): MeasureResult {
    const rect = this.requireView().getBoundingClientRect();

    return {
      pageX: rect.left,
      pageY: rect.top,
      width: rect.right - rect.left,
      height: rect.bottom - rect.top,
    };
  }

  absoluteToLocal(absoluteX: number, absoluteY: number) {
    const rect = this.requireView().getBoundingClientRect();

    return { x: absoluteX - rect.left, y: absoluteY - rect.top };
  }

  reset(): void {
    this.eventManagers.forEach((manager) => manager.resetManager());
  }

  onEnabledChange(): void {
    this.eventManagers.forEach((manager) =>
      manager.setEnabled(this.gestureHandler.enabled)
    );
  }

  onBegin(): void {}

  onActivate(): void {
    setLinuxGestureActive(this, true);
  }

  onEnd(): void {
    setLinuxGestureActive(this, false);
  }

  onCancel(): void {
    setLinuxGestureActive(this, false);
  }

  onFail(): void {
    setLinuxGestureActive(this, false);
  }

  destroy(): void {
    this.eventManagers.forEach((manager) => manager.unregisterListeners());
    this.forgetView();
  }

  private forgetView(): void {
    setLinuxGestureActive(this, false);
    if (this.view) {
      forgetLinuxView(this.view);
      this.view = null;
    }
  }

  private requireView(): LinuxView {
    if (!this.view) {
      throw new Error('react-native-gesture-handler: the view is detached');
    }

    return this.view;
  }
}
