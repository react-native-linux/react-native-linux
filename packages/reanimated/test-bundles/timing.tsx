import Animated, { useAnimatedReaction, useAnimatedStyle, useSharedValue, withTiming } from "react-native-reanimated";
import { AppRegistry, StyleSheet, View } from "react-native";
import { scheduleOnRN } from "react-native-worklets";
import { useEffect } from "react";

const animationMilliseconds = 500;
const stallMilliseconds = 1500;
const travel = 400;
// A 500 ms ramp is 30 frames at 60 Hz; a value that jumped to its end would step once.
const minimumSteps = 20;

const styles = StyleSheet.create({
  box: { backgroundColor: "#38bdf8", height: 100, left: 40, position: "absolute", top: 100, width: 100 },
  root: { backgroundColor: "#0f172a", flex: 1 },
});

const report = (message: string): void => {
  console.log(`reanimated-timing: ${message}`);
};

const TimingApp = (): React.JSX.Element => {
  const progress = useSharedValue(0);
  const steps = useSharedValue(0);
  const slide = useAnimatedStyle(() => ({ transform: [{ translateX: progress.get() * travel }] }));

  useAnimatedReaction(
    () => progress.get(),
    (current, previous) => {
      if (previous !== null && current !== previous) {
        steps.set(steps.get() + 1);
      }
    },
  );

  useEffect(() => {
    progress.set(
      withTiming(1, { duration: animationMilliseconds }, (finished) => {
        const stepped = steps.get() >= minimumSteps ? "frame by frame" : `only ${String(steps.get())} times`;

        scheduleOnRN(report, `finished ${String(finished)}, stepped ${stepped}`);
      }),
    );
    report("started");

    // Worklets sends the animation to the UI runtime once this effect returns, so the stall starts a task later.
    setTimeout(() => {
      const stallStart = Date.now();

      while (Date.now() - stallStart < stallMilliseconds) {
        // Hold the JavaScript thread for the whole animation: only the frame thread can move the box.
      }

      report(`value after the stall ${String(progress.get())}`);
    }, 0);
  }, [progress, steps]);

  return (
    <View style={styles.root}>
      <Animated.View style={[styles.box, slide]} />
    </View>
  );
};

AppRegistry.registerComponent("Timing", () => TimingApp);
AppRegistry.runApplication("Timing", { initialProps: {}, rootTag: 1 });
