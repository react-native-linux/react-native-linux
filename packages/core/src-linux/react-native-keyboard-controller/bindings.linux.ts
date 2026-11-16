import { Dimensions, TextInput } from "react-native";
import type { EmitterSubscription } from "react-native";

interface WindowSize {
  readonly height: number;
  readonly width: number;
}

const doNothing = (): null => null;

const subscriptionThatNeverFires = (): Pick<EmitterSubscription, "remove"> => ({ remove: doNothing });

const KeyboardControllerNative = {
  addListener: doNothing,
  dismiss: (): void => {
    TextInput.State.blurTextInput(TextInput.State.currentlyFocusedInput());
  },
  getConstants: (): { keyboardBorderRadius: number } => ({ keyboardBorderRadius: 0 }),
  preload: doNothing,
  removeListeners: doNothing,
  setDefaultMode: doNothing,
  setFocusTo: doNothing,
  setInputMode: doNothing,
  setTranslucent: doNothing,
  viewPositionInWindow: (): Promise<never> =>
    Promise.reject(new Error("react-native-keyboard-controller: viewPositionInWindow has no Linux answer")),
};

const KeyboardEvents = { addListener: subscriptionThatNeverFires };

const FocusedInputEvents = { addListener: subscriptionThatNeverFires };

const WindowDimensionsEvents = {
  addListener: (_name: "windowDidResize", listener: (size: WindowSize) => void): Pick<EmitterSubscription, "remove"> =>
    Dimensions.addEventListener("change", ({ window }: { window: WindowSize }) => {
      listener({ height: window.height, width: window.width });
    }),
};

const KeyboardControllerViewCommands = { synchronizeFocusedInputLayout: doNothing };

export {
  View as ClippingScrollView,
  View as KeyboardBackgroundView,
  View as KeyboardControllerView,
  View as KeyboardGestureArea,
  View as RCTKeyboardExtender,
  View as RCTKeyboardToolbarGroupView,
  View as RCTOverKeyboardView,
} from "react-native";
export {
  FocusedInputEvents,
  KeyboardControllerNative,
  KeyboardControllerViewCommands,
  KeyboardEvents,
  WindowDimensionsEvents,
};
