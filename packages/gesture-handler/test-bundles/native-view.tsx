import { AppRegistry, Pressable, View } from "react-native";
import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";

let completedGestures = 0;
let presses = 0;

const nativeView = Gesture.Native()
  .runOnJS(true)
  .onStart(() => {
    console.log("native-view: active");
  })
  .onEnd((_event, success) => {
    console.log(`native-view: end success=${String(success)}`);
  })
  .onFinalize((_event, success) => {
    completedGestures += 1;
    console.log(`native-view: finalize ${completedGestures} success=${String(success)}`);
  });

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
  </GestureHandlerRootView>
);

AppRegistry.registerComponent("NativeView", () => NativeViewApp);
AppRegistry.runApplication("NativeView", { initialProps: {}, rootTag: 1 });
