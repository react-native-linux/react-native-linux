import { AppRegistry, Pressable, TextInput, View } from "react-native";
import { useRef, useState } from "react";

const FocusRemountApp = (): React.JSX.Element => {
  const input = useRef<TextInput>(null);
  const [generation, setGeneration] = useState(0);

  return (
    <View style={{ flex: 1 }}>
      <TextInput
        key={generation}
        onChangeText={(text) => console.log(`focus-remount: text=${text} generation=${generation}`)}
        onFocus={() => console.log(`focus-remount: focused=${generation}`)}
        onLayout={() => console.log(`focus-remount: mounted=${generation}`)}
        ref={input}
        style={{ backgroundColor: "#ffffff", height: 80, left: 100, position: "absolute", top: 100, width: 400 }}
      />
      <Pressable
        onPress={() => {
          console.log(`focus-remount: requested=${generation}`);
          input.current?.focus();
        }}
        style={{ backgroundColor: "#3b82f6", height: 60, left: 100, position: "absolute", top: 220, width: 80 }}
      />
      <Pressable
        onPress={() => setGeneration(generation + 1)}
        style={{ backgroundColor: "#22c55e", height: 60, left: 200, position: "absolute", top: 220, width: 80 }}
      />
    </View>
  );
};

AppRegistry.registerComponent("FocusRemount", () => FocusRemountApp);
AppRegistry.runApplication("FocusRemount", { initialProps: {}, rootTag: 1 });
