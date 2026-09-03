import type { ComponentLedger, ComponentProps, LedgerEntry, PropCoverageLedger } from "./prop-coverage-types.ts";

const HEADER_LINES: readonly string[] = [
  "# Prop coverage",
  "",
  "Every prop the five shipped components declare is in exactly one of three states — `implemented`, `deviating`",
  "or `not-implemented` — and a prop in none of them fails `pnpm prop-coverage:check`, which the `validate` job",
  "runs. This file is generated: edit `docs/prop-coverage.json` and run `pnpm prop-coverage:report`.",
  "",
  "## How the list is derived",
  "",
  "`scripts/prop-coverage.ts` parses the props classes React Native declares in the vendored headers under",
  "`third_party/react-native`, plus the one props class this platform declares itself",
  "(`packages/core/src/TextInputComponent.h`). Nothing below is hand-maintained: a prop is listed because a header",
  "declares it, so an upstream bump that adds a prop fails the check rather than reaching a user.",
  "",
  "A prop is the name matched by",
  "",
  "```regexp",
  String.raw`(?<=^[ \t]+(?!(?:constexpr|explicit|friend|inline|return|static|template|typedef|using|virtual)\b)[A-Za-z_][\w:<>,&* \t]*?[ \t]+)[A-Za-z_]\w*(?=[ \t]*(?:\{[^;]*\}|=(?!=)[^;]*)?;[ \t]*(?://.*)?$)`,
  "```",
  "",
  "on one line inside the brace-matched body of the named `class` or `struct`: a lookbehind for an indented type,",
  "the name, and a lookahead for an optional brace or `=` initialiser, a semicolon and an optional trailing",
  "comment. A method declaration cannot match, because neither the type nor the name may contain a parenthesis;",
  "`operator==` cannot match, because the initialiser is `=(?!=)`; and a forward declaration is skipped, because a",
  "`;` before the `{` is not a body. A source that declares its name as a `using` alias contributes no props and is",
  "listed as a source anyway, so a platform header that grows real members on an upstream bump is enumerated the",
  "day it does.",
  "",
  "Each component's props are its **own** declarations. Everything `<Paragraph>`, `<Image>`, `<ScrollView>` and",
  "`<TextInput>` inherit from `ViewProps` is enumerated once, under **View**. Layout (`YogaStylableProps`) and",
  "accessibility (`AccessibilityProps`) are not paint props and are out of scope here; they belong to the layout",
  "and accessibility issues. The generated tree under `packages/core/generated` declares no props class for any of",
  "the five — upstream hand-writes all five rather than generating them — so the vendored headers are the whole",
  "source of truth.",
  "",
  "## The three states",
  "",
  "| State | What it means | What it carries |",
  "| --- | --- | --- |",
  "| `implemented` | the platform reads the prop and an assertion proves it | `test`: a GoogleTest name from `packages/core/tests/**/*.cpp`, or a golden file name from `packages/core/goldens/*.ts` |",
  "| `deviating` | the platform deliberately does something else, as the *Fidelity limits* prose of `docs/cpp-toolchain.md` records | `reason` |",
  "| `not-implemented` | nothing reads the prop, or nothing asserts it | `issue`: the open issue that owns it, `#69` when no narrower one does |",
  "",
  "`implemented` is the strong claim and it is checked: the `test` string has to appear verbatim in a test source",
  "or a golden registration, so deleting or renaming a test turns its props back into a build failure. A prop the",
  "platform reads but nothing asserts is `not-implemented`, because issue #69's acceptance criterion is an",
  "assertion and not an implementation.",
  "",
  "## What the check enforces",
  "",
  "- a declared prop with no ledger entry fails, which is what an upstream bump or a new component looks like;",
  "- a ledger entry for a prop no longer declared fails, which is what an upstream removal looks like;",
  "- an `implemented` entry whose `test` no longer exists fails, which is what a deleted test looks like;",
  "- a rendered report that differs from this file fails, so the tables below are always the ledger.",
];

const SUMMARY_HEADER: readonly string[] = [
  "| Component | Props | Implemented | Deviating | Not implemented |",
  "| --- | --- | --- | --- | --- |",
];

const DETAIL_HEADER: readonly string[] = [
  "| Prop | Declared at | State | Proof, reason or owner |",
  "| --- | --- | --- | --- |",
];

const MISSING_STATE = "missing";
const MISSING_DETAIL = "not in the ledger";

interface PropRow {
  readonly detail: string;
  readonly line: number;
  readonly name: string;
  readonly source: string;
  readonly state: string;
}

const describeEntry = (entry: LedgerEntry | null): { detail: string; state: string } => {
  if (entry === null) {
    return { detail: MISSING_DETAIL, state: MISSING_STATE };
  }

  if (entry.state === "implemented") {
    return { detail: `\`${entry.test}\``, state: entry.state };
  }

  if (entry.state === "deviating") {
    return { detail: entry.reason, state: entry.state };
  }

  return { detail: entry.issue, state: entry.state };
};

const buildRows = (component: ComponentProps, entries: ComponentLedger): readonly PropRow[] =>
  component.props.map((prop) => ({
    ...describeEntry(entries[prop.name] ?? null),
    line: prop.line,
    name: prop.name,
    source: prop.source,
  }));

const countRows = (rows: readonly PropRow[], state: string): number => rows.filter((row) => row.state === state).length;

const renderSummaryRow = (label: string, rows: readonly PropRow[]): string =>
  `| ${label} | ${rows.length} | ${countRows(rows, "implemented")} | ${countRows(rows, "deviating")} | ${countRows(rows, "not-implemented")} |`;

const renderComponentSection = (component: ComponentProps, rows: readonly PropRow[]): readonly string[] => [
  `## ${component.component}`,
  "",
  ...DETAIL_HEADER,
  ...rows.map((row) => `| \`${row.name}\` | \`${row.source}:${row.line}\` | ${row.state} | ${row.detail} |`),
  "",
];

const renderPropCoverage = (components: readonly ComponentProps[], ledger: PropCoverageLedger): string => {
  const sections = components.map((component) => ({
    component,
    rows: buildRows(component, ledger[component.component] ?? {}),
  }));

  return `${[
    ...HEADER_LINES,
    "",
    "## Summary",
    "",
    ...SUMMARY_HEADER,
    ...sections.map((section) => renderSummaryRow(section.component.component, section.rows)),
    renderSummaryRow(
      "**Total**",
      sections.flatMap((section) => section.rows),
    ),
    "",
    ...sections.flatMap((section) => renderComponentSection(section.component, section.rows)),
  ]
    .join("\n")
    .trimEnd()}\n`;
};

export { renderPropCoverage };
