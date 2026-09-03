# Session handoff — 2026-09-03, 13:40 Vienna

Written for whoever picks this up next. Everything below is verified against the repository and the
issue tracker at the time of writing, not recalled.

## Where things stand

`main` is at `716d35b`, and CI run **33742719968 is green on all seven jobs**. That is the last
pushed commit and the last CI verdict; nothing pushed is red.

The tracker has **130 open issues**: M0 1, M1 46, M2 16, M3 18, M4 40, M5 7, unassigned 2. GitHub caps
an issue at 100 sub-issues, so epic #1 now holds six milestone epics (#172 M0 … #177 M5) and new
issues go under the milestone epic, never directly under #1. Epic #96 owns the ecosystem programme
separately.

## Uncommitted work in the tree — read this first

Two agents were killed mid-task by an Opus session rate limit (resets 13:40 Vienna). Their partial
work is on disk, uncommitted, and **the working tree will not pass `pnpm validate` as it stands**.

```
 M docs/cpp-toolchain.md            docs/prop-coverage.json   docs/prop-coverage.md
 M packages/core/goldens/golden.spec.ts
 M packages/core/src/RetainedScene.{cpp,h}   packages/core/src/ScenePainter.{cpp,h}
 M packages/core/tests/CMakeLists.txt
 ?? packages/core/test-bundles/border-matrix.js   packages/core/test-bundles/rounded-box.js
 ?? packages/core/tests/BorderGeometryTest.cpp
```

That is issues **#99** (one rounded box: fill, ring, clip, content and hit region all derive from one
geometry) and **#100** (border painting matrix: per-side colours, transparent edges, hairlines,
corner seams). The agent had reached the point of documenting its two new golden fixtures. To
continue, either resume that work or `git checkout` the modified files and `rm` the untracked ones —
but read `BorderGeometryTest.cpp` first, it is the substance.

A second agent was killed at the same moment with **nothing written**: issues **#108** (decoded image
lifetime — a bitmap dies with its node) and **#109** (programmatic scroll must not feed itself). Its
brief is in the session transcript; the essentials are in those two issues.

## How this session was run

Three standing rules from the user, all still in force:

- **All implementation by subagents**, never more than **two concurrent** — count before spawning.
  Subagents cannot write via Bash in this repository (sandbox); they use Read/Edit/Write only, and
  the coordinator runs builds, gates, git and CI.
- **No overengineering.** The Prime Directive is the first section of AGENTS.md and it is enforced:
  prefer deleting a branch to adding a test for it.
- **Every user remark gets grilled into a GitHub issue** under the right epic before work starts.

The loop that worked: spawn agent → agent reports → coordinator builds, runs the coverage gate, runs
`pnpm validate`, commits by **explicit paths** (never `git add -A` while an agent is in flight) →
push → watch CI → close the issue with evidence.

## Hard-won facts you will otherwise rediscover

**Gates.** `pnpm validate` is format, ts, lint, deadcode, cpd, test, meta. jscpd runs at threshold 0,
so every new test file trips it — shared fixtures live in `packages/core/tests/SceneTestSupport.h`.
The C++ gate is `node scripts/cpp-coverage.ts`, 100% line **and** branch on the files listed in its
`scopedSourcePaths`. Commit chains must gate on both coverage and cpd before committing; one push
went out red because they did not.

**oxfmt's `ignorePatterns` does not match a vendored tree nested under a workspace package.** Neither
`packages/*/upstream/**` nor `**/upstream/**` nor a bare name works. The ignore lives in
`.prettierignore`, which oxfmt reads by default. This cost half an hour; do not re-litigate it.

**A paint-less `<View>` is flattened by Fabric before the diff**, so test views must carry a real
`backgroundColor` or the mounting layer sees nothing at all.

**`std::string::replace(pos, len, {})`** resolves `{}` to a null `const char*` and segfaults. Use
`erase`.

**Debug builds abort at exit** if any `jsi` handle outlives the runtime. `~ReactHost` releases the
TurboModule registry and the animated-nodes provider before `reactInstance_`. `TimerManager` has the
same latent hazard, filed as **#171**.

**In the animation path**, `handleAnimatedEvent` drops events unless a prior `pullAnimationMutations`
has claimed the render thread — hence one warm-up frame in the headless runner. The shared backend
also needs `connectAnimatedNodeToShadowNodeFamily`, not just `connectAnimatedNodeToView`.

**e2e runs under `cage`, not weston** — weston ships neither virtual-input protocol on Ubuntu. Input
is frame-batched, so two keystrokes in one frame produce **one** `topChange`; expectations must not
assume per-key events. cage's output is 1280x720, so the window goldens are not a valid e2e baseline.

## What landed today

Animation phase 1 is essentially complete: #127 #128 #129 #130 #131 #132 #122 #124 (unit half) #97
#121, plus #7's first two slices (the cage driver and the `wp_presentation` frame-timing probe).

Tooling and strategy: #179 the upstream streaming pipeline, #180 the package template, #69 the
prop-coverage ledger, #159 ADR-0002, #158 the support contract, #182 the catch-up playbook, and #181
the first overlay package.

**The strategic decision that reframes the project** (made by the user today, recorded on #96, #1 and
in ADR-0002): this is a validation-phase project that nobody upstream supports, so every library
integration is ours to build. One monorepo package per library, `packages/<lib>` published as
`@react-native-linux/<lib>`, upstream vendored at a pinned tag with an ordered patch queue and a
drift gate, Metro aliasing the upstream name to ours on `linux`, and the community ask only after a
package works. This **reverses** §6.5 of the ecosystem survey, deliberately and with the reason
recorded.

`packages/reanimated` proves the model: 538 files vendored at 4.6.0, drift gate green. Two findings —
the tag is forced (only released line whose peer range admits React Native 0.87), and the patch we
planned cannot exist because 4.6.0 deleted `SHOULD_BE_USE_WEB` in favour of a file-suffix split that
the #178 resolver shim already steers. The queue is empty by correctness.

## Suggested next moves

1. **Finish or discard the uncommitted #99/#100 work** — it is the only thing standing between the
   tree and a clean `pnpm validate`.
2. **#108 and #109** were never started and are well-specified.
3. **M1 has 46 open issues**, mostly mined conformance tests (#98–#120) that are individually small
   and independent — good parallel work for two agents.
4. **#124's perf half** (N-node and animate-while-scrolling scenarios, artifact trend) and **#144**
   (Reanimated conformance) are the natural continuations of today's animation work.
5. The **ecosystem port ladder** starts at Nitro platform files, then `react-native-safe-area-context`
   — #161–#170, now to be delivered as `packages/<lib>` overlays under the new model.

## Where the durable notes live

`~/.claude/projects/-home-vitalyiegorov-suuudokuuu/memory/react-native-linux-project.md` carries the
running status log and every hazard above in compressed form. Read it before the transcript.
`docs/cpp-toolchain.md` is the engineering ledger — every feature has a section, and the fix ledger
at the top is real. `docs/ecosystem/` holds the four strategy documents.
