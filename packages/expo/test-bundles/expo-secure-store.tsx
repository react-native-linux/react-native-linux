import * as SecureStore from "expo-secure-store";
import { AppRegistry, TurboModuleRegistry, View } from "react-native";
import type { TurboModule } from "react-native";
import { useEffect } from "react";

interface DevSettingsSpec extends TurboModule {
  readonly reload: () => void;
}

// T3 Code's calls: its DPoP key and relay token through the asynchronous API, its voice grant through the
// synchronous one with an iOS-only option. The first instance stores and reloads; the second reads back what the
// first stored, from the keyring or, with none on the session, from the encrypted files under XDG_DATA_HOME.
const deviceOnly = { keychainAccessible: SecureStore.WHEN_UNLOCKED_THIS_DEVICE_ONLY };

const exercise = async (): Promise<string> => {
  const token = await SecureStore.getItemAsync("dpop-proof-key");

  if (token === null) {
    await SecureStore.setItemAsync("dpop-proof-key", JSON.stringify({ kty: "EC" }));
    SecureStore.setItem("voice-grant", "voice-grant-v1", deviceOnly);
    await SecureStore.setItemAsync("relay-token", "deleted");
    await SecureStore.deleteItemAsync("relay-token");

    return "stored";
  }

  const fields = [
    `token=${token}`,
    `grant=${JSON.stringify(SecureStore.getItem("voice-grant", deviceOnly))}`,
    `deleted=${String(await SecureStore.getItemAsync("relay-token"))}`,
    `other=${String(await SecureStore.getItemAsync("dpop-proof-key", { keychainService: "other" }))}`,
    `available=${String(await SecureStore.isAvailableAsync())}`,
    `biometrics=${String(SecureStore.canUseBiometricAuthentication())}`,
  ];

  return `read ${fields.join(" ")}`;
};

const ExpoSecureStoreApp = (): React.JSX.Element => {
  useEffect(() => {
    exercise().then(
      (result) => {
        console.log(`expo-secure-store: ${result}`);

        if (result === "stored") {
          TurboModuleRegistry.getEnforcing<DevSettingsSpec>("DevSettings").reload();
        }
      },
      (error: unknown) => {
        console.log(`expo-secure-store: FAILED ${String(error)}`);
      },
    );
  }, []);

  return <View />;
};

AppRegistry.registerComponent("ExpoSecureStore", () => ExpoSecureStoreApp);
AppRegistry.runApplication("ExpoSecureStore", { initialProps: {}, rootTag: 1 });
