import { PlatformColor, StyleSheet, Text, View } from "react-native";

const title = "React Native on Linux";

const styles = StyleSheet.create({
  panel: {
    alignItems: "center",
    backgroundColor: PlatformColor("windowBackgroundColor"),
    flex: 1,
    justifyContent: "center",
  },
  title: { color: PlatformColor("labelColor"), fontSize: 32 },
});

export const App = (): React.JSX.Element => (
  <View style={styles.panel} testID="app">
    <Text style={styles.title}>{title}</Text>
  </View>
);
