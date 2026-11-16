import { describe, expect, it, vi } from "vitest";

type DimensionsChange = (change: { window: { height: number; width: number } }) => void;

const { dimensions, textInputState, view } = vi.hoisted(() => ({
  dimensions: { addEventListener: vi.fn() },
  textInputState: { blurTextInput: vi.fn(), currentlyFocusedInput: vi.fn() },
  view: { displayName: "View" },
}));

vi.mock("react-native", () => ({ Dimensions: dimensions, TextInput: { State: textInputState }, View: view }));

const bindings = await import("./bindings.linux.ts");

describe("react-native-keyboard-controller's linux native module", () => {
  it("dismisses the keyboard by blurring the focused input", () => {
    const focusedInput = { blur: vi.fn() };
    textInputState.currentlyFocusedInput.mockReturnValueOnce(focusedInput);

    bindings.KeyboardControllerNative.dismiss();

    expect(textInputState.blurTextInput).toHaveBeenCalledWith(focusedInput);
  });

  it("reports a keyboard with no rounded corners", () => {
    expect(bindings.KeyboardControllerNative.getConstants()).toStrictEqual({ keyboardBorderRadius: 0 });
  });

  it.each([
    "addListener",
    "preload",
    "removeListeners",
    "setDefaultMode",
    "setFocusTo",
    "setInputMode",
    "setTranslucent",
  ] as const)("ignores %s", (method) => {
    expect(() => {
      bindings.KeyboardControllerNative[method]();
    }).not.toThrow();
  });

  it("rejects viewPositionInWindow, so KeyboardAvoidingView keeps the frame its onLayout reported", async () => {
    await expect(bindings.KeyboardControllerNative.viewPositionInWindow()).rejects.toThrow("no Linux answer");
  });
});

describe("react-native-keyboard-controller's linux events", () => {
  it.each([
    ["KeyboardEvents", bindings.KeyboardEvents],
    ["FocusedInputEvents", bindings.FocusedInputEvents],
  ])("never fires a %s listener, and removes it", (_name, events) => {
    expect(() => {
      events.addListener().remove();
    }).not.toThrow();
  });

  it("forwards the window's resizes", () => {
    const subscription = { remove: vi.fn() };
    const listener = vi.fn();
    const handlers: DimensionsChange[] = [];
    dimensions.addEventListener.mockImplementationOnce((_eventName: string, handler: DimensionsChange) => {
      handlers.push(handler);
      return subscription;
    });

    expect(bindings.WindowDimensionsEvents.addListener("windowDidResize", listener)).toBe(subscription);
    for (const handler of handlers) {
      handler({ window: { height: 600, width: 800 } });
    }

    expect(dimensions.addEventListener).toHaveBeenCalledWith("change", expect.any(Function));
    expect(listener).toHaveBeenCalledWith({ height: 600, width: 800 });
  });

  it("synchronizes no focused input layout", () => {
    expect(() => {
      bindings.KeyboardControllerViewCommands.synchronizeFocusedInputLayout();
    }).not.toThrow();
  });
});

describe("react-native-keyboard-controller's linux views", () => {
  it.each([
    "ClippingScrollView",
    "KeyboardBackgroundView",
    "KeyboardControllerView",
    "KeyboardGestureArea",
    "RCTKeyboardExtender",
    "RCTKeyboardToolbarGroupView",
    "RCTOverKeyboardView",
  ] as const)("mounts %s as a View", (name) => {
    expect(bindings[name]).toBe(view);
  });
});
