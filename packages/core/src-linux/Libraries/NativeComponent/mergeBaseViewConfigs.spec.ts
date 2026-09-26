import { describe, expect, it } from "vitest";
import { mergeBaseViewConfigs } from "./mergeBaseViewConfigs.ts";

describe("mergeBaseViewConfigs", () => {
  it("keeps every key either platform declares, and Android's value where both do", () => {
    const merged = mergeBaseViewConfigs(
      {
        bubblingEventTypes: { topPress: "android" },
        directEventTypes: { topLayout: "android" },
        validAttributes: { outlineColor: true, transform: "android" },
      },
      {
        bubblingEventTypes: { topPress: "ios", topTouchStart: "ios" },
        directEventTypes: { topAccessibilityAction: "ios" },
        validAttributes: { cursor: true, transform: "ios" },
      },
    );

    expect(merged).toStrictEqual({
      bubblingEventTypes: { topPress: "android", topTouchStart: "ios" },
      directEventTypes: { topAccessibilityAction: "ios", topLayout: "android" },
      validAttributes: { cursor: true, outlineColor: true, transform: "android" },
    });
  });

  it("merges configs that declare nothing", () => {
    expect(mergeBaseViewConfigs({}, {})).toStrictEqual({
      bubblingEventTypes: {},
      directEventTypes: {},
      validAttributes: {},
    });
  });
});
