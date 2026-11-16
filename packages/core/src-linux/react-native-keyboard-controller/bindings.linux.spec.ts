import { describe, expect, it, vi } from "vitest";
import { createRequire } from "node:module";
import path from "node:path";
import { readFileSync } from "node:fs";

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
  it("never fires a keyboard listener, and removes it", () => {
    expect(() => {
      bindings.KeyboardEvents.addListener().remove();
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
});

describe("react-native-keyboard-controller's linux focused input events", () => {
  it("completes a layout synchronization asynchronously, for the listener KeyboardProvider subscribes after it", async () => {
    const listener = vi.fn();

    bindings.KeyboardControllerViewCommands.synchronizeFocusedInputLayout();
    const subscription = bindings.FocusedInputEvents.addListener("layoutDidSynchronize", listener);
    expect(listener).not.toHaveBeenCalled();
    await Promise.resolve();
    subscription.remove();

    expect(listener).toHaveBeenCalledOnce();
  });

  it.each(["focusDidSet", "layoutDidSynchronize"])("never calls a removed %s listener", async (name) => {
    const listener = vi.fn();

    bindings.FocusedInputEvents.addListener(name, listener).remove();
    bindings.KeyboardControllerViewCommands.synchronizeFocusedInputLayout();
    await Promise.resolve();

    expect(listener).not.toHaveBeenCalled();
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

describe("react-native-keyboard-controller's linux bindings", () => {
  it("export every binding the library's own no-native fallback exports, so a library upgrade that adds one fails here", () => {
    const libraryDirectory = path.dirname(
      createRequire(import.meta.url).resolve("react-native-keyboard-controller/package.json"),
    );
    const fallbackSource = readFileSync(path.join(libraryDirectory, "src", "bindings.ts"), "utf8");
    const fallbackExports = Array.from(
      fallbackSource.matchAll(/^export const (?<name>\w+)/gmu),
      (match) => match.groups?.["name"],
    );

    expect(Object.keys(bindings).toSorted()).toStrictEqual(fallbackExports.toSorted());
  });
});
