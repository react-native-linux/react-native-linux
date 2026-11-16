import * as Application from "expo-application";
import * as Crypto from "expo-crypto";
import * as Device from "expo-device";
import * as Linking from "expo-linking";
import * as Network from "expo-network";
import { Text, View } from "react-native";
import { useEffect } from "react";
import { registerRootComponent } from "expo";

const errorCode = async (probe: () => Promise<unknown>): Promise<string> => {
  try {
    return `resolved ${String(await probe())}`;
  } catch (error) {
    return error instanceof Error && "code" in error ? String(error.code) : String(error);
  }
};

const describeApplication = async (): Promise<string> =>
  [
    Application.applicationId,
    Application.applicationName,
    Application.nativeApplicationVersion,
    Application.nativeBuildVersion,
    await errorCode(Application.getInstallationTimeAsync),
  ]
    .map(String)
    .join(",");

const describeDevice = async (): Promise<string> =>
  [
    Device.deviceType === Device.DeviceType.DESKTOP,
    Device.isDevice,
    typeof Device.osName === "string" && Device.osName.length > 0,
    Device.supportedCpuArchitectures?.length,
    await errorCode(Device.getUptimeAsync),
    await errorCode(Device.getMaxMemoryAsync),
  ].join(",");

const describeNetwork = async (): Promise<string> => {
  const subscription = Network.addNetworkStateListener(() => undefined);

  subscription.remove();

  return [
    JSON.stringify(await Network.getNetworkStateAsync()),
    await errorCode(Network.isAirplaneModeEnabledAsync),
  ].join(",");
};

const hexOf = (bytes: Uint8Array): string => Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");

const describeCrypto = async (): Promise<string> =>
  [
    await Crypto.digestStringAsync(Crypto.CryptoDigestAlgorithm.SHA256, "abc"),
    await Crypto.digestStringAsync(Crypto.CryptoDigestAlgorithm.SHA256, "abc", {
      encoding: Crypto.CryptoEncoding.BASE64,
    }),
    hexOf(new Uint8Array(await Crypto.digest(Crypto.CryptoDigestAlgorithm.MD5, new TextEncoder().encode("abc")))),
    (await Crypto.getRandomBytesAsync(16)).length,
    Crypto.getRandomValues(new Uint8Array(8)).length,
    /^[\da-f]{8}-[\da-f]{4}-4[\da-f]{3}-[89ab][\da-f]{3}-[\da-f]{12}$/u.test(Crypto.randomUUID()),
    typeof Crypto.AESEncryptionKey,
  ].join(",");

const ExpoStartupModulesApp = (): React.JSX.Element => {
  const linkingUrl = Linking.useLinkingURL();

  useEffect(() => {
    void Promise.all([describeApplication(), describeDevice(), describeNetwork(), describeCrypto()]).then(
      ([application, device, network, crypto]) => {
        console.log(
          `expo-startup-modules: rendered linking=${String(linkingUrl)} application=${application} ` +
            `device=${device} network=${network} crypto=${crypto}`,
        );
      },
    );
  }, [linkingUrl]);

  return (
    <View
      style={{ backgroundColor: "#ffffff", height: 120, left: 40, position: "absolute", top: 40, width: 240 }}
      testID="expo-startup-modules"
    >
      <Text style={{ height: 24, left: 16, position: "absolute", top: 16, width: 200 }}>expo startup modules</Text>
    </View>
  );
};

registerRootComponent(ExpoStartupModulesApp);
