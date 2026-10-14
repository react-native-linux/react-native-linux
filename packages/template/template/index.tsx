import { App } from "./app.tsx";
import { AppRegistry } from "react-native";

AppRegistry.registerComponent("HelloWorld", () => App);
AppRegistry.runApplication("HelloWorld", { initialProps: {}, rootTag: 1 });
