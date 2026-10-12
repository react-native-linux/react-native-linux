import { useEffect } from "react";
import { AppRegistry, FlatList, Pressable, View } from "react-native";

const Control = ({ name }: { name: string }): React.JSX.Element => (
  <Pressable
    onBlur={() => console.log(`focus-list: blur ${name}`)}
    onFocus={() => console.log(`focus-list: focus ${name}`)}
    onPress={() => console.log(`focus-list: press ${name}`)}
    style={{ backgroundColor: "#3b82f6", height: 60, marginBottom: 10 }}
  />
);

const App = (): React.JSX.Element => {
  useEffect(() => {
    console.log("focus-list: committed");
  }, []);

  return (
    <View style={{ flex: 1, padding: 40 }}>
      <Control name="before" />
      <FlatList
        data={["first", "second", "third"]}
        keyExtractor={(name) => name}
        renderItem={({ item }) => <Control name={item} />}
        style={{ height: 150, flexGrow: 0, marginBottom: 10 }}
      />
      <Control name="after" />
    </View>
  );
};

AppRegistry.registerComponent("FocusList", () => App);
AppRegistry.runApplication("FocusList", { initialProps: {}, rootTag: 1 });
