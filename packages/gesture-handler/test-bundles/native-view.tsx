import { useState } from "react";
import { AppRegistry, Pressable, ScrollView, Switch, TextInput, View } from "react-native";
import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";

let completedGestures = 0;
let presses = 0;
let scrollBegins = 0;
let switchBegins = 0;

const nativeView = Gesture.Native()
  .runOnJS(true)
  .onStart(() => {
    console.log("native-view: active");
  })
  .onEnd((_event, success) => {
    console.log(`native-view: end success=${String(success)}`);
  })
  .onFinalize((event, success) => {
    completedGestures += 1;
    console.log(`native-view: finalize ${completedGestures} success=${String(success)} state=${event.state}`);
  });

const nativeInput = Gesture.Native()
  .runOnJS(true)
  .onStart(() => {
    console.log("native-input: active");
  })
  .onFinalize((event, success) => {
    console.log(`native-input: finalize state=${event.state} success=${String(success)}`);
  });

const nativeScroll = Gesture.Native()
  .runOnJS(true)
  .onBegin(() => {
    scrollBegins += 1;
    console.log(`native-scroll: begin ${scrollBegins}`);
  })
  .onStart(() => console.log("native-scroll: active"))
  .onFinalize((event, success) => {
    console.log(`native-scroll: finalize state=${event.state} success=${String(success)}`);
  });

const nativeSwitch = Gesture.Native()
  .runOnJS(true)
  .onBegin(() => {
    switchBegins += 1;
    console.log(`native-switch: begin ${switchBegins}`);
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
            presses += 1;
            console.log(`native-view: press ${presses}`);
          }}
          onPressIn={() => console.log("native-view: pressIn")}
          onPressOut={() => console.log("native-view: pressOut")}
          style={{ backgroundColor: "#3b82f6", height: 80, width: 200 }}
        />
      </View>
    </GestureDetector>
    <GestureDetector gesture={nativeInput}>
      <TextInput
        onChangeText={(text) => console.log(`native-input: text=${text}`)}
        onFocus={() => console.log("native-input: focus")}
        style={{ backgroundColor: "#e2e8f0", height: 80, left: 100, position: "absolute", top: 260, width: 400 }}
      />
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
