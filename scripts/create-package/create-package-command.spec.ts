import { describe, expect, it } from "vitest";

import { createPackage } from "./create-package-command.ts";

const REPOSITORY_URL = "https://github.com/software-mansion/react-native-reanimated.git";
const TAG = "3.19.0";
const REPOSITORY_ROOT = "/repo";
const ARGUMENTS = ["reanimated", REPOSITORY_URL, TAG];
const USAGE_LINE = "  node scripts/create-package.ts <lib> <upstream-repo-url> <tag> [--sparse <path>...]";
const SUCCESS_EXIT_CODE = 0;
const FAILURE_EXIT_CODE = 1;
const NO_WRITES = 0;

interface ScaffoldEnvironment {
  readonly pathExists: (targetPath: string) => boolean;
  readonly writeTextFile: (filePath: string, contents: string) => void;
}

interface RecordingEnvironment {
  readonly environment: ScaffoldEnvironment;
  readonly written: Map<string, string>;
}

const createRecordingEnvironment = (existingPaths: readonly string[]): RecordingEnvironment => {
  const written = new Map<string, string>();

  return {
    environment: {
      pathExists: (targetPath: string): boolean => existingPaths.includes(targetPath),
      writeTextFile: (filePath: string, contents: string): void => {
        written.set(filePath, contents);
      },
    },
    written,
  };
};

describe("createPackage", () => {
  it("writes every scaffold file under packages/<lib>", () => {
    const { environment, written } = createRecordingEnvironment([]);
    const result = createPackage(ARGUMENTS, REPOSITORY_ROOT, environment);

    expect(result.exitCode).toBe(SUCCESS_EXIT_CODE);
    expect([...written.keys()]).toEqual([
      "/repo/packages/reanimated/package.json",
      "/repo/packages/reanimated/upstream.lock.json",
      "/repo/packages/reanimated/patches/.gitkeep",
      "/repo/packages/reanimated/linux/README.md",
      "/repo/packages/reanimated/src/index.ts",
      "/repo/packages/reanimated/README.md",
    ]);
  });

  it("reports what it created and the bump that vendors the tree", () => {
    const { environment } = createRecordingEnvironment([]);

    expect(createPackage(ARGUMENTS, REPOSITORY_ROOT, environment).messages).toContain(
      "  pnpm upstream:bump reanimated 3.19.0",
    );
  });

  it("refuses to overwrite an existing package", () => {
    const { environment, written } = createRecordingEnvironment(["/repo/packages/reanimated"]);
    const result = createPackage(ARGUMENTS, REPOSITORY_ROOT, environment);

    expect(result).toStrictEqual({ exitCode: FAILURE_EXIT_CODE, messages: ["packages/reanimated already exists"] });
    expect(written.size).toBe(NO_WRITES);
  });

  it("prints the usage of the command when the arguments do not parse", () => {
    const { environment, written } = createRecordingEnvironment([]);
    const result = createPackage(["reanimated"], REPOSITORY_ROOT, environment);

    expect(result.exitCode).toBe(FAILURE_EXIT_CODE);
    expect(result.messages).toContain(USAGE_LINE);
    expect(written.size).toBe(NO_WRITES);
  });
});
