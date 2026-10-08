import {
  countDiffLines,
  findManifestProblems,
  readOverrideEntries,
  renderOverrideReport,
} from "@react-native-linux/cli/override-report.ts";
import { diffOverride, validateManifest } from "react-native-platform-override";
import { readFileSync, writeFileSync } from "node:fs";
import { env } from "node:process";
import path from "node:path";

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const manifestPath = path.join(repositoryRoot, "packages", "core", "overrides.json");
const reportPath = path.join(repositoryRoot, "docs", "override-report.md");
const isCheck = process.argv.includes("--check");
const NO_PROBLEMS = 0;
const NEXT_GIT_CONFIGURATION = 1;

const gitConfigurationCount = Number(env["GIT_CONFIG_COUNT"] ?? "0");

env[`GIT_CONFIG_KEY_${String(gitConfigurationCount)}`] = "diff.algorithm";
env[`GIT_CONFIG_VALUE_${String(gitConfigurationCount)}`] = "myers";
env["GIT_CONFIG_COUNT"] = String(gitConfigurationCount + NEXT_GIT_CONFIGURATION);

const manifest: unknown = JSON.parse(readFileSync(manifestPath, "utf8"));
const vendorLock: unknown = JSON.parse(readFileSync(path.join(repositoryRoot, "scripts", "vendor.lock.json"), "utf8"));
const baseVersion =
  typeof manifest === "object" && manifest !== null && "baseVersion" in manifest ? String(manifest.baseVersion) : "";
const vendoredVersion =
  typeof vendorLock === "object" && vendorLock !== null && "tag" in vendorLock ? String(vendorLock.tag) : "";
const entries = readOverrideEntries(manifest);

/* Validated against the vendored pin rather than the manifest's own base: on a bump PR, that is what reports every
   override whose upstream file moved, before anyone writes adaptation code (#58). */
const validationErrors = await validateManifest(manifestPath, {
  reactNativeVersion: vendoredVersion.replace(/^v/u, ""),
});
const problems = [
  ...findManifestProblems(entries),
  ...validationErrors.map((error) => `${error.overrideName}: ${error.type}`),
];

if (problems.length > NO_PROBLEMS) {
  throw new Error(`packages/core/overrides.json:\n${problems.join("\n")}`);
}

const rows = [];

for (const entry of entries) {
  const diff = entry.type === "derived" ? await diffOverride(entry.file, manifestPath) : "";

  rows.push({ ...entry, ...countDiffLines(diff) });
}

const report = renderOverrideReport(rows, baseVersion, vendoredVersion);

if (!isCheck) {
  writeFileSync(reportPath, report);
} else if (readFileSync(reportPath, "utf8") !== report) {
  throw new Error("docs/override-report.md is stale; regenerate it with: pnpm override:report");
}
