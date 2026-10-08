import { AppRegistry } from "react-native";
import { FastRefreshApp } from "./fast-refresh-app.tsx";

AppRegistry.registerComponent("FastRefresh", () => FastRefreshApp);
// The window starts an empty surface 1, and a bundle renders into it the way runApplication renders into a root.
AppRegistry.runApplication("FastRefresh", { initialProps: {}, rootTag: 1 });
