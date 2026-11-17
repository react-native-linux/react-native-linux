import { describe, expect, it } from "vitest";
import { describeMissingHarnessAutolinking } from "./harness-autolinking.ts";

const cacheWith = (autolinkingCMake: string): string =>
  `CMAKE_BUILD_TYPE:STRING=Debug\nRNL_AUTOLINKING_CMAKE:FILEPATH=${autolinkingCMake}\nRNL_ENABLE_SKIA:BOOL=ON\n`;

describe("describeMissingHarnessAutolinking", () => {
  it("accepts a window configured with the harness's autolinking", () =>
    expect(
      describeMissingHarnessAutolinking(cacheWith("/repository/build/autolinking-harness/rnl_autolinking.cmake")),
    ).toBeNull());

  it.each([
    ["configured without autolinking", cacheWith("")],
    ["configured with another app's autolinking", cacheWith("/repository/build/autolinking-dev/rnl_autolinking.cmake")],
    ["never configured", null],
  ])("names the script and the configure flag for a window %s", (_case, cache) =>
    expect(describeMissingHarnessAutolinking(cache)).toContain(
      '"node scripts/autolink-harness.ts build/autolinking-harness", then "cmake --preset dev -DRNL_AUTOLINKING_CMAKE=',
    ),
  );
});
