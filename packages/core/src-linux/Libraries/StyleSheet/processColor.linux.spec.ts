import { afterEach, describe, expect, it } from "vitest";
import { PlatformColor } from "./PlatformColorValueTypes.linux.ts";
import processColor from "./processColor.linux.ts";

const labelColorArgb = -14_999_773;
const opaqueRedArgb = 4_294_901_760;
const definedTransparent = 1;
const halfBlueArgb = 2_147_483_903;
const transparentBlackNumber = 0;

describe("processColor.linux", () => {
  afterEach(() => {
    Reflect.deleteProperty(globalThis, "__rnlPlatformColor");
  });

  it("reorders a CSS colour to the 0xaarrggbb C++ reads", () => {
    expect(processColor("red")).toBe(opaqueRedArgb);
    expect(processColor("rgba(0, 0, 255, 0.5)")).toBe(halfBlueArgb);
  });

  it("sends transparent black as a defined, invisible colour instead of the undefined one", () => {
    expect(processColor("transparent")).toBe(definedTransparent);
    expect(processColor(transparentBlackNumber)).toBe(definedTransparent);
  });

  it("answers null for an unset colour and for one it cannot read", () => {
    expect(processColor(null)).toBeNull();
    expect(processColor("not a colour")).toBeNull();
    expect(processColor({ semantic: ["labelColor"] })).toBeNull();
  });

  it("resolves a PlatformColor through the linux overlay", () => {
    Reflect.set(globalThis, "__rnlPlatformColor", (name: string) => (name === "labelColor" ? labelColorArgb : null));

    expect(processColor(PlatformColor("labelColor"))).toBe(labelColorArgb);
  });
});
