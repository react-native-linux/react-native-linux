import { AppRegistry, StyleSheet, Text, View } from "react-native";
import { SafeAreaProvider, SafeAreaView, useSafeAreaFrame, useSafeAreaInsets } from "react-native-safe-area-context";

// #806: a SafeAreaProvider without initialMetrics, as T3 Code's App.tsx and the Expo template mount it, renders its
// children only once its native view sends insetsChange. The child reports what the provider handed it.

const styles = StyleSheet.create({
  block: { backgroundColor: "#61afef", height: 120, width: 240 },
  label: { color: "#ffffff", fontSize: 20 },
  safeArea: { backgroundColor: "#1e2430", flex: 1, padding: 24 },
});

const SafeAreaChild = (): React.JSX.Element => {
  const { bottom, left, right, top } = useSafeAreaInsets();
  const { height, width, x, y } = useSafeAreaFrame();

  return (
    <View
      onLayout={() => {
        console.log(`safe-area-provider: insets ${top},${right},${bottom},${left} frame ${x},${y} ${width}x${height}`);
      }}
      style={styles.block}
    >
      <Text style={styles.label}>Inside the safe area</Text>
    </View>
  );
};

const SafeAreaProviderApp = (): React.JSX.Element => (
  <SafeAreaProvider>
    <SafeAreaView style={styles.safeArea}>
      <SafeAreaChild />
    </SafeAreaView>
  </SafeAreaProvider>
);

AppRegistry.registerComponent("SafeAreaProvider", () => SafeAreaProviderApp);
AppRegistry.runApplication("SafeAreaProvider", { initialProps: {}, rootTag: 1 });
