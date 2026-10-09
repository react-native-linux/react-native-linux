import { expect, it, vi } from "vitest";

vi.mock("react-native", () => ({ Platform: { OS: "linux" } }));

const modulePath = "../upstream/packages/react-native-gesture-handler/src/web/handlers/NativeViewGestureHandler.ts";
const handlerTag = 1;
const delegate = { init: vi.fn(), onEnabledChange: vi.fn(), view: {} };

it("attaches and updates a Native recognizer without browser APIs on the Linux view", async () => {
  const loaded: unknown = await import(modulePath);

  if (typeof loaded !== "object" || loaded === null || !("default" in loaded) || typeof loaded.default !== "function") {
    throw new TypeError("NativeViewGestureHandler must export its constructor");
  }

  const handler: unknown = Reflect.construct(loaded.default, [delegate]);

  if (
    typeof handler !== "object" ||
    handler === null ||
    !("init" in handler) ||
    typeof handler.init !== "function" ||
    !("updateGestureConfig" in handler) ||
    typeof handler.updateGestureConfig !== "function" ||
    !("isButton" in handler) ||
    typeof handler.isButton !== "function"
  ) {
    throw new TypeError("NativeViewGestureHandler must expose its lifecycle and button classification");
  }

  handler.init(handlerTag, { current: { onGestureHandlerEvent: vi.fn(), onGestureHandlerStateChange: vi.fn() } });
  handler.updateGestureConfig({ enabled: true });

  expect(delegate.onEnabledChange).toHaveBeenCalledWith(true);
  expect(handler.isButton()).toBe(false);
});
