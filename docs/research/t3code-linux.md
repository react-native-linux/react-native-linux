# T3 Code on Linux: the compatibility ledger and the desktop feature profile

- Research note, 2026-11-15, for epic #634 and issue #635. Source: `apps/mobile` of
  [pingdotgg/t3code](https://github.com/pingdotgg/t3code) at `238678951ac4` (Expo 58, react-native 0.88.1,
  React Navigation 7).
- Method. The transitive closure of `apps/mobile` in T3 Code's `pnpm-lock.yaml` (806 packages, workspace packages
  followed), each package checked for native code in its published files: `android/`, `ios/`, `apple/`, `cpp/`, a
  podspec, `expo-module.config.json`, `react-native.config.js` or a `codegenConfig`. Then an import walk from the
  app's `index.ts` with platform `linux`'s resolution order (`.linux`, then `.native`, then the shared file), which
  reaches 961 of the app's files.
- The rows themselves live in `packages/cli/src/ecosystem-matrix.ts`, the ledger #87 started for Suuudokuuu. Its
  spec reads the table in §4 and fails when a package's status or owner here disagrees with its row there.

## 1. Findings

1. **The whole app loads at startup.** `index.ts` statically reaches 46 packages with native code. Most Expo
   modules call `requireNativeModule` at import time, and that throws when the module is missing, so one absent
   module stops the bundle before the first frame. The ones built on `requireOptionalNativeModule`, such as
   `expo-haptics`, import cleanly and throw `UnavailabilityError` at the first call instead. A package therefore
   needs a Linux implementation, or the Linux entry must cut the import or resolve it to a stub. Choosing which is
   the desktop feature profile's job (§2).
2. **76 rows.** 56 direct packages with native code, 17 transitive ones, and 3 JavaScript packages over a native
   library (`@clerk/expo`, `expo-auth-session`, `@tabler/icons-react-native`), listed under the ledger's existing
   rule. 59 are tracked: 51 by issues that already existed, 8 by the five this note filed (#646–#650). 17 are declined:
   8 left out on the desktop and 6 replaced (§2), and 3 with no place in a desktop application.
3. **The shared files are written for iOS.** T3 Code splits views into `.ios`, `.android` and a shared file. On
   Linux Metro resolves the shared file, not the Android one, and eight shared views import
   `@expo/ui/swift-ui` (`Progress`, `NumberField`, `SliderRow`, `NativeThemeHost`, `CustomSnoozeSheet` and three
   widget files). `AppSymbol` imports `expo-symbols`. `thread-work-log` and the scheduled-task editor import
   `@expo/ui/community/masked-view` and `datetime-picker`, and `nativeTheme.ts` imports `@expo/ui/jetpack-compose`.
   `@expo/ui` stays declined (#87), so each of these
   gets a core-component variant for Linux. #707 wrote them in `packages/test-harness/modules/t3-expo-ui/src` as
   `.linux.tsx` files T3 Code carries beside `Progress`, `SliderRow`, `NumberField` and `CustomSnoozeSheet`, and the
   fixture copies them into its checkout; `NumberField`'s re-exports T3 Code's own Android stepper. `NativeThemeHost` has no Linux importer once those three are replaced, the
   widget files are reached only from `.ios` and `.android` files, `masked-view`'s base file already draws its
   children unmasked, and `nativeTheme.ts` imports only a type. The `datetime-picker` is left out (§2).
4. **The pin is behind.** T3 Code is on react-native 0.88.1 and this platform pins 0.87.1: #636.
5. **T3 Code patches its native dependencies.** `react-native-screens` is a fork (`5.0.0-t3.50`) and
   gesture-handler is on `~3.5`. `patches/` changes reanimated's `Common/cpp/.../LayoutAnimationsUtils.h`,
   keyboard-controller (1,222 lines, JavaScript included), gesture-handler's `ReanimatedSwipeable`, menu,
   expo-modules-core, expo-network, expo-sharing, expo-audio, expo-video, expo-widgets, `@expo/ui`, react-native-svg,
   react-native-webview, react-native-pdf-jsi, `@clerk/expo-native-components`, `@react-native-ai/apple`, and the
   JavaScript of `@react-navigation/native-stack` and `react-native-image-viewing`. A port targets the patched code.
6. **Not in the ledger, because they never reach the React Native bundle.** Node-only prebuilt binaries
   (`lightningcss`, `@tailwindcss/oxide`, `@expo/modules-macros-*`, `@yuuang/ffi-rs`, `@cursor/sdk`,
   `@noxcturnal/*`) come in through T3 Code's build tools and server-side workspace packages; the iOS and
   Android apps ship without them. Pure JavaScript needs nothing from a platform: React Navigation (over
   `react-native-screens`, #166), `@legendapp/list`, `uniwind`, `pele`, `react-native-image-viewing`, `effect`,
   General Translation's JavaScript packages and `@formatjs/*`.

### What throws at import

Read from each package's published `build/`, as a `linux` bundle resolves it (the `.native.js` file, else the
shared one), at T3 Code's locked versions. A lookup that runs as the file evaluates is a lookup at import, because
Expo's Metro config turns `inlineRequires` off.

| Native module | Package | Linux |
| --- | --- | --- |
| `ExpoAsset` | `expo-asset` | registered (#677) |
| `ExpoFetchModule` | `expo` (`expo/fetch`, imported by four app files and installed as the global `fetch`) | registered (#673); upstream's `expo/fetch` runs unmodified, with a body streamed chunk by chunk |
| `ExpoApplication`, `ExpoDevice`, `ExpoLinking`, `ExpoNetwork` | `expo-application`, `expo-device`, `expo-linking`, `expo-network` | registered; the `expo-startup-modules` e2e imports all four through `expo` |
| `ExpoFontLoader` | `expo-font` | registered: its `typeof window === 'undefined'` guard is false under React Native, which defines `window`, so it needs the module; the `expo-font` e2e draws a face `useFonts` loaded |
| `ExpoClipboard` | `expo-clipboard` | registered (#278): the string methods over the in-process clipboard, which #754 connects to the Wayland selection |
| `ExpoCrypto`, `ExpoCryptoAES` | `expo-crypto` | registered (#276): digests, random bytes and UUIDs over libcrypto; `ExpoCryptoAES` only has the two classes `expo-crypto`'s entry extends, because T3 Code calls no AES |
| `FileSystem` | `expo-file-system` | registered (#676) |
| `ExpoImage` | `expo-image` | registered (#273), with no method yet; the `ExpoImage` view mounts as ADR-0004's unregistered fallback, which lays out and draws nothing, until #273 registers it |
| `ExponentImagePicker` | `expo-image-picker` | throws; #275 |
| `ExpoImageManipulator`, `ExpoDocumentPicker` | `expo-image-manipulator`, `expo-document-picker` | throws; #650. `expo-document-picker` is imported lazily, so not at startup |
| the ten notification modules `expo-notifications`' index requires | `expo-notifications` | registered (#274): local notifications over `org.freedesktop.Notifications`, the push token rejecting with upstream's `ERR_UNAVAILABLE`; the `expo-notifications` e2e runs T3 Code's calls against a fake daemon |
| `ExpoSecureStore` | `expo-secure-store` | registered (#270): the desktop keyring over the Secret Service, or an encrypted file when the session has none |
| `ExpoSharing` | `expo-sharing` | registered (#744): `shareAsync` hands the file to the OpenURI portal's chooser; `isAvailableAsync` is `false` without a portal |
| `ExpoSQLite` | `expo-sqlite` | registered (#665) |
| `ExpoWebBrowser` | `expo-web-browser` | throws; #271 |
| `ExpoAudio` | `expo-audio`, imported by eight app files | throws, so the fixture's desktop profile (§2) resolves `expo-audio` to a stand-in (#760): a player's `play` opens its source in the default application, the session calls resolve, recording permission reads denied and a recorder throws; the `t3-expo-audio` e2e runs T3 Code's calls |
| `ExpoKeepAwake`, `ExpoCamera`, `ExpoObserve`, `ExpoAppMetrics`, the `expo-sensors` modules, `SymbolModule`, `ExpoUpdates`, `ExpoWidgets` | `expo-keep-awake`, `expo-camera`, `expo-observe`, `expo-app-metrics`, `expo-sensors`, `expo-symbols`, `expo-updates`, `expo-widgets` | throws; left out or replaced by the feature profile (§2), so #641's Linux entry cuts the import |
| `ExpoVideo` | `expo-video` | throws; left out (§2). `MediaVideoPlayer.linux.tsx` cuts the one import that runs at startup (#759). The others are type-only, or the lazy `import()` in `videoThumbnails.ts`, whose failure the thumbnail loader turns into no thumbnail |

These import cleanly with nothing registered: `expo-haptics` and `expo-splash-screen` look their modules up with
`requireOptionalNativeModule` (haptics throws `UnavailabilityError` per call; the splash screen's calls return), the
shared files of `expo-glass-effect` are a plain `View` and `false`, and `expo-quick-actions` reads
`globalThis.expo.modules` and answers `isSupported` `false`.

## 2. The desktop feature profile

The Linux entry that #641 adds leaves the modules below out, so the first render waits only on what the main
screen draws. A left-out module that is still imported answers through its own documented "unavailable" path,
never by silently doing nothing. #641 decides how: `.linux.tsx` files, a Metro resolver stub, or both. Cutting the
import is not enough on its own: Expo autolinking links every installed Expo module whatever the JavaScript imports,
so #155 and #641 also exclude the left-out modules from autolinking.

| Left out | Why | What Linux does instead |
| --- | --- | --- |
| `expo-haptics` | no haptics on a desktop | the feedback calls are stubbed: without its native module each one throws `UnavailabilityError` (#156) |
| `expo-splash-screen` | no launch screen | the platform's first-frame policy (#156) |
| `expo-quick-actions` | home-screen shortcuts | nothing |
| `expo-widgets`, `t3-widget-expiry`, `t3-subscription-widget` | home-screen widgets | nothing |
| `@react-native-ai/apple` | Apple Intelligence | nothing |
| `expo-audio`, `expo-keep-awake`, `t3-voice-transcription` | voice input | no microphone button; audio attachments open in the desktop's default application |
| `expo-video` | inline video | video attachments open in the desktop's default application from a poster (#759); the poster's long-press actions (Copy full path, Copy relative path, Copy URL, Open in file viewer, Save or share) are not offered until #762 |
| `expo-sensors` | device-preview motion | nothing |
| `expo-camera` | QR pairing | the pairing link, typed or pasted |
| `@expo/ui/community/datetime-picker` | the scheduled-task editor's date and time picker | a text field: the date typed as `YYYY-MM-DD`, the time as `HH:MM`, until a real picker (#750) |
| `expo-observe`, `expo-app-metrics` | the error relay | the console and the e2e event trace |
| `expo-updates`, `expo-dev-client` | over-the-air updates and the development launcher | the distribution's package manager (#360); development builds wait on #155 |
| `@clerk/expo`, `@clerk/expo-native-components` | cloud sign-in | not needed to pair with a local server; the browser flow of #271 comes later |
| `t3-agent-notifications` | Android notification actions | nothing until #274 |
| `t3-web-socket` | iOS's WebSocket | React Native's WebSocket over the networking stack of #79 |

These are replaced by a different file on Linux, not left out:

| Module | Linux uses |
| --- | --- |
| `@expo/ui/swift-ui` views | a core-component `.linux.tsx` variant of each T3 Code view that imports them (#707) |
| `expo-video` in `MediaVideoPlayer` | a `.linux.tsx` poster that opens the video URL with `Linking.openURL` (#759) |
| `@expo/ui/community/masked-view` | `@react-native-masked-view/masked-view` (#164) |
| `expo-symbols` in `AppSymbol` | the tabler icon set `AppSymbol.android.tsx` already uses |
| `T3ComposerEditor.native.tsx` | the plain `TextInput` variant, `T3ComposerEditor.tsx`, until #640's composer editor lands |
| `react-native-pdf-jsi` | nothing to do: only `PdfPreview.android.tsx` imports it; Linux gets the shared `PdfPreview` over `react-native-webview` (#272) |

## 3. Dependency order

The order epic #634 follows. Each step needs the ones before it.

1. **Pin**: react-native 0.88.1 (#636).
2. **Module hosts**: the Expo decision (#155), which blocks every Expo row, and Nitro (#152).
3. **The frame**: worklets and Reanimated (#137–#144), gesture-handler (#168), `react-native-screens` under
   React Navigation's native stack (#166), safe-area-context (#161), keyboard-controller (#279), svg with the
   tabler icons (#167), the thin shims (#156) and blur (#165).
4. **Session and data**: the expo-sqlite database (#638), secure-store (#270), crypto (#276), T3 Code's
   native-controls with its DPoP keys (#640), `gt-react-native` (#647), and network state (#648).
5. **The main screen's content**: expo-image (#273), file-system (#269), markdown (#639), menus (#646), paste
   (#649), clipboard (#278), source text and highlights (#684).
6. **Intl**: Hermes is built with `HERMES_ENABLE_INTL` off (`packages/core/CMakeLists.txt`), and T3 Code
   constructs `Intl.DateTimeFormat` (`messageTimestamp.ts`), `Intl.Collator` (`fileTree.ts`, `zipTree.ts`) and
   `Intl.NumberFormat` (`diffCount.ts`, `usageFormat.ts`) at module scope, so the bundle throws before the first
   frame without them. Its `intl-polyfills.ts` adds `Intl.Locale`, `getCanonicalLocales` and English `DisplayNames`
   but assumes the rest from the engine. #83 chose the platform's own `Intl` over the system ICU
   (`packages/core/src/Intl.h`), with Hermes' Intl kept off; `IntlStartupTest` starts a bundle that builds those
   module-scope formatters, and `IntlBindingTest` covers the three constructors with T3 Code's options.
7. **The fixture**: the main screen renders in `rnl_window` against a local T3 server (#641). `pnpm e2e:t3code
   <checkout>` builds T3 Code's own development bundle for `linux` (it pairs from a link only under `__DEV__`) with its
   own Metro config under this platform's resolver (`packages/test-harness/scripts/t3code-metro-config.ts`), starts the
   checkout's T3 server on loopback, and runs the bundle in the headless rig with the link that pairs it. It passes only
   when the window presents a frame and exits cleanly with no fatal JavaScript error, and otherwise names the first such
   error; #641 keeps the ordered list of what stands between that error and the main screen.
8. **The measurement**: against the Electron build (#642), with frame pacing (#20) and the trace surface (#90).

Secondary screens come after the fixture: attachments (#650), webviews (#272), notifications (#274), the image
picker (#275), and T3 Code's review-diff and terminal views (#640).

## 4. The ledger

The status words are the ledger's: **works** and **shimmed** (the test named proves it), **tracked** (`in progress`
or `blocked` on the issue named) and **declined** (`declined`, with the substitute the ledger names; §2 says which
are left out and which are replaced). Intl is not
a row: it is the engine's, not a package's, and #83 tracks it. The range is T3 Code's, or the locked version for a
transitive package.

| Package | T3 Code range | Status | Owner | Note |
| --- | --- | --- | --- | --- |
| `expo` | `58.0.23` | tracked | #155 | the Expo host decision; `registerRootComponent` and `requireNativeModule` at the entry |
| `expo-modules-core` | `58.0.30` | tracked | #155 | under every Expo module |
| `expo-modules-jsi` | `58.0.22` | tracked | #155 | from expo-modules-core |
| `expo-modules-macros` | `0.29.0` | tracked | #155 | Swift macros from expo-modules-core |
| `expo-asset` | `~58.0.20` | tracked | #155 | asset resolution under expo-font and expo-image |
| `@expo/log-box` | `58.0.15` | tracked | #155 | development error overlay |
| `@expo/dom-webview` | `58.0.2` | tracked | #155 | DOM components; T3 Code uses none |
| `expo-dev-client` | `~58.0.18` | tracked | #155 | development builds only |
| `expo-dev-launcher` | `58.0.18` | tracked | #155 | from expo-dev-client |
| `expo-dev-menu` | `58.0.12` | tracked | #155 | from expo-dev-client |
| `expo-dev-menu-interface` | `58.0.1` | tracked | #155 | from expo-dev-client |
| `expo-manifests` | `58.0.1` | tracked | #155 | from expo-dev-client and expo-updates |
| `expo-json-utils` | `58.0.1` | tracked | #155 | from expo-manifests |
| `expo-updates-interface` | `58.0.1` | tracked | #155 | from expo-dev-client and expo-updates |
| `expo-updates` | `~58.0.21` | declined | #360 | diagnostics and app updates |
| `expo-eas-client` | `58.0.2` | declined | #360 | from expo-updates and expo-observe |
| `expo-structured-headers` | `58.0.1` | declined | #360 | from expo-updates |
| `expo-constants` | `~58.0.15` | tracked | #156 | 17 files: app config and relay URL; `expoConfig` is the app config the CLI's `embedExpoAppAssets` writes into the application's assets directory, for a bundle file and a bundle Metro serves alike (the `expo-constants` and `expo-constants-dev-server` e2e) |
| `expo-font` | `~58.0.11` | works | `packages/expo/e2e/expo-font.json` | DM Sans and Material Symbols; `ExpoFontLoader` registers into the application fonts of #70, and the CLI's `embedExpoAppAssets` copies the DM Sans faces the config plugin declares into them (#696) |
| `expo-linking` | `~58.0.16` | tracked | #156 | deep links at the root; imports over `ExpoLinking`, and `createURL`, which `App.tsx` calls, answers from the embedded app config's `scheme` (the `expo-constants` e2e, from a file and from Metro) |
| `expo-haptics` | `~58.0.7` | tracked | #156 | 21 files; reports itself unavailable |
| `expo-glass-effect` | `58.0.5` | tracked | #156 | chips, toolbar and the floating working control |
| `expo-splash-screen` | `~58.0.13` | tracked | #156 | root; the platform's first-frame policy |
| `expo-sharing` | `58.0.22` | works | `packages/expo/e2e/expo-sharing.json` | attachment download over the OpenURI portal's chooser (#744); the e2e runs without a session bus, so it proves the module is wired into the window and rejects, and the hand-off to a portal is proved by `OpenUriPortalTest` against a fake one; incoming shares are read on Android and iOS only, and answer `[]` |
| `expo-blur` | `58.0.5` | tracked | #165 | GlassBackdrop |
| `expo-sqlite` | `~58.0.15` | tracked | #638 | T3 Code opens a database; Suuudokuuu's kv-store (#163) builds on the same port |
| `@expo/ui` | `58.0.27` | declined | #87 | eight shared views import `@expo/ui/swift-ui`; see the feature profile |
| `@react-native-masked-view/masked-view` | `0.3.2` | tracked | #164 | from @react-navigation/elements |
| `react-native-reanimated` | `4.7.1` | tracked | #144 | 37 files |
| `react-native-worklets` | `0.13.1` | tracked | #135 | from Reanimated and the behaviour probe |
| `react-native-gesture-handler` | `~3.5.0` | tracked | #168 | imported by the entry |
| `react-native-screens` | `file:deps/react-native-screens-5.0.0-t3.50.tgz` | tracked | #166 | T3 Code's fork, imported by the entry; under React Navigation native-stack |
| `react-native-safe-area-context` | `~5.11.1` | tracked | #161 | 71 files |
| `react-native-svg` | `15.16.1` | tracked | #167 | provider icons and charts |
| `react-native-nitro-modules` | `0.37.2` | tracked | #152 | the highlights module |
| `nitro-core` | `0.37.2` | tracked | #152 | from react-native-nitro-modules |
| `@tabler/icons-react-native` | `^3.44.0` | tracked | #167 | JavaScript over react-native-svg |
| `expo-file-system` | `~58.0.13` | tracked | #269 | 22 files: attachments and caches |
| `expo-secure-store` | `~58.0.2` | works | `packages/expo/e2e/expo-secure-store.json` | tokens and DPoP keys; the desktop keyring over the Secret Service, or an encrypted file under the application directory when the session has none, which protects against copying only |
| `expo-web-browser` | `~58.0.7` | tracked | #271 | cloud sign-in |
| `expo-auth-session` | `~58.0.17` | tracked | #271 | JavaScript over expo-web-browser and expo-crypto |
| `@clerk/expo` | `catalog: (4.15.1)` | tracked | #271 | JavaScript over expo-web-browser and expo-auth-session; cloud sign-in only |
| `react-native-webview` | `14.0.1` | tracked | #272 | previews, PDFs, MCP apps and legal pages |
| `expo-image` | `~58.0.18` | tracked | #273 | 14 files: favicons, avatars and attachments; imports over `ExpoImage`, whose view and static methods (`loadAsync` and the cache methods, which T3 Code's favicon thumbnails call) are left |
| `expo-notifications` | `~58.1.8` | works | `packages/expo/e2e/expo-notifications.json` | agent-awareness notifications: permissions, the handler, the response listener and the last response; no push token on a desktop |
| `expo-image-picker` | `~58.0.10` | tracked | #275 | composer photos |
| `expo-crypto` | `~58.0.7` | works | `packages/expo/e2e/expo-startup-modules.json` | DPoP proofs, share fingerprints and UUIDs: `digest`, `digestStringAsync`, `getRandomBytes` and `randomUUID` |
| `expo-device` | `~58.0.5` | works | `packages/expo/e2e/expo-startup-modules.json` | appearance defaults; `ExpoDevice` answers the machine |
| `expo-clipboard` | `~58.0.4` | tracked | #278 | 8 files: copying codes and commands, terminal paste; `ExpoClipboard` answers strings in-process until #754, and its change listener is left |
| `react-native-keyboard-controller` | `1.23.0` | shimmed | `packages/test-harness/e2e/keyboard-controller.json` | KeyboardProvider at the root; a desktop has no soft keyboard, so its height stays zero, its events and handlers never fire, its views are Views and `dismiss` blurs the focused input (`linuxPackageOverlayIndex` replaces the library's native bindings) |
| `react-native-enriched-markdown` | `file:deps/react-native-enriched-markdown-1.2.0-t3.18-16db4c75.tgz` | tracked | #688 | T3 Code's fork; every message; #639 landed the base |
| `@t3tools/mobile-source-text` | `file:./modules/t3-source-text` | tracked | #684 | source file view; no Linux code, its `Text` fallback renders |
| `@t3tools/mobile-highlights-native` | `file:./modules/t3-highlights` | tracked | #684 | syntax highlighting, a Nitro module; a `linux/` TurboModule until #152, reached through T3 Code's `nativeHighlights.linux.ts` (below) |
| `@t3tools/mobile-review-diff-native` | `file:./modules/t3-review-diff` | tracked | #685 | review diff view; Linux renders it in JavaScript, no native code |
| `@t3tools/mobile-terminal-native` | `file:./modules/t3-terminal` | tracked | #640 | terminal view |
| `@react-native-menu/menu` | `^2.0.0` | tracked | #646 | 28 files: thread rows, list filter, composer |
| `gt-react-native` | `11.4.18` | tracked | #647 | i18n root; `getEnforcing` throws at import |
| `expo-network` | `~58.0.4` | tracked | #648 | online and offline state of the connection layer; imports over `ExpoNetwork`, which answers `UNKNOWN` until the NetworkMonitor portal is read |
| `expo-application` | `~58.1.2` | tracked | #648 | diagnostics screen; imports over `ExpoApplication`, whose identity is `null` until a `linux` build embeds it |
| `expo-paste-input` | `^0.2.2` | tracked | #649 | both composer variants |
| `expo-document-picker` | `~58.0.4` | tracked | #650 | composer attachments, imported lazily |
| `expo-image-manipulator` | `~58.0.16` | tracked | #650 | fits images under the upload limit |
| `expo-image-loader` | `58.0.3` | tracked | #650 | from expo-image-manipulator and expo-image-picker |
| `@clerk/expo-native-components` | `0.4.4` | declined | #635 | native sign-in UI |
| `@react-native-ai/apple` | `0.12.0` | declined | #635 | Apple Intelligence; reached only from iOS files |
| `expo-observe` | `~58.0.18` | declined | #635 | imported by the entry |
| `expo-app-metrics` | `58.0.15` | declined | #635 | from expo-observe |
| `expo-quick-actions` | `^6.0.2` | declined | #635 | app shortcuts |
| `expo-widgets` | `58.0.27` | declined | #635 | home-screen widgets |
| `expo-sensors` | `~58.0.4` | declined | #635 | device preview motion |
| `expo-camera` | `~58.0.14` | declined | #635 | QR pairing |
| `expo-symbols` | `~58.0.4` | declined | #635 | SF Symbols in the shared AppSymbol |
| `expo-audio` | `58.0.11` | declined | #635 | voice input and audio previews; the desktop profile's stand-in answers its eight importers (#760) |
| `expo-keep-awake` | `~58.0.3` | declined | #635 | left out with voice input, its only user; expo's dev-only `withDevTools` requires it inside `try`/`catch` |
| `expo-video` | `58.0.13` | declined | #759 | video attachments open in the default application from `MediaVideoPlayer.linux.tsx`, without its long-press media actions (#762) |
| `react-native-pdf-jsi` | `6.1.0` | declined | #635 | imported only by PdfPreview.android.tsx |

T3 Code's own native modules under `apps/mobile/modules`. The four with a `package.json` are rows above; the
other seven are Expo local modules, linked by directory rather than declared as dependencies.

| Module | Platforms | Linux |
| --- | --- | --- |
| `t3-native-controls` | iOS, Android | #640: DPoP keys, keyboard commands, workspace navigation; required |
| `t3-composer-editor` | iOS, Android | #640; the plain `TextInput` composer until then |
| `t3-source-text` | iOS | #684: none needed, `SourceText` renders `Text` off iOS; selection is #43 |
| `t3-highlights` | Nitro, C++ core | #684: a `linux/` TurboModule over the same lexer until #152 and #153 |
| `t3-review-diff` | iOS, Android | #685: `nativeReviewDiffSurface.linux.ts` draws the same rows, props and events with React Native views |
| `t3-terminal` | iOS, Android | #640 |
| `t3-voice-transcription` | iOS, Android | left out with voice input |
| `t3-agent-notifications` | Android | left out until #274 |
| `t3-widget-expiry` | Android | left out with widgets |
| `t3-subscription-widget` | Android | left out with widgets |
| `t3-web-socket` | iOS | React Native's WebSocket (#79) |

T3 Code's `src/lib/highlights.ts` imports `react-native-nitro-modules` at the top, and 0.37.2 throws when it loads on
Linux (`TurboModuleRegistry.getEnforcing('NitroModules')`), so the app cannot reach `T3Highlights` until T3 Code
splits the lookup into one file: `nativeHighlights.ts` keeps `NitroModules.hasHybridObject("T3Highlights")`, and
`nativeHighlights.linux.ts` is `TurboModuleRegistry.get("T3Highlights") ?? undefined`. T3 Code owns that copy;
`test-bundles/t3-highlights.js` reaches the module the same way. Nitro on Linux (#152, #153) is the real fix.
