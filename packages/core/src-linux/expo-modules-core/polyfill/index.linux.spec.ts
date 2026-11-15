import { afterEach, describe, expect, it, vi } from "vitest";

await import("./index.linux.ts");

const registeredComponentNames = new Set([
  "View",
  "ViewManagerAdapter_ExpoBlur_ExpoBlurView",
  "ViewManagerAdapter_Solo",
]);

const bindRegistry = (): void => {
  vi.stubGlobal("__nativeComponentRegistry__hasComponent", (name: string) => registeredComponentNames.has(name));
};

describe("the linux globalThis.expo", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("is upstream's polyfill, installed", () => {
    expect(Reflect.get(globalThis.expo, "EventEmitter")).toBeTypeOf("function");
  });

  it("answers a view config for a view whose host component is registered", () => {
    bindRegistry();

    expect(globalThis.expo.getViewConfig("ExpoBlur", "ExpoBlurView")).toStrictEqual({
      directEventTypes: {},
      validAttributes: {},
    });
  });

  it("names a module's default view without a view suffix", () => {
    bindRegistry();

    expect(globalThis.expo.getViewConfig("Solo")).not.toBeNull();
  });

  it("names a module's default view without a view suffix when the view name is empty", () => {
    bindRegistry();

    expect(globalThis.expo.getViewConfig("Solo", "")).not.toBeNull();
  });

  it("answers null, upstream's answer for a view that is not linked, when no component is registered", () => {
    bindRegistry();

    expect(globalThis.expo.getViewConfig("T3NativeControls", "FrostedCutout")).toBeNull();
  });

  it("answers null where no host binds the component registry", () => {
    expect(globalThis.expo.getViewConfig("ExpoBlur", "ExpoBlurView")).toBeNull();
  });
});
