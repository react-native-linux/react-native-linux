import { AppRegistry, Text, View } from "react-native";
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

const downloadAsset = async (): Promise<string> =>
  String((await Asset.fromURI(LOCAL_FILE).downloadAsync()).localUri);

const ExpoEntryApp = (): React.JSX.Element => {
  useEffect(() => {
    void Promise.all([settle(readThroughFetch), settle(downloadAsset)]).then(([fetched, downloaded]) => {
      console.log(`expo-entry: rendered fetch=${fetched} asset=${downloaded}`);
    });
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
AppRegistry.runApplication("main", { initialProps: {}, rootTag: 1 });
