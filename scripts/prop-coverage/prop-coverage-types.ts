interface PropSource {
  readonly className: string;
  readonly path: string;
}

interface ComponentSources {
  readonly component: string;
  readonly sources: readonly PropSource[];
}

interface DeclaredProp {
  readonly line: number;
  readonly name: string;
  readonly source: string;
}

interface ComponentProps {
  readonly component: string;
  readonly props: readonly DeclaredProp[];
}

interface DeviatingEntry {
  readonly reason: string;
  readonly state: "deviating";
}

interface ImplementedEntry {
  readonly state: "implemented";
  readonly test: string;
}

interface NotImplementedEntry {
  readonly issue: string;
  readonly state: "not-implemented";
}

type LedgerEntry = DeviatingEntry | ImplementedEntry | NotImplementedEntry;

type ComponentLedger = Readonly<Record<string, LedgerEntry>>;

type PropCoverageLedger = Readonly<Record<string, ComponentLedger>>;

export type {
  ComponentLedger,
  ComponentProps,
  ComponentSources,
  DeclaredProp,
  LedgerEntry,
  PropCoverageLedger,
  PropSource,
};
