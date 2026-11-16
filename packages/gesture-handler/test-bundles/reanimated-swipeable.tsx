import { AppRegistry, ScrollView, StyleSheet, Text, View } from "react-native";
import type { LayoutChangeEvent } from "react-native";
import { GestureDetector, GestureHandlerRootView, useNativeGesture, usePanGesture } from "react-native-gesture-handler";
import ReanimatedSwipeable from "react-native-gesture-handler/ReanimatedSwipeable";
import Animated, { useAnimatedStyle, useSharedValue } from "react-native-reanimated";
import { scheduleOnRN } from "react-native-worklets";

const styles = StyleSheet.create({
  root: { flex: 1 },
  row: { backgroundColor: "#3b82f6", height: 60, width: 400 },
  scroll: { height: 240, width: 400 },
  toast: { backgroundColor: "#22c55e", height: 60, left: 0, position: "absolute", top: 300, width: 400 },
});

const reportRow = (event: LayoutChangeEvent): void => {
  const { height, width } = event.nativeEvent.layout;

  console.log(`swipeable: row ${width}x${height}`);
};

const report = (message: string): void => {
  console.log(message);
};

const renderRightActions = (): React.JSX.Element => <Text>Archive</Text>;

// T3 Code's thread list: the rows scroll in a list wrapped in a GestureDetector for a Native gesture.
const SwipeableRowInScroll = (): React.JSX.Element => {
  const scrollGesture = useNativeGesture();

  return (
    <GestureDetector gesture={scrollGesture}>
      <ScrollView style={styles.scroll}>
        <ReanimatedSwipeable
          onSwipeableOpenStartDrag={(direction) => console.log(`swipeable: drag to open ${direction}`)}
          onSwipeableWillOpen={(direction) => console.log(`swipeable: will open ${direction}`)}
          onSwipeableOpen={(direction) => console.log(`swipeable: open ${direction}`)}
          renderRightActions={renderRightActions}
        >
          <View onLayout={reportRow} style={styles.row} />
        </ReanimatedSwipeable>
        <View style={{ height: 600 }} />
      </ScrollView>
    </GestureDetector>
  );
};

// T3 Code's toast: a worklet pan that follows horizontal drags and fails on vertical ones.
const SwipeableToast = (): React.JSX.Element => {
  const translation = useSharedValue(0);
  const pan = usePanGesture({
    activeOffsetX: [-10, 10],
    failOffsetY: [-12, 12],
    onUpdate: (event) => {
      translation.set(event.translationX);
    },
    onDeactivate: (event) => {
      scheduleOnRN(report, `toast: end translationX=${String(Math.round(event.translationX))}`);
    },
    onFinalize: (event) => {
      scheduleOnRN(report, `toast: finalize canceled=${String(event.canceled)}`);
    },
  });
  const animatedStyle = useAnimatedStyle(() => ({ transform: [{ translateX: translation.get() }] }));

  return (
    <GestureDetector gesture={pan}>
      <Animated.View style={[styles.toast, animatedStyle]} />
    </GestureDetector>
  );
};

const SwipeableApp = (): React.JSX.Element => (
  <GestureHandlerRootView style={styles.root}>
    <SwipeableRowInScroll />
    <SwipeableToast />
  </GestureHandlerRootView>
);

AppRegistry.registerComponent("Swipeable", () => SwipeableApp);
// The window starts an empty surface 1, and a bundle renders into it the way runApplication renders into a root.
AppRegistry.runApplication("Swipeable", { initialProps: {}, rootTag: 1 });
