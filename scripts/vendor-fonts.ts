import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { isStampCurrent, parseFontsLock, parseFontsStamp } from "./vendor-fonts/fonts-lock.ts";
import { createHash } from "node:crypto";
import path from "node:path";

type FontsLock = ReturnType<typeof parseFontsLock>;
type FontSourceLock = FontsLock["sources"][number];
type FontFileLock = FontSourceLock["files"][number];

const scriptDirectory = import.meta.dirname;
const repositoryRoot = path.resolve(scriptDirectory, "..");
const lockFilePath = path.join(scriptDirectory, "fonts.lock.json");
const vendorDirectory = path.join(repositoryRoot, "packages", "core", "fonts");
const stampFilePath = path.join(vendorDirectory, ".vendor-stamp.json");
const jsonIndentation = 2;

const report = (message: string): void => {
  process.stdout.write(`${message}\n`);
};

const readStamp = (): FontsLock | null => {
  if (!existsSync(stampFilePath)) {
    return null;
  }

  return parseFontsStamp(readFileSync(stampFilePath, "utf8"));
};

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

  if (isStampCurrent(lock, readStamp())) {
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
