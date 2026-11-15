import { AppRegistry } from "react-native";
import { DevImageApp } from "./dev-image-app.tsx";

AppRegistry.registerComponent("DevImage", () => DevImageApp);
// The window starts an empty surface 1, and a bundle renders into it the way runApplication renders into a root.
AppRegistry.runApplication("DevImage", { initialProps: {}, rootTag: 1 });
