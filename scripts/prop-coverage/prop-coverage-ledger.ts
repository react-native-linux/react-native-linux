import type { ComponentLedger, ComponentProps, LedgerEntry, PropCoverageLedger } from "./prop-coverage-types.ts";

const ISSUE_PATTERN = /^#\d+$/u;
const EMPTY = 0;

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const requireText = (value: unknown, message: string): string => {
  if (typeof value !== "string" || value.length === EMPTY) {
    throw new TypeError(message);
  }

  return value;
};

const requireIssue = (value: unknown, message: string): string => {
  if (typeof value !== "string" || !ISSUE_PATTERN.test(value)) {
    throw new TypeError(message);
  }

  return value;
};

const parseLedgerEntry = (value: unknown, origin: string): LedgerEntry => {
  if (!isRecord(value)) {
    throw new TypeError(`${origin} must be a JSON object`);
  }

  const { issue, reason, state, test } = value;

  if (state === "implemented") {
    return { state, test: requireText(test, `${origin} must name in "test" the GoogleTest or golden that proves it`) };
  }

  if (state === "deviating") {
    return { reason: requireText(reason, `${origin} must give a "reason" for deviating`), state };
  }

  if (state === "not-implemented") {
    return { issue: requireIssue(issue, `${origin} must name in "issue" the issue that owns it, as "#N"`), state };
  }

  throw new TypeError(`${origin} must declare a "state" of "implemented", "deviating" or "not-implemented"`);
};

const parseComponentLedger = (value: unknown, origin: string): ComponentLedger => {
  if (!isRecord(value)) {
    throw new TypeError(`${origin} must be a JSON object`);
  }

  return Object.fromEntries(
    Object.entries(value).map(([name, entry]) => [name, parseLedgerEntry(entry, `${origin}.${name}`)]),
  );
};

const parseLedger = (source: string, origin: string): PropCoverageLedger => {
  const parsed: unknown = JSON.parse(source);

  if (!isRecord(parsed)) {
    throw new TypeError(`${origin} must contain a JSON object`);
  }

  return Object.fromEntries(
    Object.entries(parsed).map(([component, entries]) => [
      component,
      parseComponentLedger(entries, `${origin} entry "${component}"`),
    ]),
  );
};

const findUnknownComponents = (
  components: readonly ComponentProps[],
  ledger: PropCoverageLedger,
): readonly string[] => {
  const enumeratedComponents = new Set(components.map((entry) => entry.component));

  return Object.keys(ledger)
    .filter((component) => !enumeratedComponents.has(component))
    .map((component) => `${component} is in the ledger and is not an enumerated component`);
};

const findMissingEntries = (component: ComponentProps, entries: ComponentLedger): readonly string[] =>
  component.props
    .filter((prop) => !Object.hasOwn(entries, prop.name))
    .map(
      (prop) =>
        `${component.component}.${prop.name} is declared at ${prop.source}:${prop.line} and is missing from the ledger`,
    );

const findStaleEntries = (
  component: ComponentProps,
  entries: ComponentLedger,
  proofCorpus: string,
): readonly string[] => {
  const declaredNames = new Set(component.props.map((prop) => prop.name));

  return Object.entries(entries).flatMap(([name, entry]) => {
    if (!declaredNames.has(name)) {
      return [`${component.component}.${name} is in the ledger and is no longer declared by any source header`];
    }

    if (entry.state === "implemented" && !proofCorpus.includes(entry.test)) {
      return [
        `${component.component}.${name} names the test "${entry.test}", which no test source and no golden contains`,
      ];
    }

    return [];
  });
};

const findLedgerProblems = (
  components: readonly ComponentProps[],
  ledger: PropCoverageLedger,
  proofCorpus: string,
): readonly string[] => [
  ...findUnknownComponents(components, ledger),
  ...components.flatMap((component) => {
    const entries = ledger[component.component] ?? {};

    return [...findMissingEntries(component, entries), ...findStaleEntries(component, entries, proofCorpus)];
  }),
];

export { findLedgerProblems, parseLedger };
