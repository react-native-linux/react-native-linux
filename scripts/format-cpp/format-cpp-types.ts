type FormatCppMode = "check" | "write";

interface DirectoryEntry {
  readonly isDirectory: boolean;
  readonly name: string;
}

interface ClangFormatBinary {
  readonly binaryName: string;
  readonly version: string;
}

interface FormatCppEnvironment {
  readonly listDirectory: (directoryPath: string) => readonly DirectoryEntry[];
  readonly readClangFormatVersion: (binaryName: string) => string | null;
  readonly runClangFormat: (binaryName: string, args: readonly string[]) => number;
  readonly report: (message: string) => void;
}

export type { ClangFormatBinary, DirectoryEntry, FormatCppEnvironment, FormatCppMode };
