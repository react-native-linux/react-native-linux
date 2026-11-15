# @react-native-linux/expo

The JavaScript shim tier of [ADR-0004](../../docs/adr/0004-expo-modules-are-a-javascript-shim-tier.md): one Linux
implementation per Expo module. `packages/cli/src/package-aliases.ts` rewrites the upstream package name to its
subpath here in a `linux` bundle, once the application installs this package. The upstream package stays installed
for its types and for the other platforms.

| Module | Written against | Implemented | Says no |
| --- | --- | --- | --- |
| `expo-constants` | 58.0.15 | `expoConfig`, `manifest`, `easConfig`, `expoGoConfig` from `process.env.APP_MANIFEST`; `executionEnvironment` (`bare`), `appOwnership` (`null`), `sessionId`, `statusBarHeight` (`0`), `manifest2` (`null`), the three enums | `deviceName` and `systemVersion` are absent, `systemFonts` is empty, `getWebViewUserAgentAsync` resolves `null` |

`test-bundles/` and `e2e/` are the scenarios `pnpm e2e` discovers.
