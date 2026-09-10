/**
 * The commit-lint rule of AGENTS.md's *Git Commits And Pull Requests* section, exactly as written there and no
 * further: `type(scope): short description`, the nine types, the lane scopes (omitted for repo-wide docs and
 * tooling), a non-empty subject without a trailing full stop. Deliberately not
 * `@commitlint/config-conventional` — that config carries rules AGENTS.md does not mandate (body line length,
 * subject casing beyond the header shape) and its defaults reject compliant history.
 *
 * Applied to commit messages *and* to pull-request titles: the squash-merge flow makes the PR title the
 * landed commit, so one rule set governs both. PR titles additionally carry the closing issue reference —
 * `(#123)` — which is a subject suffix, not a type or scope.
 */

/** Commitlint's own rule severity: error. */
const error = 2;

/** The longest header the merged history carries, with headroom. */
const headerMaxLength = 200;

/** The types of AGENTS.md, in its own order. */
const types = ["feat", "fix", "refactor", "chore", "docs", "ci", "test", "perf", "build"];

/** The scopes of AGENTS.md's lanes plus the ones the merged history already uses (the issue area:* labels). */
const scopes = [
  "core",
  "cli",
  "harness",
  "modules",
  "renderer",
  "text",
  "input",
  "a11y",
  "animation",
  "packaging",
  "testing",
  "infra",
  "tooling",
  "research",
  "adr",
  "main",
  "reanimated",
  "ecosystem",
  "doctor",
  "window",
  "e2e",
];

const rules = {
  "header-max-length": [error, "always", headerMaxLength],
  "scope-case": [error, "always", "lower-case"],
  "scope-enum": [error, "always", scopes],
  "subject-empty": [error, "never"],
  "subject-full-stop": [error, "never", "."],
  "type-case": [error, "always", "lower-case"],
  "type-empty": [error, "never"],
  "type-enum": [error, "always", types],
};

// Commitlint resolves its config only through a default export — there is no named-export entry point.
const commitlintConfig = { extends: [], rules };

export default commitlintConfig;
