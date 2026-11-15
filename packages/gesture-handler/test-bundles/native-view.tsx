import { useState } from "react";
import { AppRegistry, Pressable, ScrollView, Switch, TextInput, View } from "react-native";
import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";

// One object, not module-level lets: the worklets Babel plugin turns these gesture callbacks into worklets, and a
// worklet captures a primitive by copy but an object by reference, so only an object's counts are shared between them.
const counts = { completedGestures: 0, presses: 0, scrollBegins: 0, switchBegins: 0, inputPanBegins: 0 };

const nativeView = Gesture.Native()
  .runOnJS(true)
  .onStart(() => {
    console.log("native-view: active");
  })
  .onEnd((_event, success) => {
    console.log(`native-view: end success=${String(success)}`);
  })
  .onFinalize((event, success) => {
    counts.completedGestures += 1;
    console.log(`native-view: finalize ${counts.completedGestures} success=${String(success)} state=${event.state}`);
  });

const nativeInput = Gesture.Native()
  .disallowInterruption(true)
  .runOnJS(true)
  .onStart(() => {
    console.log("native-input: active");
  })
  .onFinalize((event, success) => {
    console.log(`native-input: finalize state=${event.state} success=${String(success)}`);
  });

const inputPan = Gesture.Pan()
  .runOnJS(true)
  .minDistance(40)
  .onBegin(() => {
    counts.inputPanBegins += 1;
    console.log(`native-input-pan: begin ${counts.inputPanBegins}`);
  })
  .onStart(() => console.log("native-input-pan: active"))
  .onFinalize((event, success) => {
    console.log(`native-input-pan: finalize ${counts.inputPanBegins} state=${event.state} success=${String(success)}`);
  });

const nativeScroll = Gesture.Native()
  .runOnJS(true)
  .onBegin(() => {
    counts.scrollBegins += 1;
    console.log(`native-scroll: begin ${counts.scrollBegins}`);
  })
  .onStart(() => console.log("native-scroll: active"))
  .onFinalize((event, success) => {
    console.log(`native-scroll: finalize state=${event.state} success=${String(success)}`);
  });

const nativeSwitch = Gesture.Native()
  .runOnJS(true)
  .onBegin(() => {
    counts.switchBegins += 1;
    console.log(`native-switch: begin ${counts.switchBegins}`);
  })
  .onStart(() => console.log("native-switch: active"))
  .onFinalize((event, success) => {
    console.log(`native-switch: finalize state=${event.state} success=${String(success)}`);
  });

const NativeSwitch = (): React.JSX.Element => {
  const [value, setValue] = useState(false);
  return (
    <GestureDetector gesture={nativeSwitch}>
      <Switch
        onValueChange={(nextValue) => {
          console.log(`native-switch: value=${String(nextValue)}`);
          setValue(nextValue);
        }}
        style={{ left: 100, position: "absolute", top: 580 }}
        value={value}
      />
    </GestureDetector>
  );
};

const NativeViewApp = (): React.JSX.Element => (
  <GestureHandlerRootView onLayout={() => console.log("native-view: committed")} style={{ flex: 1 }}>
    <GestureDetector gesture={nativeView}>
      <View style={{ height: 80, left: 100, position: "absolute", top: 100, width: 400 }}>
        <Pressable
          onPress={() => {
            counts.presses += 1;
            console.log(`native-view: press ${counts.presses}`);
          }}
          onPressIn={() => console.log("native-view: pressIn")}
          onPressOut={() => console.log("native-view: pressOut")}
          style={{ backgroundColor: "#3b82f6", height: 80, width: 200 }}
        />
      </View>
    </GestureDetector>
    <GestureDetector gesture={inputPan}>
      <View style={{ height: 80, left: 100, position: "absolute", top: 260, width: 400 }}>
        <GestureDetector gesture={nativeInput}>
          <TextInput
            onChangeText={(text) => console.log(`native-input: text=${text}`)}
            onFocus={() => console.log("native-input: focus")}
            style={{ backgroundColor: "#e2e8f0", height: 80, width: 400 }}
          />
        </GestureDetector>
      </View>
    </GestureDetector>
    <GestureDetector gesture={nativeScroll}>
      <ScrollView
        contentContainerStyle={{ width: 800 }}
        onScroll={(event) => {
          console.log(`native-scroll: scrolled=${String(event.nativeEvent.contentOffset.y > 0)}`);
          if (event.nativeEvent.contentOffset.x > 0) {
            console.log("native-scroll: horizontal");
          }
        }}
        scrollEventThrottle={16}
        style={{ height: 160, left: 100, position: "absolute", top: 380, width: 400 }}
      >
        <View style={{ backgroundColor: "#3b82f6", height: 600 }} />
      </ScrollView>
    </GestureDetector>
    <NativeSwitch />
  </GestureHandlerRootView>
);

AppRegistry.registerComponent("NativeView", () => NativeViewApp);
AppRegistry.runApplication("NativeView", { initialProps: {}, rootTag: 1 });
