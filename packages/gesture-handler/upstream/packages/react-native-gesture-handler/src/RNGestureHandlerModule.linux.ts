import type { ActionType } from './ActionType';
import { Gestures } from './web/Gestures';
import type { Config } from './web/interfaces';
import InteractionManager from './web/tools/InteractionManager';
import NodeManager from './web/tools/NodeManager';
import { GestureHandlerLinuxDelegate } from './web/tools/GestureHandlerLinuxDelegate';

// Linux runs the library's own JavaScript engine (src/web) with GestureHandlerLinuxDelegate in place of the DOM
// delegate: recognition happens on the JavaScript thread, fed by GestureHandlerRootView.linux.
export default {
  handleSetJSResponder(_tag: number, _blockNativeResponder: boolean) {},
  handleClearJSResponder() {},
  createGestureHandler<T>(
    handlerName: keyof typeof Gestures,
    handlerTag: number,
    config: T
  ) {
    if (!(handlerName in Gestures)) {
      throw new Error(
        `react-native-gesture-handler: ${handlerName} is not supported on linux.`
      );
    }

    const GestureClass = Gestures[handlerName];
    NodeManager.createGestureHandler(
      handlerTag,
      new GestureClass(new GestureHandlerLinuxDelegate())
    );
    this.updateGestureHandler(handlerTag, config as unknown as Config);
  },
  attachGestureHandler(
    handlerTag: number,
    newView: unknown,
    _actionType: ActionType,
    propsRef: React.RefObject<unknown>
  ) {
    // @ts-ignore The view is the React Native element findNodeHandle.linux returns.
    NodeManager.getHandler(handlerTag).init(newView, propsRef);
  },
  updateGestureHandler(handlerTag: number, newConfig: Config) {
    NodeManager.getHandler(handlerTag).updateGestureConfig(newConfig);
    InteractionManager.instance.configureInteractions(
      NodeManager.getHandler(handlerTag),
      newConfig
    );
  },
  getGestureHandlerNode(handlerTag: number) {
    return NodeManager.getHandler(handlerTag);
  },
  dropGestureHandler(handlerTag: number) {
    NodeManager.dropGestureHandler(handlerTag);
  },
  install() {
    return true;
  },
  flushOperations() {},
};
