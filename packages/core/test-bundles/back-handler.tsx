import { AppRegistry, BackHandler, TextInput, View } from "react-native";
import { useEffect } from "react";

const report = (message: string): void => {
  console.log(`back-handler: ${message}`);
};

const BackHandlerApp = (): React.JSX.Element => {
  useEffect(() => {
    let backPresses = 0;
    const subscription = BackHandler.addEventListener("hardwareBackPress", () => {
      backPresses += 1;
      report(`back ${backPresses}`);

      return false;
    });

    return () => {
      subscription.remove();
    };
  }, []);

  return (
    <View onLayout={() => report("committed")} style={{ flex: 1 }}>
      <TextInput
        onBlur={() => report("blur")}
        onFocus={() => report("focus")}
        style={{ backgroundColor: "#ffffff", height: 80, left: 100, position: "absolute", top: 100, width: 400 }}
      />
    </View>
  );
};

AppRegistry.registerComponent("BackHandlerApp", () => BackHandlerApp);
AppRegistry.runApplication("BackHandlerApp", { initialProps: {}, rootTag: 1 });
