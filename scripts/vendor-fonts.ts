import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createHash } from "node:crypto";
import path from "node:path";

const scriptDirectory = import.meta.dirname;
const repositoryRoot = path.resolve(scriptDirectory, "..");
const lockFilePath = path.join(scriptDirectory, "fonts.lock.json");
const vendorDirectory = path.join(repositoryRoot, "packages", "core", "fonts");
const stampFilePath = path.join(vendorDirectory, ".vendor-stamp.json");
const jsonIndentation = 2;

interface FontFileLock {
  name: string;
  path: string;
  sha256: string;
}

interface FontSourceLock {
  baseUrl: string;
  commit: string;
  family: string;
  files: FontFileLock[];
  license: string;
}

interface FontsLock {
  sources: FontSourceLock[];
}

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const parseFontFile = (value: unknown, origin: string): FontFileLock => {
  if (
    !isRecord(value) ||
    typeof value.name !== "string" ||
    typeof value.path !== "string" ||
    typeof value.sha256 !== "string"
  ) {
    throw new Error(`${origin} must declare every "files" entry with string "name", "path" and "sha256"`);
  }

  return { name: value.name, path: value.path, sha256: value.sha256 };
};

const parseFontSource = (value: unknown, origin: string): FontSourceLock => {
  if (
    !isRecord(value) ||
    typeof value.baseUrl !== "string" ||
    typeof value.commit !== "string" ||
    typeof value.family !== "string" ||
    typeof value.license !== "string" ||
    !Array.isArray(value.files)
  ) {
    throw new Error(
      `${origin} must declare every "sources" entry with string "baseUrl", "commit", "family", "license" and "files"`,
    );
  }

  return {
    baseUrl: value.baseUrl,
    commit: value.commit,
    family: value.family,
    files: value.files.map((entry) => parseFontFile(entry, origin)),
    license: value.license,
  };
};

const parseFontsLock = (source: string, origin: string): FontsLock => {
  const parsed: unknown = JSON.parse(source);

  if (!isRecord(parsed) || !Array.isArray(parsed.sources)) {
    throw new Error(`${origin} must contain a JSON object with a "sources" array`);
  }

  return { sources: parsed.sources.map((entry) => parseFontSource(entry, origin)) };
};

const report = (message: string): void => {
  process.stdout.write(`${message}\n`);
};

const readStamp = (): FontsLock | null => {
  if (!existsSync(stampFilePath)) {
    return null;
  }

  return parseFontsLock(readFileSync(stampFilePath, "utf8"), stampFilePath);
};

const describeSource = (source: FontSourceLock): string =>
  `${source.baseUrl}@${source.commit} ${source.files.map((file) => `${file.path}@${file.sha256}`).join(" ")}`;

const describeLock = (lock: FontsLock): string => lock.sources.map(describeSource).join(" | ");

const matchesStamp = (lock: FontsLock, stamp: FontsLock | null): boolean =>
  stamp !== null && describeLock(stamp) === describeLock(lock);

const verifyDigest = (file: FontFileLock, contents: Buffer): Buffer => {
  const digest = createHash("sha256").update(contents).digest("hex");

  if (digest !== file.sha256) {
    throw new Error(`${file.path} sha256 is ${digest}, the lock file pins ${file.sha256}`);
  }

  report(`  verified sha256 ${digest}`);

  return contents;
};

const fetchVerifiedFile = async (source: FontSourceLock, file: FontFileLock): Promise<Buffer> => {
  const url = `${source.baseUrl}/${source.commit}/${file.path}`;

  report(`Downloading ${url}`);

  const response = await fetch(url);

  if (!response.ok) {
    throw new Error(`${url} responded with ${response.status} ${response.statusText}`);
  }

  return verifyDigest(file, Buffer.from(await response.arrayBuffer()));
};

const vendor = async (lock: FontsLock): Promise<void> => {
  const fetched = await Promise.all(
    lock.sources.flatMap((source) =>
      source.files.map(async (file) => ({ contents: await fetchVerifiedFile(source, file), name: file.name })),
    ),
  );

  rmSync(vendorDirectory, { force: true, recursive: true });
  mkdirSync(vendorDirectory, { recursive: true });

  for (const file of fetched) {
    writeFileSync(path.join(vendorDirectory, file.name), file.contents);
  }

  writeFileSync(stampFilePath, `${JSON.stringify(lock, null, jsonIndentation)}\n`);
};

const main = async (): Promise<void> => {
  const lock = parseFontsLock(readFileSync(lockFilePath, "utf8"), lockFilePath);

  if (matchesStamp(lock, readStamp())) {
    report("packages/core/fonts is already vendored at the pinned commits; nothing to do");
  } else {
    await vendor(lock);
  }

  for (const source of lock.sources) {
    report(`Font family: ${source.family}`);
    report(`License:     ${source.license}`);
    report(`Pinned at:   ${source.commit}`);
  }
};

await main();
