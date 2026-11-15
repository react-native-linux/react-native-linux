# @react-native-linux/expo

Expo on Linux, as [ADR-0004](../../docs/adr/0004-expo-javascript-runs-unmodified-over-the-native-module-seam.md)
decides it: each Expo package's own JavaScript runs unmodified, over `expo-modules-core`'s JavaScript `globalThis.expo`
(installed by the resolver rule in `packages/cli/src/package-variants.ts`) and a C++ TurboModule of this platform
registered under the module's Expo native-module name. A package alias to a subpath here is the escape hatch for a
module whose JavaScript has to differ; no module uses it yet.

| Module | Written against | Seam or alias | Native module | Answers |
| --- | --- | --- | --- | --- |
| `expo-constants` | 58.0.15 | seam | `ExponentConstants` | every required `NativeConstants` field for a bare desktop app; `deviceName` is the host name; `manifest`, and so `expoConfig`, is `null` until a `linux` build embeds the app config (#156); `systemFonts` is empty; `getWebViewUserAgentAsync` resolves `null` |

`src/` holds each module's required-field list, typed against upstream's declarations. `test-bundles/` and `e2e/`
are the scenarios `pnpm e2e` discovers; each installs the real upstream package.
