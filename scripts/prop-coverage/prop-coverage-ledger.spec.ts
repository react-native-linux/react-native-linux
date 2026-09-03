import type { ComponentProps, PropCoverageLedger } from "./prop-coverage-types.ts";

import { describe, expect, it } from "vitest";
import { findLedgerProblems, parseLedger } from "./prop-coverage-ledger.ts";

const ORIGIN = "docs/prop-coverage.json";

const parse = (ledger: unknown): PropCoverageLedger => parseLedger(JSON.stringify(ledger), ORIGIN);

const VIEW: ComponentProps = {
  component: "View",
  props: [
    { line: 46, name: "opacity", source: "vendor/BaseViewProps.h" },
    { line: 73, name: "filter", source: "vendor/BaseViewProps.h" },
  ],
};

const PROOF_CORPUS = 'TEST(RetainedSceneTest, OpacityMultipliesDownTheTree) {\n  { bundleFileName: "view-props.js" }';

describe("parseLedger", () => {
  it("keeps the three well-formed entry shapes and drops nothing else", () => {
    const ledger = {
      View: {
        borderStyles: { reason: "every border is solid", state: "deviating" },
        filter: { issue: "#68", state: "not-implemented" },
        opacity: { state: "implemented", test: "RetainedSceneTest, OpacityMultipliesDownTheTree" },
      },
    };

    expect(parse(ledger)).toStrictEqual(ledger);
  });

  it("refuses a document, a component or an entry that is not a JSON object", () => {
    expect(() => parse("text")).toThrow(`${ORIGIN} must contain a JSON object`);
    expect(() => parse(null)).toThrow(`${ORIGIN} must contain a JSON object`);
    expect(() => parse(["View"])).toThrow(`${ORIGIN} must contain a JSON object`);
    expect(() => parse({ View: "opacity" })).toThrow(`${ORIGIN} entry "View" must be a JSON object`);
    expect(() => parse({ View: { opacity: "implemented" } })).toThrow(`${ORIGIN} entry "View".opacity must be`);
  });

  it("refuses an implemented entry with no test naming what proves it", () => {
    expect(() => parse({ View: { opacity: { state: "implemented" } } })).toThrow('must name in "test"');
    expect(() => parse({ View: { opacity: { state: "implemented", test: "" } } })).toThrow('must name in "test"');
  });

  it("refuses a deviating entry with no reason", () => {
    expect(() => parse({ View: { borderStyles: { state: "deviating" } } })).toThrow('must give a "reason"');
    expect(() => parse({ View: { borderStyles: { reason: "", state: "deviating" } } })).toThrow('must give a "reason"');
  });

  it("refuses a not-implemented entry without an issue number", () => {
    expect(() => parse({ View: { filter: { state: "not-implemented" } } })).toThrow('must name in "issue"');
    expect(() => parse({ View: { filter: { issue: "68", state: "not-implemented" } } })).toThrow(
      'must name in "issue"',
    );
  });

  it("refuses a state that is none of the three", () => {
    expect(() => parse({ View: { filter: { state: "partial" } } })).toThrow('must declare a "state"');
  });
});

describe("findLedgerProblems", () => {
  it("reports nothing when every declared prop is in the ledger and every proof still exists", () => {
    const ledger = parse({
      View: {
        filter: { issue: "#68", state: "not-implemented" },
        opacity: { state: "implemented", test: "RetainedSceneTest, OpacityMultipliesDownTheTree" },
      },
    });

    expect(findLedgerProblems([VIEW], ledger, PROOF_CORPUS)).toStrictEqual([]);
  });

  it("reports a declared prop that has no ledger entry, with where it is declared", () => {
    const ledger = parse({ View: { opacity: { state: "implemented", test: "view-props.js" } } });

    expect(findLedgerProblems([VIEW], ledger, PROOF_CORPUS)).toStrictEqual([
      "View.filter is declared at vendor/BaseViewProps.h:73 and is missing from the ledger",
    ]);
  });

  it("reports a ledger entry that no source header declares any more", () => {
    const ledger = parse({ View: { shadowColor: { issue: "#67", state: "not-implemented" } } });

    expect(findLedgerProblems([VIEW], ledger, PROOF_CORPUS)).toContain(
      "View.shadowColor is in the ledger and is no longer declared by any source header",
    );
  });

  it("reports an implemented entry whose proof no test source and no golden contains", () => {
    const ledger = parse({
      View: {
        filter: { issue: "#68", state: "not-implemented" },
        opacity: { state: "implemented", test: "RetainedSceneTest, ThisTestWasDeleted" },
      },
    });

    expect(findLedgerProblems([VIEW], ledger, PROOF_CORPUS)).toStrictEqual([
      'View.opacity names the test "RetainedSceneTest, ThisTestWasDeleted", which no test source and no golden contains',
    ]);
  });

  it("reports every prop of a component the ledger has no section for", () => {
    expect(findLedgerProblems([VIEW], parse({}), PROOF_CORPUS)).toHaveLength(VIEW.props.length);
  });
});

describe("findLedgerProblems across components", () => {
  it("reports a ledger section for a component nobody enumerates", () => {
    const ledger = parse({ Switch: { disabled: { issue: "#69", state: "not-implemented" } } });

    expect(findLedgerProblems([VIEW], ledger, PROOF_CORPUS)).toContain(
      "Switch is in the ledger and is not an enumerated component",
    );
  });
});
