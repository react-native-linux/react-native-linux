import { AppRegistry, Image } from "react-native";

// #808: a file bundle resolves a required image beside itself, where the bundle step wrote it.
const source = require("./dev-image-before.png");

const logLoad = (event: { nativeEvent: { source: { uri: string } } }): void => {
  console.log(`bundled-image: loaded ${event.nativeEvent.source.uri}`);
};

const logError = (): void => {
  console.log("bundled-image: error");
};

const BundledImageApp = (): React.JSX.Element => (
  <Image onError={logError} onLoad={logLoad} source={source} style={{ height: 96, width: 144 }} />
);

AppRegistry.registerComponent("BundledImage", () => BundledImageApp);
AppRegistry.runApplication("BundledImage", { initialProps: {}, rootTag: 1 });
