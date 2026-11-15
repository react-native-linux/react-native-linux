import { AppRegistry, StyleSheet, Text, View } from "react-native";
import type { LayoutChangeEvent } from "react-native";
import { GestureHandlerRootView } from "react-native-gesture-handler";
import ReanimatedSwipeable from "react-native-gesture-handler/ReanimatedSwipeable";

const styles = StyleSheet.create({
  root: { flex: 1 },
  row: { backgroundColor: "#3b82f6", height: 60, width: 400 },
});

const reportRow = (event: LayoutChangeEvent): void => {
  const { height, width } = event.nativeEvent.layout;

  console.log(`swipeable: row ${width}x${height}`);
};

const renderRightActions = (): React.JSX.Element => <Text>Archive</Text>;

const SwipeableApp = (): React.JSX.Element => (
  <GestureHandlerRootView style={styles.root}>
    <ReanimatedSwipeable renderRightActions={renderRightActions}>
      <View onLayout={reportRow} style={styles.row} />
    </ReanimatedSwipeable>
  </GestureHandlerRootView>
);

AppRegistry.registerComponent("Swipeable", () => SwipeableApp);
// The window starts an empty surface 1, and a bundle renders into it the way runApplication renders into a root.
AppRegistry.runApplication("Swipeable", { initialProps: {}, rootTag: 1 });
