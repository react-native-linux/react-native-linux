import { AppRegistry, Text } from "react-native";
import { reloadTitle } from "./reload-title.ts";

// The bundle runs from the top on every reload, so this line names the title each new instance started with.
console.log(`reload: title ${reloadTitle}`);

const ReloadApp = (): React.JSX.Element => <Text testID="reload">{reloadTitle}</Text>;

AppRegistry.registerComponent("Reload", () => ReloadApp);
// The window starts an empty surface 1, and a bundle renders into it the way runApplication renders into a root.
AppRegistry.runApplication("Reload", { initialProps: {}, rootTag: 1 });
