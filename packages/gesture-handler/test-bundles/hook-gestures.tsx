import { useState } from "react";
import { AppRegistry, ScrollView, View } from "react-native";
import {
  GestureDetector,
  GestureHandlerRootView,
  useExclusiveGestures,
  useNativeGesture,
  usePanGesture,
  useTapGesture,
} from "react-native-gesture-handler";

const doubleTapCount = 2;
const doubleTapDelay = 250;
// One object, not module-level lets: the worklets Babel plugin turns these gesture callbacks into worklets, and a
// worklet captures a primitive by copy but an object by reference, so only an object's counts are shared between them.
const counts = { singleTaps: 0, doubleTaps: 0, scrollBegins: 0 };

const PannedBox = (): React.JSX.Element => {
  const [translationX, setTranslationX] = useState(0);
  const pan = usePanGesture({
    runOnJS: true,
    onBegin: () => console.log("hook-pan: began"),
    onActivate: (event) => console.log(`hook-pan: active x=${Math.round(event.x)}`),
    onUpdate: (event) => setTranslationX(event.translationX),
    onDeactivate: (event) =>
      console.log(`hook-pan: end translationX=${Math.round(event.translationX)} canceled=${String(event.canceled)}`),
    onFinalize: (event) => console.log(`hook-pan: finalize canceled=${String(event.canceled)}`),
  });

  return (
    <GestureDetector gesture={pan}>
      <View
        style={{
          backgroundColor: "#3b82f6",
          height: 80,
          left: 100,
          position: "absolute",
          top: 100,
          transform: [{ translateX: translationX }],
          width: 200,
        }}
      />
    </GestureDetector>
  );
};

const TappedBox = (): React.JSX.Element => {
  const doubleTap = useTapGesture({
    runOnJS: true,
    numberOfTaps: doubleTapCount,
    maxDelay: doubleTapDelay,
    onDeactivate: (event) => {
      if (!event.canceled) {
        counts.doubleTaps += 1;
        console.log(`hook-tap: double taps=${counts.doubleTaps} single taps=${counts.singleTaps}`);
      }
    },
  });
  const singleTap = useTapGesture({
    runOnJS: true,
    onDeactivate: (event) => {
      if (!event.canceled) {
        counts.singleTaps += 1;
        console.log(`hook-tap: single taps=${counts.singleTaps} double taps=${counts.doubleTaps}`);
      }
    },
  });
  const exclusive = useExclusiveGestures(doubleTap, singleTap);

  return (
    <GestureDetector gesture={exclusive}>
      <View style={{ backgroundColor: "#22c55e", height: 80, left: 100, position: "absolute", top: 250, width: 400 }} />
    </GestureDetector>
  );
};

const NativeScroll = (): React.JSX.Element => {
  const native = useNativeGesture({
    runOnJS: true,
    onBegin: () => {
      counts.scrollBegins += 1;
      console.log(`hook-scroll: begin ${counts.scrollBegins}`);
    },
    onActivate: () => console.log("hook-scroll: active"),
  });

  return (
    <GestureDetector gesture={native}>
      <ScrollView
        onScroll={(event) => console.log(`hook-scroll: scrolled=${String(event.nativeEvent.contentOffset.y > 0)}`)}
        scrollEventThrottle={16}
        style={{ height: 160, left: 100, position: "absolute", top: 380, width: 400 }}
      >
        <View style={{ backgroundColor: "#f59e0b", height: 600 }} />
      </ScrollView>
    </GestureDetector>
  );
};

const HookGesturesApp = (): React.JSX.Element => (
  <GestureHandlerRootView onLayout={() => console.log("hook-gestures: committed")} style={{ flex: 1 }}>
    <PannedBox />
    <TappedBox />
    <NativeScroll />
  </GestureHandlerRootView>
);

AppRegistry.registerComponent("HookGestures", () => HookGesturesApp);
AppRegistry.runApplication("HookGestures", { initialProps: {}, rootTag: 1 });
