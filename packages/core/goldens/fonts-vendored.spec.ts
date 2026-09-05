import { describe, expect, it } from "vitest";

import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";

import { checkFontsAreVendored } from "./fonts-vendored.ts";
import path from "node:path";
import { tmpdir } from "node:os";

const FILE = { name: "NotoSans-Regular.ttf", path: "fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf", sha256: "abc" };

const SOURCE = {
  baseUrl: "https://example.invalid/notofonts",
  commit: "2ad4e55",
  family: "Noto Sans",
  files: [FILE],
  license: "SIL Open Font License 1.1",
};

const LOCK = { sources: [SOURCE] };

const withFixtureDirectory = (run: (lockFilePath: string, vendorDirectory: string) => void): void => {
  const scratchDirectory = mkdtempSync(path.join(tmpdir(), "rnl-fonts-vendored-"));

  try {
    const lockFilePath = path.join(scratchDirectory, "fonts.lock.json");
    const vendorDirectory = path.join(scratchDirectory, "fonts");

    writeFileSync(lockFilePath, JSON.stringify(LOCK));
    mkdirSync(vendorDirectory);

    run(lockFilePath, vendorDirectory);
  } finally {
    rmSync(scratchDirectory, { force: true, recursive: true });
  }
};

const vendorCurrently = (vendorDirectory: string): void => {
  writeFileSync(path.join(vendorDirectory, FILE.name), "font bytes");
  writeFileSync(path.join(vendorDirectory, ".vendor-stamp.json"), JSON.stringify(LOCK));
};

describe("checkFontsAreVendored", () => {
  it("does not throw when every pinned file exists and the stamp matches the lock", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      vendorCurrently(vendorDirectory);

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).not.toThrow();
    });
  });

  it("names the missing file and the vendor command when a pinned face is absent", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      const message = `${path.join(vendorDirectory, FILE.name)} is missing. Run "pnpm --filter @react-native-linux/core vendor:fonts" and retry.`;

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).toThrow(message);
    });
  });

  it("does not need every file present twice over to report only the missing one", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      vendorCurrently(vendorDirectory);
      rmSync(path.join(vendorDirectory, FILE.name));

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).toThrow(FILE.name);
    });
  });

  it("names the missing stamp and the vendor command when every file exists but the stamp is absent", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      writeFileSync(path.join(vendorDirectory, FILE.name), "font bytes");

      const message = `${path.join(vendorDirectory, ".vendor-stamp.json")} is missing. Run "pnpm --filter @react-native-linux/core vendor:fonts" and retry.`;

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).toThrow(message);
    });
  });

  it("names the stamp, the lock and the vendor command when the stamp does not match the lock", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      vendorCurrently(vendorDirectory);
      writeFileSync(path.join(vendorDirectory, ".vendor-stamp.json"), JSON.stringify({ sources: [] }));

      const stampFilePath = path.join(vendorDirectory, ".vendor-stamp.json");
      const message = `${stampFilePath} does not match ${lockFilePath}. Run "pnpm --filter @react-native-linux/core vendor:fonts" and retry.`;

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).toThrow(message);
    });
  });
});

describe("checkFontsAreVendored against a malformed lock", () => {
  it("rejects a lock that is not a JSON object with a sources array", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      writeFileSync(lockFilePath, JSON.stringify({ notSources: [] }));

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).toThrow(
        'must contain a JSON object with a "sources" array',
      );
    });
  });

  it("rejects a source that does not declare a files array", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      writeFileSync(lockFilePath, JSON.stringify({ sources: [{ ...SOURCE, files: "not-an-array" }] }));

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).toThrow(
        'must declare every "sources" entry with a "files" array',
      );
    });
  });

  it("rejects a file entry with no string name", () => {
    withFixtureDirectory((lockFilePath, vendorDirectory) => {
      writeFileSync(lockFilePath, JSON.stringify({ sources: [{ ...SOURCE, files: [{ ...FILE, name: 1 }] }] }));

      expect(() => checkFontsAreVendored(lockFilePath, vendorDirectory)).toThrow(
        'must declare every "files" entry with a string "name"',
      );
    });
  });
});
