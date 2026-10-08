interface NativeBuildDescriptor {
  readonly cmakeListsPath?: string | null;
  readonly cxxModuleCMakeListsModuleName?: string | null;
  readonly cxxModuleCMakeListsPath?: string | null;
  readonly cxxModuleHeaderName?: string | null;
  readonly sourceDir?: string | null;
}

interface AutolinkingDependency {
  readonly name: string;
  readonly platforms: {
    readonly android?: NativeBuildDescriptor | null;
    readonly linux?: NativeBuildDescriptor | null;
  };
  readonly root: string;
}

interface AutolinkingRequest {
  readonly applicationConfigPath: string;
  readonly dependencies: readonly AutolinkingDependency[];
  readonly listSourceFiles: (directoryPath: string) => readonly string[];
  readonly optedOutDependencyNames: ReadonlySet<string>;
  readonly readFile: (filePath: string) => string | null;
}

interface LinkedLibrary {
  readonly cmakeListsPath: string;
  readonly moduleHeaderName: string | null;
  readonly moduleName: string | null;
}

type AutolinkingVerdict =
  | ({ readonly kind: "linked"; readonly rule: "cxx-fallback" | "explicit" } & LinkedLibrary & VerdictMessage)
  | ({
      readonly compiled: readonly string[];
      readonly kind: "nitro";
      readonly unimplemented: readonly string[];
    } & VerdictMessage)
  | ({ readonly kind: "expo-module" | "no-native-code" | "opted-out" | "rejected" } & VerdictMessage);

interface VerdictMessage {
  readonly message: string;
  readonly packageName: string;
}

type AutolinkedLibrary = Extract<AutolinkingVerdict, { readonly kind: "linked" }>;

/**
 * A dependency's codegen output: its `codegenConfig.name`, and the components whose shadow node and descriptor
 * codegen generated (every component spec that is not `interfaceOnly`), which the host registers (#149).
 */
interface CodegenLibrary {
  readonly components: readonly string[];
  readonly name: string;
}

export type {
  AutolinkedLibrary,
  AutolinkingDependency,
  CodegenLibrary,
  AutolinkingRequest,
  AutolinkingVerdict,
  NativeBuildDescriptor,
};
