import { describe, expect, it } from "vitest";
import { createRequire } from "node:module";
import path from "node:path";
import { readFileSync } from "node:fs";

const repositoryRoot = path.resolve(import.meta.dirname, "..", "..", "..");
const templateRoot = path.join(repositoryRoot, "packages", "template");

const readJson = (filePath: string): unknown => JSON.parse(readFileSync(filePath, "utf8"));

const readDependencyVersions = (filePath: string): Readonly<Record<string, unknown>> => {
  const manifest = readJson(filePath);

  if (typeof manifest !== "object" || manifest === null) {
    throw new TypeError(`${filePath} is not a JSON object`);
  }

  return {
    ...("dependencies" in manifest && typeof manifest.dependencies === "object" ? manifest.dependencies : {}),
    ...("devDependencies" in manifest && typeof manifest.devDependencies === "object" ? manifest.devDependencies : {}),
  };
};

const templateConfig: unknown = createRequire(import.meta.url)(path.join(templateRoot, "template.config.js"));
const appDirectory = path.join(templateRoot, "template");
const appVersions = readDependencyVersions(path.join(appDirectory, "package.json"));

describe("the init template", () => {
  it("names its placeholder and template directory the way the community CLI reads them", () => {
    expect(templateConfig).toStrictEqual({ placeholderName: "HelloWorld", templateDir: "./template" });
    expect(readFileSync(path.join(appDirectory, "index.tsx"), "utf8")).toContain('registerComponent("HelloWorld"');
  });

  it("pins every dependency the harness app also pins to the harness's own version", () => {
    const harnessVersions = readDependencyVersions(
      path.join(repositoryRoot, "packages", "test-harness", "package.json"),
    );
    const shared = Object.keys(appVersions).filter((name) => Object.hasOwn(harnessVersions, name));

    expect(shared.length).toBe(Object.keys(appVersions).length);

    for (const name of shared) {
      expect({ name, version: appVersions[name] }).toStrictEqual({ name, version: harnessVersions[name] });
    }
  });

  it("runs the worklets Babel plugin last, as react-native-worklets requires, with the harness's own config", () => {
    const babelConfig = readJson(path.join(appDirectory, ".babelrc"));

    expect(babelConfig).toMatchObject({
      plugins: ["babel-plugin-transform-flow-enums", "react-native-worklets/plugin"],
    });
    expect(babelConfig).toStrictEqual(readJson(path.join(repositoryRoot, "packages", "test-harness", ".babelrc")));
  });

  it("pins react-native-worklets to the version core compiles the worklets runtime from", () => {
    const catalogVersion = /^\s+react-native-worklets: (?<version>\S+)$/mu.exec(
      readFileSync(path.join(repositoryRoot, "pnpm-workspace.yaml"), "utf8"),
    )?.groups?.["version"];

    expect(appVersions["react-native-worklets"]).toBe(catalogVersion);
  });
});
