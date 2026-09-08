import { describe, expect, it } from "vitest";
import {
  describeTraceFailures,
  findErrorLines,
  findMissingExpectations,
  findRejectedMatches,
  resolveExpectedOutcome,
} from "./trace-grading.ts";
import { parseScenario } from "./scenario.ts";

const validScenario = {
  bundle: "pressable.js",
  expect: ["pressable: topClick on box at 200,140"],
  name: "pressable-click",
  ready: "pressable: committed surface 1",
  steps: ["sleep 500", "click 200 140"],
};

describe("findMissingExpectations", () => {
  const trace = ["pressable: committed surface 1", "pressable: topPointerDown on box", "pressable: topClick on box"];

  it("reports nothing when every expectation appears in order", () => {
    expect(findMissingExpectations(trace, ["topPointerDown", "topClick"])).toEqual([]);
  });

  it("reports an expectation the trace never produced", () => {
    expect(findMissingExpectations(trace, ["topKeyPress"])).toEqual(["topKeyPress"]);
  });

  it("reports an expectation that only appears before the one it has to follow", () => {
    expect(findMissingExpectations(trace, ["topClick", "topPointerDown"])).toEqual(["topPointerDown"]);
  });
});

describe("findRejectedMatches", () => {
  const trace = ["pressable: committed surface 1", "[rnl-window] wayland protocol error: xdg_surface#1 code 3"];

  it("reports nothing when no rejection appears", () => {
    expect(findRejectedMatches(trace, ["Broken pipe"])).toEqual([]);
  });

  it("reports a rejection that appears anywhere in the trace", () => {
    expect(findRejectedMatches([...trace, "Broken pipe (os error 32)"], ["Broken pipe"])).toEqual(["Broken pipe"]);
  });
});

describe("findErrorLines", () => {
  it("reports nothing when no line matches a known pattern", () => {
    expect(findErrorLines(["pressable: committed surface 1", "pressable: topClick on box"])).toEqual([]);
  });

  it("finds an uncaught JS error's own report", () => {
    const trace = ["throws: failing bundle evaluated", "[js-error] fatal Error: intentional bundle failure"];

    expect(findErrorLines(trace)).toEqual(["[js-error] fatal Error: intentional bundle failure"]);
  });

  it("finds a native diagnostic prefix", () => {
    const trace = ["[rnl-window] the compositor does not advertise zwp_text_input_manager_v3"];

    expect(findErrorLines(trace)).toEqual(trace);
  });
});

describe("describeTraceFailures", () => {
  const scenario = parseScenario(validScenario, "fixture.json");
  const passingTrace = ["pressable: committed surface 1", "pressable: topClick on box at 200,140"].join("\n");

  it("reports nothing for a trace with every expectation and no error line", () => {
    expect(describeTraceFailures(scenario, passingTrace)).toEqual([]);
  });

  it("reports a missing expectation", () => {
    expect(describeTraceFailures(scenario, "pressable: committed surface 1")).toEqual([
      'the trace never produced "pressable: topClick on box at 200,140"',
    ]);
  });

  it("reports a logged error line", () => {
    const trace = `${passingTrace}\n[js-error] fatal Error: intentional bundle failure`;

    expect(describeTraceFailures(scenario, trace)).toEqual([
      "the trace logged an error: [js-error] fatal Error: intentional bundle failure",
    ]);
  });

  it("does not report an error line when allowErrors is set", () => {
    const tolerant = { ...scenario, allowErrors: true };
    const trace = `${passingTrace}\n[js-error] fatal Error: intentional bundle failure`;

    expect(describeTraceFailures(tolerant, trace)).toEqual([]);
  });

  it("reports a rejected substring even when allowErrors is set", () => {
    const tolerant = { ...scenario, allowErrors: true, reject: ["Broken pipe"] };
    const trace = `${passingTrace}\nBroken pipe (os error 32)`;

    expect(describeTraceFailures(tolerant, trace)).toEqual(['the trace produced the rejected "Broken pipe"']);
  });
});

describe("resolveExpectedOutcome", () => {
  const scenario = parseScenario(validScenario, "fixture.json");
  const negativeControl = { ...scenario, expectFailure: true };

  it("passes failures through unchanged when expectFailure is not set", () => {
    expect(resolveExpectedOutcome(scenario, ["boom"])).toEqual(["boom"]);
    expect(resolveExpectedOutcome(scenario, [])).toEqual([]);
  });

  it("turns a failing run into a pass when expectFailure is set", () => {
    expect(resolveExpectedOutcome(negativeControl, ["boom"])).toEqual([]);
  });

  it("fails a run with no failures when expectFailure is set", () => {
    expect(resolveExpectedOutcome(negativeControl, [])).toEqual([
      "expectFailure is set, but the scenario produced no failures",
    ]);
  });
});
