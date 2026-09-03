# @react-native-linux/reanimated

The Linux overlay of https://github.com/software-mansion/react-native-reanimated, vendored at `4.6.0`.

## Layout

- `upstream/` — the vendored tree at the locked tag with the patch queue applied. Checked in, so `pnpm install`
  needs no network, and generated: change it through the commands below, never by hand.
- `patches/` — our deviations from upstream, applied in numeric order.
- `linux/` — our native Linux sources; see `linux/README.md`.
- `src/` — our own JavaScript, when a patch is not the right shape for it. Absent until something needs it:
  the package exports the vendored entry directly, so our typecheck and coverage never reach upstream's sources.
- `e2e/` — conformance scenarios, discovered by `pnpm e2e`.

## Upstream

| Field | Value |
| --- | --- |
| Repository | https://github.com/software-mansion/react-native-reanimated |
| Tag | `4.6.0` |
| Sparse paths | `packages/react-native-reanimated/src` |

`4.6.0` is the only released Reanimated line whose `peerDependencies` accept React Native `0.87`, which is the
version `scripts/vendor.lock.json` pins; `4.5.x` caps at `0.86`. The sparse cone is the TypeScript sources alone —
the JavaScript bring-up rung of issue #178 needs nothing else, and `Common/` arrives on the bump that starts the
native port (#134–#144). `react-native-worklets` is a sibling package in the same monorepo but releases under its
own `worklets-<version>` tags, and `upstream.lock.json` holds exactly one tag, so it becomes `packages/worklets`
when Phase 2 first needs to patch it — not before.

```bash
pnpm upstream:bump reanimated <tag>       # re-vendor at a new tag and replay the patch queue
pnpm upstream:patch reanimated <name>     # capture the current edits to upstream/ as the next patch
pnpm upstream:check reanimated            # prove the vendored tree still matches tag plus queue
```

## Patches

Every patch records what it changes and what deletes it, so that carrying one forever is a decision instead of an
accident. A patch that upstream merges is deleted on the bump that first contains it.

| Patch | What it changes | Deletion trigger |
| --- | --- | --- |
| _none yet_ | | |

The queue is empty on purpose at `4.6.0`. Issue #181 planned `0001` as a flip of Reanimated's `SHOULD_BE_USE_WEB`
disjunction — the runtime branch the resolver shim of #178 cannot reach. That constant was deleted in `4.6.0`:
`src/common/constants/platform.ts` still exports `IS_WINDOWS`, but nothing consumes it, and the web/native split is
now 38 `.native.*` files under `src/` against two in `4.5.0`. A file-suffix split is exactly what
`shouldUseJavaScriptFallback` in `packages/cli/src/metro-config.ts` already steers, so at this tag the bring-up rung
needs no upstream deviation at all. The queue's first entry is expected to be the `react-native-web` import that
`src/ReanimatedModule/js-reanimated/webUtils.ts` performs at module scope, captured with `pnpm upstream:patch` once
a `linux` bundle can prove it; the deletion trigger for the rung as a whole stays #136/#138, and the upstream ask
that would retire our shim is #145.

## Conformance

Scenarios live in `e2e/*.json`, run against a real bundle under the headless compositor by
`pnpm e2e --scenario <scenario name>`, and grade against `e2e/goldens` and `test-bundles` of this package.

## Before the first release

- [ ] Point the `exports` of `package.json` at this library's real entry points; the scaffold guesses `upstream/packages/react-native-reanimated/src/index.ts` and maps only `.`.
- [x] Register `{ linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" }` in
      `packages/cli/src/package-aliases.ts` so that a `linux` bundle resolves this package instead.
- [ ] Drop `"private": true` when the package is published as `@react-native-linux/reanimated`, versioned
      with the platform.
