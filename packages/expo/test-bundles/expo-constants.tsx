import Constants, { ExecutionEnvironment } from "expo-constants";
import { AppRegistry, View } from "react-native";
import { useEffect } from "react";

const ExpoConstantsApp = (): React.JSX.Element => {
  useEffect(() => {
    console.log(
      `expo-constants: bare=${String(Constants.executionEnvironment === ExecutionEnvironment.Bare)} ` +
        `appOwnership=${String(Constants.appOwnership)} statusBarHeight=${Constants.statusBarHeight} ` +
        `expoConfig=${String(Constants.expoConfig)} session=${String(Constants.sessionId.length > 0)}`,
    );
  }, []);

  return <View />;
};

AppRegistry.registerComponent("ExpoConstants", () => ExpoConstantsApp);
AppRegistry.runApplication("ExpoConstants", { initialProps: {}, rootTag: 1 });
