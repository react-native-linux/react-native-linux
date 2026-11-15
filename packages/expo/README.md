# @react-native-linux/expo

Expo on Linux, as [ADR-0004](../../docs/adr/0004-expo-javascript-runs-unmodified-over-the-native-module-seam.md) decides
it: each Expo package's own JavaScript runs unmodified, over `expo-modules-core`'s JavaScript `globalThis.expo` and a
C++ TurboModule of this platform registered under the module's Expo native-module name. The resolver rule in
`packages/cli/src/package-overlays.ts` swaps in this platform's polyfill, which installs upstream's and answers
`getViewConfig` with `null`, upstream's answer for a view that is not linked: `requireNativeView` mounts an empty view
after upstream's warning, until the first registered Expo view lands (#165). A subpath here, resolved instead of the
upstream file, is the escape hatch for a module whose JavaScript has to differ; `fetch` is the only one.

| Module | Written against | Seam or alias | Native module | Answers |
| --- | --- | --- | --- | --- |
| `expo` | 58.0.23 | seam, and an escape hatch for `fetch` | none of its own; `ExpoFetchModule` waits on #663 (#673) | `registerRootComponent` and `Expo.fx` run unmodified; the global `fetch` and `expo/fetch` are React Native's own `fetch` (`src/fetch.ts`), without a streaming body |
| `expo-application` | 58.1.2 | seam | `ExpoApplication` | `applicationId`, `applicationName`, `nativeApplicationVersion` and `nativeBuildVersion` are `null` until a `linux` build embeds them (#648); the installation-time, referrer, vendor-id and store queries are not answered, so upstream rejects them with `ERR_UNAVAILABLE` |
| `expo-asset` | 58.0.21 | seam | `ExpoAsset` | `downloadAsync` resolves a `file://` URL unchanged, decodes a base64 `data:` URL, and downloads any other once, off the JavaScript thread and following redirects, into `$XDG_CACHE_HOME/<app-id>/expo-asset`, as upstream's iOS module does |
| `expo-constants` | 58.0.15 | seam | `ExponentConstants` | every required `NativeConstants` field for a bare desktop app; `deviceName` is the host name; `manifest`, and so `expoConfig`, is `null` until a `linux` build embeds the app config (#156); `systemFonts` is empty; `getWebViewUserAgentAsync` resolves `null` |
| `expo-device` | 58.0.5 | seam | `ExpoDevice` | the machine: `manufacturer` and `modelName` from `/sys/class/dmi/id`, `osName`, `osVersion` and `osBuildId` from `os-release`, `osInternalBuildId` and `supportedCpuArchitectures` from `uname`, `totalMemory`, the host name as `deviceName`, `DeviceType.DESKTOP`, the monotonic uptime and `isRootedExperimentalAsync` as "runs as root"; `brand`, `modelId` and `deviceYearClass` are `null`; the Android-only queries reject with `ERR_UNAVAILABLE` |
| `expo-file-system` | 58.0.13 | seam | `FileSystem` | `File`, `Directory` and `Paths` over `makeSharedObjectClass` (#663): `Paths.document` is `$XDG_DATA_HOME/<app-id>/Documents`, `Paths.cache` is `$XDG_CACHE_HOME/<app-id>`; existence, size, create, delete, text, base64 and bytes reads and writes, copy, move and listing, with the asynchronous methods' file work on a worker thread; file handles, downloads, uploads, watching, digests, pickers and previews are absent; `Paths.join` needs the WHATWG `URL` `expo`'s runtime installs |
| `expo-linking` | 58.0.16 | seam | `ExpoLinking` | `getLinkingURL` is the URL that last activated the app, the state React Native's `Linking` reads; `onURLReceived` delivers a later activation as `{ url }`; `clearInitialURL` is not answered, so it is the no-op it is on web; `createURL` throws until a `linux` build embeds the app config (#156) |
| `expo-network` | 58.0.4 | seam | `ExpoNetwork` | `getNetworkStateAsync` resolves `{ type: 'UNKNOWN' }` until the NetworkMonitor portal is read (#648); a listener subscribes and is never called while the state stays unknown; the IP-address and airplane-mode queries reject with `ERR_UNAVAILABLE` |

`src/` holds each module's required-field list, typed against upstream's declarations. `test-bundles/` and `e2e/`
are the scenarios `pnpm e2e` discovers; each installs the real upstream package.
