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

interface FontsLock {
  baseUrl: string;
  commit: string;
  family: string;
  files: FontFileLock[];
  license: string;
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

const parseFontsLock = (source: string, origin: string): FontsLock => {
  const parsed: unknown = JSON.parse(source);

  if (
    !isRecord(parsed) ||
    typeof parsed.baseUrl !== "string" ||
    typeof parsed.commit !== "string" ||
    typeof parsed.family !== "string" ||
    typeof parsed.license !== "string" ||
    !Array.isArray(parsed.files)
  ) {
    throw new Error(
      `${origin} must contain a JSON object with string "baseUrl", "commit", "family", "license" and "files"`,
    );
  }

  return {
    baseUrl: parsed.baseUrl,
    commit: parsed.commit,
    family: parsed.family,
    files: parsed.files.map((entry) => parseFontFile(entry, origin)),
    license: parsed.license,
  };
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

const describeFiles = (lock: FontsLock): string => lock.files.map((file) => `${file.path}@${file.sha256}`).join(" ");

const matchesStamp = (lock: FontsLock, stamp: FontsLock | null): boolean =>
  stamp !== null &&
  stamp.baseUrl === lock.baseUrl &&
  stamp.commit === lock.commit &&
  describeFiles(stamp) === describeFiles(lock);

const verifyDigest = (file: FontFileLock, contents: Buffer): Buffer => {
  const digest = createHash("sha256").update(contents).digest("hex");

  if (digest !== file.sha256) {
    throw new Error(`${file.path} sha256 is ${digest}, the lock file pins ${file.sha256}`);
  }

  report(`  verified sha256 ${digest}`);

  return contents;
};

const fetchVerifiedFile = async (lock: FontsLock, file: FontFileLock): Promise<Buffer> => {
  const url = `${lock.baseUrl}/${lock.commit}/${file.path}`;

  report(`Downloading ${url}`);

  const response = await fetch(url);

  if (!response.ok) {
    throw new Error(`${url} responded with ${response.status} ${response.statusText}`);
  }

  return verifyDigest(file, Buffer.from(await response.arrayBuffer()));
};

const vendor = async (lock: FontsLock): Promise<void> => {
  const fetched = await Promise.all(
    lock.files.map(async (file) => ({ contents: await fetchVerifiedFile(lock, file), name: file.name })),
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
    report(`packages/core/fonts is already vendored at ${lock.commit}; nothing to do`);
  } else {
    await vendor(lock);
  }

  report(`Font family: ${lock.family}`);
  report(`License:     ${lock.license}`);
  report(`Pinned at:   ${lock.commit}`);
};

await main();
