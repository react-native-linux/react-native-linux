import { Image } from "react-native";

// #79: from a dev=true bundle, Metro serves a required asset over HTTP, so this source is an http:// URL and the
// window downloads it. The e2e scenario swaps the required file; `onLoad` only fires once the download decoded.
const source = require("./dev-image-before.png");

const logLoad = (event: { nativeEvent: { source: { uri: string } } }): void => {
  console.log(`dev-image: loaded ${new URL(event.nativeEvent.source.uri).pathname}`);
};

const logError = (): void => {
  console.log("dev-image: error");
};

export const DevImageApp = (): React.JSX.Element => (
  <Image onError={logError} onLoad={logLoad} source={source} style={{ height: 96, width: 96 }} testID="dev-image" />
);
