/**
 * The flagship's native dependency ledger (#87): every dependency of Suuudokuuu that needs a Linux implementation,
 * with where it stands. M4 is measured against this file, so a row changes in the same PR as the work it records.
 *
 * Read from `packages/app/package.json` of https://github.com/vitalyiegorov/suuudokuuu at commit `17357d6d9188`
 * (version 2.18.1); each row's `flagshipVersion` is the range that file declares. Left out because they need nothing from a platform:
 * the pure JavaScript dependencies (`effect`, `@lingui/*`, `zod`, `date-fns`, `@rnw-community/*`, the flagship's
 * own `@suuudokuuu/*` workspaces), web-only ones (`react-native-web`, `@effect/wa-sqlite`, `@effect/sql-sqlite-wasm`)
 * and build-time ones (`expo-build-properties`, `@expo/fingerprint`). A row that is pure JavaScript over a native
 * library is listed, blocked on that library's issue. The per-library research behind each state is
 * docs/research/ecosystem-compatibility.md §1.
 *
 * The second flagship, T3 Code (#634), adds every package with native code that `apps/mobile` reaches, direct or
 * transitive, read from `pnpm-lock.yaml` of https://github.com/pingdotgg/t3code at commit `238678951ac4`. A row only
 * T3 Code needs carries its range, or the locked version for a transitive one; a shared row keeps Suuudokuuu's.
 * docs/research/t3code-linux.md lists T3 Code's range for every row, its desktop feature profile, and the reasoning
 * behind each `declined` row that names #635.
 */
/**
 * `works` and `shimmed` name the test that proves it, a path from the repository root. `in progress` and `blocked`
 * name the issue that owns the work: `blocked` when that issue is waiting on another decision or issue, `in
 * progress` otherwise. `declined` names the decision issue and what the flagship uses on Linux instead.
 */
type EcosystemStanding =
  | { readonly state: "works" | "shimmed"; readonly test: string }
  | { readonly state: "in progress" | "blocked"; readonly issue: number }
  | { readonly state: "declined"; readonly issue: number; readonly substitute: string };

type EcosystemRow = EcosystemStanding & {
  readonly packageName: string;
  readonly flagshipVersion: string;
};

const ecosystemMatrix: readonly EcosystemRow[] = [
  { flagshipVersion: "^1.49.0", issue: 167, packageName: "lucide-react-native", state: "blocked" },
  { flagshipVersion: "^0.37.1", issue: 152, packageName: "react-native-nitro-modules", state: "blocked" },
  { flagshipVersion: "^12.3.1", issue: 170, packageName: "react-native-share", state: "in progress" },
  { flagshipVersion: "^5.1.3", issue: 144, packageName: "reanimated-color-picker", state: "blocked" },
  { flagshipVersion: "~3.2.1", issue: 168, packageName: "react-native-gesture-handler", state: "in progress" },
  { flagshipVersion: "~4.28.0", issue: 166, packageName: "react-native-screens", state: "blocked" },
  { flagshipVersion: "~5.9.1", issue: 161, packageName: "react-native-safe-area-context", state: "blocked" },
  { flagshipVersion: "~58.0.2", issue: 155, packageName: "expo", state: "blocked" },
  { flagshipVersion: "~58.0.3", issue: 165, packageName: "expo-blur", state: "blocked" },
  { flagshipVersion: "~58.0.9", issue: 156, packageName: "expo-constants", state: "in progress" },
  { flagshipVersion: "~58.0.10", issue: 155, packageName: "expo-dev-client", state: "blocked" },
  { flagshipVersion: "~58.0.5", packageName: "expo-font", state: "works", test: "packages/expo/e2e/expo-font.json" },
  { flagshipVersion: "~58.0.3", issue: 156, packageName: "expo-glass-effect", state: "blocked" },
  { flagshipVersion: "~58.0.4", issue: 156, packageName: "expo-haptics", state: "blocked" },
  { flagshipVersion: "~58.0.3", issue: 165, packageName: "expo-linear-gradient", state: "blocked" },
  { flagshipVersion: "~58.0.10", issue: 156, packageName: "expo-linking", state: "in progress" },
  { flagshipVersion: "~58.0.3", issue: 156, packageName: "expo-localization", state: "blocked" },
  { flagshipVersion: "~58.0.12", issue: 155, packageName: "expo-router", state: "blocked" },
  { flagshipVersion: "~58.0.5", issue: 156, packageName: "expo-screen-capture", state: "blocked" },
  { flagshipVersion: "~58.0.13", issue: 156, packageName: "expo-sharing", state: "blocked" },
  { flagshipVersion: "~58.0.4", issue: 156, packageName: "expo-splash-screen", state: "blocked" },
  { flagshipVersion: "~58.0.8", issue: 638, packageName: "expo-sqlite", state: "blocked" },
  { flagshipVersion: "~58.0.3", issue: 156, packageName: "expo-status-bar", state: "blocked" },
  { flagshipVersion: "~58.0.5", issue: 156, packageName: "expo-system-ui", state: "blocked" },
  { flagshipVersion: "58.0.2", issue: 155, packageName: "@expo/dom-webview", state: "blocked" },
  { flagshipVersion: "58.0.15", issue: 155, packageName: "@expo/log-box", state: "blocked" },
  { flagshipVersion: "~58.0.20", issue: 155, packageName: "expo-asset", state: "blocked" },
  { flagshipVersion: "58.0.18", issue: 155, packageName: "expo-dev-launcher", state: "blocked" },
  { flagshipVersion: "58.0.12", issue: 155, packageName: "expo-dev-menu", state: "blocked" },
  { flagshipVersion: "58.0.1", issue: 155, packageName: "expo-dev-menu-interface", state: "blocked" },
  { flagshipVersion: "58.0.1", issue: 155, packageName: "expo-json-utils", state: "blocked" },
  { flagshipVersion: "58.0.1", issue: 155, packageName: "expo-manifests", state: "blocked" },
  { flagshipVersion: "58.0.30", issue: 155, packageName: "expo-modules-core", state: "blocked" },
  { flagshipVersion: "58.0.22", issue: 155, packageName: "expo-modules-jsi", state: "blocked" },
  { flagshipVersion: "0.29.0", issue: 155, packageName: "expo-modules-macros", state: "blocked" },
  { flagshipVersion: "58.0.1", issue: 155, packageName: "expo-updates-interface", state: "blocked" },
  { flagshipVersion: "0.37.2", issue: 152, packageName: "nitro-core", state: "blocked" },
  { flagshipVersion: "^3.44.0", issue: 167, packageName: "@tabler/icons-react-native", state: "blocked" },
  { flagshipVersion: "~58.0.13", issue: 269, packageName: "expo-file-system", state: "blocked" },
  { flagshipVersion: "~58.0.2", issue: 270, packageName: "expo-secure-store", state: "blocked" },
  { flagshipVersion: "~58.0.7", issue: 271, packageName: "expo-web-browser", state: "blocked" },
  { flagshipVersion: "~58.0.17", issue: 271, packageName: "expo-auth-session", state: "blocked" },
  { flagshipVersion: "4.15.1", issue: 271, packageName: "@clerk/expo", state: "blocked" },
  { flagshipVersion: "14.0.1", issue: 272, packageName: "react-native-webview", state: "blocked" },
  { flagshipVersion: "~58.0.18", issue: 273, packageName: "expo-image", state: "blocked" },
  { flagshipVersion: "~58.1.8", issue: 274, packageName: "expo-notifications", state: "blocked" },
  { flagshipVersion: "~58.0.10", issue: 275, packageName: "expo-image-picker", state: "blocked" },
  {
    flagshipVersion: "~58.0.7",
    packageName: "expo-crypto",
    state: "works",
    test: "packages/expo/e2e/expo-startup-modules.json",
  },
  {
    flagshipVersion: "~58.0.5",
    packageName: "expo-device",
    state: "works",
    test: "packages/expo/e2e/expo-startup-modules.json",
  },
  { flagshipVersion: "~58.0.4", issue: 278, packageName: "expo-clipboard", state: "blocked" },
  { flagshipVersion: "1.23.0", issue: 279, packageName: "react-native-keyboard-controller", state: "blocked" },
  {
    flagshipVersion: "file:deps/react-native-enriched-markdown-1.2.0-t3.18-16db4c75.tgz",
    issue: 688,
    packageName: "react-native-enriched-markdown",
    state: "in progress",
  },
  {
    flagshipVersion: "file:./modules/t3-source-text",
    issue: 684,
    packageName: "@t3tools/mobile-source-text",
    state: "in progress",
  },
  {
    flagshipVersion: "file:./modules/t3-highlights",
    issue: 684,
    packageName: "@t3tools/mobile-highlights-native",
    state: "in progress",
  },
  {
    flagshipVersion: "file:./modules/t3-review-diff",
    issue: 685,
    packageName: "@t3tools/mobile-review-diff-native",
    state: "in progress",
  },
  {
    flagshipVersion: "file:./modules/t3-terminal",
    issue: 640,
    packageName: "@t3tools/mobile-terminal-native",
    state: "blocked",
  },
  { flagshipVersion: "^2.0.0", issue: 646, packageName: "@react-native-menu/menu", state: "in progress" },
  { flagshipVersion: "11.4.18", issue: 647, packageName: "gt-react-native", state: "in progress" },
  { flagshipVersion: "~58.0.4", issue: 648, packageName: "expo-network", state: "in progress" },
  { flagshipVersion: "~58.1.2", issue: 648, packageName: "expo-application", state: "in progress" },
  { flagshipVersion: "^0.2.2", issue: 649, packageName: "expo-paste-input", state: "blocked" },
  { flagshipVersion: "~58.0.4", issue: 650, packageName: "expo-document-picker", state: "blocked" },
  { flagshipVersion: "~58.0.16", issue: 650, packageName: "expo-image-manipulator", state: "blocked" },
  { flagshipVersion: "58.0.3", issue: 650, packageName: "expo-image-loader", state: "blocked" },
  {
    flagshipVersion: "0.4.4",
    issue: 635,
    packageName: "@clerk/expo-native-components",
    state: "declined",
    substitute: "Clerk's browser sign-in through @clerk/expo over expo-web-browser and expo-auth-session (#271)",
  },
  {
    flagshipVersion: "0.12.0",
    issue: 635,
    packageName: "@react-native-ai/apple",
    state: "declined",
    substitute: "nothing: Apple Intelligence is left out by the desktop feature profile",
  },
  {
    flagshipVersion: "~58.0.18",
    issue: 635,
    packageName: "expo-observe",
    state: "declined",
    substitute: "errors reach the console and the e2e event trace; the relay reporter is left out",
  },
  {
    flagshipVersion: "58.0.15",
    issue: 635,
    packageName: "expo-app-metrics",
    state: "declined",
    substitute: "left out with expo-observe, its only dependent",
  },
  {
    flagshipVersion: "^6.0.2",
    issue: 635,
    packageName: "expo-quick-actions",
    state: "declined",
    substitute: "nothing: home-screen quick actions are left out by the desktop feature profile",
  },
  {
    flagshipVersion: "58.0.27",
    issue: 635,
    packageName: "expo-widgets",
    state: "declined",
    substitute: "nothing: home-screen widgets are left out by the desktop feature profile",
  },
  {
    flagshipVersion: "~58.0.4",
    issue: 635,
    packageName: "expo-sensors",
    state: "declined",
    substitute: "nothing: the device preview's motion input is left out by the desktop feature profile",
  },
  {
    flagshipVersion: "~58.0.14",
    issue: 635,
    packageName: "expo-camera",
    state: "declined",
    substitute: "the pairing link typed or pasted on the connection screen instead of a scanned QR code",
  },
  {
    flagshipVersion: "~58.0.4",
    issue: 635,
    packageName: "expo-symbols",
    state: "declined",
    substitute: "the app's own tabler icon set, AppSymbol's Android variant",
  },
  {
    flagshipVersion: "58.0.11",
    issue: 635,
    packageName: "expo-audio",
    state: "declined",
    substitute: "voice input is left out; audio attachments open in the desktop's default application",
  },
  {
    flagshipVersion: "~58.0.3",
    issue: 635,
    packageName: "expo-keep-awake",
    state: "declined",
    substitute: "left out with voice input, its only user",
  },
  {
    flagshipVersion: "58.0.13",
    issue: 635,
    packageName: "expo-video",
    state: "declined",
    substitute: "video attachments open in the desktop's default application",
  },
  {
    flagshipVersion: "6.1.0",
    issue: 635,
    packageName: "react-native-pdf-jsi",
    state: "declined",
    substitute: "the app's shared PdfPreview over react-native-webview (#272); only PdfPreview.android.tsx imports it",
  },
  {
    flagshipVersion: "~58.0.13",
    issue: 360,
    packageName: "expo-updates",
    state: "declined",
    substitute: "the distribution's package manager delivers updates (#360)",
  },
  {
    flagshipVersion: "58.0.2",
    issue: 360,
    packageName: "expo-eas-client",
    state: "declined",
    substitute: "left out with expo-updates and expo-observe, its only dependents",
  },
  {
    flagshipVersion: "58.0.1",
    issue: 360,
    packageName: "expo-structured-headers",
    state: "declined",
    substitute: "left out with expo-updates, its only dependent",
  },
  {
    flagshipVersion: "~58.0.11",
    issue: 87,
    packageName: "@expo/ui",
    state: "declined",
    substitute:
      "core components: Suuudokuuu's own bottom sheet, and a core-component variant of each T3 Code view on it",
  },
  { flagshipVersion: "18.2.5", issue: 582, packageName: "@op-engineering/op-sqlite", state: "in progress" },
  { flagshipVersion: "4.0.0", issue: 582, packageName: "@effect/sql-sqlite-react-native", state: "in progress" },
  { flagshipVersion: "0.3.2", issue: 164, packageName: "@react-native-masked-view/masked-view", state: "blocked" },
  { flagshipVersion: "2.13.1", issue: 169, packageName: "@shopify/react-native-skia", state: "blocked" },
  { flagshipVersion: "4.7.0", issue: 144, packageName: "react-native-reanimated", state: "blocked" },
  { flagshipVersion: "15.15.5", issue: 167, packageName: "react-native-svg", state: "blocked" },
  { flagshipVersion: "3.3.0", issue: 162, packageName: "react-native-unistyles", state: "blocked" },
  { flagshipVersion: "0.13.0", issue: 135, packageName: "react-native-worklets", state: "blocked" },
];

export { ecosystemMatrix };
