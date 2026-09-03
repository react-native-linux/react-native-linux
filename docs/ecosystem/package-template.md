# Package template

Every ecosystem library is one workspace package, `packages/<lib>`, published as `@react-native-linux/<lib>`. The
model is decided in [issue #96](https://github.com/react-native-linux/react-native-linux/issues/96), the streaming
pipeline that fills the package with upstream source is
[issue #179](https://github.com/react-native-linux/react-native-linux/issues/179) and
`docs/ecosystem/upstream-streaming.md`, and this template is
[issue #180](https://github.com/react-native-linux/react-native-linux/issues/180).

```bash
pnpm create-package <lib> <upstream-repo-url> <tag> [--sparse <path>...]
```

`node scripts/create-package.ts` is the same command. `<lib>` is lowercase kebab case, the sparse paths default to
`src`, and the command refuses to write over an existing `packages/<lib>`.

## Layout

```text
packages/<lib>/
├── package.json         # @react-native-linux/<lib>, private, peers on react-native and our core
├── upstream.lock.json   # repo, tag, sparsePaths, "sha256": {} — the bootstrap state the first bump fills
├── upstream/            # the vendored tree, checked in, patches already applied
├── patches/             # 0001-*.patch, applied in numeric order
├── linux/               # our C++ TurboModules, Fabric components, and CMake fragment
├── src/                 # our JavaScript, and the entry point the package exports
├── e2e/                 # conformance scenarios
└── README.md            # the tag, the patch table, the deletion triggers, the conformance command
```

`upstream/` is **checked in**, and nothing about it goes into `.gitignore`. A vendored tree in the repository is
what makes `pnpm install` need no network, a `git clone` enough to build, and a code review able to see what we
actually ship. Nothing is lost by checking it in, because `pnpm upstream:check` reproduces the tree from the lock
and the patch queue on every CI run and fails on any drift — the tree is generated output whose generator is
proven in CI, not a hand-maintained fork.

The scaffold is deliberately incomplete in two places, both listed as checkboxes in the generated `README.md`:

- `exports` maps only `.`, at `./src/index.ts`, and `src/index.ts` re-exports `../upstream/<first sparse
  path>/index.ts`. That path is a guess about where the library keeps its entry point; the maintainer points both
  at the real entry, and adds the upstream subpath exports (`/plugin`, `/babel`, …) that consumers import.
- `package.json` stays `"private": true` until the package is actually published.

## The order of the first commands

```bash
pnpm create-package reanimated https://github.com/software-mansion/react-native-reanimated.git 3.19.0 \
  --sparse packages/react-native-reanimated/src packages/react-native-reanimated/Common
pnpm upstream:bump reanimated 3.19.0     # vendors the tree and fills "sha256" in
pnpm upstream:patch reanimated <name>    # captures edits to upstream/ as the next patch
pnpm upstream:check reanimated           # the drift gate, also run for every package in CI
```

Scaffold and bump belong in one sitting: until the tree is vendored, `src/index.ts` re-exports a file that does not
exist, so `pnpm ts` and `pnpm deadcode` fail on the package.

## Metro alias registration

`packages/cli/src/package-aliases.ts` holds one declarative list:

```ts
const packageAliases: readonly PackageAlias[] = [{ linux: "@react-native-linux/reanimated", upstream: "react-native-reanimated" }];
```

It ships empty; a package registers itself when it can actually replace its upstream. `resolveLinuxPackageAlias`
rewrites a module name when **all three** hold, and returns `null` otherwise, which leaves the name to the rest of
the chain:

1. the active Metro platform is `linux`,
2. the module name is an aliased upstream package name, or a subpath of one (`react-native-reanimated/plugin` →
   `@react-native-linux/reanimated/plugin`; `react-native-reanimated-extra` is not a match),
3. the linux package resolves — an alias for a package that is not installed would turn a working upstream import
   into an unresolvable one.

`metro-config.ts` wires it into the chain as `resolveLinuxModuleName(moduleName, platform, isPackageResolvable)`,
which is the name-rewriting head: the `react-native` subpath overlay of `linuxOverlayIndex` first, then the package
aliases. `docs/platform-identity.md` documents the rest of the chain, and the composition into a real
`resolveRequest` is issue #22's.

## Gates

`upstream/` is third-party source held to upstream's standards, not ours, so it sits outside our repository-wide
gates exactly like `third_party`:

| File | Entry | Why |
| --- | --- | --- |
| `.oxlintrc.json` | `ignorePatterns: packages/*/upstream` | vendored code is upstream's to lint |
| `.oxfmtrc.json` | `ignorePatterns: packages/*/upstream`, `packages/*/upstream.lock.json` | the same, plus a lock whose shape `serializeUpstreamLock` owns |
| `.jscpd.json` | `ignore: **/packages/*/upstream/**`, `**/packages/*/README.md`, `**/packages/*/linux/README.md` | vendored code, and the two templated documents every package repeats |
| `_typos.toml` | `extend-exclude: packages/*/upstream/` | upstream's spelling is upstream's problem |
| `knip.json` | `workspaces: packages/*` with `project: src/**/*.ts` | knip sees only our sources; the vendored tree is not dead code |

Our own sources in `packages/<lib>/src`, `linux/` and `e2e/` stay inside every gate. The one deviation is an
`.oxlintrc.json` override that turns `import/no-relative-parent-imports` off for `packages/*/src/index.ts`, because
the entry point re-exports the vendored tree that sits beside `src/`, and `upstream/` is not a package it could
import by name.

`pnpm-workspace.yaml` already globs `packages/*`, so a scaffolded package is a workspace with no edit at all.

## Conformance

A package ships scenarios as `packages/<lib>/e2e/*.json`, in the format `scripts/e2e/scenario.ts` parses.
`scripts/e2e/discovery.ts` finds every `packages/*/e2e/*.json` and grades each scenario against the
`test-bundles/` and `e2e/goldens/` of the package that ships it, so a package's conformance never depends on
`packages/core`'s fixtures. Both halves are pure functions over an injected filesystem seam, unit tested at 100%;
the driver itself is unchanged apart from asking discovery instead of one hardcoded directory.

```bash
pnpm e2e                          # every package's scenarios
pnpm e2e --scenario <name>        # one of them
```

## Publishing

A package is published as `@react-native-linux/<lib>` and versioned with the platform: the overlay is only
meaningful against the `@react-native-linux/core` it was built for, which is also why `core` and `react-native` are
`peerDependencies` rather than dependencies. Until the first publish the package stays `private`, and the alias
list keeps it out of consumers' resolution until it can replace its upstream.
