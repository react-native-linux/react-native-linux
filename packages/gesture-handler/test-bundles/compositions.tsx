import { AppRegistry, View } from "react-native";
import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";

const simultaneousPanCount = 2;
const doubleTapCount = 2;
const doubleTapDelay = 250;
let activePans = 0;
let completedPans = 0;
let singleTaps = 0;
let doubleTaps = 0;

const simultaneous = Gesture.Simultaneous(
  ...Array.from({ length: simultaneousPanCount }, () =>
    Gesture.Pan()
      .runOnJS(true)
      .onStart(() => {
        activePans += 1;
        console.log(`compositions: active pans=${activePans}`);
      })
      .onEnd((_event, success) => {
        if (success) {
          completedPans += 1;
          console.log(`compositions: completed pans=${completedPans}`);
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
      doubleTaps += 1;
      console.log(`compositions: double taps=${doubleTaps} single taps=${singleTaps}`);
    }
  });
const singleTap = Gesture.Tap()
  .runOnJS(true)
  .onEnd((_event, success) => {
    if (success) {
      singleTaps += 1;
      console.log(`compositions: single taps=${singleTaps} double taps=${doubleTaps}`);
    }
  });
const exclusive = Gesture.Exclusive(doubleTap, singleTap);

const CompositionsApp = (): React.JSX.Element => (
  <GestureHandlerRootView onLayout={() => console.log("compositions: committed")} style={{ flex: 1 }}>
    <GestureDetector gesture={simultaneous}>
      <View style={{ backgroundColor: "#3b82f6", height: 80, left: 100, position: "absolute", top: 100, width: 400 }} />
    </GestureDetector>
    <GestureDetector gesture={exclusive}>
      <View style={{ backgroundColor: "#22c55e", height: 80, left: 100, position: "absolute", top: 250, width: 400 }} />
    </GestureDetector>
  </GestureHandlerRootView>
);

AppRegistry.registerComponent("Compositions", () => CompositionsApp);
AppRegistry.runApplication("Compositions", { initialProps: {}, rootTag: 1 });
