import { buildPackageFiles, parseCreatePackageArguments } from "./create-package-scaffold.ts";
import { describe, expect, it } from "vitest";

const REPOSITORY_URL = "https://github.com/software-mansion/react-native-reanimated.git";
const TAG = "3.19.0";
const POSITIONAL_ARGUMENTS = ["reanimated", REPOSITORY_URL, TAG];
const SPARSE_PATHS = ["packages/react-native-reanimated/src", "packages/react-native-reanimated/Common"] as const;

const request = {
  libraryName: "reanimated",
  repo: REPOSITORY_URL,
  sparsePaths: ["src"],
  tag: TAG,
} as const;

const files = buildPackageFiles(request);

const contentsOf = (relativePath: string): string =>
  files.find((file) => file.relativePath === relativePath)?.contents ?? "";

const parsedJsonOf = (relativePath: string): unknown => JSON.parse(contentsOf(relativePath));

describe("parseCreatePackageArguments", () => {
  it("reads the three positional arguments and defaults the sparse paths to src", () => {
    expect(parseCreatePackageArguments(POSITIONAL_ARGUMENTS)).toStrictEqual(request);
  });

  it("reads every path after the sparse flag", () => {
    expect(parseCreatePackageArguments([...POSITIONAL_ARGUMENTS, "--sparse", ...SPARSE_PATHS])).toStrictEqual({
      ...request,
      sparsePaths: SPARSE_PATHS,
    });
  });

  it("rejects a call without a library name, a repository, or a tag", () => {
    expect(parseCreatePackageArguments([])).toBeNull();
    expect(parseCreatePackageArguments(["reanimated"])).toBeNull();
    expect(parseCreatePackageArguments(["reanimated", REPOSITORY_URL])).toBeNull();
  });

  it("rejects a fourth positional argument", () => {
    expect(parseCreatePackageArguments([...POSITIONAL_ARGUMENTS, "extra"])).toBeNull();
  });

  it("rejects a library name that is not lowercase kebab case", () => {
    expect(parseCreatePackageArguments(["Reanimated", REPOSITORY_URL, TAG])).toBeNull();
    expect(parseCreatePackageArguments(["@scope/reanimated", REPOSITORY_URL, TAG])).toBeNull();
  });

  it("rejects a sparse flag without a path, because the lock needs a non-empty cone", () => {
    expect(parseCreatePackageArguments([...POSITIONAL_ARGUMENTS, "--sparse"])).toBeNull();
  });
});

describe("buildPackageFiles", () => {
  it("scaffolds the package layout of the template", () => {
    expect(files.map((file) => file.relativePath)).toEqual([
      "package.json",
      "upstream.lock.json",
      "patches/.gitkeep",
      "linux/README.md",
      "README.md",
    ]);
  });

  it("names the package after the platform scope and keeps it private with both peers", () => {
    expect(parsedJsonOf("package.json")).toStrictEqual({
      exports: { ".": "./upstream/src/index.ts" },
      name: "@react-native-linux/reanimated",
      peerDependencies: { "@react-native-linux/core": "workspace:*", "react-native": "*" },
      private: true,
      type: "module",
      version: "0.0.0",
    });
  });

  it("writes the bootstrap lock the first bump fills in", () => {
    expect(parsedJsonOf("upstream.lock.json")).toStrictEqual({
      repo: REPOSITORY_URL,
      sha256: {},
      sparsePaths: ["src"],
      tag: TAG,
    });
  });

  it("locks every requested sparse path", () => {
    const lockContents = buildPackageFiles({ ...request, sparsePaths: SPARSE_PATHS }).find(
      (file) => file.relativePath === "upstream.lock.json",
    );

    expect(JSON.parse(lockContents?.contents ?? "")).toStrictEqual({
      repo: REPOSITORY_URL,
      sha256: {},
      sparsePaths: SPARSE_PATHS,
      tag: TAG,
    });
  });
});

describe("the scaffolded sources and documentation", () => {
  it("keeps the patch queue directory empty", () => {
    expect(contentsOf("patches/.gitkeep")).toBe("");
  });

  it("documents the native directory autolinking discovers", () => {
    expect(contentsOf("linux/README.md")).toContain("sourceDir");
  });

  it("records the vendored tag, the empty patch table, and the conformance command", () => {
    const readme = contentsOf("README.md");

    expect(readme).toContain(`vendored at \`${TAG}\``);
    expect(readme).toContain("| Patch | What it changes | Deletion trigger |");
    expect(readme).toContain("pnpm e2e --scenario");
  });

  it("tells the maintainer to register the metro alias and to unset private", () => {
    const readme = contentsOf("README.md");

    expect(readme).toContain("packages/cli/src/package-aliases.ts");
    expect(readme).toContain('Drop `"private": true`');
  });
});
