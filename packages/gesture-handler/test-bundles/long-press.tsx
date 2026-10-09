import { AppRegistry, StyleSheet, View } from "react-native";
import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";
import { useEffect } from "react";

const minimumDurationMilliseconds = 500;
const maximumDistance = 10;
let completedGestures = 0;

const styles = StyleSheet.create({
  target: { backgroundColor: "#3b82f6", height: 80, left: 100, position: "absolute", top: 100, width: 400 },
  root: { flex: 1 },
});

const longPress = Gesture.LongPress()
  .runOnJS(true)
  .minDuration(minimumDurationMilliseconds)
  .maxDistance(maximumDistance)
  .onStart(() => {
    console.log("long-press: active");
  })
  .onEnd((_event, success) => {
    console.log(`long-press: end success=${String(success)}`);
  })
  .onFinalize((_event, success) => {
    completedGestures += 1;
    console.log(`long-press: finalize ${completedGestures} success=${String(success)}`);
  });

const LongPressApp = (): React.JSX.Element => {
  useEffect(() => {
    console.log("long-press: committed");
  }, []);

  return (
    <GestureHandlerRootView style={styles.root}>
      <GestureDetector gesture={longPress}>
        <View style={styles.target} />
      </GestureDetector>
    </GestureHandlerRootView>
  );
};

AppRegistry.registerComponent("LongPress", () => LongPressApp);
AppRegistry.runApplication("LongPress", { initialProps: {}, rootTag: 1 });
