import { describe, expect, it } from "vitest";
import { findAccessibilityNodes, findAutomationSocketPath, findSurfaceChildren, readAnswer } from "./automation.ts";

const SOCKET_PATH = "/run/user/1000/rnl-automation-9.sock";

describe("findAutomationSocketPath", () => {
  it("finds the socket the window printed", () => {
    expect(findAutomationSocketPath(`boot\n[rnl-automation] listening on ${SOCKET_PATH}\n`)).toBe(SOCKET_PATH);
  });

  it("finds nothing in a trace that never opened the channel", () => {
    expect(findAutomationSocketPath("boot\n")).toBeNull();
  });
});

describe("readAnswer", () => {
  it("reads the result of a successful response", () => {
    expect(readAnswer('{"ok":true,"command":"ListErrors","result":{"errors":[]}}')).toEqual({
      failure: null,
      result: { errors: [] },
    });
  });

  it("reports the reason a refused request names", () => {
    expect(readAnswer('{"ok":false,"error":"unknown command Explode"}').failure).toBe("unknown command Explode");
  });

  it("reports a refusal that names no reason as the whole line", () => {
    expect(readAnswer('{"ok":false}').failure).toBe('{"ok":false}');
  });

  it("reports a line that is not JSON", () => {
    expect(readAnswer("nonsense").failure).toContain("is not JSON");
  });

  it("reports a line that is not a JSON object", () => {
    expect(readAnswer("[1,2]").failure).toContain("is not a JSON object");
  });

  it("reports a success carrying no result object", () => {
    expect(readAnswer('{"ok":true,"result":7}').failure).toContain("without a result object");
  });
});

describe("findSurfaceChildren", () => {
  it("takes the children of the surface root", () => {
    expect(findSurfaceChildren({ roots: [{ children: [{ testID: "box" }], componentName: "RootView" }] })).toEqual([
      { testID: "box" },
    ]);
  });

  it("takes an empty list from a surface root that mounted nothing", () => {
    expect(findSurfaceChildren({ roots: [{ componentName: "RootView" }] })).toEqual([]);
  });

  it("takes nothing from a dump with no surface at all", () => {
    expect(findSurfaceChildren({ roots: [] })).toBeNull();
  });

  it("takes nothing from a dump whose roots are not a list", () => {
    expect(findSurfaceChildren({})).toBeNull();
  });
});

describe("findAccessibilityNodes", () => {
  it("takes the projected nodes", () => {
    expect(findAccessibilityNodes({ nodes: [{ role: "button", tag: 3 }] })).toEqual([{ role: "button", tag: 3 }]);
  });

  it("takes nothing from an answer whose nodes are not a list", () => {
    expect(findAccessibilityNodes({})).toBeNull();
  });
});
