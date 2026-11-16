import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    coverage: {
      exclude: [
        // #168 trusts the upstream engine; repository coverage excludes vendored source.
        "packages/*/upstream/**",
        "packages/*/src/**/*.spec.ts",
        "packages/core/goldens/*.spec.ts",
        "packages/core/src-linux/**/*.spec.ts",
        // Imports upstream Flow Vitest cannot parse; the application golden renders through it (#22).
        "packages/core/src-linux/Libraries/NativeComponent/BaseViewConfig.linux.ts",
        // Renders React Native components, which Vitest can neither load (react-native is Flow) nor render (#639).
        // Its logic is in lex.ts and styles.ts, and the streaming-markdown golden draws each element it renders.
        "packages/enriched-markdown/src/index.tsx",
        "scripts/codegen-core/**/*.spec.ts",
        "scripts/create-package/**/*.spec.ts",
        "scripts/doctor/**/*.spec.ts",
        "scripts/e2e/**/*.spec.ts",
        "scripts/format-cpp/**/*.spec.ts",
        "scripts/pack-local/**/*.spec.ts",
        "scripts/perf/**/*.spec.ts",
        "scripts/prop-coverage/**/*.spec.ts",
        "scripts/upstream/**/*.spec.ts",
        "scripts/vendor-fonts/**/*.spec.ts",
      ],
      include: [
        "packages/*/src/**/*.ts",
        "packages/enriched-markdown/src/**/*.tsx",
        // The rendering half, nativeReviewDiffSurface.linux.ts, is drawn by the t3-review-diff golden and e2e.
        "packages/test-harness/modules/t3-review-diff/src/features/diffs/nativeReviewDiffRows.ts",
        // The views around it are drawn by the t3-expo-ui golden and e2e.
        "packages/test-harness/modules/t3-expo-ui/src/components/ui/controlValues.linux.ts",
        // The stand-ins of T3 Code's desktop profile; the spec beside each holds its contract, the TurboModule one for Nitro.
        "packages/test-harness/test-bundles/t3code-desktop-profile/react-native-nitro-modules.ts",
        "packages/test-harness/test-bundles/t3code-desktop-profile/expo-camera.ts",
        "packages/test-harness/test-bundles/t3code-desktop-profile/expo-keep-awake.ts",
        "packages/test-harness/test-bundles/t3code-desktop-profile/expo-sensors.ts",
        "packages/core/goldens/*.ts",
        "packages/core/src-linux/**/*.ts",
        "scripts/codegen-core/**/*.ts",
        "scripts/create-package/**/*.ts",
        "scripts/doctor/**/*.ts",
        "scripts/e2e/**/*.ts",
        "scripts/format-cpp/**/*.ts",
        "scripts/pack-local/**/*.ts",
        "scripts/perf/**/*.ts",
        "scripts/prop-coverage/**/*.ts",
        "scripts/upstream/**/*.ts",
        "scripts/vendor-fonts/**/*.ts",
      ],
      provider: "v8",
      reporter: ["text", "lcov"],
      thresholds: {
        branches: 100,
        functions: 100,
        lines: 100,
        statements: 100,
      },
    },
    include: [
      "packages/*/src/**/*.spec.ts",
      "packages/core/goldens/*.spec.ts",
      "packages/test-harness/modules/*/*.spec.ts",
      "packages/test-harness/test-bundles/t3code-desktop-profile/*.spec.ts",
      "packages/core/src-linux/**/*.spec.ts",
      "scripts/codegen-core/**/*.spec.ts",
      "scripts/create-package/**/*.spec.ts",
      "scripts/doctor/**/*.spec.ts",
      "scripts/e2e/**/*.spec.ts",
      "scripts/format-cpp/**/*.spec.ts",
      "scripts/pack-local/**/*.spec.ts",
      "scripts/perf/**/*.spec.ts",
      "scripts/prop-coverage/**/*.spec.ts",
      "scripts/upstream/**/*.spec.ts",
      "scripts/vendor-fonts/**/*.spec.ts",
    ],
  },
});
