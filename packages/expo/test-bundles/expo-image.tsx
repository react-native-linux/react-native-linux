import { AppRegistry, type LayoutChangeEvent, View } from "react-native";
import { Image } from "expo-image";

// T3 Code's `ProjectFavicon` call. The view is not registered yet (#273), so it lays out as ADR-0004's fallback.
const logLayout = ({ nativeEvent: { layout } }: LayoutChangeEvent): void => {
  console.log(`expo-image: image layout ${layout.x},${layout.y} ${layout.width}x${layout.height}`);
};

const ExpoImageApp = (): React.JSX.Element => (
  <View style={{ flex: 1, padding: 20 }}>
    <Image
      cachePolicy="memory-disk"
      contentFit="contain"
      onLayout={logLayout}
      source={{ uri: "file:///nonexistent/favicon.png" }}
      style={{ height: 16, width: 16 }}
    />
  </View>
);

AppRegistry.registerComponent("ExpoImage", () => ExpoImageApp);
AppRegistry.runApplication("ExpoImage", { initialProps: {}, rootTag: 1 });
