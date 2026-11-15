import { AppRegistry, View } from "react-native";
import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";

const simultaneousPanCount = 2;
const doubleTapCount = 2;
const doubleTapDelay = 250;
const failureCheckpointDelay = 100;
// One object, not module-level lets: the worklets Babel plugin turns these gesture callbacks into worklets, and a
// worklet captures a primitive by copy but an object by reference, so only an object's counts are shared between them.
const counts = { activePans: 0, completedPans: 0, singleTaps: 0, doubleTaps: 0, outerTaps: 0, innerDoubleTaps: 0 };

const simultaneous = Gesture.Simultaneous(
  ...Array.from({ length: simultaneousPanCount }, () =>
    Gesture.Pan()
      .runOnJS(true)
      .onStart(() => {
        counts.activePans += 1;
        console.log(`compositions: active pans=${counts.activePans}`);
      })
      .onEnd((_event, success) => {
        if (success) {
          counts.completedPans += 1;
          console.log(`compositions: completed pans=${counts.completedPans}`);
        }
      }),
  ),
);
const doubleTap = Gesture.Tap()
  .runOnJS(true)
  .numberOfTaps(doubleTapCount)
  .maxDelay(doubleTapDelay)
  .onEnd((_event, success) => {
    if (success) {
      counts.doubleTaps += 1;
      console.log(`compositions: double taps=${counts.doubleTaps} single taps=${counts.singleTaps}`);
    }
  });
const singleTap = Gesture.Tap()
  .runOnJS(true)
  .onEnd((_event, success) => {
    if (success) {
      counts.singleTaps += 1;
      console.log(`compositions: single taps=${counts.singleTaps} double taps=${counts.doubleTaps}`);
    }
  });
const exclusive = Gesture.Exclusive(doubleTap, singleTap);

const innerDoubleTap = Gesture.Tap()
  .runOnJS(true)
  .numberOfTaps(doubleTapCount)
  .maxDelay(doubleTapDelay)
  .onTouchesUp(() => {
    setTimeout(() => {
      console.log(`external: awaiting outer taps=${counts.outerTaps}`);
    }, failureCheckpointDelay);
  })
  .onEnd((_event, success) => {
    if (success) {
      counts.innerDoubleTaps += 1;
      console.log(`external: inner double taps=${counts.innerDoubleTaps} outer taps=${counts.outerTaps}`);
    }
  });
const outerTap = Gesture.Tap()
  .runOnJS(true)
  .requireExternalGestureToFail(innerDoubleTap)
  .onEnd((_event, success) => {
    if (success) {
      counts.outerTaps += 1;
      console.log(`external: outer taps=${counts.outerTaps} inner double taps=${counts.innerDoubleTaps}`);
    }
  });

const CompositionsApp = (): React.JSX.Element => (
  <GestureHandlerRootView onLayout={() => console.log("compositions: committed")} style={{ flex: 1 }}>
    <GestureDetector gesture={simultaneous}>
      <View style={{ backgroundColor: "#3b82f6", height: 80, left: 100, position: "absolute", top: 100, width: 400 }} />
    </GestureDetector>
    <GestureDetector gesture={exclusive}>
      <View style={{ backgroundColor: "#22c55e", height: 80, left: 100, position: "absolute", top: 250, width: 400 }} />
    </GestureDetector>
    <GestureDetector gesture={outerTap}>
      <View style={{ height: 80, left: 100, position: "absolute", top: 400, width: 400 }}>
        <GestureDetector gesture={innerDoubleTap}>
          <View style={{ backgroundColor: "#f59e0b", height: 80, width: 200 }} />
        </GestureDetector>
      </View>
    </GestureDetector>
  </GestureHandlerRootView>
);

AppRegistry.registerComponent("Compositions", () => CompositionsApp);
AppRegistry.runApplication("Compositions", { initialProps: {}, rootTag: 1 });
