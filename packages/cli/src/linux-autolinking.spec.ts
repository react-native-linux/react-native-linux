import type { AutolinkingDependency, AutolinkingVerdict } from "./linux-autolinking-types.ts";
import { describe, expect, it } from "vitest";
import { discoverLinuxAutolinking, parseReactNativeConfig, readOptedOutDependencyNames } from "./linux-autolinking.ts";

const applicationConfigPath = "/app/react-native.config.js";
const noFiles: Readonly<Record<string, string>> = {};
const nobodyOptedOut: ReadonlySet<string> = new Set();

const cppLibraryCMakeListsPath = "/node_modules/react-native-cpp-library/android/CMakeLists.txt";
const cppLibrarySourcePath = "/node_modules/react-native-cpp-library/cpp/CppLibraryImpl.cpp";
const templateCMakeLists = "add_library(\n    react-native-cpp-library\n    STATIC\n    ../cpp/CppLibraryImpl.cpp\n)\n";
const cppLibraryFiles: Readonly<Record<string, string>> = {
  [cppLibraryCMakeListsPath]: templateCMakeLists,
  [cppLibrarySourcePath]: '#include "CppLibraryImpl.h"',
};

const cppLibrary: AutolinkingDependency = {
  name: "react-native-cpp-library",
  platforms: {
    android: {
      cmakeListsPath: "/node_modules/react-native-cpp-library/android/generated/jni/CMakeLists.txt",
      cxxModuleCMakeListsModuleName: "react-native-cpp-library",
      cxxModuleCMakeListsPath: cppLibraryCMakeListsPath,
      cxxModuleHeaderName: "CppLibraryImpl",
    },
    linux: null,
  },
  root: "/node_modules/react-native-cpp-library",
};

const listCppSources =
  (files: Readonly<Record<string, string>>) =>
  (directoryPath: string): readonly string[] =>
    Object.keys(files).filter((filePath) => filePath.startsWith(`${directoryPath}/`) && filePath.endsWith(".cpp"));

const discover = (
  dependencies: readonly AutolinkingDependency[],
  files: Readonly<Record<string, string>> = noFiles,
  optedOutDependencyNames: ReadonlySet<string> = nobodyOptedOut,
): readonly AutolinkingVerdict[] =>
  discoverLinuxAutolinking({
    applicationConfigPath,
    dependencies,
    listSourceFiles: listCppSources(files),
    optedOutDependencyNames,
    readFile: (filePath) => files[filePath] ?? null,
  });

const discoverOne = (
  dependency: AutolinkingDependency,
  files: Readonly<Record<string, string>> = noFiles,
): AutolinkingVerdict | undefined => {
  const [verdict] = discover([dependency], files);

  return verdict;
};

const packageAt = (name: string, platforms: AutolinkingDependency["platforms"] = {}): AutolinkingDependency => ({
  name,
  platforms,
  root: `/node_modules/${name}`,
});

describe("discoverLinuxAutolinking, the pure-C++ fallback", () => {
  it("links react-native-cpp-library, which mentions linux nowhere, through its Android cxxModule descriptor", () => {
    expect(discoverOne(cppLibrary, cppLibraryFiles)).toStrictEqual({
      cmakeListsPath: cppLibraryCMakeListsPath,
      kind: "linked",
      message: `react-native-cpp-library: linked by the cxx-fallback rule from ${cppLibraryCMakeListsPath}`,
      moduleHeaderName: "CppLibraryImpl",
      moduleName: "react-native-cpp-library",
      packageName: "react-native-cpp-library",
      rule: "cxx-fallback",
    });
  });

  it("ignores a JNI adapter beside the CMakeLists that the CMakeLists does not compile", () => {
    const files = {
      ...cppLibraryFiles,
      "/node_modules/react-native-cpp-library/android/cpp-adapter.cpp": "#include <jni.h>",
    };

    expect(discoverOne(cppLibrary, files)).toMatchObject({ kind: "linked", rule: "cxx-fallback" });
  });

  it("links with no module name or header when the descriptor names only the CMakeLists", () => {
    const dependency = packageAt("bare", {
      android: { cxxModuleCMakeListsPath: "/node_modules/bare/cpp/CMakeLists.txt" },
    });

    expect(discoverOne(dependency)).toMatchObject({ moduleHeaderName: null, moduleName: null, rule: "cxx-fallback" });
  });

  it("treats a listed source that cannot be read, or a CMakeLists that cannot, as portable", () => {
    const files: Readonly<Record<string, string>> = { [cppLibraryCMakeListsPath]: templateCMakeLists };
    const verdicts = discoverLinuxAutolinking({
      applicationConfigPath,
      dependencies: [
        cppLibrary,
        packageAt("unreadable", { android: { cxxModuleCMakeListsPath: "/gone/CMakeLists.txt" } }),
      ],
      listSourceFiles: () => [cppLibrarySourcePath],
      optedOutDependencyNames: nobodyOptedOut,
      readFile: (filePath) => files[filePath] ?? null,
    });

    expect(verdicts).toMatchObject([{ kind: "linked" }, { kind: "linked" }]);
  });
});

describe("discoverLinuxAutolinking, the fallback's guard", () => {
  it.each([
    ["#include <jni.h>", "<jni.h>"],
    ["  #include <fbjni/fbjni.h>", "<fbjni/"],
    ['#import "RCTBridgeModule.h"', "#import"],
    ["#include <Foundation/Foundation.h>", "an Apple framework header"],
  ])("rejects a source reading %s, naming the source and %s", (line, include) => {
    const sourcePath = "/node_modules/react-native-cpp-library/cpp/Platform.cpp";
    const files = {
      [cppLibraryCMakeListsPath]: `file(GLOB sources "$${"{"}CMAKE_CURRENT_SOURCE_DIR}/../cpp/*.cpp")`,
      [sourcePath]: `${line}\n`,
    };

    expect(discoverOne(cppLibrary, files)).toStrictEqual({
      kind: "rejected",
      message: `react-native-cpp-library: not linked, ${sourcePath} includes ${include}; declare a portable build under platforms.linux in its react-native.config.js`,
      packageName: "react-native-cpp-library",
    });
  });
});

describe("discoverLinuxAutolinking, an explicit platforms.linux descriptor", () => {
  it("links by its cxxModule CMakeLists first", () => {
    const dependency = packageAt("explicit", {
      linux: {
        cmakeListsPath: "/node_modules/explicit/linux/CMakeLists.txt",
        cxxModuleCMakeListsModuleName: "explicit",
        cxxModuleCMakeListsPath: "/node_modules/explicit/cpp/CMakeLists.txt",
        cxxModuleHeaderName: "NativeExplicit",
      },
    });

    expect(discoverOne(dependency)).toMatchObject({
      cmakeListsPath: "/node_modules/explicit/cpp/CMakeLists.txt",
      moduleHeaderName: "NativeExplicit",
      moduleName: "explicit",
      rule: "explicit",
    });
  });

  it.each([
    [{ cmakeListsPath: "/node_modules/explicit/linux/Build.cmake" }, "/node_modules/explicit/linux/Build.cmake"],
    [{ sourceDir: "/node_modules/explicit/native" }, "/node_modules/explicit/native/CMakeLists.txt"],
    [{}, "/node_modules/explicit/linux/CMakeLists.txt"],
  ])("resolves %j to %s", (linux, cmakeListsPath) => {
    expect(discoverOne(packageAt("explicit", { linux }))).toMatchObject({ cmakeListsPath, rule: "explicit" });
  });

  it("loses to the application's opt-out, which names the application config", () => {
    const dependency = packageAt("explicit", { linux: { sourceDir: "/node_modules/explicit/linux" } });

    expect(discover([dependency], noFiles, new Set(["explicit"]))).toStrictEqual([
      {
        kind: "opted-out",
        message: `explicit: not linked, opted out by platforms.linux: null in ${applicationConfigPath}; remove that entry to link it`,
        packageName: "explicit",
      },
    ]);
  });
});

describe("discoverLinuxAutolinking, Nitro", () => {
  it("compiles every hybrid object with an all implementation and names the rest", () => {
    const autolinking = {
      HybridBroken: "not an object",
      HybridCamera: { android: { language: "kotlin" }, ios: { language: "swift" } },
      HybridMath: { all: { language: "c++" } },
    };
    const files = { "/node_modules/nitro-lib/nitro.json": JSON.stringify({ autolinking }) };

    expect(discoverOne(packageAt("nitro-lib"), files)).toStrictEqual({
      compiled: ["HybridMath"],
      kind: "nitro",
      message:
        'nitro-lib: Nitro hybrid objects compiled: HybridMath; no "all" implementation in /node_modules/nitro-lib/nitro.json for HybridBroken, HybridCamera, which throw when created',
      packageName: "nitro-lib",
      unimplemented: ["HybridBroken", "HybridCamera"],
    });
  });

  it("reads a nitro.json without an autolinking object as compiling nothing", () => {
    const files = { "/node_modules/nitro-lib/nitro.json": "{}" };

    expect(discoverOne(packageAt("nitro-lib"), files)).toMatchObject({
      message: "nitro-lib: Nitro hybrid objects compiled: none",
      unimplemented: [],
    });
  });

  it("fails loudly, naming the file, when a config is not a JSON object", () => {
    const files = { "/node_modules/nitro-lib/nitro.json": "[]" };

    expect(() => discoverOne(packageAt("nitro-lib"), files)).toThrow(
      "/node_modules/nitro-lib/nitro.json must contain a JSON object",
    );
  });
});

describe("discoverLinuxAutolinking, Expo and plain packages", () => {
  const expoConfigPath = "/node_modules/expo-thing/expo-module.config.json";

  it.each([['{ "platforms": ["ios", "android"] }'], ["{}"]])(
    "reports an Expo module whose config %s lacks linux",
    (contents) => {
      expect(discoverOne(packageAt("expo-thing"), { [expoConfigPath]: contents })).toStrictEqual({
        kind: "expo-module",
        message: `expo-thing: not linked, ${expoConfigPath} does not list linux; Expo modules are not supported on Linux yet`,
        packageName: "expo-thing",
      });
    },
  );

  it("falls through an Expo config that lists linux to the remaining rules", () => {
    const files = { [expoConfigPath]: '{ "platforms": ["linux"] }' };

    expect(discoverOne(packageAt("expo-thing"), files)).toMatchObject({ kind: "no-native-code" });
  });

  it("finds no native code without a cxxModule descriptor, and sorts verdicts by package", () => {
    const zeta = packageAt("zeta", { android: { cmakeListsPath: "/node_modules/zeta/android/CMakeLists.txt" } });

    expect(discover([zeta, packageAt("alpha", { android: null })])).toStrictEqual([
      { kind: "no-native-code", message: "alpha: no native code for Linux; nothing to link", packageName: "alpha" },
      { kind: "no-native-code", message: "zeta: no native code for Linux; nothing to link", packageName: "zeta" },
    ]);
  });
});

describe("parseReactNativeConfig", () => {
  it("reads the application root and each dependency's root and descriptors, dropping everything else", () => {
    const config = {
      dependencies: {
        broken: "not an object",
        lib: {
          platforms: {
            android: { cxxModuleCMakeListsPath: "/lib/android/CMakeLists.txt", javaPackageName: 1 },
            ios: {},
          },
          root: "/lib",
        },
        rootless: { platforms: {} },
        unplatformed: { root: "/unplatformed" },
      },
      root: "/app",
    };

    expect(parseReactNativeConfig(JSON.stringify(config))).toStrictEqual({
      dependencies: [
        {
          name: "lib",
          platforms: { android: { cxxModuleCMakeListsPath: "/lib/android/CMakeLists.txt" }, linux: null },
          root: "/lib",
        },
        { name: "unplatformed", platforms: { android: null, linux: null }, root: "/unplatformed" },
      ],
      root: "/app",
    });
  });

  it("reads a config without dependencies as an empty tree", () => {
    expect(parseReactNativeConfig('{ "root": "/app" }')).toStrictEqual({ dependencies: [], root: "/app" });
  });

  it.each([["[]"], ['{ "dependencies": {} }']])("fails loudly on %s", (contents) => {
    expect(() => parseReactNativeConfig(contents)).toThrow(
      "react-native config output must be a JSON object with a string root",
    );
  });
});

describe("readOptedOutDependencyNames", () => {
  it("names each dependency whose platforms.linux is null", () => {
    const applicationConfig = {
      dependencies: {
        kept: { platforms: { linux: {} } },
        loose: "x",
        optedOut: { platforms: { linux: null } },
        plain: {},
      },
    };

    expect([...readOptedOutDependencyNames(applicationConfig)]).toStrictEqual(["optedOut"]);
  });

  it.each([[null], [{}]])("names nobody for %j", (applicationConfig) => {
    expect(readOptedOutDependencyNames(applicationConfig)).toStrictEqual(new Set());
  });
});
