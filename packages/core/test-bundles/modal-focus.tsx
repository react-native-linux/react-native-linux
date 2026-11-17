// ADR-0005's input slice (#795): React Native's own <Modal>, declared inside the page's <ScrollView> as an app
// declares one inside a list row, over a draft <TextInput>.
//
// `modal-focus-keyboard` opens it with the keyboard: Tab and Shift+Tab cycle inside it only, Escape is its
// `onRequestClose` and no back press, focus returns to the invoker, and the draft beneath keeps its text.
// `modal-focus-wheel` turns the wheel over the modal's list, which must scroll and leave the page beneath still.
import { useEffect, useState } from "react";
import { AppRegistry, BackHandler, Modal, Pressable, ScrollView, TextInput, View } from "react-native";

const log = (line: string): void => {
  console.log(`modal-focus: ${line}`);
};

type ControlProps = { readonly name: string; readonly onPress?: () => void };

const Control = ({ name, onPress }: ControlProps): React.JSX.Element => (
  <Pressable
    onBlur={() => log(`blur ${name}`)}
    onFocus={() => log(`focus ${name}`)}
    onPress={onPress}
    style={{ backgroundColor: "#3b82f6", height: 48, margin: 8, width: 240 }}
  />
);

const rows = (): React.JSX.Element[] =>
  Array.from({ length: 40 }, (_, index) => (
    <View key={index} style={{ backgroundColor: index % 2 === 0 ? "#98c379" : "#61afef", height: 40 }} />
  ));

const App = (): React.JSX.Element => {
  const [isOpen, setIsOpen] = useState(false);

  useEffect(() => {
    const subscription = BackHandler.addEventListener("hardwareBackPress", () => {
      log("back");

      return false;
    });

    log("committed");

    return () => subscription.remove();
  }, []);

  return (
    <View style={{ backgroundColor: "#1e2430", flex: 1 }}>
      <TextInput
        onChangeText={(text) => log(`draft ${text}`)}
        style={{ backgroundColor: "#ffffff", height: 40, margin: 8, width: 300 }}
      />
      <Control name="open" onPress={() => setIsOpen(true)} />
      <ScrollView onScroll={() => log("page scroll")} style={{ flex: 1 }}>
        {rows()}
        <Modal
          animationType="none"
          onRequestClose={() => {
            log("request close");
            setIsOpen(false);
          }}
          onShow={() => log("show")}
          transparent
          visible={isOpen}
        >
          <View style={{ backgroundColor: "rgba(0, 0, 0, 0.5)", flex: 1, padding: 80 }}>
            <Control name="first" />
            <Control name="second" />
            <ScrollView onScroll={() => log("list scroll")} style={{ backgroundColor: "#f5f5f5", height: 200 }}>
              {rows()}
            </ScrollView>
          </View>
        </Modal>
      </ScrollView>
    </View>
  );
};

AppRegistry.registerComponent("ModalFocus", () => App);
AppRegistry.runApplication("ModalFocus", { initialProps: {}, rootTag: 1 });
