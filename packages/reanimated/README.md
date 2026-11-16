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
| Sparse paths | `packages/react-native-reanimated/Common/cpp`, `packages/react-native-reanimated/src`, `packages/react-native-reanimated/scripts` |

`4.6.0` is the only released Reanimated line whose `peerDependencies` accept React Native `0.87`, which is the
version `scripts/vendor.lock.json` pins; `4.5.x` caps at `0.86`. The sparse cone is the TypeScript sources and the
shared C++ that `@react-native-linux/core` compiles as `rnl_reanimated` (#139; *react-native-reanimated* in
`docs/cpp-toolchain.md`). `scripts/` is there because `src` imports `scripts/validate-worklets-version.js`, which the
package exports. `react-native-worklets` is a sibling package in the same monorepo but releases under its
own `worklets-<version>` tags, and `upstream.lock.json` holds exactly one tag. Its one JavaScript patch is a pnpm
`patchedDependencies` entry (#671, see *react-native-worklets* in `docs/cpp-toolchain.md`); it becomes
`packages/worklets` only if it needs a patch queue of its own.

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
| `0002-layout-animations-start-surface-lock-order` | `LayoutAnimationsProxy_Legacy::startSurface` reads the mounting coordinator's base revision before it takes its own mutex. `pullTransaction` takes the two in the other order, so a surface the commit hook first sees while that surface mounts on another thread could deadlock; TSan reports the inversion when the proxy attaches to a running surface (#140). | Upstream issue to file: the same order inversion exists on every platform. | The bump to a release whose `startSurface` no longer calls into the coordinator under `mutex`. |
| `0003-operations-loop-registry-cycle` | `OperationsLoop` holds the `UpdatesRegistryManager` weakly. The manager owns the CSS registries and they own the loop, so every `ReanimatedModuleProxy` leaked them, and through `ViewStylesRepository` the `UIManager` and the whole Fabric graph with them; LeakSanitizer reports it on every proxy teardown (#140), which on Linux is every reload. | Upstream issue to file: the cycle exists on every platform. | The bump to a release whose `OperationsLoop` does not own the manager. |
| `0004-backend-mount-hook-node-removals` | With `USE_ANIMATION_BACKEND`, `ReanimatedMountHook::shadowTreeDidMount` still drains removed nodes (`handleNodeRemovals` under the registry lock) before it returns. Upstream returns first (`initializeFabric`: "for now we leave this to leak the memory"), so every unmounted view Reanimated's JavaScript marked removable kept its `ShadowNodeFamily` and its registry and static-props entries for the proxy's lifetime; Linux enables the flag for every app (#140). | Upstream's own TODO in `initializeFabric` names the follow-up. | The bump to a release whose backend path handles node removals. |
| `0005-idle-mapper-runs` | `mappers.native.ts` queues a mapper run only when a mapper starts or stops, one of its inputs changes, or a mapper-run finalizer is requested outside a run. Upstream re-queues the run as a frame finalizer after every run, so an app that starts Reanimated requests a UI-runtime frame every frame, its animation-backend callback never goes idle, and the frame clock ticks at the refresh rate forever (#671). | software-mansion/react-native-reanimated#11237, the Reanimated half (the worklets half is `patches/react-native-worklets@0.12.2.patch`). | The bump to a release that contains #11237. |

## Conformance

Scenarios live in `e2e/*.json`, run against a real bundle under the headless compositor by
`pnpm e2e --scenario <scenario name>`, and grade against `e2e/goldens` and `test-bundles` of this package.

## Before the first release

- [ ] Point the `exports` of `package.json` at this library's real entry points; the scaffold guesses `upstream/packages/react-native-reanimated/src/index.ts` and maps only `.`.
- [x] Register `{ linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" }` in
      `packages/cli/src/package-aliases.ts` so that a `linux` bundle resolves this package instead.
- [ ] Drop `"private": true` when the package is published as `@react-native-linux/reanimated`, versioned
      with the platform.
