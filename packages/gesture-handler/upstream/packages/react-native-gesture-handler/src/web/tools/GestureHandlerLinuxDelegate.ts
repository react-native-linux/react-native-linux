import type IGestureHandler from '../handlers/IGestureHandler';
import {
  GestureHandlerDelegate,
  MeasureResult,
} from './GestureHandlerDelegate';
import LinuxPointerEventManager from './LinuxPointerEventManager';
import {
  forgetLinuxView,
  LinuxElement,
  LinuxView,
  setLinuxGestureActive,
} from './LinuxPointerRouter';
import EventManager from './EventManager';

// The web delegate's job on a React Native element: the bounds come from the element's own
// getBoundingClientRect, and the pointer events from LinuxPointerRouter rather than from DOM listeners.
export class GestureHandlerLinuxDelegate
  implements GestureHandlerDelegate<LinuxView, IGestureHandler>
{
  public view!: LinuxView;
  private eventManagers: EventManager<unknown>[] = [];

  init(viewRef: number, handler: IGestureHandler): void {
    if (!viewRef) {
      throw new Error(
        `Cannot find the view for handler ${handler.handlerTag}`
      );
    }

    this.view = new LinuxView(viewRef as unknown as LinuxElement);
    this.eventManagers = [
      new LinuxPointerEventManager(this.view as unknown as HTMLElement),
    ];
    this.eventManagers.forEach((manager) => handler.attachEventManager(manager));
  }

  isPointerInBounds({ x, y }: { x: number; y: number }): boolean {
    const rect = this.view.getBoundingClientRect();

    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
  }

  measureView(): MeasureResult {
    const rect = this.view.getBoundingClientRect();

    return {
      pageX: rect.left,
      pageY: rect.top,
      width: rect.right - rect.left,
      height: rect.bottom - rect.top,
    };
  }

  reset(): void {
    this.eventManagers.forEach((manager) => manager.resetManager());
  }

  onEnabledChange(enabled: boolean): void {
    this.eventManagers.forEach((manager) =>
      enabled ? manager.registerListeners() : manager.unregisterListeners()
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
    setLinuxGestureActive(this, false);
    forgetLinuxView(this.view);
  }
}
