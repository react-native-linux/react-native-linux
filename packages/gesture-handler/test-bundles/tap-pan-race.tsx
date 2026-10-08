import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";
import { AppRegistry, StyleSheet, View } from "react-native";
import { useEffect } from "react";

const activeOffsetX = 10;
const failOffsetY = 12;
const tapMaxDurationMs = 250;

const styles = StyleSheet.create({
  rail: { backgroundColor: "#3b82f6", height: 80, left: 100, position: "absolute", top: 100, width: 400 },
  root: { flex: 1 },
});

const log = (line: string): void => {
  console.log(`gestures: ${line}`);
};

// The flagship's replay scrubber (#168): a tap and a horizontal pan raced on one rail, both run on JavaScript.
const tap = Gesture.Tap()
  .runOnJS(true)
  .maxDistance(activeOffsetX)
  .maxDuration(tapMaxDurationMs)
  .onEnd((event, success) => {
    log(`tap end x=${Math.round(event.x)} success=${String(success)}`);
  });
const pan = Gesture.Pan()
  .runOnJS(true)
  .activeOffsetX([-activeOffsetX, activeOffsetX])
  .failOffsetY([-failOffsetY, failOffsetY])
  .onStart((event) => {
    log(`pan start x=${Math.round(event.x)}`);
  })
  .onEnd((event, success) => {
    log(`pan end x=${Math.round(event.x)} translationX=${Math.round(event.translationX)} success=${String(success)}`);
  });
const scrubber = Gesture.Race(tap, pan);

const GesturesApp = (): React.JSX.Element => {
  useEffect(() => {
    log("committed");
  }, []);

  return (
    <GestureHandlerRootView style={styles.root}>
      <GestureDetector gesture={scrubber}>
        <View style={styles.rail} />
      </GestureDetector>
    </GestureHandlerRootView>
  );
};

AppRegistry.registerComponent("Gestures", () => GesturesApp);
// The window starts an empty surface 1, and a bundle renders into it the way runApplication renders into a root.
AppRegistry.runApplication("Gestures", { initialProps: {}, rootTag: 1 });
