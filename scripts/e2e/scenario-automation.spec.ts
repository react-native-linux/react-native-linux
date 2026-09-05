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
      accessibilityTreeSnapshot: "a11y.json",
      listErrorsMustBeEmpty: true,
      markTestPassed: true,
      visualTreeSnapshot: "tree.json",
    });
  });

  it("defaults every field of an automation block that names none of them", () => {
    expect(parseScenario({ ...validScenario, automation: {} }, "fixture.json").automation).toEqual({
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
