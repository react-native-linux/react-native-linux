import { AppRegistry, View } from "react-native";
import { isRNRuntime, isUIRuntime, scheduleOnRN, scheduleOnUI } from "react-native-worklets";

const capturedBase = 40;

const report = (uiRuntimeResult: string): void => {
  console.log(`worklets: ${uiRuntimeResult}, reported on the RN runtime ${String(isRNRuntime())}`);
};

const WorkletsApp = (): React.JSX.Element => <View style={{ backgroundColor: "#0f172a", flex: 1 }} />;

AppRegistry.registerComponent("Worklets", () => WorkletsApp);
AppRegistry.runApplication("Worklets", { initialProps: {}, rootTag: 1 });

console.log("worklets: started");
scheduleOnUI((offset: number) => {
  "worklet";
  scheduleOnRN(report, `UI runtime ${String(isUIRuntime())} computed ${String(capturedBase + offset)}`);
}, 2);
