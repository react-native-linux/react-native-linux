import { afterEach, describe, expect, it, vi } from "vitest";
import { gradeRendererRung } from "./renderer-rung.ts";
import { stdout } from "node:process";

const SOCKET_PATH = "/run/user/1000/rnl-automation-9.sock";
const FALLBACK_REASON = "the rungs above it failed to come up; the last said: injected";

const answering =
  (result: Record<string, unknown> | null, failure: string | null = null) =>
  (): Promise<{ readonly failure: string | null; readonly result: Record<string, unknown> | null }> =>
    Promise.resolve({ failure, result });

afterEach(() => {
  vi.restoreAllMocks();
});

describe("gradeRendererRung", () => {
  it("asks nothing when the scenario names no rung", async () => {
    expect(await gradeRendererRung(answering(null, "unreachable"), SOCKET_PATH, null)).toEqual([]);
  });

  it("passes on the expected rung and prints the reason the window gave", async () => {
    const write = vi.spyOn(stdout, "write").mockReturnValue(true);
    const request = answering({ reason: FALLBACK_REASON, rung: "raster" });

    expect(await gradeRendererRung(request, SOCKET_PATH, "raster")).toEqual([]);
    expect(write).toHaveBeenCalledWith(`e2e DescribeRenderer: "raster" because "${FALLBACK_REASON}"\n`);
  });

  it("fails on any other rung", async () => {
    vi.spyOn(stdout, "write").mockReturnValue(true);

    const request = answering({ reason: "no persisted decision", rung: "preferred-vulkan" });

    expect(await gradeRendererRung(request, SOCKET_PATH, "raster")).toEqual([
      'DescribeRenderer answered rung "preferred-vulkan", expected "raster"',
    ]);
  });

  it("fails when the window refuses the request", async () => {
    const request = answering(null, "unknown command DescribeRenderer");

    expect(await gradeRendererRung(request, SOCKET_PATH, "raster")).toEqual(["unknown command DescribeRenderer"]);
  });
});
