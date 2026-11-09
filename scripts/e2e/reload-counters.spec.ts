import { describe, expect, it } from "vitest";
import { describeReloadCounterFailures } from "./reload-counters.ts";

const PROBE = "[rnl-teardown] gpu-resources=7 gpu-bytes=4096";
const ONE_FAILURE = 1;
const bootLines = [
  "[1.000]  -> wl_display@1.get_registry(new id wl_registry@2)",
  '[1.001]  -> wl_registry@2.bind(1, "wl_compositor", 6, new id [unknown]@3)',
  "[1.002]  -> wl_compositor@3.create_surface(new id wl_surface@4)",
  "[1.003]  -> wl_display@1.sync(new id wl_callback@5)",
  "[1.004] wl_callback@5.done(1)",
  "[1.005] wl_display@1.delete_id(5)",
];

describe("describeReloadCounterFailures", () => {
  it("passes when every reload finds the objects and GPU resources the first one did", () => {
    expect(describeReloadCounterFailures([...bootLines, PROBE, "teardown: instance 2 committed", PROBE])).toEqual([]);
  });

  it("does not count the per-frame objects a probe catches in flight", () => {
    const inFlight = [
      "[2.000]  -> wl_surface@4.frame(new id wl_callback@5)",
      "[2.001]  -> wp_presentation@6.feedback(wl_surface@4, new id wp_presentation_feedback@7)",
    ];

    expect(describeReloadCounterFailures([...bootLines, PROBE, ...inFlight, PROBE])).toEqual([]);
  });

  it("forgets an object once the client destroys or releases it, before the compositor deletes its id", () => {
    const createdAndDestroyed = [
      "[2.000]  -> wl_compositor@3.create_surface(new id wl_surface@8)",
      "[2.001]  -> wl_surface@8.destroy()",
      "[2.002]  -> wl_compositor@3.create_region(new id wl_region@9)",
      "[2.003]  -> wl_region@9.release()",
    ];

    expect(describeReloadCounterFailures([...bootLines, PROBE, ...createdAndDestroyed, PROBE])).toEqual([]);
  });
});

describe("describeReloadCounterFailures on a leak", () => {
  it("names the Wayland objects a reload left behind", () => {
    const leaked = ["[2.000]  -> wl_compositor@3.create_surface(new id wl_surface@8)"];

    expect(describeReloadCounterFailures([...bootLines, PROBE, ...leaked, PROBE])).toEqual([
      `reload 2 did not return to reload 1's state: "${PROBE}" with Wayland objects wl_compositor×1 ` +
        `wl_registry×1 wl_surface×2, versus "${PROBE}" with Wayland objects wl_compositor×1 wl_registry×1 ` +
        "wl_surface×1",
    ]);
  });

  it("names GPU resources a reload left behind", () => {
    const grown = "[rnl-teardown] gpu-resources=8 gpu-bytes=8192";

    expect(describeReloadCounterFailures([...bootLines, PROBE, grown])).toHaveLength(ONE_FAILURE);
  });
});

describe("describeReloadCounterFailures on what it does not grade", () => {
  it("ignores server-created objects, whose ids the compositor owns", () => {
    const serverCreated = ["[2.000] wl_data_device@10.data_offer(new id wl_data_offer@4278190080)"];

    expect(describeReloadCounterFailures([...bootLines, PROBE, ...serverCreated, PROBE])).toEqual([]);
  });

  it("fails a scenario that reloaded fewer than twice", () => {
    expect(describeReloadCounterFailures([...bootLines, PROBE])).toEqual([
      'steadyAcrossReloads needs at least 2 "[rnl-teardown]" lines',
    ]);
    expect(describeReloadCounterFailures(bootLines)).toHaveLength(ONE_FAILURE);
  });
});
