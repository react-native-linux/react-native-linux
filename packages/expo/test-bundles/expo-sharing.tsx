import "./install-whatwg-url.ts";
import * as Sharing from "expo-sharing";
import { File, Paths } from "expo-file-system";
import { AppRegistry, View } from "react-native";
import { useEffect } from "react";

const messageOf = async (share: () => Promise<void>): Promise<string> => {
  try {
    await share();

    return "resolved";
  } catch (error) {
    return error instanceof Error ? error.message : String(error);
  }
};

// T3 Code's attachment download: a file in the cache, handed on with its MIME type and name once sharing says it is
// available. The e2e session has no session bus, so there is no portal: sharing is unavailable, and a share rejects
// with the reason rather than waiting on a bus.
const exercise = async (): Promise<string> => {
  const attachment = new File(Paths.cache, "report.pdf");

  attachment.create({ overwrite: true });
  attachment.write("%PDF-1.7");

  const fields = [
    `available=${String(await Sharing.isAvailableAsync())}`,
    `share=${await messageOf(() => Sharing.shareAsync(attachment.uri, { mimeType: "application/pdf", dialogTitle: "report.pdf" }))}`,
    `payloads=${JSON.stringify(Sharing.getSharedPayloads())}`,
    `resolved=${JSON.stringify(await Sharing.getResolvedSharedPayloadsAsync())}`,
  ];

  Sharing.clearSharedPayloads();
  attachment.delete();

  return fields.join(" ");
};

const ExpoSharingApp = (): React.JSX.Element => {
  useEffect(() => {
    exercise().then(
      (result) => {
        console.log(`expo-sharing: ${result}`);
      },
      (error: unknown) => {
        console.log(`expo-sharing: FAILED ${String(error)}`);
      },
    );
  }, []);

  return <View />;
};

AppRegistry.registerComponent("ExpoSharing", () => ExpoSharingApp);
AppRegistry.runApplication("ExpoSharing", { initialProps: {}, rootTag: 1 });
