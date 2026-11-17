// Issue #797: React Native's own StatusBar in a Metro-built application. Importing it evaluates both platform specs,
// each of which requires the StatusBarManager TurboModule as it loads, and a desktop window has no status bar, so
// the component and its static methods change nothing and the content beside it still renders.
import { AppRegistry, StatusBar, View } from "react-native";
import { useEffect } from "react";

const log = (line: string): void => {
  console.log(`status-bar: ${line}`);
};

const App = (): React.JSX.Element => {
  useEffect(() => {
    StatusBar.setBarStyle("light-content", true);
    StatusBar.setHidden(true, "fade");
    StatusBar.popStackEntry(StatusBar.pushStackEntry({ barStyle: "dark-content", hidden: false }));
    log(`currentHeight=${String(StatusBar.currentHeight)}`);
    log("committed");
  }, []);

  return (
    <View style={{ backgroundColor: "#11141a", flex: 1 }}>
      <StatusBar barStyle="light-content" hidden />
      <View style={{ backgroundColor: "#3b82f6", height: 80, left: 100, position: "absolute", top: 100, width: 200 }} />
    </View>
  );
};

AppRegistry.registerComponent("StatusBarApp", () => App);
AppRegistry.runApplication("StatusBarApp", { initialProps: {}, rootTag: 1 });
