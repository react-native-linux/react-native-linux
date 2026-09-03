import type { ComponentProps, PropCoverageLedger } from "./prop-coverage-types.ts";

import { describe, expect, it } from "vitest";

import { renderPropCoverage } from "./render-prop-coverage.ts";

const COMPONENTS: readonly ComponentProps[] = [
  {
    component: "View",
    props: [
      { line: 46, name: "opacity", source: "vendor/BaseViewProps.h" },
      { line: 53, name: "borderStyles", source: "vendor/BaseViewProps.h" },
      { line: 73, name: "filter", source: "vendor/BaseViewProps.h" },
      { line: 97, name: "shouldRasterize", source: "vendor/BaseViewProps.h" },
    ],
  },
  { component: "Image", props: [{ line: 28, name: "sources", source: "vendor/ImageProps.h" }] },
];

const LEDGER: PropCoverageLedger = {
  View: {
    borderStyles: { reason: "every border is solid", state: "deviating" },
    filter: { issue: "#68", state: "not-implemented" },
    opacity: { state: "implemented", test: "RetainedSceneTest, OpacityMultipliesDownTheTree" },
  },
};

const TITLE_LINE = 0;

const report = renderPropCoverage(COMPONENTS, LEDGER);
const lines = report.split("\n");

describe("renderPropCoverage", () => {
  it("documents how the list is derived above the tables", () => {
    expect(lines[TITLE_LINE]).toBe("# Prop coverage");
    expect(report).toContain("## How the list is derived");
    expect(report).toContain("## The three states");
    expect(report).toContain("## What the check enforces");
  });

  it("counts every state per component and totals them", () => {
    expect(lines).toContain("| Component | Props | Implemented | Deviating | Not implemented |");
    expect(lines).toContain("| View | 4 | 1 | 1 | 1 |");
    expect(lines).toContain("| Image | 1 | 0 | 0 | 0 |");
    expect(lines).toContain("| **Total** | 5 | 1 | 1 | 1 |");
  });

  it("names the proof, the reason or the owning issue in the row of each prop", () => {
    expect(lines).toContain(
      "| `opacity` | `vendor/BaseViewProps.h:46` | implemented | `RetainedSceneTest, OpacityMultipliesDownTheTree` |",
    );
    expect(lines).toContain("| `borderStyles` | `vendor/BaseViewProps.h:53` | deviating | every border is solid |");
    expect(lines).toContain("| `filter` | `vendor/BaseViewProps.h:73` | not-implemented | #68 |");
  });

  it("marks a prop the ledger does not cover, in a section the ledger does not cover either", () => {
    expect(lines).toContain("| `shouldRasterize` | `vendor/BaseViewProps.h:97` | missing | not in the ledger |");
    expect(lines).toContain("| `sources` | `vendor/ImageProps.h:28` | missing | not in the ledger |");
  });

  it("ends with exactly one trailing newline", () => {
    expect(report.endsWith("|\n")).toBe(true);
  });
});
