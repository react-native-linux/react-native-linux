// Issue #168: pointer ownership between a gesture and a Pressable. A click on the Pressable is its press, because
// the pan never activates; a drag that activates the pan takes the pointer, so the Pressable reports pressOut and
// no second press, as RNGH cancels the JavaScript responder on Android and iOS.
import { AppRegistry, Pressable, StyleSheet, View } from "react-native";
import { Gesture, GestureDetector, GestureHandlerRootView } from "react-native-gesture-handler";
import { useEffect } from "react";

const activeOffsetX = 10;
let presses = 0;

const styles = StyleSheet.create({
  button: { backgroundColor: "#3b82f6", height: 80, width: 200 },
  rail: { backgroundColor: "#1e293b", height: 80, left: 100, position: "absolute", top: 100, width: 400 },
  root: { flex: 1 },
});

const log = (line: string): void => {
  console.log(`ownership: ${line}`);
};

const pan = Gesture.Pan()
  .runOnJS(true)
  .activeOffsetX([-activeOffsetX, activeOffsetX])
  .onStart(() => {
    log("pan start");
  })
  .onEnd((_event, success) => {
    log(`pan end success=${String(success)}`);
  });

const App = (): React.JSX.Element => {
  useEffect(() => {
    log("committed");
  }, []);

  return (
    <GestureHandlerRootView style={styles.root}>
      <GestureDetector gesture={pan}>
        <View style={styles.rail}>
          <Pressable
            onPress={() => {
              presses += 1;
              log(`press ${presses}`);
            }}
            onPressIn={() => log("pressIn")}
            onPressOut={() => log("pressOut")}
            style={styles.button}
          />
        </View>
      </GestureDetector>
    </GestureHandlerRootView>
  );
};

AppRegistry.registerComponent("Ownership", () => App);
AppRegistry.runApplication("Ownership", { initialProps: {}, rootTag: 1 });
