import { describe, expect, it } from "vitest";

import { describeAnimationRung } from "./animation-rung.ts";

describe("describeAnimationRung", () => {
  it("states that animations run on the JavaScript thread and names the bring-up rung issue", () => {
    expect(describeAnimationRung()).toBe(
      "Reanimated runs on the JavaScript thread on linux (bring-up rung, #178); native worklets: #134–#138",
    );
  });
});
