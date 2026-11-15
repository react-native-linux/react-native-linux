# @react-native-linux/expo

Expo on Linux, as [ADR-0004](../../docs/adr/0004-expo-javascript-runs-unmodified-over-the-native-module-seam.md)
decides it: each Expo package's own JavaScript runs unmodified, over `expo-modules-core`'s JavaScript `globalThis.expo`
(installed by the resolver rule in `packages/cli/src/package-variants.ts`) and a C++ TurboModule of this platform
registered under the module's Expo native-module name. A subpath here, resolved instead of the upstream file, is the
escape hatch for a module whose JavaScript has to differ; `fetch` is the only one.

| Module | Written against | Seam or alias | Native module | Answers |
| --- | --- | --- | --- | --- |
| `expo` | 58.0.23 | seam, and an escape hatch for `fetch` | none of its own; `ExpoFetchModule` waits on #663 (#673) | `registerRootComponent` and `Expo.fx` run unmodified; the global `fetch` and `expo/fetch` are React Native's own `fetch` (`src/fetch.ts`), without a streaming body |
| `expo-asset` | 58.0.21 | seam | `ExpoAsset` | `downloadAsync` resolves a `file://` URL unchanged and downloads any other once into `$XDG_CACHE_HOME/react-native-linux/expo-asset`, as upstream's iOS module does |
| `expo-constants` | 58.0.15 | seam | `ExponentConstants` | every required `NativeConstants` field for a bare desktop app; `deviceName` is the host name; `manifest`, and so `expoConfig`, is `null` until a `linux` build embeds the app config (#156); `systemFonts` is empty; `getWebViewUserAgentAsync` resolves `null` |

`src/` holds each module's required-field list, typed against upstream's declarations. `test-bundles/` and `e2e/`
are the scenarios `pnpm e2e` discovers; each installs the real upstream package.
