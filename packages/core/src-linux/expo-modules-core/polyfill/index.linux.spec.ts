import { describe, expect, it } from "vitest";

await import("./index.linux.ts");

describe("the linux globalThis.expo", () => {
  it("is upstream's polyfill, installed", () => {
    expect(Reflect.get(globalThis.expo, "EventEmitter")).toBeTypeOf("function");
  });

  it("answers null, upstream's answer for a view that is not linked", () => {
    expect(globalThis.expo.getViewConfig("T3NativeControls", "FrostedCutout")).toBeNull();
  });
});
