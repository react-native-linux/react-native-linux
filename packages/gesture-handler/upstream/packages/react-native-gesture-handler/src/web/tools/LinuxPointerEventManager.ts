import PointerEventManager from './PointerEventManager';
import { AdaptedEvent, EventTypes } from '../interfaces';
import { PointerTypeMapping } from '../utils';
import { PointerType } from '../../PointerType';
import { MouseButton } from '../../handlers/gestureHandlerCommon';
import type { LinuxPointerEvent, LinuxView } from './LinuxPointerRouter';

const mouseButtons = [
  MouseButton.LEFT,
  MouseButton.MIDDLE,
  MouseButton.RIGHT,
  MouseButton.BUTTON_4,
  MouseButton.BUTTON_5,
];

// The web manager unchanged, except that a React Native view has no computed style to read a CSS scale from:
// its bounding rectangle already carries every transform, so offsets come from it alone.
export default class LinuxPointerEventManager extends PointerEventManager {
  protected mapEvent(event: PointerEvent, eventType: EventTypes): AdaptedEvent {
    const linuxEvent = event as unknown as LinuxPointerEvent;
    const rect = (this.view as unknown as LinuxView).getBoundingClientRect();

    return {
      x: linuxEvent.clientX,
      y: linuxEvent.clientY,
      offsetX: linuxEvent.clientX - rect.left,
      offsetY: linuxEvent.clientY - rect.top,
      pointerId: linuxEvent.pointerId,
      eventType,
      pointerType:
        PointerTypeMapping.get(linuxEvent.pointerType) ?? PointerType.OTHER,
      button: mouseButtons[linuxEvent.button],
      time: linuxEvent.timeStamp,
    };
  }
}
