import { afterEach, describe, expect, it } from "vitest";
import { declaredFonts, embedExpoAppAssets, requireFunction } from "./expo-app-assets.ts";
import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  rmSync,
  symlinkSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import { tmpdir } from "node:os";

const expoModules = path.resolve(import.meta.dirname, "..", "..", "expo", "node_modules");
const FIRST_PROJECT = 0;
const projects: string[] = [];
const fontsAppJson = JSON.stringify({
  expo: {
    name: "probe",
    plugins: [
      "expo-asset",
      [
        "expo-font",
        {
          android: {
            fonts: [
              "./android-only.ttf",
              { fontDefinitions: [{ path: "./android.ttf", weight: 400 }], fontFamily: "A" },
            ],
          },
          fonts: ["./fonts/Shared.ttf"],
          ios: { fonts: ["./fonts/family"] },
        },
      ],
    ],
    scheme: "probe",
    slug: "probe",
  },
});

const makeProject = (appJson: string | null): string => {
  const projectRoot = mkdtempSync(path.join(tmpdir(), "rnl-expo-app-assets-"));

  projects.push(projectRoot);
  writeFileSync(path.join(projectRoot, "package.json"), '{"name":"probe","version":"1.0.0"}');

  if (appJson !== null) {
    symlinkSync(expoModules, path.join(projectRoot, "node_modules"));
    writeFileSync(path.join(projectRoot, "app.json"), appJson);
  }

  return projectRoot;
};

const writeFont = (filePath: string): void => {
  mkdirSync(path.dirname(filePath), { recursive: true });
  writeFileSync(filePath, path.basename(filePath));
};

afterEach(() => {
  for (const projectRoot of projects.splice(FIRST_PROJECT)) {
    rmSync(projectRoot, { force: true, recursive: true });
  }
});

describe("embedExpoAppAssets", () => {
  it("writes upstream's app config and copies the fonts expo-font declares for iOS, from a relative root", async () => {
    const projectRoot = makeProject(fontsAppJson);
    const assetsDirectory = path.join(projectRoot, "build", "assets");

    writeFont(path.join(projectRoot, "fonts", "Shared.ttf"));
    writeFont(path.join(projectRoot, "fonts", "family", "Face-Bold.otf"));
    writeFont(path.join(projectRoot, "fonts", "family", "LICENSE.txt"));

    await embedExpoAppAssets(path.relative(process.cwd(), projectRoot), assetsDirectory);

    const appConfig = readFileSync(path.join(assetsDirectory, "app.config"), "utf8");

    expect(JSON.parse(appConfig)).toMatchObject({
      name: "probe",
      scheme: "probe",
    });
    expect(readdirSync(path.join(assetsDirectory, "fonts")).toSorted()).toStrictEqual(["Face-Bold.otf", "Shared.ttf"]);
  });

  it("writes the app config and no fonts for an app that declares none, clearing an earlier build's", async () => {
    const projectRoot = makeProject('{"expo":{"name":"probe","slug":"probe"}}');
    const assetsDirectory = path.join(projectRoot, "assets-out");

    writeFont(path.join(assetsDirectory, "fonts", "Stale.ttf"));

    await embedExpoAppAssets(projectRoot, assetsDirectory);

    expect(existsSync(path.join(assetsDirectory, "app.config"))).toBe(true);
    expect(existsSync(path.join(assetsDirectory, "fonts"))).toBe(false);
  });

  it("writes nothing for a project without expo-constants", async () => {
    const projectRoot = makeProject(null);
    const assetsDirectory = path.join(projectRoot, "assets-out");

    await embedExpoAppAssets(projectRoot, assetsDirectory);

    expect(existsSync(assetsDirectory)).toBe(false);
  });

  it("fails with upstream's message when the app config does not evaluate", async () => {
    const projectRoot = makeProject('{"expo": ');

    await expect(embedExpoAppAssets(projectRoot, path.join(projectRoot, "assets-out"))).rejects.toThrow(
      /expo-constants could not evaluate the app config of .*JsonFileError: Error parsing JSON/su,
    );
  });
});

describe("declaredFonts", () => {
  it.each([
    [null],
    [{}],
    [{ plugins: "expo-font" }],
    [{ plugins: ["expo-font"] }],
    [{ plugins: [["expo-font", ["./a.ttf"]]] }],
    [{ plugins: [["expo-font", { ios: ["./a.ttf"] }]] }],
  ])("finds no font in %j", (appConfig) => {
    expect(declaredFonts(appConfig)).toStrictEqual([]);
  });
});

describe("requireFunction", () => {
  it("refuses a module whose export is not a function", () => {
    const projectRoot = makeProject(null);
    const modulePath = path.join(projectRoot, "number.json");

    writeFileSync(modulePath, "1");

    expect(() => requireFunction(modulePath, "resolveFontPaths")).toThrow(`${modulePath} exports no function`);
    expect(() => requireFunction(path.join(projectRoot, "package.json"), "name")).toThrow("exports no function name");
  });
});
