import { useEffect } from "react";
import { AppRegistry, Pressable, ScrollView, View } from "react-native";

const Control = ({
  name,
  disabled = false,
  hidden = false,
}: {
  name: string;
  disabled?: boolean;
  hidden?: boolean;
}): React.JSX.Element => (
  <Pressable
    disabled={disabled}
    onBlur={() => console.log(`focus-containers: blur ${name}`)}
    onFocus={() => console.log(`focus-containers: focus ${name}`)}
    onPress={() => console.log(`focus-containers: press ${name}`)}
    style={{ backgroundColor: "#3b82f6", display: hidden ? "none" : "flex", height: 60, marginBottom: 10 }}
  />
);

const App = (): React.JSX.Element => {
  useEffect(() => {
    console.log("focus-containers: committed");
  }, []);

  return (
    <View style={{ flex: 1, padding: 40 }}>
      <Control name="before" />
      <ScrollView style={{ height: 150, flexGrow: 0, marginBottom: 10 }}>
        <View>
          <Control name="first" />
          <Control name="disabled" disabled />
          <Control name="hidden" hidden />
          <Control name="second" />
        </View>
      </ScrollView>
      <Control name="after" />
    </View>
  );
};

AppRegistry.registerComponent("FocusContainers", () => App);
AppRegistry.runApplication("FocusContainers", { initialProps: {}, rootTag: 1 });
