import { describe, expect, it } from "vitest";
import { planCoreBinary } from "./core-binary-plan.ts";

describe("planCoreBinary", () => {
  it("stages the binary, its two shared objects and the fonts flat in bin", () => {
    expect(planCoreBinary("/build/release", "/core/fonts", "/core/bin")).toStrictEqual([
      { from: "/build/release/bin/rnl_window", to: "/core/bin/rnl_window" },
      { from: "/build/release/_deps/hermes-build/lib/libhermesvm.so", to: "/core/bin/libhermesvm.so" },
      { from: "/build/release/_deps/hermes-build/jsi/libjsi.so", to: "/core/bin/libjsi.so" },
      { from: "/core/fonts", to: "/core/bin/fonts" },
    ]);
  });
});
