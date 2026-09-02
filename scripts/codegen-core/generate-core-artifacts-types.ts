import type { LibraryConfig, LibraryOptions } from "@react-native/codegen/lib/generators/RNCodegen.js";
import type { SchemaType } from "@react-native/codegen/lib/CodegenSchema.js";

interface DirectoryEntry {
  readonly isDirectory: boolean;
  readonly name: string;
}

interface CoreCodegenEnvironment {
  readonly generateArtifacts: (options: LibraryOptions, config: LibraryConfig) => boolean;
  readonly listDirectory: (directoryPath: string) => readonly DirectoryEntry[];
  readonly parseSpecFile: (filePath: string) => SchemaType;
  readonly readFile: (filePath: string) => string;
  readonly removeFile: (filePath: string) => void;
  readonly report: (message: string) => void;
  readonly resetDirectory: (directoryPath: string) => void;
  readonly writeFile: (filePath: string, contents: string) => void;
}

interface CoreCodegenPaths {
  readonly codegenPackageJsonPath: string;
  readonly outputDirectory: string;
  readonly repositoryRoot: string;
  readonly specSourceDirectory: string;
}

export type { CoreCodegenEnvironment, CoreCodegenPaths, DirectoryEntry };
