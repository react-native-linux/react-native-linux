import { useEffect } from "react";
import { StyleSheet, View } from "react-native";
import Animated, {
  Easing,
  cancelAnimation,
  useAnimatedStyle,
  useReducedMotion,
  useSharedValue,
  withRepeat,
  withTiming,
} from "react-native-reanimated";
import { useControlTheme } from "./useControlTheme";
import { clampProgress, type ProgressProps } from "./Progress.types";

const TRACK_HEIGHT = 4;
const TRACK_OPACITY = 0.16;
const SWEEP_FRACTION = 0.3;
const SWEEP_DURATION_MS = 1200;

// react-native-linux: SwiftUI's linear ProgressView, drawn with Views. The indeterminate sweep
// moves a transform on the UI runtime, so it never commits a layout.
export function Progress(props: ProgressProps) {
  const value = clampProgress(props.value);
  const reducedMotion = useReducedMotion();
  const { colors } = useControlTheme();
  return (
    <View
      className={props.className}
      accessible
      accessibilityRole="progressbar"
      accessibilityLabel={props.label}
      accessibilityValue={
        value === undefined ? undefined : { min: 0, max: 100, now: Math.round(value * 100) }
      }
      accessibilityState={{ busy: value === undefined }}
    >
      <View
        style={{
          height: TRACK_HEIGHT,
          borderRadius: TRACK_HEIGHT / 2,
          overflow: "hidden",
        }}
      >
        <View
          style={[
            StyleSheet.absoluteFill,
            { backgroundColor: colors["--color-foreground"], opacity: TRACK_OPACITY },
          ]}
        />
        {value !== undefined ? (
          <View
            style={{ width: `${value * 100}%`, height: "100%", backgroundColor: colors["--color-primary"] }}
          />
        ) : reducedMotion ? null : (
          <Sweep color={colors["--color-primary"]} />
        )}
      </View>
    </View>
  );
}

function Sweep(props: { readonly color: string }) {
  const trackWidth = useSharedValue(0);
  const progress = useSharedValue(0);
  useEffect(() => {
    progress.value = withRepeat(
      withTiming(1, { duration: SWEEP_DURATION_MS, easing: Easing.inOut(Easing.ease) }),
      -1,
    );
    return () => cancelAnimation(progress);
  }, [progress]);
  const style = useAnimatedStyle(() => ({
    transform: [
      { translateX: (progress.value * (1 + SWEEP_FRACTION) - SWEEP_FRACTION) * trackWidth.value },
    ],
  }));
  return (
    <View
      style={{ flex: 1 }}
      onLayout={(event) => {
        trackWidth.value = event.nativeEvent.layout.width;
      }}
    >
      <Animated.View
        style={[
          { width: `${SWEEP_FRACTION * 100}%`, height: "100%", backgroundColor: props.color },
          style,
        ]}
      />
    </View>
  );
}
