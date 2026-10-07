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
 */
/**
 * `works` and `shimmed` name the test that proves it, a path from the repository root. `in progress` and `blocked`
 * name the issue that owns the work: `blocked` when that issue is waiting on another decision or issue, `in
 * progress` otherwise. `declined` names what the flagship uses on Linux instead.
 */
type EcosystemStanding =
  | { readonly state: "works" | "shimmed"; readonly test: string }
  | { readonly state: "in progress" | "blocked"; readonly issue: number }
  | { readonly state: "declined"; readonly substitute: string };

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
  { flagshipVersion: "~58.0.9", issue: 156, packageName: "expo-constants", state: "blocked" },
  { flagshipVersion: "~58.0.10", issue: 155, packageName: "expo-dev-client", state: "blocked" },
  { flagshipVersion: "~58.0.5", issue: 156, packageName: "expo-font", state: "blocked" },
  { flagshipVersion: "~58.0.3", issue: 156, packageName: "expo-glass-effect", state: "blocked" },
  { flagshipVersion: "~58.0.4", issue: 156, packageName: "expo-haptics", state: "blocked" },
  { flagshipVersion: "~58.0.3", issue: 165, packageName: "expo-linear-gradient", state: "blocked" },
  { flagshipVersion: "~58.0.10", issue: 156, packageName: "expo-linking", state: "blocked" },
  { flagshipVersion: "~58.0.3", issue: 156, packageName: "expo-localization", state: "blocked" },
  { flagshipVersion: "~58.0.12", issue: 155, packageName: "expo-router", state: "blocked" },
  { flagshipVersion: "~58.0.5", issue: 156, packageName: "expo-screen-capture", state: "blocked" },
  { flagshipVersion: "~58.0.13", issue: 156, packageName: "expo-sharing", state: "blocked" },
  { flagshipVersion: "~58.0.4", issue: 156, packageName: "expo-splash-screen", state: "blocked" },
  { flagshipVersion: "~58.0.8", issue: 163, packageName: "expo-sqlite", state: "blocked" },
  { flagshipVersion: "~58.0.3", issue: 156, packageName: "expo-status-bar", state: "blocked" },
  { flagshipVersion: "~58.0.5", issue: 156, packageName: "expo-system-ui", state: "blocked" },
  {
    flagshipVersion: "~58.0.13",
    packageName: "expo-updates",
    state: "declined",
    substitute: "the distribution's package manager delivers updates (#360)",
  },
  {
    flagshipVersion: "~58.0.11",
    packageName: "@expo/ui",
    state: "declined",
    substitute: "the flagship's own bottom sheet built from core components",
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
