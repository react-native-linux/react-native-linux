import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import path from "node:path";

const scriptDirectory = import.meta.dirname;
const repositoryRoot = path.resolve(scriptDirectory, "..");
const lockFilePath = path.join(scriptDirectory, "skia.lock.json");
const vendorDirectory = path.join(repositoryRoot, "third_party", "skia");
const libraryDirectory = path.join(vendorDirectory, "lib");
const archiveFilePath = path.join(vendorDirectory, "skia-binaries.tar.gz");
const stampFilePath = path.join(vendorDirectory, ".vendor-stamp.json");
const jsonIndentation = 2;

interface SkiaBinariesLock {
  sha256: string;
  url: string;
}

interface SkiaHeadersLock {
  commit: string;
  repository: string;
  sparsePaths: string[];
}

interface SkiaLock {
  binaries: SkiaBinariesLock;
  headers: SkiaHeadersLock;
  milestone: string;
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isStringArray = (value: unknown): value is string[] =>
  Array.isArray(value) && value.every((entry) => typeof entry === "string");

const parseBinaries = (value: unknown, origin: string): SkiaBinariesLock => {
  if (!isRecord(value) || typeof value.sha256 !== "string" || typeof value.url !== "string") {
    throw new Error(`${origin} must declare "binaries" with string "url" and string "sha256"`);
  }

  return { sha256: value.sha256, url: value.url };
};

const parseHeaders = (value: unknown, origin: string): SkiaHeadersLock => {
  if (
    !isRecord(value) ||
    typeof value.commit !== "string" ||
    typeof value.repository !== "string" ||
    !isStringArray(value.sparsePaths)
  ) {
    throw new Error(
      `${origin} must declare "headers" with string "repository", string "commit" and string[] "sparsePaths"`,
    );
  }

  return { commit: value.commit, repository: value.repository, sparsePaths: value.sparsePaths };
};

const parseSkiaLock = (source: string, origin: string): SkiaLock => {
  const parsed: unknown = JSON.parse(source);

  if (!isRecord(parsed) || typeof parsed.milestone !== "string") {
    throw new Error(`${origin} must contain a JSON object with a string "milestone"`);
  }

  return {
    binaries: parseBinaries(parsed.binaries, origin),
    headers: parseHeaders(parsed.headers, origin),
    milestone: parsed.milestone,
  };
};

const report = (message: string): void => {
  process.stdout.write(`${message}\n`);
};

const runGit = (args: readonly string[], workingDirectory: string): void => {
  execFileSync("git", args, { cwd: workingDirectory, stdio: "inherit" });
};

const readStamp = (): SkiaLock | null => {
  if (!existsSync(stampFilePath)) {
    return null;
  }

  return parseSkiaLock(readFileSync(stampFilePath, "utf8"), stampFilePath);
};

const matchesStamp = (lock: SkiaLock, stamp: SkiaLock | null): boolean =>
  stamp !== null &&
  stamp.binaries.sha256 === lock.binaries.sha256 &&
  stamp.binaries.url === lock.binaries.url &&
  stamp.headers.commit === lock.headers.commit &&
  stamp.headers.repository === lock.headers.repository &&
  stamp.headers.sparsePaths.join(" ") === lock.headers.sparsePaths.join(" ");

const fetchVerifiedArchive = async (binaries: SkiaBinariesLock): Promise<Buffer> => {
  report(`Downloading ${binaries.url}`);

  const response = await fetch(binaries.url);

  if (!response.ok) {
    throw new Error(`${binaries.url} responded with ${response.status} ${response.statusText}`);
  }

  const archive = Buffer.from(await response.arrayBuffer());
  const digest = createHash("sha256").update(archive).digest("hex");

  if (digest !== binaries.sha256) {
    throw new Error(`Skia archive sha256 is ${digest}, the lock file pins ${binaries.sha256}`);
  }

  report(`  verified sha256 ${digest}`);

  return archive;
};

const extractArchive = (archive: Buffer): void => {
  mkdirSync(libraryDirectory, { recursive: true });
  writeFileSync(archiveFilePath, archive);
  execFileSync("tar", ["-xzf", archiveFilePath, "-C", libraryDirectory, "--strip-components", "1"], {
    stdio: "inherit",
  });
  rmSync(archiveFilePath, { force: true });
};

const cloneHeaders = (headers: SkiaHeadersLock): void => {
  report(`Cloning ${headers.repository} at ${headers.commit}`);

  runGit(["init", "--quiet", vendorDirectory], repositoryRoot);
  runGit(["remote", "add", "origin", headers.repository], vendorDirectory);
  runGit(["sparse-checkout", "init", "--cone"], vendorDirectory);
  runGit(["sparse-checkout", "set", ...headers.sparsePaths], vendorDirectory);
  runGit(["fetch", "--filter=blob:none", "--depth", "1", "origin", headers.commit], vendorDirectory);
  runGit(["checkout", "--quiet", "FETCH_HEAD"], vendorDirectory);
};

const vendor = async (lock: SkiaLock): Promise<void> => {
  rmSync(vendorDirectory, { force: true, recursive: true });
  mkdirSync(vendorDirectory, { recursive: true });

  cloneHeaders(lock.headers);
  extractArchive(await fetchVerifiedArchive(lock.binaries));

  writeFileSync(stampFilePath, `${JSON.stringify(lock, null, jsonIndentation)}\n`);
};

const main = async (): Promise<void> => {
  const lock = parseSkiaLock(readFileSync(lockFilePath, "utf8"), lockFilePath);

  if (matchesStamp(lock, readStamp())) {
    report(`third_party/skia is already vendored at ${lock.milestone}; nothing to do`);
  } else {
    await vendor(lock);
  }

  report(`Skia milestone: ${lock.milestone}`);
  report(`Skia headers:   ${lock.headers.commit}`);
};

await main();
