import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    coverage: {
      exclude: [
        "packages/*/src/**/*.spec.ts",
        "packages/core/goldens/*.spec.ts",
        "packages/core/src-linux/**/*.spec.ts",
        "scripts/codegen-core/**/*.spec.ts",
        "scripts/doctor/**/*.spec.ts",
        "scripts/e2e/**/*.spec.ts",
        "scripts/upstream/**/*.spec.ts",
      ],
      include: [
        "packages/*/src/**/*.ts",
        "packages/core/goldens/*.ts",
        "packages/core/src-linux/**/*.ts",
        "scripts/codegen-core/**/*.ts",
        "scripts/doctor/**/*.ts",
        "scripts/e2e/**/*.ts",
        "scripts/upstream/**/*.ts",
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
      "packages/core/src-linux/**/*.spec.ts",
      "scripts/codegen-core/**/*.spec.ts",
      "scripts/doctor/**/*.spec.ts",
      "scripts/e2e/**/*.spec.ts",
      "scripts/upstream/**/*.spec.ts",
    ],
  },
});
