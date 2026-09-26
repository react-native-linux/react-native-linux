import { PlatformColor, normalizeColorObject, processColorObject } from "./PlatformColorValueTypes.linux.ts";
import { afterEach, describe, expect, it } from "vitest";

const labelColorArgb = -14_999_773;
const resolveLabelColor = (name: string): number | null => (name === "labelColor" ? labelColorArgb : null);

describe("PlatformColorValueTypes.linux", () => {
  afterEach(() => {
    Reflect.deleteProperty(globalThis, "__rnlPlatformColor");
  });

  it("resolves the first name the platform knows, through the host function by default", () => {
    Reflect.set(globalThis, "__rnlPlatformColor", resolveLabelColor);

    expect(processColorObject(PlatformColor("accentColorThatDoesNotExist", "labelColor"))).toBe(labelColorArgb);
  });

  it("throws naming every name tried when none resolves", () => {
    expect(() => processColorObject(PlatformColor("nope", "neither"), resolveLabelColor)).toThrow(
      "PlatformColor('nope', 'neither'): none of these is a Linux platform color",
    );
  });

  it("throws when the host function is not installed at all", () => {
    expect(() => processColorObject(PlatformColor("labelColor"))).toThrow("none of these is a Linux platform color");
  });

  it.each([[{ semantic: ["labelColor"] }], ["#fff"], [null]])(
    "leaves %j to the rest of the colour pipeline",
    (color) => {
      expect(normalizeColorObject(color)).toBeNull();
      expect(processColorObject(color, resolveLabelColor)).toBeNull();
    },
  );

  it("normalizes its own value to itself", () => {
    const color = PlatformColor("labelColor");

    expect(normalizeColorObject(color)).toBe(color);
  });
});
