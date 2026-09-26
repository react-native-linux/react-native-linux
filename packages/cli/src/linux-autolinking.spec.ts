import type { AutolinkingDependency, AutolinkingVerdict } from "./linux-autolinking-types.ts";
import { describe, expect, it } from "vitest";
import { discoverLinuxAutolinking } from "./linux-autolinking.ts";

const applicationConfigPath = "/app/react-native.config.js";
const noFiles: Readonly<Record<string, string>> = {};
const nobodyOptedOut: ReadonlySet<string> = new Set();

const cppLibrary: AutolinkingDependency = {
  name: "react-native-cpp-library",
  platforms: {
    android: {
      cmakeListsPath: "/node_modules/react-native-cpp-library/android/CMakeLists.txt",
      cxxModuleCMakeListsModuleName: "react-native-cpp-library",
      cxxModuleCMakeListsPath: "/node_modules/react-native-cpp-library/cpp/CMakeLists.txt",
      cxxModuleHeaderName: "NativeCppLibraryModule",
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
    const files = { "/node_modules/react-native-cpp-library/cpp/Module.cpp": '#include "NativeCppLibraryModule.h"' };

    expect(discoverOne(cppLibrary, files)).toStrictEqual({
      cmakeListsPath: "/node_modules/react-native-cpp-library/cpp/CMakeLists.txt",
      kind: "linked",
      message:
        "react-native-cpp-library: linked by the cxx-fallback rule from /node_modules/react-native-cpp-library/cpp/CMakeLists.txt",
      moduleHeaderName: "NativeCppLibraryModule",
      moduleName: "react-native-cpp-library",
      packageName: "react-native-cpp-library",
      rule: "cxx-fallback",
    });
  });

  it("links with no module name or header when the descriptor names only the CMakeLists", () => {
    const dependency = packageAt("bare", {
      android: { cxxModuleCMakeListsPath: "/node_modules/bare/cpp/CMakeLists.txt" },
    });

    expect(discoverOne(dependency)).toMatchObject({ moduleHeaderName: null, moduleName: null, rule: "cxx-fallback" });
  });

  it("treats a listed source that cannot be read as portable", () => {
    const verdicts = discoverLinuxAutolinking({
      applicationConfigPath,
      dependencies: [cppLibrary],
      listSourceFiles: () => ["/node_modules/react-native-cpp-library/cpp/Gone.cpp"],
      optedOutDependencyNames: nobodyOptedOut,
      readFile: () => null,
    });

    expect(verdicts).toMatchObject([{ kind: "linked", rule: "cxx-fallback" }]);
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

    expect(discoverOne(cppLibrary, { [sourcePath]: `${line}\n` })).toStrictEqual({
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
