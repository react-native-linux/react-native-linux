import { Text, View } from "react-native";

const greeting = "Welcome to HelloWorld";

export const App = (): React.JSX.Element => (
  <View style={{ alignItems: "center", backgroundColor: "#0f172a", flex: 1, justifyContent: "center" }}>
    <Text style={{ color: "#f8fafc", fontSize: 32 }}>{greeting}</Text>
  </View>
);
