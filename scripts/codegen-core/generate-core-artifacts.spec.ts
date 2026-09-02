import type { CoreCodegenEnvironment, CoreCodegenPaths, DirectoryEntry } from "./generate-core-artifacts-types.ts";
import type { LibraryConfig, LibraryOptions } from "@react-native/codegen/lib/generators/RNCodegen.js";

import { buildCodegenLock, collectSpecFilePaths, runCoreCodegen } from "./generate-core-artifacts.ts";
import { describe, expect, it } from "vitest";

interface GenerateCall {
  readonly config: LibraryConfig;
  readonly options: LibraryOptions;
}

interface CodegenRecording {
  readonly generateCalls: readonly GenerateCall[];
  readonly removedFiles: readonly string[];
  readonly reports: readonly string[];
  readonly resetDirectories: readonly string[];
  readonly writtenFiles: ReadonlyMap<string, string>;
}

type FileSystemSeam = Pick<CoreCodegenEnvironment, "listDirectory" | "readFile">;

const REPOSITORY_ROOT = "/repo";
const SPEC_SOURCE_DIRECTORY = "/repo/third_party/react-native/packages/react-native/src";
const OUTPUT_DIRECTORY = "/repo/packages/core/generated";
const CODEGEN_PACKAGE_JSON_PATH = "/repo/node_modules/@react-native/codegen/package.json";
const CODEGEN_PACKAGE_JSON = '{"version":"0.87.1"}';

const TURBO_MODULE_SOURCE = "export interface Spec extends TurboModule {}";
const NATIVE_COMPONENT_SOURCE = "export default codegenNativeComponent<NativeProps>('Widget');";
const PARENTHESISED_COMPONENT_SOURCE = "export default (codegenNativeComponent<NativeProps>('Widget'): Widget);";
const UNRELATED_SOURCE = "export default null;";
const EMPTY_FILE_SHA256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

const paths: CoreCodegenPaths = {
  codegenPackageJsonPath: CODEGEN_PACKAGE_JSON_PATH,
  outputDirectory: OUTPUT_DIRECTORY,
  repositoryRoot: REPOSITORY_ROOT,
  specSourceDirectory: SPEC_SOURCE_DIRECTORY,
};

const addDirectoryEntry = (entries: DirectoryEntry[], seenNames: Set<string>, entry: DirectoryEntry): void => {
  if (!seenNames.has(entry.name)) {
    seenNames.add(entry.name);
    entries.push(entry);
  }
};

const buildFileSystem = (files: Readonly<Record<string, string>>): FileSystemSeam => ({
  listDirectory: (directoryPath) => {
    const prefix = `${directoryPath}/`;
    const seenNames = new Set<string>();
    const entries: DirectoryEntry[] = [];

    for (const filePath of Object.keys(files)) {
      if (filePath.startsWith(prefix)) {
        const remainder = filePath.slice(prefix.length);
        const [name = remainder] = remainder.split("/");

        addDirectoryEntry(entries, seenNames, { isDirectory: name !== remainder, name });
      }
    }

    return entries;
  },
  readFile: (filePath) => {
    const contents = files[filePath];

    if (typeof contents !== "string") {
      throw new TypeError(`unexpected read of ${filePath}`);
    }

    return contents;
  },
});

const specFiles: Readonly<Record<string, string>> = {
  [CODEGEN_PACKAGE_JSON_PATH]: CODEGEN_PACKAGE_JSON,
  [`${SPEC_SOURCE_DIRECTORY}/NativeWidget.js`]: TURBO_MODULE_SOURCE,
  [`${SPEC_SOURCE_DIRECTORY}/WidgetNativeComponent.js`]: NATIVE_COMPONENT_SOURCE,
};

const recordCodegenRun = (): CodegenRecording => {
  const generateCalls: GenerateCall[] = [];
  const removedFiles: string[] = [];
  const reports: string[] = [];
  const resetDirectories: string[] = [];
  const writtenFiles = new Map<string, string>();

  const environment: CoreCodegenEnvironment = {
    ...buildFileSystem(specFiles),
    generateArtifacts: (options, config) => {
      generateCalls.push({ config, options });
      return true;
    },
    parseSpecFile: (filePath) => ({
      modules: {
        [filePath.endsWith("NativeWidget.js") ? "NativeWidget" : "Widget"]: { components: {}, type: "Component" },
      },
    }),
    removeFile: (filePath) => {
      removedFiles.push(filePath);
    },
    report: (message) => {
      reports.push(message);
    },
    resetDirectory: (directoryPath) => {
      resetDirectories.push(directoryPath);
    },
    writeFile: (filePath, contents) => {
      writtenFiles.set(filePath, contents);
    },
  };

  runCoreCodegen(paths, environment);

  return { generateCalls, removedFiles, reports, resetDirectories, writtenFiles };
};

describe("collectSpecFilePaths", () => {
  it("keeps only platform-agnostic spec file names", () => {
    const files = {
      [`${SPEC_SOURCE_DIRECTORY}/private/NativeSpecialised.ios.js`]: TURBO_MODULE_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/private/NativeTypes.d.ts`]: TURBO_MODULE_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/private/NativeWidget.js`]: TURBO_MODULE_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/private/Unrelated.js`]: TURBO_MODULE_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/private/WidgetNativeComponent.js`]: NATIVE_COMPONENT_SOURCE,
    };

    expect(collectSpecFilePaths(SPEC_SOURCE_DIRECTORY, buildFileSystem(files))).toEqual([
      `${SPEC_SOURCE_DIRECTORY}/private/NativeWidget.js`,
      `${SPEC_SOURCE_DIRECTORY}/private/WidgetNativeComponent.js`,
    ]);
  });

  it("accepts a parenthesised codegenNativeComponent export and drops a file that declares no spec", () => {
    const files = {
      [`${SPEC_SOURCE_DIRECTORY}/NativeIgnored.js`]: UNRELATED_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/WrappedNativeComponent.js`]: PARENTHESISED_COMPONENT_SOURCE,
    };

    expect(collectSpecFilePaths(SPEC_SOURCE_DIRECTORY, buildFileSystem(files))).toEqual([
      `${SPEC_SOURCE_DIRECTORY}/WrappedNativeComponent.js`,
    ]);
  });

  it("skips NativeUIManager and directories whose name starts with a double underscore", () => {
    const files = {
      [`${SPEC_SOURCE_DIRECTORY}/__tests__/NativeFixture.js`]: TURBO_MODULE_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/modules/NativeUIManager.js`]: TURBO_MODULE_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/modules/nested/NativeKept.js`]: TURBO_MODULE_SOURCE,
    };

    expect(collectSpecFilePaths(SPEC_SOURCE_DIRECTORY, buildFileSystem(files))).toEqual([
      `${SPEC_SOURCE_DIRECTORY}/modules/nested/NativeKept.js`,
    ]);
  });
});

describe("collectSpecFilePaths ordering", () => {
  it("returns paths in ascending order whatever order the directories are listed in", () => {
    const files = {
      [`${SPEC_SOURCE_DIRECTORY}/zeta/NativeZeta.js`]: TURBO_MODULE_SOURCE,
      [`${SPEC_SOURCE_DIRECTORY}/alpha/NativeAlpha.js`]: TURBO_MODULE_SOURCE,
    };

    expect(collectSpecFilePaths(SPEC_SOURCE_DIRECTORY, buildFileSystem(files))).toEqual([
      `${SPEC_SOURCE_DIRECTORY}/alpha/NativeAlpha.js`,
      `${SPEC_SOURCE_DIRECTORY}/zeta/NativeZeta.js`,
    ]);
  });
});

describe("buildCodegenLock", () => {
  it("records repository-relative posix input paths, their digests and the codegen version", () => {
    const files = {
      [CODEGEN_PACKAGE_JSON_PATH]: CODEGEN_PACKAGE_JSON,
      [`${SPEC_SOURCE_DIRECTORY}/NativeWidget.js`]: "",
    };

    expect(buildCodegenLock([`${SPEC_SOURCE_DIRECTORY}/NativeWidget.js`], paths, buildFileSystem(files))).toEqual({
      codegenVersion: "0.87.1",
      inputs: [
        {
          path: "third_party/react-native/packages/react-native/src/NativeWidget.js",
          sha256: EMPTY_FILE_SHA256,
        },
      ],
      libraryName: "FBReactNativeSpec",
      specSourceDirectory: "third_party/react-native/packages/react-native/src",
    });
  });

  it.each(["[]", "null", "7"])("rejects a codegen manifest whose contents are %s", (manifest) => {
    const files = { [CODEGEN_PACKAGE_JSON_PATH]: manifest };

    expect(() => buildCodegenLock([], paths, buildFileSystem(files))).toThrow("must contain a JSON object");
  });

  it("rejects a codegen manifest whose version is not a string", () => {
    const files = { [CODEGEN_PACKAGE_JSON_PATH]: '{"version":null}' };

    expect(() => buildCodegenLock([], paths, buildFileSystem(files))).toThrow('must declare a string "version"');
  });
});

describe("runCoreCodegen", () => {
  it("clears the output directory before generating", () => {
    expect(recordCodegenRun().resetDirectories).toEqual([OUTPUT_DIRECTORY]);
  });

  it("generates the C++ module spec into the FBReactNativeSpec include directory", () => {
    const [moduleCall] = recordCodegenRun().generateCalls;

    expect(moduleCall?.config).toEqual({ generators: ["modulesCxx"] });
    expect(moduleCall?.options.outputDirectory).toBe(`${OUTPUT_DIRECTORY}/FBReactNativeSpec`);
    expect(moduleCall?.options.libraryName).toBe("FBReactNativeSpec");
    expect(moduleCall?.options.packageName).toBe("com.facebook.fbreact.specs");
    expect(moduleCall?.options.assumeNonnull).toBe(false);
  });

  it("generates the components at the output root so the generator composes the renderer include path", () => {
    const [, componentCall] = recordCodegenRun().generateCalls;

    expect(componentCall?.config).toEqual({ generators: ["componentsIOS"] });
    expect(componentCall?.options.outputDirectory).toBe(OUTPUT_DIRECTORY);
  });

  it("merges every spec file into one schema, in sorted input order", () => {
    const [moduleCall] = recordCodegenRun().generateCalls;

    expect(Object.keys(moduleCall?.options.schema.modules ?? {})).toEqual(["NativeWidget", "Widget"]);
  });

  it("removes the Objective-C++ view helper the components generator emits", () => {
    expect(recordCodegenRun().removedFiles).toEqual([
      `${OUTPUT_DIRECTORY}/react/renderer/components/FBReactNativeSpec/RCTComponentViewHelpers.h`,
    ]);
  });

  it("writes a newline-terminated lock file naming every input", () => {
    const lockContents = recordCodegenRun().writtenFiles.get(`${OUTPUT_DIRECTORY}/codegen.lock.json`);

    expect(lockContents).toMatch(/\n$/u);
    expect(lockContents).toContain('"codegenVersion": "0.87.1"');
    expect(lockContents).toContain("react-native/src/NativeWidget.js");
    expect(lockContents).toContain("react-native/src/WidgetNativeComponent.js");
  });

  it("reports how many spec files were consumed", () => {
    expect(recordCodegenRun().reports).toEqual([
      `Generated FBReactNativeSpec from 2 spec files into ${OUTPUT_DIRECTORY}`,
    ]);
  });
});
