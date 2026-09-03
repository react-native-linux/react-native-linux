interface CommandOutput {
  readonly exitCode: number | null;
  readonly standardError: string;
  readonly standardOutput: string;
}

interface UpstreamEnvironment {
  readonly applyPatch: (patchFilePath: string, workingDirectory: string) => CommandOutput;
  readonly copyDirectory: (sourceDirectory: string, targetDirectory: string) => void;
  readonly createTemporaryDirectory: () => string;
  readonly hashFile: (filePath: string) => string;
  readonly listDirectories: (directoryPath: string) => readonly string[];
  readonly listFiles: (directoryPath: string) => readonly string[];
  readonly pathExists: (targetPath: string) => boolean;
  readonly readTextFile: (filePath: string) => string;
  readonly removePath: (targetPath: string) => void;
  readonly runGit: (commandArguments: readonly string[], workingDirectory: string) => CommandOutput;
  readonly writeTextFile: (filePath: string, contents: string) => void;
}

interface UpstreamLock {
  readonly repo: string;
  readonly sha256: Readonly<Record<string, string>>;
  readonly sparsePaths: readonly string[];
  readonly tag: string;
}

interface LibraryPaths {
  readonly lockFilePath: string;
  readonly patchesDirectory: string;
  readonly treeDirectory: string;
}

interface LibraryContext {
  readonly libraryName: string;
  readonly libraryPaths: LibraryPaths;
  readonly lock: UpstreamLock;
  readonly patchFileNames: readonly string[];
}

type PreparationOutcome = "cloneFailed" | "digestMismatch" | "patchFailed" | "prepared";

interface Preparation {
  readonly directory: string;
  readonly failureMessages: readonly string[];
  readonly outcome: PreparationOutcome;
  readonly pristineDigests: Readonly<Record<string, string>>;
}

interface PreparationRequest {
  readonly context: LibraryContext;
  readonly patchCount: number;
  readonly verifyDigests: boolean;
}

interface CaptureRequest {
  readonly context: LibraryContext;
  readonly patchFileName: string;
  readonly preparedDirectory: string;
}

interface CommandResult {
  readonly exitCode: number;
  readonly messages: readonly string[];
}

type DigestDifferenceKind = "added" | "changed" | "removed";

interface DigestDifference {
  readonly kind: DigestDifferenceKind;
  readonly path: string;
}

export type {
  CaptureRequest,
  CommandOutput,
  CommandResult,
  DigestDifference,
  LibraryContext,
  LibraryPaths,
  Preparation,
  PreparationRequest,
  UpstreamEnvironment,
  UpstreamLock,
};
