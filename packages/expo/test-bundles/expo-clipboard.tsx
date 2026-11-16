import * as Clipboard from "expo-clipboard";
import { AppRegistry, View } from "react-native";
import { useEffect } from "react";

// T3 Code's calls, copying a value from JavaScript and pasting it back in this process; the round trip through the
// system selection is #754's e2e. The URL and image methods are left to upstream's UnavailabilityError.
const exercise = async (): Promise<string> => {
  const copied = await Clipboard.setStringAsync("t3 device code ABCD-1234");
  const has = await Clipboard.hasStringAsync();
  const pasted = await Clipboard.getStringAsync();
  const unavailable = await Promise.allSettled([Clipboard.getUrlAsync(), Clipboard.hasImageAsync()]);
  const codes = unavailable.map((outcome) =>
    outcome.status === "rejected" && outcome.reason instanceof Error && "code" in outcome.reason
      ? String(outcome.reason.code)
      : outcome.status,
  );

  return `copied=${String(copied)} has=${String(has)} pasted=${JSON.stringify(pasted)} unavailable=${codes.join(",")}`;
};

const ExpoClipboardApp = (): React.JSX.Element => {
  useEffect(() => {
    exercise().then(
      (result) => {
        console.log(`expo-clipboard: ${result}`);
      },
      (error: unknown) => {
        console.log(`expo-clipboard: FAILED ${String(error)}`);
      },
    );
  }, []);

  return <View />;
};

AppRegistry.registerComponent("ExpoClipboard", () => ExpoClipboardApp);
AppRegistry.runApplication("ExpoClipboard", { initialProps: {}, rootTag: 1 });
