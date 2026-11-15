import { AppRegistry, type LayoutChangeEvent, UIManager, View, type ViewProps } from "react-native";
import { requireNativeViewManager } from "expo-modules-core";

// T3 Code's `FrostedCutout`, which `expo` exports as `requireNativeView`; nothing registers it on Linux.
const FrostedCutout = requireNativeViewManager<ViewProps>("T3NativeControls", "FrostedCutout");

const logLayout =
  (name: string) =>
  ({ nativeEvent: { layout } }: LayoutChangeEvent): void => {
    console.log(`expo-native-view: ${name} layout ${layout.x},${layout.y} ${layout.width}x${layout.height}`);
  };

const ExpoNativeViewApp = (): React.JSX.Element => {
  const frostedCutoutConfig = globalThis.expo.getViewConfig("T3NativeControls", "FrostedCutout");

  console.log(
    `expo-native-view: hasViewManagerConfig View=${String(UIManager.hasViewManagerConfig("View"))} ` +
      `FrostedCutout=${String(UIManager.hasViewManagerConfig("ViewManagerAdapter_T3NativeControls_FrostedCutout"))} ` +
      `getViewConfig FrostedCutout=${JSON.stringify(frostedCutoutConfig)}`,
  );

  return (
    <View style={{ flex: 1, padding: 20 }}>
      <FrostedCutout onLayout={logLayout("FrostedCutout")} style={{ height: 80, padding: 10, width: 120 }}>
        <View onLayout={logLayout("child")} style={{ backgroundColor: "#61afef", height: 30, width: 40 }} />
      </FrostedCutout>
    </View>
  );
};

AppRegistry.registerComponent("ExpoNativeView", () => ExpoNativeViewApp);
AppRegistry.runApplication("ExpoNativeView", { initialProps: {}, rootTag: 1 });
