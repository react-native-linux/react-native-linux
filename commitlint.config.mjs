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

/** The length of a scope nobody supplied. */
const emptyScopeLength = 0;

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

/**
 * The types whose commits may be repo-wide, and so may omit a scope — the same exemption AGENTS.md's sentence
 * carries ("omit it for repo-wide docs and tooling"). Every other type must name its lane.
 */
const scopeExemptTypes = ["docs", "chore", "ci"];

/*
 * Commitlint's own `scope-enum` validates a scope that is *supplied*; it cannot require one. This local rule
 * closes that gap, so a `fix:` or `feat:` without a lane is rejected rather than silently accepted as if the
 * exemption applied to it.
 */
const scopeRequiredPlugin = {
  rules: {
    "scope-required-unless-repo-wide": (parsed) => {
      const type = parsed.type ?? "";
      const hasScope = typeof parsed.scope === "string" && parsed.scope.length > emptyScopeLength;

      if (hasScope || scopeExemptTypes.includes(type)) {
        return [true];
      }

      return [
        false,
        `a "${type}" commit must name its lane; only repo-wide docs and tooling (${scopeExemptTypes.join(", ")}) may omit the scope`,
      ];
    },
  },
};

const rules = {
  "header-max-length": [error, "always", headerMaxLength],
  "scope-case": [error, "always", "lower-case"],
  "scope-enum": [error, "always", scopes],
  "scope-required-unless-repo-wide": [error, "always"],
  "subject-empty": [error, "never"],
  "subject-full-stop": [error, "never", "."],
  "type-case": [error, "always", "lower-case"],
  "type-empty": [error, "never"],
  "type-enum": [error, "always", types],
};

// Commitlint resolves its config only through a default export — there is no named-export entry point.
const commitlintConfig = { extends: [], plugins: [scopeRequiredPlugin], rules };

export default commitlintConfig;
