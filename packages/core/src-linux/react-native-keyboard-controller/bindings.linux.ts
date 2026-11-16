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

const layoutSynchronizedListeners = new Set<() => void>();

const FocusedInputEvents = {
  addListener: (name: string, listener: () => void): Pick<EmitterSubscription, "remove"> => {
    if (name !== "layoutDidSynchronize") {
      return subscriptionThatNeverFires();
    }
    layoutSynchronizedListeners.add(listener);
    return {
      remove: () => {
        layoutSynchronizedListeners.delete(listener);
      },
    };
  },
};

const WindowDimensionsEvents = {
  addListener: (_name: "windowDidResize", listener: (size: WindowSize) => void): Pick<EmitterSubscription, "remove"> =>
    Dimensions.addEventListener("change", ({ window }: { window: WindowSize }) => {
      listener({ height: window.height, width: window.width });
    }),
};

/** KeyboardProvider's `update()` subscribes to `layoutDidSynchronize` only after it sends this command. */
const KeyboardControllerViewCommands = {
  synchronizeFocusedInputLayout: (): void => {
    queueMicrotask(() => {
      for (const listener of layoutSynchronizedListeners) {
        listener();
      }
    });
  },
};

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
