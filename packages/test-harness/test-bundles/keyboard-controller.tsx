import { AppRegistry, StyleSheet, TextInput, View } from "react-native";
import type { LayoutChangeEvent } from "react-native";
import {
  KeyboardAvoidingView,
  KeyboardController,
  KeyboardEvents,
  KeyboardProvider,
  KeyboardStickyView,
  useKeyboardHandler,
  useKeyboardState,
  useReanimatedFocusedInput,
  useReanimatedKeyboardAnimation,
} from "react-native-keyboard-controller";
import { useAnimatedReaction } from "react-native-reanimated";
import { scheduleOnRN } from "react-native-worklets";

const styles = StyleSheet.create({
  avoiding: { flex: 1, justifyContent: "flex-end" },
  input: { backgroundColor: "#ffffff", color: "#000000", height: 60, width: 400 },
  root: { backgroundColor: "#e5e7eb", flex: 1, paddingTop: 60 },
});

const report = (message: string): void => {
  console.log(`keyboard-controller: ${message}`);
};

const firstFrames = new Map<string, string>();
let reportedState = "";

const reportFrame =
  (name: string) =>
  (event: LayoutChangeEvent): void => {
    const { height, width, x, y } = event.nativeEvent.layout;
    const frame = `${x},${y} ${width}x${height}`;
    const firstFrame = firstFrames.get(name);

    if (firstFrame === undefined) {
      firstFrames.set(name, frame);
      report(`${name} ${frame}`);
    } else if (firstFrame !== frame) {
      report(`${name} shifted from ${firstFrame} to ${frame}`);
    }
  };

for (const eventName of ["keyboardWillShow", "keyboardDidShow", "keyboardWillHide", "keyboardDidHide"] as const) {
  KeyboardEvents.addListener(eventName, () => report(`event ${eventName}`));
}

const KeyboardStatus = (): null => {
  const height = useKeyboardState((state) => state.height);
  const isVisible = useKeyboardState((state) => state.isVisible);
  const reanimatedHeight = useReanimatedKeyboardAnimation().height;

  useKeyboardHandler({
    onEnd: () => {
      "worklet";
      scheduleOnRN(report, "handler onEnd");
    },
    onMove: () => {
      "worklet";
      scheduleOnRN(report, "handler onMove");
    },
    onStart: () => {
      "worklet";
      scheduleOnRN(report, "handler onStart");
    },
  });
  useAnimatedReaction(
    () => reanimatedHeight.value,
    (value) => {
      scheduleOnRN(report, `reanimated height=${value}`);
    },
  );
  const state = `state height=${height} visible=${isVisible} isVisible()=${KeyboardController.isVisible()}`;

  if (state !== reportedState) {
    reportedState = state;
    report(state);
  }

  return null;
};

const dismiss = (): void => {
  KeyboardController.dismiss()
    .then(() => report(`dismissed isVisible()=${KeyboardController.isVisible()}`))
    .catch((error: unknown) => report(`dismiss failed ${String(error)}`));
};

const inputEvents = (name: string): Partial<React.ComponentProps<typeof TextInput>> => ({
  onBlur: () => report(`${name} blur`),
  onChangeText: (text: string) => report(`${name} text=${text}`),
  onFocus: () => report(`${name} focus`),
  onLayout: reportFrame(`${name} input`),
});

const AvoidingInput = (): React.JSX.Element => {
  const { update } = useReanimatedFocusedInput();
  const onFocus = (): void => {
    report("avoiding focus");
    update()
      .then(() => report("focused input update resolved"))
      .catch((error: unknown) => report(`focused input update failed ${String(error)}`));
  };

  return <TextInput {...inputEvents("avoiding")} onFocus={onFocus} style={styles.input} />;
};

const KeyboardControllerApp = (): React.JSX.Element => (
  <KeyboardProvider>
    <View onLayout={() => report("mounted")} style={styles.root}>
      <KeyboardStatus />
      <KeyboardAvoidingView automaticOffset behavior="padding" onLayout={reportFrame("avoiding")} style={styles.avoiding}>
        <AvoidingInput />
      </KeyboardAvoidingView>
      <KeyboardStickyView onLayout={reportFrame("sticky")}>
        <TextInput {...inputEvents("sticky")} onSubmitEditing={dismiss} style={styles.input} submitBehavior="submit" />
      </KeyboardStickyView>
    </View>
  </KeyboardProvider>
);

AppRegistry.registerComponent("KeyboardController", () => KeyboardControllerApp);
AppRegistry.runApplication("KeyboardController", { initialProps: {}, rootTag: 1 });
