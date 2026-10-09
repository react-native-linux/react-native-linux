import { AppRegistry, Pressable, TextInput, View } from "react-native";
import { useRef } from "react";

const TextInputApp = (): React.JSX.Element => {
  const input = useRef<TextInput>(null);

  return (
    <View onLayout={() => console.log("public-text-input: committed")} style={{ flex: 1 }}>
      <TextInput
        defaultValue="Hello"
        onBlur={() => console.log("public-text-input: blur")}
        onChangeText={(text) => console.log(`public-text-input: text=${text}`)}
        onFocus={() => console.log("public-text-input: focus")}
        ref={input}
        style={{
          backgroundColor: "#ffffff",
          color: "#000000",
          height: 80,
          left: 100,
          position: "absolute",
          top: 100,
          width: 400,
        }}
      />
      <Pressable
        onPress={() => input.current?.focus()}
        style={{ backgroundColor: "#3b82f6", height: 60, left: 100, position: "absolute", top: 220, width: 80 }}
      />
      <Pressable
        onPress={() => input.current?.blur()}
        style={{ backgroundColor: "#ef4444", height: 60, left: 200, position: "absolute", top: 220, width: 80 }}
      />
      <Pressable
        onPress={() => input.current?.clear()}
        style={{ backgroundColor: "#22c55e", height: 60, left: 300, position: "absolute", top: 220, width: 80 }}
      />
      <Pressable
        onPress={() => input.current?.setSelection(0, 2)}
        style={{ backgroundColor: "#a855f7", height: 60, left: 400, position: "absolute", top: 220, width: 80 }}
      />
    </View>
  );
};

AppRegistry.registerComponent("PublicTextInput", () => TextInputApp);
AppRegistry.runApplication("PublicTextInput", { initialProps: {}, rootTag: 1 });
