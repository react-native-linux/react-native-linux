import type { DistroRemedy, Tier } from "./doctor-check.ts";
import type { CheckResult } from "./evaluate-check.ts";

type Distro = "arch" | "ubuntu" | "unknown";
type Severity = "FAIL" | "PASS" | "WARN";

const OS_RELEASE_ID_PATTERN = /^ID=(?<quote>"?)(?<id>[^"\n]+)\k<quote>$/mu;
const TIER_ORDER: readonly Tier[] = ["core", "window", "goldens", "coverage"];
const JSON_INDENT_SPACE_COUNT = 2;

const parseDistroId = (osReleaseContent: string): Distro => {
  const id = OS_RELEASE_ID_PATTERN.exec(osReleaseContent)?.groups?.["id"] ?? null;

  if (id === "arch") {
    return "arch";
  }

  if (id === "ubuntu" || id === "debian") {
    return "ubuntu";
  }

  return "unknown";
};

const pickRemedyLines = (remedy: DistroRemedy, distro: Distro): readonly string[] => {
  if (distro === "arch") {
    return [`Arch: ${remedy.arch}`];
  }

  if (distro === "ubuntu") {
    return [`Ubuntu: ${remedy.ubuntu}`];
  }

  return [`Arch: ${remedy.arch}`, `Ubuntu: ${remedy.ubuntu}`];
};

const describeSeverity = (result: CheckResult): Severity => {
  if (result.outcome === "pass") {
    return "PASS";
  }

  return result.check.required ? "FAIL" : "WARN";
};

const formatCheckLines = (result: CheckResult, distro: Distro): readonly string[] => {
  const headline = `[${describeSeverity(result)}] ${result.check.name} — ${result.detail}`;

  if (result.outcome === "pass") {
    return [headline];
  }

  return [
    headline,
    `  why: ${result.check.why}`,
    ...pickRemedyLines(result.check.remedy, distro).map((line) => `  ${line}`),
  ];
};

const formatTierSection = (tier: Tier, results: readonly CheckResult[], distro: Distro): string => {
  const tierResults = results.filter((result) => result.check.tier === tier);
  const lines = tierResults.flatMap((result) => formatCheckLines(result, distro).map((line) => `  ${line}`));

  return [`${tier}:`, ...lines].join("\n");
};

const formatSummaryLine = (results: readonly CheckResult[]): string => {
  const failedCount = results.filter((result) => describeSeverity(result) === "FAIL").length;
  const warnedCount = results.filter((result) => describeSeverity(result) === "WARN").length;
  const passedCount = results.length - failedCount - warnedCount;

  return `${String(passedCount)} passed, ${String(warnedCount)} warned, ${String(failedCount)} failed`;
};

const formatReport = (results: readonly CheckResult[], distro: Distro): string => {
  const sections = TIER_ORDER.filter((tier) => results.some((result) => result.check.tier === tier)).map((tier) =>
    formatTierSection(tier, results, distro),
  );

  return [...sections, formatSummaryLine(results)].join("\n\n");
};

interface JsonCheckResult {
  readonly detail: string;
  readonly name: string;
  readonly remedy: DistroRemedy;
  readonly required: boolean;
  readonly severity: Severity;
  readonly tier: Tier;
  readonly why: string;
}

const formatJsonReport = (results: readonly CheckResult[], exitCode: number): string =>
  JSON.stringify(
    {
      exitCode,
      results: results.map((result): JsonCheckResult => ({
        detail: result.detail,
        name: result.check.name,
        remedy: result.check.remedy,
        required: result.check.required,
        severity: describeSeverity(result),
        tier: result.check.tier,
        why: result.check.why,
      })),
    },
    null,
    JSON_INDENT_SPACE_COUNT,
  );

export { describeSeverity, formatJsonReport, formatReport, parseDistroId, pickRemedyLines };
