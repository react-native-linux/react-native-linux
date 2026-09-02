# Doctor

`pnpm doctor` is a clean-machine preflight check. It looks at the running machine — not at the
repository — and reports which of `docs/cpp-toolchain.md`'s prerequisites are present, which are
missing, and the exact distro package to install for each one. It never builds, configures, or
installs anything; every probe is a read-only inspection (`--version`, `--exists`, a directory
listing, an environment variable read).

```bash
pnpm doctor                 # every tier
pnpm doctor --core          # just the hello_react baseline
pnpm doctor --window        # + the Wayland/Vulkan/Skia stack rnl_window needs
pnpm doctor --goldens       # + the weston/lavapipe window-golden rig
pnpm doctor --coverage      # + llvm-cov/llvm-profdata for the C++ coverage gate
pnpm doctor --json          # machine-readable report on stdout
pnpm doctor --distro arch   # pin the remedy column instead of reading /etc/os-release
```

Tiers are additive when combined (`pnpm doctor --core --window`) and default to all four when no
tier flag is given. The command exits `0` only when every **required** check in the selected
tiers passes. A missing check that has a documented fallback — Boost and glog, which
`packages/core/CMakeLists.txt` fetches from source when `find_package` fails, and ccache and
clang, which are recommended but not load-bearing — is reported as a warning and never fails the
exit code, matching the graceful-degradation behaviour `RNL_ENABLE_SKIA`/`RNL_ENABLE_WINDOW`
already have at configure time.

## Tiers

| Tier | What it proves | Checks |
| --- | --- | --- |
| `core` | `hello_react` builds: the bridgeless Hermes host, no Skia, no window | CMake ≥3.28, Ninja, Git, Python, ccache*, clang*, double-conversion, fmt, ICU, Boost*, glog* |
| `window` | `rnl_window` builds and can reach a compositor | pkg-config, FreeType, Fontconfig, Wayland client, Wayland protocols, wayland-scanner, xkbcommon, Vulkan loader and headers, a live `WAYLAND_DISPLAY`* |
| `goldens` | the headless window-golden rig (`scripts/window-golden.ts`) can render | Weston, a lavapipe ICD or `libvulkan_lvp.so` |
| `coverage` | the C++ coverage gate (`scripts/cpp-coverage.ts`) can run | llvm-cov, llvm-profdata |

`*` marks an optional, warn-only check.

## Adding or changing a check

The whole registry is one table: `scripts/doctor/doctor-checks.ts`. Each entry names the tool,
states in one sentence what breaks without it (usually a direct reference to the
`packages/core/CMakeLists.txt` `find_package`/`pkg_check_modules` call or the `docs/cpp-toolchain.md`
paragraph that documents it), declares whether it is required, and gives the exact `pacman`/`apt-get`
remedy. `scripts/doctor/evaluate-check.ts` is the probe engine — it takes an injected
`ProbeEnvironment` (a handful of read-only ports: run a command, list a directory, read an env var,
say whether lavapipe is present) so its ~65 Vitest cases run against fixture environments and never
touch the real machine. `scripts/doctor/format-report.ts` renders the human and `--json` reports.
`scripts/doctor.ts` is the only file that talks to the real machine: it wires the pure engine to
real `spawnSync`/`fs`/`process.env` calls and to `scripts/window-golden.ts`'s exported
`findLavapipeIcdManifestPath`/`findLavapipeLibraryPath`, which is the same lavapipe search the
window-golden rig itself uses — one detector, not two.

**Placement note.** The engine lives under `scripts/doctor/` rather than `packages/cli` today.
`packages/cli` would be the right home once it is a real workspace dependency of the root package —
the doctor's CLI entry point needs to import both the pure engine and `scripts/window-golden.ts`'s
lavapipe detector, and this repo's oxlint config (`import/no-relative-parent-imports`) forbids any
`../` import crossing a directory boundary, in either direction, without one. Wiring that dependency
needs a `pnpm install`, which is out of scope here; moving the engine is a mechanical follow-up once
it lands.
