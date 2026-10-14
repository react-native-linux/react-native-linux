import { describe, expect, it } from "vitest";
import { findTraversalProblems } from "./focus-traversal.ts";

const REGISTRY = `
    add(facebook::react::concreteComponentDescriptorProvider<facebook::react::ViewComponentDescriptor>());
    add(facebook::react::concreteComponentDescriptorProvider<
        facebook::react::ActivityIndicatorViewComponentDescriptor>());
    add(facebook::react::concreteComponentDescriptorProvider<SwitchComponentDescriptor>());
`;

const SCENARIOS: Readonly<Record<string, readonly string[]>> = {
  "click.json": ["click 10 10"],
  "tab.json": ["key Tab press", "key Tab release"],
};

const readScenarioSteps = (scenario: string): readonly string[] | null => SCENARIOS[scenario] ?? null;

const LEDGERED = {
  ActivityIndicatorView: { reason: "display only", state: "not-interactive" },
  Switch: { issue: "#1", state: "not-traversed" },
  View: { scenario: "tab.json", state: "traversed" },
};

const problemsFor = (ledger: unknown): readonly string[] => findTraversalProblems(REGISTRY, ledger, readScenarioSteps);

describe("findTraversalProblems", () => {
  it("accepts a ledger with one well-formed entry per registered component", () => {
    expect(problemsFor(LEDGERED)).toStrictEqual([]);
  });

  it("accepts an empty ledger when nothing is registered", () => {
    expect(findTraversalProblems("", {}, readScenarioSteps)).toStrictEqual([]);
  });

  it("refuses a ledger that is not a JSON object", () => {
    expect(problemsFor(["View"])).toStrictEqual(["docs/focus-traversal.json must contain a JSON object"]);
  });

  it("fails a registered component that has no entry, which is what registering a new one does", () => {
    expect(problemsFor({ ...LEDGERED, Switch: null })).toStrictEqual([
      "Switch is registered by FabricHost.cpp and has no entry in docs/focus-traversal.json",
    ]);
  });

  it("fails a traversal scenario that does not exist or never presses Tab", () => {
    expect(problemsFor({ ...LEDGERED, View: { scenario: "gone.json", state: "traversed" } })).toStrictEqual([
      'View names the scenario "gone.json", which does not exist',
    ]);
    expect(problemsFor({ ...LEDGERED, View: { scenario: "click.json", state: "traversed" } })).toStrictEqual([
      'View names the scenario "click.json", which never presses Tab',
    ]);
  });

  it("fails an entry without its reason, its issue or a known state", () => {
    const malformed = [
      { reason: "", state: "not-interactive" },
      { issue: "1", state: "not-traversed" },
      { scenario: 1, state: "traversed" },
      { state: "skipped" },
    ];

    for (const entry of malformed) {
      expect(problemsFor({ ...LEDGERED, Switch: entry })).toStrictEqual([
        'Switch must be { state: "traversed", scenario }, { state: "not-interactive", reason } or { state: "not-traversed", issue: "#N" }',
      ]);
    }
  });

  it("fails an entry for a component nothing registers", () => {
    expect(problemsFor({ ...LEDGERED, Slider: { reason: "gone", state: "not-interactive" } })).toStrictEqual([
      "Slider is in docs/focus-traversal.json and FabricHost.cpp does not register it",
    ]);
  });
});
