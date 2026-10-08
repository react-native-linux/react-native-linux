// Issue #578: React Native's own Pressable in a Metro-built application, pressing through the responder system
// the platform feeds with touch events. A click is pressIn, press and pressOut; a drag off the button before the
// release is pressIn and pressOut with no second press. Pressability itself orders a short click press before
// pressOut, which it delays to its minimum press duration.
import { AppRegistry, Pressable, View } from "react-native";
import { useEffect } from "react";

let presses = 0;

const log = (line: string): void => {
  console.log(`pressable-react: ${line}`);
};

const App = (): React.JSX.Element => {
  useEffect(() => {
    log("committed");
  }, []);

  return (
    <View style={{ flex: 1 }}>
      <Pressable
        onPress={() => {
          presses += 1;
          log(`press ${presses}`);
        }}
        onPressIn={() => log("pressIn")}
        onPressOut={() => log("pressOut")}
        style={{ backgroundColor: "#3b82f6", height: 80, left: 100, position: "absolute", top: 100, width: 200 }}
      />
    </View>
  );
};

AppRegistry.registerComponent("PressableReact", () => App);
AppRegistry.runApplication("PressableReact", { initialProps: {}, rootTag: 1 });
