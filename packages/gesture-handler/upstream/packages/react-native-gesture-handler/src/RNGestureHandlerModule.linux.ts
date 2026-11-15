import type React from 'react';

import type { ActionType } from './ActionType';
import type { GestureRelations } from './v3/types';
import { Gestures } from './web/Gestures';
import type { Config, HostDetector, PropsRef } from './web/interfaces';
import { GestureHandlerLinuxDelegate } from './web/tools/GestureHandlerLinuxDelegate';
import InteractionManager from './web/tools/InteractionManager';
import NodeManager from './web/tools/NodeManager';

// Linux runs the library's own JavaScript engine (src/web) with GestureHandlerLinuxDelegate in place of the DOM
// delegate: recognition happens on the JavaScript thread, fed by GestureHandlerRootView.linux. The methods are
// RNGestureHandlerModule.web's.
export default {
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
    this.setGestureHandlerConfig(handlerTag, config as unknown as Config);
  },
  attachGestureHandler(
    handlerTag: number,
    newView: unknown,
    actionType: ActionType,
    propsRef: React.RefObject<PropsRef>,
    hostDetector?: HostDetector | null
  ) {
    NodeManager.getHandler(handlerTag).init(
      // @ts-expect-error The view is the React Native element findNodeHandle.linux returns.
      newView,
      propsRef,
      actionType,
      hostDetector ?? null
    );
  },
  detachGestureHandler(handlerTag: number, hostDetector?: HostDetector | null) {
    NodeManager.detachGestureHandler(handlerTag, hostDetector);
  },
  setGestureHandlerConfig(handlerTag: number, newConfig: Config) {
    NodeManager.getHandler(handlerTag).setGestureConfig(newConfig);
  },
  updateGestureHandlerConfig(handlerTag: number, newConfig: Partial<Config>) {
    NodeManager.getHandler(handlerTag).updateGestureConfig(newConfig);
  },
  getGestureHandlerNode(handlerTag: number) {
    return NodeManager.getHandler(handlerTag);
  },
  dropGestureHandler(handlerTag: number) {
    NodeManager.dropGestureHandler(handlerTag);
  },
  configureRelations(handlerTag: number, relations: GestureRelations) {
    if (!NodeManager.hasHandler(handlerTag)) {
      return;
    }
    InteractionManager.instance.configureInteractions(
      NodeManager.getHandler(handlerTag),
      relations
    );
  },
  flushOperations() {},
  installUIRuntimeBindings() {
    return true;
  },
  getModuleId() {
    return -1;
  },
};
