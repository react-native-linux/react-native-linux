// ADR-0005's Alert slice (#796): React Native's own Alert, which resolves to Alert.linux.ts and draws through the
// AppContainer overlay's host as a card on a transparent <Modal>.
//
// An alert is raised before the root mounts, so every scenario first sees it and dismisses its lone OK with Escape.
// The launchers then open one alert each:
//
//   two          Cancel and a destructive Discard: Escape presses Cancel, the alert-two-buttons golden
//   three        Don't Save, Cancel and a preferred Save: Tab cycles inside it, the alert-three-buttons golden
//   destructive  one destructive Erase, not cancelable: Enter and Escape do nothing
//   prompt       a plain-text prompt: typing reaches its field without a click, and Enter presses Rename; the
//                alert-prompt golden
//   dark         userInterfaceStyle "dark": Enter presses the focused default, Archive; the alert-dark golden
//   dismiss      cancelable, with no cancel button: Escape calls onDismiss and no button
import { Alert, AppRegistry, Pressable, Text, View } from "react-native";
import { useEffect } from "react";

const log = (line: string): void => {
  console.log(`alert: ${line}`);
};

const launchers: Readonly<Record<string, () => void>> = {
  destructive: () =>
    Alert.alert("Erase this disk?", "Everything on it is deleted.", [
      { onPress: () => log("press erase"), style: "destructive", text: "Erase" },
    ]),
  prompt: () =>
    Alert.prompt("Rename thread", "Enter a new name.", [
      { onPress: () => log("press prompt cancel"), style: "cancel", text: "Cancel" },
      { onPress: (value) => log(`press rename ${String(value)}`), text: "Rename" },
    ]),
  three: () =>
    Alert.alert("Save changes to “Notes”?", "Your changes will be lost if you don't save them.", [
      { onPress: () => log("press dont-save"), style: "destructive", text: "Don't Save" },
      { onPress: () => log("press cancel"), style: "cancel", text: "Cancel" },
      { isPreferred: true, onPress: () => log("press save"), text: "Save" },
    ]),
  two: () =>
    Alert.alert("Discard draft?", "This draft will be removed from the thread.", [
      { onPress: () => log("press cancel"), style: "cancel", text: "Cancel" },
      { onPress: () => log("press discard"), style: "destructive", text: "Discard" },
    ]),
  dark: () =>
    Alert.alert(
      "Archive thread?",
      "You can find it later in Archived.",
      [
        { onPress: () => log("press dark cancel"), style: "cancel", text: "Cancel" },
        { onPress: () => log("press archive"), text: "Archive" },
      ],
      { userInterfaceStyle: "dark" },
    ),
  dismiss: () =>
    Alert.alert(
      "Leave this page?",
      "Your draft stays in the thread.",
      [
        { onPress: () => log("press leave"), style: "destructive", text: "Leave" },
        { onPress: () => log("press stay"), text: "Stay" },
      ],
      { cancelable: true, onDismiss: () => log("dismiss") },
    ),
};

Alert.alert("Raised before mount", "This alert was raised before the root mounted.", [
  { onPress: () => log("press early ok"), text: "OK" },
]);

const App = (): React.JSX.Element => {
  useEffect(() => {
    log("committed");
  }, []);

  return (
    <View style={{ backgroundColor: "#1e2430", flex: 1, gap: 10, padding: 20 }}>
      {Object.entries(launchers).map(([name, open]) => (
        <Pressable
          key={name}
          onFocus={() => log(`focus launcher ${name}`)}
          onPress={() => {
            log(`launch ${name}`);
            open();
          }}
          style={{ backgroundColor: "#3b82f6", height: 40, justifyContent: "center", paddingHorizontal: 12, width: 200 }}
        >
          <Text style={{ color: "#ffffff" }}>{name}</Text>
        </Pressable>
      ))}
    </View>
  );
};

AppRegistry.registerComponent("AlertApp", () => App);
AppRegistry.runApplication("AlertApp", { initialProps: {}, rootTag: 1 });
