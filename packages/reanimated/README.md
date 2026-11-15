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
| Sparse paths | `packages/react-native-reanimated/Common/cpp`, `packages/react-native-reanimated/src` |

`4.6.0` is the only released Reanimated line whose `peerDependencies` accept React Native `0.87`, which is the
version `scripts/vendor.lock.json` pins; `4.5.x` caps at `0.86`. The sparse cone is the TypeScript sources and the
shared C++ that `@react-native-linux/core` compiles as `rnl_reanimated` (#139; *react-native-reanimated* in
`docs/cpp-toolchain.md`). `react-native-worklets` is a sibling package in the same monorepo but releases under its
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

| Patch | What it changes | Upstream ask | Deletion trigger |
| --- | --- | --- | --- |
| `0001-generic-platform-synchronous-props` | On a platform that is neither Android nor Apple, `SynchronouslyUpdateUIPropsFunction` takes Apple's `(int, const folly::dynamic&)` shape and `applySynchronousUpdates` calls it the Apple way. Without it `PlatformDepMethodsHolder.h` does not compile, and the synchronous props batch would be dropped. | RFC ask 1 (`docs/research/animation-on-linux.md`, appendix A) | The bump to a release whose `PlatformDepMethodsHolder.h` has an `#else` branch. |

## Conformance

Scenarios live in `e2e/*.json`, run against a real bundle under the headless compositor by
`pnpm e2e --scenario <scenario name>`, and grade against `e2e/goldens` and `test-bundles` of this package.

## Before the first release

- [ ] Point the `exports` of `package.json` at this library's real entry points; the scaffold guesses `upstream/packages/react-native-reanimated/src/index.ts` and maps only `.`.
- [x] Register `{ linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" }` in
      `packages/cli/src/package-aliases.ts` so that a `linux` bundle resolves this package instead.
- [ ] Drop `"private": true` when the package is published as `@react-native-linux/reanimated`, versioned
      with the platform.
