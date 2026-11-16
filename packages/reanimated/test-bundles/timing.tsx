import Animated, { useAnimatedReaction, useAnimatedStyle, useSharedValue, withTiming } from "react-native-reanimated";
import { AppRegistry, Pressable, StyleSheet } from "react-native";
import { scheduleOnRN } from "react-native-worklets";
import { useCallback, useEffect } from "react";

const animationMilliseconds = 1200;
const stallMilliseconds = 2500;
const travel = 400;
// A 1200 ms ramp is 72 frames at 60 Hz, enough for the frame budget's sample on a window that presents only the
// frames with work (#703); a value that jumped to its end would step once.
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
  const slideStyle = useAnimatedStyle(() => ({ transform: [{ translateX: progress.get() * travel }] }));

  useAnimatedReaction(
    () => progress.get(),
    (current, previous) => {
      if (previous !== null && current !== previous) {
        steps.set(steps.get() + 1);
      }
    },
  );

  const slide = useCallback(
    (origin: string): void => {
      steps.set(0);
      progress.set(0);
      progress.set(
        withTiming(1, { duration: animationMilliseconds }, (finished) => {
          const stepped = steps.get() >= minimumSteps ? "frame by frame" : `only ${String(steps.get())} times`;

          scheduleOnRN(report, `${origin} finished ${String(finished)}, stepped ${stepped}`);
        }),
      );
    },
    [progress, steps],
  );

  useEffect(() => {
    slide("mount");
    report("started");

    // Worklets sends the animation to the UI runtime once this effect returns, so the stall starts a task later.
    setTimeout(() => {
      const stallStart = Date.now();

      while (Date.now() - stallStart < stallMilliseconds) {
        // Hold the JavaScript thread for the whole animation: only the frame thread can move the box.
      }

      report(`value after the stall ${String(progress.get())}`);
    }, 0);
  }, [progress, slide]);

  return (
    <Pressable
      onPress={() => {
        slide("idle");
      }}
      style={styles.root}
    >
      <Animated.View style={[styles.box, slideStyle]} />
    </Pressable>
  );
};

AppRegistry.registerComponent("Timing", () => TimingApp);
AppRegistry.runApplication("Timing", { initialProps: {}, rootTag: 1 });
