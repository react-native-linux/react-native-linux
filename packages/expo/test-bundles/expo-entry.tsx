// Expo CLI's Metro config prepends this to every bundle; this platform's does not yet (#687).
import "expo/virtual/streams.js";
import { Text, View } from "react-native";
import { Asset } from "expo-asset";
import { registerRootComponent } from "expo";
import { useEffect } from "react";

const LOCAL_FILE = "file:///proc/self/comm";

const settle = async (probe: () => Promise<string>): Promise<string> => {
  try {
    return await probe();
  } catch (error) {
    return String(error);
  }
};

const readThroughFetch = async (): Promise<string> => {
  const response = await fetch(LOCAL_FILE);

  return `${String(response.status)} ${(await response.text()).trim()}`;
};

const streamThroughFetch = async (): Promise<string> => {
  const response = await fetch(LOCAL_FILE);
  const { body } = response;

  if (body === null) {
    return "no body";
  }

  const reader = body.getReader();
  let streamed = "";

  for (let chunk = await reader.read(); !chunk.done; chunk = await reader.read()) {
    streamed += String.fromCodePoint(...chunk.value);
  }

  return streamed.trim();
};

const downloadAsset = async (): Promise<string> =>
  String((await Asset.fromURI(LOCAL_FILE).downloadAsync()).localUri);

const ExpoEntryApp = (): React.JSX.Element => {
  useEffect(() => {
    void Promise.all([settle(readThroughFetch), settle(streamThroughFetch), settle(downloadAsset)]).then(
      ([fetched, streamed, downloaded]) => {
        console.log(`expo-entry: rendered fetch=${fetched} stream=${streamed} asset=${downloaded}`);
      },
    );
  }, []);

  return (
    <View
      style={{ backgroundColor: "#ffffff", height: 120, left: 40, position: "absolute", top: 40, width: 240 }}
      testID="expo-entry"
    >
      <Text style={{ height: 24, left: 16, position: "absolute", top: 16, width: 200 }}>registerRootComponent</Text>
    </View>
  );
};

registerRootComponent(ExpoEntryApp);
