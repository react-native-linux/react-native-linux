import { describe, expect, it } from "vitest";
import { parseScenario } from "./scenario.ts";

const validScenario = {
  bundle: "automation.js",
  expect: ["automation: committed surface 1"],
  name: "automation-channel",
  ready: "automation: committed surface 1",
  steps: ["sleep 300"],
};

describe("parseScenario automation", () => {
  it("reads the four things the channel is asked to prove", () => {
    const scenario = parseScenario(
      {
        ...validScenario,
        automation: {
          accessibilityTreeSnapshot: "a11y.json",
          listErrorsMustBeEmpty: true,
          markTestPassed: true,
          visualTreeSnapshot: "tree.json",
        },
      },
      "fixture.json",
    );

    expect(scenario.automation).toEqual({
      accessibilityChanges: null,
      accessibilityTreeSnapshot: "a11y.json",
      listErrorsMustBeEmpty: true,
      markTestPassed: true,
      visualTreeSnapshot: "tree.json",
    });
  });

  it("defaults every field of an automation block that names none of them", () => {
    expect(parseScenario({ ...validScenario, automation: {} }, "fixture.json").automation).toEqual({
      accessibilityChanges: null,
      accessibilityTreeSnapshot: null,
      listErrorsMustBeEmpty: false,
      markTestPassed: false,
      visualTreeSnapshot: null,
    });
  });

  it("rejects an automation block that is not an object", () => {
    expect(() => parseScenario({ ...validScenario, automation: true }, "fixture.json")).toThrow(
      'fixture.json: "automation" must be a JSON object',
    );
  });
});

describe("parseScenario automation visualTreeSnapshot", () => {
  it("rejects a visualTreeSnapshot that could leave the goldens directory", () => {
    for (const snapshot of ["../escaped.json", "nested/../../escaped.json", "/etc/passwd"]) {
      expect(() => parseScenario({ ...validScenario, automation: { visualTreeSnapshot: snapshot } }, "f.json")).toThrow(
        'f.json: "automation.visualTreeSnapshot" must be a relative path inside the goldens directory',
      );
    }
  });

  it("keeps a visualTreeSnapshot in a subdirectory of the goldens directory", () => {
    const scenario = parseScenario({ ...validScenario, automation: { visualTreeSnapshot: "nested/tree.json" } }, "f");

    expect(scenario.automation?.visualTreeSnapshot).toBe("nested/tree.json");
  });

  it("rejects a visualTreeSnapshot that is not a file name", () => {
    expect(() => parseScenario({ ...validScenario, automation: { visualTreeSnapshot: "" } }, "fixture.json")).toThrow(
      'fixture.json: "automation.visualTreeSnapshot" must be a non-empty string',
    );
  });

  it("rejects an accessibilityTreeSnapshot that is not a file name", () => {
    expect(() =>
      parseScenario({ ...validScenario, automation: { accessibilityTreeSnapshot: 7 } }, "fixture.json"),
    ).toThrow('fixture.json: "automation.accessibilityTreeSnapshot" must be a non-empty string');
  });
});

describe("parseScenario automation accessibilityChanges", () => {
  it("reads each entry's testID, defaulting state and value to false", () => {
    const scenario = parseScenario(
      { ...validScenario, automation: { accessibilityChanges: [{ testID: "toggle" }] } },
      "fixture.json",
    );

    expect(scenario.automation?.accessibilityChanges).toEqual([{ state: false, testID: "toggle", value: false }]);
  });

  it("reads state and value when the scenario sets them", () => {
    const scenario = parseScenario(
      {
        ...validScenario,
        automation: { accessibilityChanges: [{ state: true, testID: "toggle", value: true }] },
      },
      "fixture.json",
    );

    expect(scenario.automation?.accessibilityChanges).toEqual([{ state: true, testID: "toggle", value: true }]);
  });

  it("defaults to null when the automation block names no accessibilityChanges", () => {
    expect(parseScenario({ ...validScenario, automation: {} }, "fixture.json").automation?.accessibilityChanges).toBe(
      null,
    );
  });

  it("rejects an accessibilityChanges that is not a non-empty array", () => {
    for (const changes of [[], "toggle", {}]) {
      expect(() =>
        parseScenario({ ...validScenario, automation: { accessibilityChanges: changes } }, "f.json"),
      ).toThrow('f.json: "automation.accessibilityChanges" must be a non-empty array');
    }
  });

  it("rejects an entry that is not an object", () => {
    expect(() =>
      parseScenario({ ...validScenario, automation: { accessibilityChanges: ["toggle"] } }, "fixture.json"),
    ).toThrow('fixture.json: "automation.accessibilityChanges[0]" must be a JSON object');
  });

  it("rejects an entry without a testID", () => {
    expect(() =>
      parseScenario({ ...validScenario, automation: { accessibilityChanges: [{}] } }, "fixture.json"),
    ).toThrow('fixture.json: "automation.accessibilityChanges[0].testID" must be a non-empty string');
  });
});
