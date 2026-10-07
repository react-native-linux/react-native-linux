import * as React from 'react';
import { PropsWithChildren } from 'react';
import { View, ViewProps, StyleSheet } from 'react-native';
import GestureHandlerRootViewContext from '../GestureHandlerRootViewContext';
import {
  RootPointerEvent,
  routeLinuxPointerEvent,
} from '../web/tools/LinuxPointerRouter';

export interface GestureHandlerRootViewProps
  extends PropsWithChildren<ViewProps> {}

// The capture phase sees every pointer event under the root before any child handles it, which is where the web
// engine's views would have received theirs.
export default function GestureHandlerRootView({
  style,
  ...rest
}: GestureHandlerRootViewProps) {
  return (
    <GestureHandlerRootViewContext.Provider value>
      <View
        style={style ?? styles.container}
        onPointerDownCapture={(event: RootPointerEvent) =>
          routeLinuxPointerEvent('pointerdown', event)
        }
        onPointerMoveCapture={(event: RootPointerEvent) =>
          routeLinuxPointerEvent('pointermove', event)
        }
        onPointerUpCapture={(event: RootPointerEvent) =>
          routeLinuxPointerEvent('pointerup', event)
        }
        onPointerCancelCapture={(event: RootPointerEvent) =>
          routeLinuxPointerEvent('pointercancel', event)
        }
        {...rest}
      />
    </GestureHandlerRootViewContext.Provider>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1 },
});
