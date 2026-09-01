import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    coverage: {
      exclude: ["packages/*/src/**/*.spec.ts", "packages/core/goldens/*.spec.ts"],
      include: ["packages/*/src/**/*.ts", "packages/core/goldens/*.ts"],
      provider: "v8",
      reporter: ["text", "lcov"],
      thresholds: {
        branches: 100,
        functions: 100,
        lines: 100,
        statements: 100,
      },
    },
    include: ["packages/*/src/**/*.spec.ts", "packages/core/goldens/*.spec.ts"],
  },
});
