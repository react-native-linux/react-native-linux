const LEDGER_PATH = "docs/focus-traversal.json";
const REGISTRATION_PATTERN =
  /(?<=concreteComponentDescriptorProvider<\s*(?:facebook::react::)?)\w+?(?=ComponentDescriptor>)/gu;
const ISSUE_PATTERN = /^#\d+$/u;
const TAB_STEP = "key Tab press";
const EMPTY = 0;

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

/** The component names `createComponentRegistryFactory` registers, in registration order. */
const findRegisteredComponents = (registrySource: string): readonly string[] =>
  registrySource.match(REGISTRATION_PATTERN) ?? [];

const findScenarioProblem = (component: string, scenario: string, steps: readonly string[] | null): string | null => {
  if (steps === null) {
    return `${component} names the scenario "${scenario}", which does not exist`;
  }

  return steps.includes(TAB_STEP) ? null : `${component} names the scenario "${scenario}", which never presses Tab`;
};

const findEntryProblem = (
  component: string,
  entry: unknown,
  readScenarioSteps: (scenario: string) => readonly string[] | null,
): string | null => {
  if (!isRecord(entry)) {
    return `${component} is registered by FabricHost.cpp and has no entry in ${LEDGER_PATH}`;
  }

  const { issue, reason, scenario, state } = entry;

  if (state === "traversed" && typeof scenario === "string") {
    return findScenarioProblem(component, scenario, readScenarioSteps(scenario));
  }

  if (state === "not-interactive" && typeof reason === "string" && reason.length > EMPTY) {
    return null;
  }

  if (state === "not-traversed" && typeof issue === "string" && ISSUE_PATTERN.test(issue)) {
    return null;
  }

  return `${component} must be { state: "traversed", scenario }, { state: "not-interactive", reason } or { state: "not-traversed", issue: "#N" }`;
};

/**
 * Every way the ledger disagrees with the registry: a registered component with no well-formed entry, a
 * traversal scenario that is missing or never presses Tab, and an entry for a component nothing registers.
 * Registering an interactive component therefore fails until a keyboard scenario or an owning issue is named.
 */
const findTraversalProblems = (
  registrySource: string,
  ledger: unknown,
  readScenarioSteps: (scenario: string) => readonly string[] | null,
): readonly string[] => {
  if (!isRecord(ledger)) {
    return [`${LEDGER_PATH} must contain a JSON object`];
  }

  const components = findRegisteredComponents(registrySource);
  const registered = new Set(components);

  return [
    ...components.flatMap((component) => findEntryProblem(component, ledger[component], readScenarioSteps) ?? []),
    ...Object.keys(ledger)
      .filter((component) => !registered.has(component))
      .map((component) => `${component} is in ${LEDGER_PATH} and FabricHost.cpp does not register it`),
  ];
};

export { findTraversalProblems, LEDGER_PATH };
