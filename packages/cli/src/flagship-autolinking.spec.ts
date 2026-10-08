import { describe, expect, it } from "vitest";
import { discoverLinuxAutolinking, parseReactNativeConfig } from "./linux-autolinking.ts";
import { readFileSync } from "node:fs";

const snapshotIndentation = 2;
const files: Readonly<Record<string, string>> = {
  "/flagship/node_modules/expo/expo-module.config.json":
    '{\n  "platforms": ["apple", "android"],\n  "apple": {\n    "modules": ["ExpoFetchModule"],\n    "podspecPath": "Expo.podspec"\n  },\n  "android": {\n    "modules": ["expo.modules.fetch.ExpoFetchModule"]\n  }\n}\n',
};

describe("flagship discovery", () => {
  it("pins every verdict from the Suuudokuuu 2.18.1 native config", async () => {
    const config = parseReactNativeConfig(
      readFileSync(new URL("fixtures/flagship-react-native-config.json", import.meta.url), "utf8"),
    );
    const verdicts = discoverLinuxAutolinking({
      applicationConfigPath: `${config.root}/react-native.config.js`,
      dependencies: config.dependencies,
      listSourceFiles: () => [],
      optedOutDependencyNames: new Set(),
      readFile: (filePath) => files[filePath] ?? null,
    });

    expect(verdicts.map(({ packageName }) => packageName)).toStrictEqual(
      config.dependencies.map(({ name }) => name).toSorted(),
    );
    await expect(`${JSON.stringify(verdicts, null, snapshotIndentation)}\n`).toMatchFileSnapshot(
      "./fixtures/flagship-autolinking-verdicts.json",
    );
  });
});
