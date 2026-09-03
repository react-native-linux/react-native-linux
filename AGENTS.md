# AGENTS.md

## Prime Directive: No Overengineering

This rule outranks every other rule in this file. Solve every task with the **minimal working code** that satisfies the acceptance criteria and the test layers — nothing more.

- Reduce complexity at every decision: fewer abstractions, fewer files, fewer layers, fewer options. Add an abstraction only after the second concrete consumer exists, never for an imagined future one.
- No speculative generality: no plugin systems, no renderer-agnostic interfaces, no configuration for things with one value, no "tier" scaffolding before the first tier works end to end.
- Prefer deleting code to adding it. Prefer a 40-line direct implementation to a 200-line "extensible" one. Prefer copying a reference implementation's simple approach over inventing a clever one.
- Reuse what upstream already ships before writing anything: `ReactCxxPlatform`, the generic `cxx` renderer platforms, RN's own JSI conformance suite, Skia's modules. The best code for this project is code we did not write.
- The quality bar (tests, sanitizers, coverage) is not overengineering — it is how minimal code stays minimal. Everything else is guilty until proven necessary.

react-native-linux is a GPU-first out-of-tree React Native platform for desktop Linux: Fabric rendered through Skia (Graphite/Vulkan) into a Wayland surface, Hermes for JavaScript, New Architecture only. The founding decision record is `docs/adr/0001-gpu-first-out-of-tree-react-native-platform-for-linux.md`; read it before proposing architectural changes. Amendments to it require a new ADR in `docs/adr/`.

## Canonical Agent Surfaces

- `AGENTS.md` is the canonical instruction file. `CLAUDE.md` is a symlink to it.
- Project skills live in `.agents/skills`; `.claude/skills` entries are symlinks to them.
- Keep instruction text provider-neutral.

## Planned Structure

```text
packages/
├── core/           # @react-native-linux/core — C++ platform: Fabric host, renderer, modules
├── cli/            # @react-native-linux/cli — init template, Metro platform registration
├── test-harness/   # @react-native-linux/test-harness — e2e driver, golden-image rig, perf gates
└── virtual-modules/# JS-side platform module implementations
docs/
├── adr/            # Architecture decision records
└── research/       # Prior-art surveys and calibration reports
```

## Reference Implementations

Never design platform plumbing from scratch without first checking how the maintained desktop platforms did it:

- **react-native-windows** (microsoft/react-native-windows) — the structural model: out-of-tree platform layout, `vnext/` C++ Fabric composition renderer, platform overrides, codegen wiring, CI shape.
- **react-native-macos** (microsoft/react-native-macos) — fork-based platform maintenance, upstream-merge cadence, desktop interaction patterns (pointer, keyboard, menus).
- **react-native-harmony** and **react-native-skia/react-native-skia** (Kudo/NAGRA, dormant) — out-of-tree New Architecture registration and Skia-based RN rendering respectively.

Use the `rn-reference` skill for how to mine these efficiently.

## Engineering Rules

1. C++20 for `core`; enforced by `clang-format` and `clang-tidy` configs at the root. No raw owning pointers; RAII everywhere.
2. TypeScript strict mode everywhere. Do not use `any`; model unknown data as `unknown`, validate, then narrow. No `as` type assertions (`as const` is allowed), no `@ts-ignore`/`@ts-expect-error`.
3. No explanatory comments; prefer clearer names and smaller functions. Comments only for constraints the code cannot express (protocol quirks, upstream bug workarounds — with links).
4. Full descriptive names. No abbreviations like `cfg`, `idx`, `ctx`, `mgr`.
5. Every renderer feature must be observable: expose it through the test harness (golden image, event trace, or frame-timing probe) in the same PR that adds it.
6. Threading contracts are load-bearing: JS thread, commit/layout thread, render thread. Any code crossing threads states its contract in the type or the class docblock and is covered by a TSan-clean test.

## Testing Gospel (non-negotiable, day one)

- **Unit, C++:** GoogleTest via CTest. Coverage with `llvm-cov`; the CI gate is 100% line and branch coverage for `packages/core` source, with exclusions allowed only via `// COV_EXCL` markers that name a reason and are reviewed like API changes.
- **Unit, TypeScript:** Vitest with V8 coverage, 100% thresholds in `vitest.config.ts` — statements, branches, functions, lines.
- **Sanitizers:** ASan+UBSan and TSan jobs run the full C++ suite on every PR. A sanitizer failure blocks merge; no suppression files without an issue link.
- **Golden-image tests:** every visual capability renders fixtures headlessly (lavapipe software Vulkan + headless Wayland compositor) and compares against checked-in goldens with a perceptual diff threshold. Goldens update only via an explicit regeneration command reviewed in the PR diff.
- **E2E:** the test-harness drives a real app bundle under a headless compositor with virtual Wayland input (pointer, keyboard, IME later), asserting on screenshots, event traces, and `wp_presentation` frame timings.
- **Performance gates:** frame-time budget tests (8.33 ms target scenarios) run in CI and fail on regression beyond threshold; numbers are tracked, not vibed.
- New components and renderer features land with all applicable layers in the same PR: unit + golden + e2e. A PR that adds behavior without tests is incomplete by definition.

See the `testing-gospel` skill for the harness architecture and commands.

## Toolchain

Package manager is **pnpm** (workspaces, `catalog:` version pinning, version locked via corepack `packageManager` field). pnpm's strict symlinked layout makes phantom dependencies impossible — do not weaken it with global hoisting; if Metro ever chokes on symlinks in the test-harness app, scope `node-linker=hoisted` to that package only. Everything JS-side is **TypeScript** — CLI, codegen driver, harness, and all repo scripts (run via `tsx`); no plain JavaScript files.

One entry point mirrors CI exactly: `pnpm validate` (format check, typecheck, lint, deadcode, duplication, meta-file checks). Agents run it before finishing any change; if it passes locally it passes in CI. Individual commands:

- `pnpm format` / `pnpm format:check` — **oxfmt** (Oxc formatter). `pnpm lint` — **oxlint** at max config: every rule category enabled (`correctness`, `suspicious`, `pedantic`, `style`, `restriction` — deviations are per-rule opt-outs with reasons, not category opt-outs) plus the full plugin set (`eslint`, `typescript`, `react` — which contains the hooks rules, `unicorn`, `import`, `promise`, `jsx-a11y`, `node`, `oxc`; listing `plugins` overwrites oxlint's defaults, so `eslint` must be explicit). Anti-slop custom rules (comment bans, wrapper bans, naming rules from this file) are added as oxlint JS plugins as they prove enforceable. No ESLint, no Prettier, no Biome.
- `pnpm ts` — `tsc --noEmit`, maximum strictness: `strict`, `noUncheckedIndexedAccess`, `exactOptionalPropertyTypes`, `isolatedModules`.
- `pnpm deadcode` — **knip**: unused files, exports, and dependencies across the workspace.
- `pnpm cpd` — **jscpd** duplication check.
- C++: **clang-format** and **clang-tidy** (curated set: `bugprone-*`, `performance-*`, `modernize-*`, selected `cppcoreguidelines-*`) run through the CMake presets. `CMAKE_EXPORT_COMPILE_COMMANDS=ON` is always set so clangd works from a fresh checkout. A `NOLINT` needs a stated reason and is reviewed like an API change.
- Meta-files: **actionlint** (workflows), **shellcheck** + **shfmt** (scripts), **typos** (spelling), **gitleaks** (secrets); **lychee** link-checks docs weekly in CI.
- **Renovate** keeps pinned dependencies fresh; PR titles are gated on Conventional Commits.

Deliberately excluded until proven necessary (Prime Directive): cppcheck, include-what-you-use, pre-commit hook frameworks, markdownlint.

## Git Commits And Pull Requests

Conventional Commits for commit messages and PR titles: `type(scope): short description`. Scopes: `core`, `cli`, `harness`, `modules`; omit for repo-wide docs and tooling. Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`, `perf`, `build`.

Never mention AI tools, bots, generated output, co-authors, or automation services in commits, PR titles, or PR descriptions.

## Issue Tracking

Work is tracked as GitHub issues under milestone labels `M0`–`M5` (roadmap in ADR-0001), linked as sub-issues of the founding epic. New work gets an issue before a branch. Issues state acceptance criteria including the required test layers.

## Bot Reviews

Automated reviewers (CodeRabbit today, any review bot after it) review every pull request, and their findings are addressed the same way a human reviewer's are:

- **Valid finding — fix it** in the PR, in the same commit series as the change it comments on.
- **Invalid finding — answer it** in the PR with the specific, checkable reason it will not be implemented (a rule in this file, an ADR, a measured number, or the lane boundary that owns the file). "Will not be implemented" without the why is not an answer.

Neither ignored nor blanket-dismissed findings count as addressed. A PR is not ready to merge while a bot finding is open without a fix or a written answer. Fixing a finding inside another lane's file still goes through the lane's owner, exactly like any other edit.

Every bug and every accepted finding — from a bot, a human, or an issue report — is covered by a real-world test: a test that reproduces the scenario as the reporter hit it, through the real commit path, event pipeline or window, not a stub written to make the diff green. The fix and its reproducing test land in the same PR; a bug fix whose test only exercises the code's internals rather than the reported scenario is incomplete, and the test names the issue or report it reproduces.

## Multi-Agent Protocol

Several agents work this repository at once, with different models. Two agents on one issue is worse than one
agent idle, and a silent collision costs more than both. This section is the whole coordination system; it is
GitHub issues and labels and nothing else, because a second store would drift.

### The state of an issue is a label

| Label | Means |
| --- | --- |
| *(none)* | Untriaged. **Not eligible to pick up.** |
| `status:ready` | Groomed, unblocked, free to take. |
| `status:in-progress` | Claimed by the agent named in the `agent:*` label beside it. |
| `status:blocked` | Waiting on another issue or on a decision, named in a comment. |
| `status:review` | A pull request is open; the issue closes when it merges. |

Exactly one status label at a time. An issue with `status:in-progress` also carries exactly one `agent:*` label —
`agent:claude-opus-5`, `agent:glm-5.3-flash`, one per model, created as new models join.

### What to work on

Lanes first, priority second. **Lanes are how collisions are prevented structurally**, rather than by two agents
being lucky:

| Lane | Areas | Files |
| --- | --- | --- |
| Renderer and text | `area:renderer`, `area:text`, `area:animation` | `packages/core/src/{RetainedScene,ScenePainter,Text*,Scroll*,Image*,Golden*,BundleRunner}.*`, `goldens/`, `test-bundles/` |
| Layout and conformance tests | `area:testing`, layout issues | `packages/core/tests/*Test.cpp` only |
| Input and windowing | `area:input`, `area:infra` | `packages/core/src/{Input*,Wayland*,Window*}.*` |
| CLI, modules, ecosystem | `area:cli`, `area:modules`, `area:packaging` | `packages/cli`, `packages/*/`, `scripts/` |

Within your lane, take the **first** issue by: `priority:P0` before P1 before P2 before P3, then lowest issue
number. That rule is deterministic, so two agents in one lane reading the same board pick the same next issue and
the claim protocol below decides it in one round rather than in a merge conflict.

```bash
# The board: what is free in a lane, in the order it should be taken
gh issue list --state open --label status:ready --label area:renderer --limit 50 \
  --json number,title,labels \
  --jq 'sort_by(([.labels[].name | select(startswith("priority:"))] + ["priority:P9"])[0], .number)
        | .[] | "#\(.number) \(.title)"'

# What is already being worked, by whom
gh issue list --state open --label status:in-progress \
  --json number,title,labels,updatedAt \
  --jq '.[] | "#\(.number) \(.updatedAt) [\([.labels[].name | select(startswith("agent:"))] | join(","))] \(.title)"'
```

### Claiming, and the race

Claiming is optimistic and then **verified**, because two agents can label the same issue in the same second:

```bash
gh issue edit <N> --add-label status:in-progress --add-label agent:<model> --remove-label status:ready
gh issue comment <N> --body "CLAIM $(date -u +%Y-%m-%dT%H:%MZ) — model: <full model name> — session: <id>
Scope: <what of this issue you are doing>
Files: <every path you will edit>
Expires: $(date -u -d '+4 hours' +%Y-%m-%dT%H:%MZ)"

sleep 10 && gh issue view <N> --json labels,comments \
  --jq '[.labels[].name | select(startswith("agent:"))], (.comments | map(select(.body | startswith("CLAIM"))) | last | .body)'
```

If that read shows another agent's `agent:*` label or a `CLAIM` comment newer than yours, **you lost the race**:
remove your label, say so in a comment, and take the next issue. The agent whose claim comment is *oldest* wins.

The first line of the comment is fixed so it can be grepped, and the model name is the full one —
`claude-opus-5`, `glm-5.3-flash` — never "the agent" or "Claude". When two agents disagree about an approach it
matters which model wrote what.

### Holding, releasing, finishing

- **A claim expires after four hours.** Agents are killed by rate limits mid-task, so an abandoned claim is
  normal rather than a conflict. If the newest `CLAIM` has expired and no commit since names the issue, take it
  over with a comment saying so, then re-claim. If you are still working, post a new `CLAIM` line to extend it.
- **Release whenever you stop**, finished or not:

  ```bash
  gh issue edit <N> --remove-label status:in-progress --remove-label agent:<model>
  gh issue comment <N> --body "RELEASE $(date -u +%Y-%m-%dT%H:%MZ) — model: <full model name>
  Landed: <commits>. Left: <what is not done, and why>."
  ```
- **Opening a PR** swaps `status:in-progress` for `status:review` and the PR body says `Closes #N`. Merging
  closes the issue; the closing comment states what landed and what was deferred, with the issue that now owns
  each deferral.
- **Partial delivery is normal and is not a close.** Report what is proved, name what is not, and release.

### File ownership

The lane table is the default split; the `Files:` line of a claim is the exact one. Two agents may hold issues
that touch one file only if both claim comments say so and agree the split. When in doubt the rule is: the agent
whose lane owns the file owns the edit, and the other one asks in the issue.

